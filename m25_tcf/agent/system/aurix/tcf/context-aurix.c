/*
 * context-aurix.c - native TCF debug context backend for AURIX TriCore.
 *
 * M25 prototype of the "missing 10%": the original aurix-debugger.exe
 * replaces the host OS debug context backend (context-win32.c) with its
 * own AURIX backend (system/aurix/tcf/context-aurix.cpp).  This file is
 * the open equivalent: it implements the complete native context API on
 * top of the TAS wire client (aurix-tas.c) and the reference MCD
 * behaviour calibrated in the M1-M22 milestones:
 *
 *   - attach: SERVER_CONNECT -> SESSION_START -> SET_DEVICE_KEY(hot)
 *     -> DEVICE_CONNECT, then halt every core (reference "OpenCore+Halt")
 *   - memory access: TAS PL0 read/write (RD8..RD64 / RDBLK / WRBLK)
 *   - register access: per-core CSFR window (CPU0 0xF8810000,
 *     +0x20000 per core, M20b) - the register map of the reference
 *     MCD stack (M7)
 *   - stop:  DBGSR (CSFR+0xFD00) <- 0x00000006 + HALT verify (M16/M17b)
 *   - run:   access HW 0xF0000494 <- 0x8000003E / DBGSR <- 4 /
 *            0xF0000494 <- 0x8000003F / DBGSR <- 4 /
 *            0xF000049C <- 2 (adapter run command) + HALT verify (M22b)
 *   - step:  debug init (trigger window + DBGSR clear + 0xFD10) ->
 *            access HW single-step mode -> DBGSR <- 4 -> normal mode ->
 *            debug init -> halt verify (M22)
 *   - software breakpoints use the TriCore 'debug' instruction
 *     (BREAK_INST); hardware breakpoints (OCDS TR0..TR7) are M25-2.
 *
 * SPDX-License-Identifier: MIT (project code only; the TCF framework in
 * this tree keeps its original EPL/EDL license).
 */
#include <tcf/config.h>

#if ENABLE_DebugContext

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <tcf/framework/context.h>
#include <tcf/framework/cpudefs.h>
#include <tcf/framework/errors.h>
#include <tcf/framework/events.h>
#include <tcf/framework/myalloc.h>
#include <tcf/framework/trace.h>
#include <tcf/services/memorymap.h>
#include <tcf/services/runctrl.h>

#include <tcf/cpudefs-mdep.h>

#if defined(_WIN32)
#  include <process.h>
#  define aurix_getpid() ((unsigned)_getpid())
#else
#  include <unistd.h>
#  define aurix_getpid() ((unsigned)getpid())
#endif

#include "aurix-tas.h"

/* ------------------------------------------------------------------ */
/* constants (M16/M20b/M22 calibration)                                */
/* ------------------------------------------------------------------ */

#define AURIX_MAX_CORES       3

#define AURIX_CSFR_BASE       0xF8810000u  /* CPU0 CSFR window           */
#define AURIX_CSFR_STRIDE     0x20000u     /* per-core CSFR window step  */
#define AURIX_CSFR_DBGSR      0xFD00u      /* debug status register      */
#define AURIX_CSFR_TRIG       0xF000u      /* TR0EVT trigger unit window */
#define AURIX_CSFR_FD10       0xFD10u      /* debug configuration reg    */

/* access HW (DAS AURDUINO adapter) control registers */
#define AURIX_ADAPTER_CTRL    0xF0000494u
#define AURIX_ADAPTER_RUNCMD  0xF000049Cu
#define AURIX_ADAPTER_STEP_ON  0x8000003Eu
#define AURIX_ADAPTER_STEP_OFF 0x8000003Fu

/* DBGSR write codes: HALT[2:1] 10b = clear (run), 11b = set (halt) */
#define AURIX_DBGSR_WR_RUN    0x00000004u
#define AURIX_DBGSR_WR_HALT   0x00000006u

/* the core names of the reference stack (M9 calibration) */
static const char * const core_names[AURIX_MAX_CORES] = {
    "Core 0: CPU0 TriCore 1.6E",
    "Core 1: CPU1 TriCore 1.6P",
    "Core 2: CPU2 TriCore 1.6P",
};

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

typedef struct ContextExtensionAurix {
    int core_index;
} ContextExtensionAurix;

static size_t context_extension_offset = 0;
#define EXT(ctx) ((ContextExtensionAurix *)((char *)(ctx) + context_extension_offset))

static tas_conn      g_tas;
static int           g_attached = 0;
static tas_con_info  g_con;
static tas_server_info g_srv;
static tas_target    g_targets[TAS_MAX_TARGETS];
static int           g_ntargets = 0;

/* ------------------------------------------------------------------ */
/* TAS helpers                                                         */
/* ------------------------------------------------------------------ */

static int tas_rw_1(uint64_t addr, void * buf, uint32_t n, int is_write) {
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = n;
    it.is_write = is_write;
    it.data = (uint8_t *)buf;
    rc = tas_rw_exec(&g_tas, &it, 1, g_con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok)
        return 0;
    trace(LOG_ALWAYS, "AURIX: %s @0x%08llX (%u B) failed (rc %d, pl0_err 0x%02x)",
          is_write ? "write" : "read", (unsigned long long)addr, (unsigned)n, rc,
          (unsigned)pe);
    errno = EIO;
    return -1;
}

/* Arbitrary address/size access: the PL0 layer supports 1/2/4/8 byte
 * transactions plus 8-byte-aligned block transfers up to 1024 bytes.
 * The target requires natural alignment (an unaligned RD32 is rejected),
 * so unaligned heads/tails use smaller transactions. */
static int tas_rw(uint64_t addr, void * buf, size_t size, int is_write) {
    uint8_t * p = (uint8_t *)buf;

    while (size > 0) {
        uint32_t n;

        if ((addr & 7) == 0 && size >= 8) {
            n = (uint32_t)(size & ~(size_t)7);
            if (n > 1024) n = 1024;
        }
        else if ((addr & 3) == 0 && size >= 4) n = 4;
        else if ((addr & 1) == 0 && size >= 2) n = 2;
        else n = 1;
        if (tas_rw_1(addr, p, n, is_write) != 0)
            return -1;
        addr += n;
        p += n;
        size -= n;
    }
    return 0;
}

static int core_csfr_read(int core, uint32_t off, uint32_t * value) {
    return tas_rw((uint64_t)AURIX_CSFR_BASE +
                  (uint64_t)AURIX_CSFR_STRIDE * (uint32_t)core + off,
                  value, 4, 0);
}

static int core_csfr_write(int core, uint32_t off, uint32_t value) {
    return tas_rw((uint64_t)AURIX_CSFR_BASE +
                  (uint64_t)AURIX_CSFR_STRIDE * (uint32_t)core + off,
                  &value, 4, 1);
}

static int adapter_write(uint32_t addr, uint32_t value) {
    return tas_rw((uint64_t)addr, &value, 4, 1);
}

/* HALT[2:1] field of DBGSR: 01b = halted, 00b = running */
static int core_halted(int core, uint32_t * dbgsr) {
    uint32_t v = 0;

    if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) != 0)
        return -1;
    if (dbgsr != NULL) *dbgsr = v;
    return ((v >> 1) & 3u) == 1u;
}

/* Reference debug init (M22 capture): disable the OCDS trigger units,
 * clear the DBGSR..DBGSR+0xF block and write the debug configuration
 * register 0xFD10 - all in one PL0 frame.  The step sequence brackets
 * its clear-halt write with this (full = 1 before, 0 after). */
static int step_debug_init(int core, int full) {
    static uint32_t trig_init[16] = {
        0x00002000u, 0xFFFFFFFFu, 0x00002000u, 0xFFFFFFFFu,
        0x00002000u, 0xFFFFFFFFu, 0x00002000u, 0xFFFFFFFFu,
        0x00002000u, 0xFFFFFFFFu, 0x00002000u, 0xFFFFFFFFu,
        0x00002000u, 0xFFFFFFFFu, 0x00002000u, 0xFFFFFFFFu,
    };
    static uint32_t trig_init2[16] = {
        0x00003002u, 0x00000000u, 0x00000000u, 0xFFFFFFFFu,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    };
    static uint32_t dbgsr_zero[4] = { 0u, 0u, 0u, 0u };
    static uint32_t fd10_val = 0x2Au;
    uint64_t base = (uint64_t)AURIX_CSFR_BASE +
                    (uint64_t)AURIX_CSFR_STRIDE * (uint32_t)core;

    if (tas_rw(base + AURIX_CSFR_TRIG, trig_init, 64, 1) != 0)
        return -1;
    if (tas_rw(base + AURIX_CSFR_DBGSR, dbgsr_zero, 16, 1) != 0)
        return -1;
    if (tas_rw(base + AURIX_CSFR_FD10, &fd10_val, 4, 1) != 0)
        return -1;
    if (full) {
        if (tas_rw(base + AURIX_CSFR_TRIG, trig_init2, 64, 1) != 0)
            return -1;
    }
    return 0;
}

/* stop: DBGSR <- halt code + verify (M16/M17b) */
static int core_stop(int core) {
    uint32_t v = 0;

    if (core_csfr_write(core, AURIX_CSFR_DBGSR, AURIX_DBGSR_WR_HALT) != 0)
        return -1;
    if (core_halted(core, &v) < 0)
        return -1;
    if (((v >> 1) & 3u) != 1u) {
        trace(LOG_ALWAYS, "AURIX: core %d did not halt (DBGSR=0x%08X)", core,
              (unsigned)v);
        errno = EIO;
        return -1;
    }
    return 0;
}

/* run: the reference M22b adapter sequence + verify */
static int core_run(int core) {
    uint32_t v = 0;

    if (adapter_write(AURIX_ADAPTER_CTRL, AURIX_ADAPTER_STEP_ON) != 0)
        return -1;
    if (core_csfr_write(core, AURIX_CSFR_DBGSR, AURIX_DBGSR_WR_RUN) != 0)
        return -1;
    if (adapter_write(AURIX_ADAPTER_CTRL, AURIX_ADAPTER_STEP_OFF) != 0)
        return -1;
    if (core_csfr_write(core, AURIX_CSFR_DBGSR, AURIX_DBGSR_WR_RUN) != 0)
        return -1;
    if (adapter_write(AURIX_ADAPTER_RUNCMD, 2u) != 0)
        return -1;

    /* verify (M17b): the core must report HALT=00b.  A core that is
     * caught by a trigger right after the run write is a legitimate
     * stop - report success, the breakpoint did its job. */
    if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) != 0)
        return -1;
    if (((v >> 1) & 3u) != 0u) {
        uint32_t acc = 0;
        if (core_csfr_read(core, 0xFD30u /* TRIG_ACC */, &acc) == 0 && acc != 0)
            return 0;
        trace(LOG_ALWAYS, "AURIX: core %d did not start (DBGSR=0x%08X)", core,
              (unsigned)v);
        errno = EIO;
        return -1;
    }
    return 0;
}

/* step: the reference M22 sequence (single instruction) */
static int core_step(int core) {
    uint32_t v = 0;
    int i;

    /* precondition: the core must be in the debug (halted) state */
    if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) != 0)
        return -1;
    if (((v >> 1) & 3u) != 1u) {
        trace(LOG_ALWAYS, "AURIX: stepping failed - core %d is not in debug state",
              core);
        errno = EIO;
        return -1;
    }

    if (step_debug_init(core, 1) != 0)
        return -1;
    if (adapter_write(AURIX_ADAPTER_CTRL, AURIX_ADAPTER_STEP_ON) != 0)
        return -1;
    if (core_csfr_write(core, AURIX_CSFR_DBGSR, AURIX_DBGSR_WR_RUN) != 0)
        return -1;
    if (adapter_write(AURIX_ADAPTER_CTRL, AURIX_ADAPTER_STEP_OFF) != 0)
        return -1;
    if (step_debug_init(core, 0) != 0)
        return -1;

    /* verify: the core halts again after the instruction */
    for (i = 0; i < 20; i++) {
        if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) != 0)
            return -1;
        if (((v >> 1) & 3u) == 1u)
            return 0;
    }
    trace(LOG_ALWAYS, "AURIX: core %d did not complete single step", core);
    errno = EIO;
    return -1;
}

/* ------------------------------------------------------------------ */
/* the native context API                                              */
/* ------------------------------------------------------------------ */

/* The framework expects stop/step completion to be reported
 * asynchronously (the caller checks !ctx->stopped right after
 * context_stop()): the target access itself is synchronous here, the
 * state change is delivered by a posted event. */
static void aurix_stopped_event(void * args) {
    Context * ctx = (Context *)args;
    if (!ctx->stopped) {
        ctx->stopped = 1;
        send_context_stopped_event(ctx);
    }
    context_unlock(ctx);
}

static int post_stopped_event(Context * ctx) {
    context_lock(ctx);
    post_event(aurix_stopped_event, ctx);
    return 0;
}

int context_has_state(Context * ctx) {
    (void)ctx;
    return 1;
}

const char * context_suspend_reason(Context * ctx) {
    if (ctx->stopped_by_bp) return REASON_BREAKPOINT;
    if (ctx->stopped_by_exception) return REASON_EXCEPTION;
    return REASON_USER_REQUEST;
}

int context_stop(Context * ctx) {
    assert(context_has_state(ctx));
    assert(!ctx->exited);

    if (core_stop(EXT(ctx)->core_index) < 0)
        return -1;
    return post_stopped_event(ctx);
}

int context_continue(Context * ctx) {
    assert(context_has_state(ctx));
    assert(!ctx->exited);

    if (core_run(EXT(ctx)->core_index) < 0)
        return -1;
    /* the context is running again (win32_resume does the same) */
    send_context_started_event(ctx);
    return 0;
}

int context_single_step(Context * ctx) {
    assert(context_has_state(ctx));
    assert(!ctx->exited);

    if (core_step(EXT(ctx)->core_index) < 0)
        return -1;
    /* the core ran one instruction and is halted again: the stop is
     * reported asynchronously like a normal stop */
    send_context_started_event(ctx);
    return post_stopped_event(ctx);
}

int context_resume(Context * ctx, int mode, ContextAddress range_start,
                   ContextAddress range_end) {
    (void)range_start;
    (void)range_end;

    switch (mode) {
    case RM_RESUME:
        return context_continue(ctx);
    case RM_STEP_INTO:
        return context_single_step(ctx);
    }
    errno = ERR_UNSUPPORTED;
    return -1;
}

int context_can_resume(Context * ctx, int mode) {
    switch (mode) {
    case RM_RESUME:
        return 1;
    case RM_STEP_INTO:
        return context_has_state(ctx);
    }
    return 0;
}

int context_write_mem(Context * ctx, ContextAddress address, void * buf,
                      size_t size) {
    (void)ctx;
    if (!g_attached) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    return tas_rw((uint64_t)address, buf, size, 1);
}

int context_read_mem(Context * ctx, ContextAddress address, void * buf,
                     size_t size) {
    (void)ctx;
    if (!g_attached) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    return tas_rw((uint64_t)address, buf, size, 0);
}

unsigned context_word_size(Context * ctx) {
    (void)ctx;
    return 4;
}

int context_read_reg(Context * ctx, RegisterDefinition * def, unsigned offs,
                     unsigned size, void * buf) {
    uint64_t addr;

    if (def == NULL || def->memory_address == 0) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    if (offs + size > def->size) {
        errno = ERR_INV_DATA_SIZE;
        return -1;
    }
    /* the register definitions carry CPU0 CSFR addresses (M7 map);
     * shift to the addressed core (M20b: +0x20000 per core) */
    addr = (uint64_t)def->memory_address +
           (uint64_t)AURIX_CSFR_STRIDE * (uint32_t)EXT(ctx)->core_index + offs;
    return tas_rw(addr, buf, size, 0);
}

int context_write_reg(Context * ctx, RegisterDefinition * def, unsigned offs,
                      unsigned size, void * buf) {
    uint64_t addr;

    if (def == NULL || def->memory_address == 0) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    if (offs + size > def->size) {
        errno = ERR_INV_DATA_SIZE;
        return -1;
    }
    addr = (uint64_t)def->memory_address +
           (uint64_t)AURIX_CSFR_STRIDE * (uint32_t)EXT(ctx)->core_index + offs;
    return tas_rw(addr, buf, size, 1);
}

Context * context_get_group(Context * ctx, int group) {
    (void)group;
    /* every core is its own process / breakpoint / intercept group */
    return ctx;
}

int context_get_canonical_addr(Context * ctx, ContextAddress addr,
                               Context ** canonical_ctx,
                               ContextAddress * canonical_addr,
                               ContextAddress * block_addr,
                               ContextAddress * block_size) {
    const ContextAddress page_size = 0x1000;

    *canonical_ctx = ctx;
    if (canonical_addr != NULL) *canonical_addr = addr;
    if (block_addr != NULL) *block_addr = addr & ~(page_size - 1);
    if (block_size != NULL) *block_size = page_size;
    return 0;
}

int context_get_memory_map(Context * ctx, MemoryMap * map) {
    (void)ctx;
    (void)map;
    /* M25-2: the reference "Code and Data" region (M8).  An empty map
     * is valid: the client keeps its own (ELF based) regions. */
    return 0;
}

int context_get_supported_bp_access_types(Context * ctx) {
    (void)ctx;
    /* software breakpoints ('debug' instruction) are supported;
     * hardware breakpoints (OCDS TR0..TR7) are M25-2 */
    return CTX_BP_ACCESS_INSTRUCTION | CTX_BP_ACCESS_SOFTWARE;
}

int context_plant_breakpoint(ContextBreakpoint * bp) {
    (void)bp;
    /* ERR_UNSUPPORTED makes the Breakpoints service plant a generic
     * software breakpoint instead (breakpoints.c) */
    errno = ERR_UNSUPPORTED;
    return -1;
}

int context_unplant_breakpoint(ContextBreakpoint * bp) {
    (void)bp;
    errno = ERR_UNSUPPORTED;
    return -1;
}

#if ENABLE_ContextMemoryProperties
int context_get_memory_properties(Context * ctx, const char *** names,
                                  const char *** values, int * cnt) {
    (void)ctx;
    *cnt = 0;
    return 0;
}
#endif

#if ENABLE_ContextExtraProperties
int context_get_extra_properties(Context * ctx, const char *** names,
                                 const char *** values, int * cnt) {
    (void)ctx;
    *cnt = 0;
    return 0;
}
#endif

#if ENABLE_ContextStateProperties
int context_get_state_properties(Context * ctx, const char *** names,
                                 const char *** values, int * cnt) {
    (void)ctx;
    *cnt = 0;
    return 0;
}
#endif

#if ENABLE_ExtendedMemoryErrorReports
int context_get_mem_error_info(MemoryErrorInfo * info) {
    (void)info;
    errno = ERR_UNSUPPORTED;
    return -1;
}
#endif

#if ENABLE_MemoryAccessModes
int context_write_mem_ext(Context * ctx, MemoryAccessMode * mode,
                          ContextAddress address, void * buf, size_t size) {
    (void)mode;
    return context_write_mem(ctx, address, buf, size);
}

int context_read_mem_ext(Context * ctx, MemoryAccessMode * mode,
                         ContextAddress address, void * buf, size_t size) {
    (void)mode;
    return context_read_mem(ctx, address, buf, size);
}
#endif

#if ENABLE_ContextBreakpointCapabilities
int context_get_breakpoint_capabilities(Context * ctx, const char *** names,
                                        const char *** values, int * cnt) {
    (void)ctx;
    *cnt = 0;
    return 0;
}
#endif

#if ENABLE_ExtendedBreakpointStatus
int context_get_breakpoint_status(ContextBreakpoint * bp, const char *** names,
                                  const char *** values, int * cnt) {
    (void)bp;
    *cnt = 0;
    return 0;
}
#endif

#if ENABLE_ContextISA
int context_get_isa(Context * ctx, ContextAddress addr, ContextISA * isa) {
    (void)ctx;
    memset(isa, 0, sizeof(ContextISA));
    isa->addr = addr;
    isa->size = 0;              /* let the framework use the memory map */
    isa->alignment = 2;         /* TriCore instructions are 2 byte aligned */
    isa->max_instruction_size = 4;
    isa->isa = "TriCore";
    isa->def = "TriCore";
    isa->bp_encoding = BREAK_INST;
    isa->bp_size = sizeof(BREAK_INST);
    return 0;
}
#endif

/* The reference implementation has the same stubs: processes are not
 * started through the agent - the cores are always attached. */
Context * context_find_from_pid(pid_t pid, int thread) {
    (void)pid;
    (void)thread;
    return NULL;
}

int context_attach_self(void) {
    errno = ERR_UNSUPPORTED;
    return -1;
}

int context_attach(pid_t pid, ContextAttachCallBack * done, void * client_data,
                   int mode) {
    (void)pid;
    (void)done;
    (void)client_data;
    (void)mode;
    errno = ERR_UNSUPPORTED;
    return -1;
}

/* ------------------------------------------------------------------ */
/* the init - the TAS bring-up and the core contexts                   */
/* ------------------------------------------------------------------ */

/*
 * The TAS bring-up and the context creation run from a posted event:
 * the framework requires that no context exists while the services are
 * still initializing (several services allocate their context extension
 * in their ini_* functions, which run after ini_contexts()).
 */
static void aurix_init_event(void * args) {
    char detail[160];
    uint8_t e = 0;
    uint16_t feat = 0;
    uint32_t dtype = 0;
    int i;

    (void)args;
    trace(LOG_ALWAYS, "AURIX: init event running");

    /* the TAS bring-up: connect -> server -> targets -> session -> DC */
    detail[0] = 0;
    if (tas_connect(&g_tas, "localhost", TAS_DEFAULT_PORT, detail,
                    sizeof(detail)) != 0) {
        trace(LOG_ALWAYS, "AURIX: TAS connect failed: %s", detail);
        return;
    }
    if (tas_server_connect(&g_tas, "TCF agent (aurix)", "admin",
                           aurix_getpid(), &g_srv, &e) != 0 ||
        e != TAS_ERR_OK) {
        trace(LOG_ALWAYS, "AURIX: SERVER_CONNECT failed (tas_err=0x%02x)", e);
        return;
    }
    if (tas_get_targets(&g_tas, g_targets, TAS_MAX_TARGETS, &g_ntargets,
                        &e) != 0 || e != TAS_ERR_OK || g_ntargets < 1) {
        trace(LOG_ALWAYS, "AURIX: no target found");
        return;
    }
    if (tas_session_start(&g_tas, g_targets[0].identifier, &g_con, &e) != 0 ||
        e != TAS_ERR_OK) {
        trace(LOG_ALWAYS, "AURIX: SESSION_START failed (tas_err=0x%02x)", e);
        return;
    }
    if (tas_set_device_key(&g_tas, TAS_ULO_HOT_ATTACH, NULL, 0, &e) != 0 ||
        e != TAS_ERR_OK) {
        trace(LOG_ALWAYS, "AURIX: SET_DEVICE_KEY failed (tas_err=0x%02x)", e);
        return;
    }
    if (tas_device_connect(&g_tas, 0x0000, &feat, &dtype, &e) != 0 ||
        e != TAS_ERR_OK) {
        trace(LOG_ALWAYS, "AURIX: DEVICE_CONNECT failed (tas_err=0x%02x)", e);
        return;
    }
    g_attached = 1;
    trace(LOG_ALWAYS, "AURIX: attached to '%s' (device 0x%08X, max_rw %u)",
          g_targets[0].identifier, (unsigned)dtype,
          (unsigned)g_con.pl0_max_num_rw);

    /* reference "OpenCore+Halt": halt every core and verify */
    for (i = 0; i < AURIX_MAX_CORES; i++) {
        if (core_stop(i) != 0)
            trace(LOG_ALWAYS, "AURIX: core %d halt at attach failed", i);
    }

    /* the core contexts (CPU0..CPU2).  The cores are halted after the
     * "OpenCore+Halt" step above: create the contexts in the running
     * state (the framework requires that) and then report the stop, so
     * the RunControl service publishes them as suspended right after
     * the first client connects (the reference agent shows the cores as
     * PAUSED at attach). */
    for (i = 0; i < AURIX_MAX_CORES; i++) {
        char id[32];
        Context * ctx;

        snprintf(id, sizeof(id), "core%d", i);
        ctx = create_context(id);
        EXT(ctx)->core_index = i;
        ctx->name = loc_strdup(core_names[i]);
        ctx->big_endian = 0;
        ctx->mem = ctx;
        ctx->mem_access = MEM_ACCESS_INSTRUCTION | MEM_ACCESS_DATA |
                          MEM_ACCESS_RD_STOP | MEM_ACCESS_WR_STOP;
        ctx->reg_access = REG_ACCESS_RD_STOP | REG_ACCESS_WR_STOP;
        list_add_last(&ctx->ctxl, &context_root);
        ctx->ref_count = 1;
        send_context_created_event(ctx);

        /* report the halted state and request the intercept */
        ctx->stopped = 1;
        ctx->pending_intercept = 1;
        send_context_stopped_event(ctx);
    }
    trace(LOG_ALWAYS, "AURIX: %d core contexts created", AURIX_MAX_CORES);
}

void init_contexts_sys_dep(void) {
    context_extension_offset = context_extension(sizeof(ContextExtensionAurix));
    trace(LOG_ALWAYS, "AURIX: init_contexts_sys_dep: posting init event");
    post_event(aurix_init_event, NULL);
}

#endif /* ENABLE_DebugContext */
