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
#include <tcf/framework/json.h>
#include <tcf/framework/myalloc.h>
#include <tcf/framework/streams.h>
#include <tcf/framework/trace.h>
#include <tcf/services/breakpoints.h>
#include <tcf/services/contextreset.h>
#include <tcf/services/memorymap.h>
#include <tcf/services/runctrl.h>
#include <tcf/services/symbols.h>

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
#define AURIX_CSFR_PC         0xFE08u      /* program counter            */
#define AURIX_CSFR_DBGSR      0xFD00u      /* debug status register      */
#define AURIX_CSFR_TRIG       0xF000u      /* TR0EVT trigger unit window */
#define AURIX_CSFR_TRIG_ACC   0xFD30u      /* trigger accumulator (R/C)  */
#define AURIX_CSFR_FD10       0xFD10u      /* debug configuration reg    */

/* OCDS trigger units (M18 calibration): TR0..TR7 per core */
#define AURIX_TRIG_UNITS      8
#define AURIX_TRIG_EVT_OFF(n) (AURIX_CSFR_TRIG + 8u * (uint32_t)(n))
#define AURIX_TRIG_ADR_OFF(n) (AURIX_CSFR_TRIG + 4u + 8u * (uint32_t)(n))

/* TRxEVT values: BOD|BBM|EVTA=Halt (0x1A) + access type (M18) */
#define AURIX_TRIG_EVT_IP     0x0000101Au  /* instruction (TYP=PC)       */
#define AURIX_TRIG_EVT_IP_RANGE 0x0000301Au /* instruction range (pair)  */
#define AURIX_TRIG_EVT_WRITE  0x0800001Au  /* data write                 */
#define AURIX_TRIG_EVT_READ   0x1000001Au  /* data read                  */
#define AURIX_TRIG_EVT_RW     0x1800001Au  /* data read or write         */

/* the monitor polling interval (usec) */
#define AURIX_MONITOR_INTERVAL 100000u

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
    /* planted hardware breakpoints (OCDS TR unit -> breakpoint) */
    ContextBreakpoint * hw_bp[AURIX_TRIG_UNITS];
    /* hardware breakpoints that triggered the last stop (NULL terminated) */
    ContextBreakpoint * triggered_hw_bp[AURIX_TRIG_UNITS + 1];
    /* pending trigger accumulator (latched by the run verification) */
    uint32_t trig_acc;
} ContextExtensionAurix;

static size_t context_extension_offset = 0;
#define EXT(ctx) ((ContextExtensionAurix *)((char *)(ctx) + context_extension_offset))

static tas_conn      g_tas;
static int           g_attached = 0;
static tas_con_info  g_con;
static tas_server_info g_srv;
static tas_target    g_targets[TAS_MAX_TARGETS];
static int           g_ntargets = 0;

/* the core contexts, indexed by core (filled at attach) */
static Context *     g_core_ctx[AURIX_MAX_CORES];

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
     * stop - latch the trigger accumulator and report it as a stop
     * (return 1), the breakpoint did its job. */
    if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) != 0)
        return -1;
    if (((v >> 1) & 3u) != 0u) {
        uint32_t acc = 0;
        if (core_csfr_read(core, AURIX_CSFR_TRIG_ACC, &acc) == 0 && acc != 0) {
            if (g_core_ctx[core] != NULL)
                EXT(g_core_ctx[core])->trig_acc |= acc;
            trace(LOG_ALWAYS, "AURIX: core %d halted by a trigger right after "
                  "start (DBGSR=0x%08X, TRIG_ACC=0x%08X)", core, (unsigned)v,
                  (unsigned)acc);
            return 1;
        }
        trace(LOG_ALWAYS, "AURIX: core %d did not start (DBGSR=0x%08X)", core,
              (unsigned)v);
        errno = EIO;
        return -1;
    }
    return 0;
}

/* TRxEVT value for a breakpoint access type (M18) */
static uint32_t aurix_trig_evt(ContextBreakpoint * bp) {
    unsigned at = bp->access_types &
                  ~(unsigned)(CTX_BP_ACCESS_VIRTUAL | CTX_BP_ACCESS_SOFTWARE);

    if (at == CTX_BP_ACCESS_INSTRUCTION) return AURIX_TRIG_EVT_IP;
    if (at == CTX_BP_ACCESS_DATA_WRITE) return AURIX_TRIG_EVT_WRITE;
    if (at == CTX_BP_ACCESS_DATA_READ) return AURIX_TRIG_EVT_READ;
    if (at == (CTX_BP_ACCESS_DATA_READ | CTX_BP_ACCESS_DATA_WRITE))
        return AURIX_TRIG_EVT_RW;
    return 0;
}

/* write the trigger registers of one planted breakpoint (the unit pair
 * for range breakpoints: the even unit holds the event config and the
 * start address, the odd neighbour holds the inclusive end address) */
static void aurix_write_trig(ContextExtensionAurix * ext, int unit) {
    ContextBreakpoint * bp = ext->hw_bp[unit];
    uint32_t evt;

    if (bp == NULL) return;
    if (bp->length > 1) {
        if ((unit & 1) != 0) return;   /* handled by the even unit */
        core_csfr_write(ext->core_index, AURIX_TRIG_ADR_OFF(unit),
                        (uint32_t)bp->address);
        core_csfr_write(ext->core_index, AURIX_TRIG_EVT_OFF(unit),
                        AURIX_TRIG_EVT_IP_RANGE);
        core_csfr_write(ext->core_index, AURIX_TRIG_ADR_OFF(unit + 1),
                        (uint32_t)(bp->address + bp->length - 1));
        return;
    }
    evt = aurix_trig_evt(bp);
    if (evt == 0) return;
    core_csfr_write(ext->core_index, AURIX_TRIG_ADR_OFF(unit), (uint32_t)bp->address);
    core_csfr_write(ext->core_index, AURIX_TRIG_EVT_OFF(unit), evt);
}

/* (re)write the trigger registers of all planted hardware breakpoints:
 * the reference step sequence disables the trigger units, so they must
 * be restored after every step */
static void aurix_rearm_triggers(int core) {
    Context * ctx = g_core_ctx[core];
    ContextExtensionAurix * ext;
    int i;

    if (ctx == NULL) return;
    ext = EXT(ctx);
    for (i = 0; i < AURIX_TRIG_UNITS; i++)
        aurix_write_trig(ext, i);
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
    /* the debug init disables the trigger units - restore the planted
     * hardware breakpoints */
    aurix_rearm_triggers(core);

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

/* read the trigger accumulator: returns the latched value plus the
 * hardware value (reading TRIG_ACC clears it) */
static uint32_t aurix_trig_acc_take(Context * ctx, int core) {
    ContextExtensionAurix * ext = EXT(ctx);
    uint32_t acc = ext->trig_acc;
    uint32_t v = 0;

    ext->trig_acc = 0;
    if (core_csfr_read(core, AURIX_CSFR_TRIG_ACC, &v) == 0)
        acc |= v;
    return acc;
}

/*
 * Report an asynchronous stop of a core: determine the reason (hardware
 * breakpoint via the OCDS trigger accumulator, software breakpoint via
 * the TriCore 'debug' instruction at the PC) and notify the framework.
 * 'async_stop' marks stops that were not requested by the framework
 * (breakpoint hits): they request the intercept, so the client sees the
 * context as suspended.
 */
static void aurix_report_stop(Context * ctx, int core, int async_stop) {
    ContextExtensionAurix * ext = EXT(ctx);
    uint32_t acc = aurix_trig_acc_take(ctx, core);

    ctx->stopped_by_bp = 0;
    ctx->stopped_by_cb = NULL;
    ctx->stopped_by_exception = 0;
    ctx->stopped_by_funccall = 0;
    ext->triggered_hw_bp[0] = NULL;

    if (acc != 0) {
        int i, cnt = 0;
        for (i = 0; i < AURIX_TRIG_UNITS; i++) {
            if ((acc & (1u << i)) != 0 && ext->hw_bp[i] != NULL)
                ext->triggered_hw_bp[cnt++] = ext->hw_bp[i];
        }
        if (cnt > 0) {
            ext->triggered_hw_bp[cnt] = NULL;
            ctx->stopped_by_cb = ext->triggered_hw_bp;
            trace(LOG_ALWAYS, "AURIX: core %d stopped by hardware breakpoint "
                  "(TRIG_ACC=0x%08X, %d bp)", core, (unsigned)acc, cnt);
        }
    }
    if (ctx->stopped_by_cb == NULL) {
        /* software breakpoint?  The TriCore 'debug' instruction halts the
         * core at (or just after) the break instruction - ask the
         * Breakpoints service whether the PC is a planted breakpoint. */
        uint32_t pc = 0;
        if (core_csfr_read(core, AURIX_CSFR_PC, &pc) == 0 &&
            (is_breakpoint_address(ctx, (ContextAddress)pc) ||
             is_breakpoint_address(ctx, (ContextAddress)(pc - 2)))) {
            ctx->stopped_by_bp = 1;
            trace(LOG_ALWAYS, "AURIX: core %d stopped at a software breakpoint "
                  "(PC=0x%08X)", core, (unsigned)pc);
        }
    }

    if (async_stop && !ctx->exiting)
        ctx->pending_intercept = 1;   /* the client must see the stop */
    ctx->stopped = 1;
    send_context_stopped_event(ctx);
}

/* The framework expects stop completion to be reported asynchronously
 * (the caller checks !ctx->stopped right after context_stop()): the
 * target access itself is synchronous here, the state change is
 * delivered by a posted event. */
static void aurix_stopped_event(void * args) {
    Context * ctx = (Context *)args;
    if (!ctx->stopped)
        aurix_report_stop(ctx, EXT(ctx)->core_index, 0);
    context_unlock(ctx);
}

static int post_stopped_event(Context * ctx) {
    context_lock(ctx);
    post_event(aurix_stopped_event, ctx);
    return 0;
}

/* asynchronous stop (breakpoint hit): request the intercept */
static void aurix_async_stop_event(void * args) {
    Context * ctx = (Context *)args;
    if (!ctx->stopped)
        aurix_report_stop(ctx, EXT(ctx)->core_index, 1);
    context_unlock(ctx);
}

static int post_async_stop_event(Context * ctx) {
    context_lock(ctx);
    post_event(aurix_async_stop_event, ctx);
    return 0;
}

/*
 * The core state monitor: the cores can halt by themselves (breakpoint,
 * trigger, trap), so the backend polls the debug status register and
 * reports the transitions (the reference agent has a monitor thread for
 * the same purpose).
 */
static void aurix_monitor(void * args) {
    int i;

    (void)args;
    for (i = 0; i < AURIX_MAX_CORES; i++) {
        Context * ctx = g_core_ctx[i];
        uint32_t v = 0;

        if (ctx == NULL || ctx->exited || ctx->stopped) continue;
        if (core_csfr_read(i, AURIX_CSFR_DBGSR, &v) != 0) continue;
        if (((v >> 1) & 3u) == 1u) {
            trace(LOG_ALWAYS, "AURIX: core %d stopped (DBGSR=0x%08X)", i,
                  (unsigned)v);
            aurix_report_stop(ctx, i, 1);
        }
    }
    if (g_attached)
        post_event_with_delay(aurix_monitor, NULL, AURIX_MONITOR_INTERVAL);
}

int context_has_state(Context * ctx) {
    (void)ctx;
    return 1;
}

const char * context_suspend_reason(Context * ctx) {
    if (ctx->stopped_by_bp || ctx->stopped_by_cb != NULL)
        return REASON_BREAKPOINT;
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

/* the core is halted at a planted instruction breakpoint? */
static int pc_at_instruction_bp(Context * ctx, int core) {
    ContextExtensionAurix * ext = EXT(ctx);
    uint32_t pc = 0;
    int i;

    if (core_csfr_read(core, AURIX_CSFR_PC, &pc) != 0)
        return 0;
    for (i = 0; i < AURIX_TRIG_UNITS; i++) {
        ContextBreakpoint * bp = ext->hw_bp[i];
        if (bp == NULL) continue;
        if ((bp->access_types & CTX_BP_ACCESS_INSTRUCTION) == 0) continue;
        if ((uint32_t)bp->address == pc) return 1;
    }
    return 0;
}

int context_continue(Context * ctx) {
    int core = EXT(ctx)->core_index;
    int rc;

    assert(context_has_state(ctx));
    assert(!ctx->exited);

    /* If the core is halted at a planted instruction breakpoint, step
     * over it first: otherwise the PC trigger fires again immediately
     * (the PC still equals the trigger address). */
    if (pc_at_instruction_bp(ctx, core)) {
        if (core_step(core) != 0)
            return -1;
    }

    rc = core_run(core);
    if (rc < 0)
        return -1;
    if (rc == 0) {
        /* the context is running again (win32_resume does the same) */
        send_context_started_event(ctx);
    }
    else {
        /* a trigger caught the core right after the start: make the
         * framework's view consistent (running) and then report the
         * stop asynchronously, like any other stop */
        send_context_started_event(ctx);
        post_async_stop_event(ctx);
    }
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

/*
 * "Stop after reset" (aurix-debugger -e<label>, or _START with -f): after
 * a device reset the agent plants a temporary breakpoint at the entry
 * function and runs until it is met.  The reference agent logs
 * "entry function found at address" / "entry function met" /
 * "Removing stop at function breakpoint".
 */
extern const char * aurix_entry_label;   /* tcf/main/main.c: -e<label> */
extern int aurix_flasher_used;           /* tcf/main/main.c: -f         */
extern const char * aurix_device_name;   /* tcf/main/main.c: -p<name>   */

static BreakpointInfo * g_entry_bp = NULL;

static void aurix_entry_bp_hit(Context * ctx, void * args) {
    (void)ctx;
    (void)args;
    trace(LOG_ALWAYS, "AURIX: entry function met (stop-at-function breakpoint)");
    if (g_entry_bp != NULL) {
        /* the reference agent: "Removing stop at function breakpoint" */
        destroy_eventpoint(g_entry_bp);
        g_entry_bp = NULL;
    }
}

static BreakpointInfo * aurix_create_entry_bp(ContextAddress addr, Context * ctx) {
    static const char * attr_list[] = { BREAKPOINT_ENABLED, BREAKPOINT_LOCATION, BREAKPOINT_SERVICE };
    BreakpointAttribute * attrs = NULL;
    BreakpointAttribute ** ref = &attrs;
    char str[32];
    unsigned i;

    for (i = 0; i < sizeof(attr_list) / sizeof(char *); i++) {
        ByteArrayOutputStream buf;
        BreakpointAttribute * attr = (BreakpointAttribute *)loc_alloc_zero(sizeof(BreakpointAttribute));
        OutputStream * out = create_byte_array_output_stream(&buf);
        attr->name = loc_strdup(attr_list[i]);
        switch (i) {
        case 0:
            json_write_boolean(out, 1);
            break;
        case 1:
            snprintf(str, sizeof(str), "%#" PRIx64, (uint64_t)addr);
            json_write_string(out, str);
            break;
        case 2:
            json_write_string(out, "RunControl");
            break;
        }
        write_stream(out, 0);
        get_byte_array_output_stream_data(&buf, &attr->value, NULL);
        *ref = attr;
        ref = &attr->next;
    }
    return create_eventpoint_ext(attrs, ctx, aurix_entry_bp_hit, NULL);
}

/* Resolve the -e label (or _START with -f) and plant the entry BP. */
static void aurix_plant_entry_bp(Context * ctx) {
    const char * label = aurix_entry_label;
    Symbol * sym = NULL;
    ContextAddress addr = 0;

    if (label == NULL && aurix_flasher_used) label = "_START";
    if (label == NULL) return;

    if (find_symbol_by_name(ctx, STACK_NO_FRAME, 0, label, &sym) < 0 || sym == NULL) {
        trace(LOG_ALWAYS, "AURIX: stop-after-reset symbol '%s' not found", label);
        return;
    }
    if (get_symbol_address(sym, &addr) < 0 || addr == 0) {
        trace(LOG_ALWAYS, "AURIX: symbol '%s' has no address", label);
        return;
    }
    if (g_entry_bp != NULL) {
        destroy_eventpoint(g_entry_bp);
        g_entry_bp = NULL;
    }
    g_entry_bp = aurix_create_entry_bp(addr, ctx);
    trace(LOG_ALWAYS, "AURIX: entry function found at address: %08x (BP %s)",
          (unsigned)addr, g_entry_bp != NULL ? "planted" : "FAILED");
}

/*
 * Reset post-processing: the device reboots after the reset, so CSFR
 * accesses fail for a short window (M17).  Wait it out, then plant the
 * stop-after-reset breakpoint and release the halt (M17b).
 */
static void aurix_post_reset_event(void * args) {
    Context * ctx = (Context *)args;
    int core = EXT(ctx)->core_index;
    int tries;

    for (tries = 0; tries < 400; tries++) {
        uint32_t v = 0;
        if (core_csfr_read(core, AURIX_CSFR_DBGSR, &v) == 0) break;
    }
    aurix_plant_entry_bp(ctx);
    for (tries = 0; tries < 40; tries++) {
        if (core_run(core) == 0) break;
    }
    send_context_started_event(ctx);
    context_unlock(ctx);
}

/*
 * ContextReset service: device reset through the TAS DEVICE_CONNECT
 * command with the RESET_AND_HALT feature (option 0x0003, M17 mechanism:
 * a repeated DC on an active session performs the reset; DBGSR is left
 * with DE=1 + HALT per M17b).
 *
 * params->suspend: non-zero = the context stays suspended after the reset,
 * zero = run after the reset (the reference agent's "Reset" behavior).
 */
static int aurix_reset(Context * ctx, const ResetParams * params) {
    int core = EXT(ctx)->core_index;
    uint16_t feat = 0;
    uint32_t dtype = 0;
    uint8_t e = 0;

    if (!g_attached) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }

    if (tas_device_connect(&g_tas, 0x0003, &feat, &dtype, &e) != 0 ||
        e != TAS_ERR_OK) {
        set_errno(ERR_OTHER, "TAS device reset (RESET_AND_HALT) failed");
        return -1;
    }
    /* Note: the device reboots after the reset, so CSFR accesses fail for
     * a short window (M17: transient read failures).  Verification and the
     * run release are therefore best-effort here - the core state monitor
     * (aurix_monitor) tracks the real state and reports the transitions. */
    trace(LOG_ALWAYS, "AURIX: core %d device reset performed (DC 0x0003)", core);

    if (params == NULL || params->suspend) {
        /* stay halted after the reset */
        if (!ctx->stopped) post_async_stop_event(ctx);
    }
    else {
        /* run after the reset: the post-processing (entry BP + halt
         * release) waits out the reboot window, see above */
        context_lock(ctx);
        post_event_with_delay(aurix_post_reset_event, ctx, 100000);
    }
    return 0;
}

/*
 * SPR local -> global alias translation (M20 calibration): the local
 * scratchpad windows are CPU private; the debug interface sees the
 * global bus aliases only.
 *   DSPR: 0xD0000000 (128 KiB) -> 0x70000000 - core*0x10000000
 *   PSPR: 0xC0000000 ( 32 KiB) -> 0x70100000 - core*0x10000000
 */
#define AURIX_SPR_DSPR_LOCAL  0xD0000000u
#define AURIX_SPR_DSPR_SIZE   0x20000u
#define AURIX_SPR_DSPR_BUS    0x70000000u
#define AURIX_SPR_PSPR_LOCAL  0xC0000000u
#define AURIX_SPR_PSPR_SIZE   0x8000u
#define AURIX_SPR_PSPR_BUS    0x70100000u
#define AURIX_SPR_CORE_STEP   0x10000000u

static uint32_t aurix_spr_translate(int core, uint32_t addr) {
    uint32_t base = (uint32_t)core * AURIX_SPR_CORE_STEP;

    if (addr >= AURIX_SPR_DSPR_LOCAL && addr < AURIX_SPR_DSPR_LOCAL + AURIX_SPR_DSPR_SIZE)
        return AURIX_SPR_DSPR_BUS - base + (addr - AURIX_SPR_DSPR_LOCAL);
    if (addr >= AURIX_SPR_PSPR_LOCAL && addr < AURIX_SPR_PSPR_LOCAL + AURIX_SPR_PSPR_SIZE)
        return AURIX_SPR_PSPR_BUS - base + (addr - AURIX_SPR_PSPR_LOCAL);
    return addr;
}

/* memory access address: SPR local windows are translated to the core's
 * global bus aliases (M20) */
static uint64_t aurix_mem_addr(Context * ctx, ContextAddress address) {
    if (address <= 0xFFFFFFFFu)
        return (uint64_t)aurix_spr_translate(EXT(ctx)->core_index, (uint32_t)address);
    return (uint64_t)address;
}

int context_write_mem(Context * ctx, ContextAddress address, void * buf,
                      size_t size) {
    if (!g_attached) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    return tas_rw(aurix_mem_addr(ctx, address), buf, size, 1);
}

int context_read_mem(Context * ctx, ContextAddress address, void * buf,
                     size_t size) {
    if (!g_attached) {
        errno = ERR_UNSUPPORTED;
        return -1;
    }
    return tas_rw(aurix_mem_addr(ctx, address), buf, size, 0);
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
    static int disassembler_inited = 0;

    /* register the TriCore disassembler once (the Disassembly service
     * looks it up through the CPU group context) */
    if (!disassembler_inited) {
        disassembler_inited = 1;
        ini_cpu_disassembler(ctx);
    }
    (void)group;
    /* every core is its own process / breakpoint / intercept group */
    return ctx;
}

int context_get_canonical_addr(Context * ctx, ContextAddress addr,
                               Context ** canonical_ctx,
                               ContextAddress * canonical_addr,
                               ContextAddress * block_addr,
                               ContextAddress * block_size) {
    int core = EXT(ctx)->core_index;
    ContextAddress ca = addr;

    if (addr <= 0xFFFFFFFFu) {
        ca = (ContextAddress)aurix_spr_translate(core, (uint32_t)addr);
    }

    *canonical_ctx = ctx;
    if (canonical_addr != NULL) *canonical_addr = ca;
    if (block_addr != NULL) *block_addr = ca & ~(ContextAddress)0xFFF;
    if (block_size != NULL) *block_size = 0x1000;
    return 0;
}

int context_get_memory_map(Context * ctx, MemoryMap * map) {
    /* the reference "Code and Data" space (M8): one region covering the
     * whole address space */
    (void)ctx;
    if (map->region_cnt >= map->region_max) {
        map->region_max += 8;
        map->regions = (MemoryRegion *)loc_realloc(map->regions,
                                                  sizeof(MemoryRegion) * map->region_max);
    }
    {
        MemoryRegion * r = map->regions + map->region_cnt++;
        memset(r, 0, sizeof(MemoryRegion));
        r->addr = 0;
        r->size = 0x100000000ull;
        r->flags = MM_FLAG_R | MM_FLAG_W | MM_FLAG_X;
        r->sect_name = loc_strdup("Code and Data");
    }
    return 0;
}

int context_get_supported_bp_access_types(Context * ctx) {
    (void)ctx;
    /* software breakpoints (the TriCore 'debug' instruction) and hardware
     * breakpoints (OCDS TR0..TR7: instruction / data read / data write) */
    return CTX_BP_ACCESS_INSTRUCTION | CTX_BP_ACCESS_DATA_READ |
           CTX_BP_ACCESS_DATA_WRITE | CTX_BP_ACCESS_SOFTWARE;
}

/*
 * Hardware breakpoints: the OCDS trigger units TR0..TR7 of the core
 * (M18 calibration).  Each unit holds one address (TRxADR) and an event
 * configuration (TRxEVT); a hit halts the core and sets the unit's bit
 * in TRIG_ACC (read clears).  Range breakpoints use an even/odd unit
 * pair: the even unit holds the event config and the start address, the
 * odd neighbour holds the inclusive end address.
 */
int context_plant_breakpoint(ContextBreakpoint * bp) {
    ContextExtensionAurix * ext = EXT(bp->ctx);
    int i;

    if (bp->length > 1) {
        /* range breakpoint: an instruction range needs a free unit pair */
        int u = -1;
        if ((bp->access_types & CTX_BP_ACCESS_INSTRUCTION) == 0 ||
            aurix_trig_evt(bp) == 0) {
            errno = ERR_UNSUPPORTED;
            return -1;
        }
        for (i = 0; i + 1 < AURIX_TRIG_UNITS; i += 2) {
            if (ext->hw_bp[i] == NULL && ext->hw_bp[i + 1] == NULL) {
                u = i;
                break;
            }
        }
        if (u < 0) {
            set_errno(ERR_OTHER, "All hardware breakpoints are already in use");
            return -1;
        }
        ext->hw_bp[u] = bp;
        ext->hw_bp[u + 1] = bp;
        bp->id |= (1u << u) | (1u << (u + 1));
        aurix_write_trig(ext, u);
        trace(LOG_ALWAYS, "AURIX: core %d: hardware breakpoint 0x%08X..0x%08X "
              "planted in TR%d/TR%d", ext->core_index, (unsigned)bp->address,
              (unsigned)(bp->address + bp->length - 1), u, u + 1);
        return 0;
    }

    {
        uint32_t evt = aurix_trig_evt(bp);
        int unit = -1;

        if (evt == 0) {
            errno = ERR_UNSUPPORTED;
            return -1;
        }
        for (i = 0; i < AURIX_TRIG_UNITS; i++) {
            if (ext->hw_bp[i] == NULL) {
                unit = i;
                break;
            }
        }
        if (unit < 0) {
            set_errno(ERR_OTHER, "All hardware breakpoints are already in use");
            return -1;
        }
        ext->hw_bp[unit] = bp;
        bp->id |= 1u << unit;
        if (core_csfr_write(ext->core_index, AURIX_TRIG_ADR_OFF(unit),
                            (uint32_t)bp->address) != 0 ||
            core_csfr_write(ext->core_index, AURIX_TRIG_EVT_OFF(unit), evt) != 0) {
            ext->hw_bp[unit] = NULL;
            bp->id = 0;
            return -1;
        }
        trace(LOG_ALWAYS, "AURIX: core %d: hardware breakpoint 0x%08X "
              "(access 0x%X) planted in TR%d", ext->core_index,
              (unsigned)bp->address, (unsigned)bp->access_types, unit);
    }
    return 0;
}

int context_unplant_breakpoint(ContextBreakpoint * bp) {
    ContextExtensionAurix * ext = EXT(bp->ctx);
    int i;

    for (i = 0; i < AURIX_TRIG_UNITS; i++) {
        if (ext->hw_bp[i] == bp) {
            ext->hw_bp[i] = NULL;
            core_csfr_write(ext->core_index, AURIX_TRIG_EVT_OFF(i), 0);
        }
    }
    if (bp->id != 0)
        trace(LOG_ALWAYS, "AURIX: core %d: hardware breakpoint removed (units 0x%X)",
              ext->core_index, bp->id);
    bp->id = 0;
    return 0;
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
    /* the reference agent reports the hardware breakpoint type (TR0..TR7) */
    static const char * nm[] = { "BreakpointType" };
    static const char * vl[] = { "\"Hardware\"" };
    (void)ctx;
    *names = nm;
    *values = vl;
    *cnt = 1;
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
    int sel = 0;

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

    /* Target selection: the -p option names the TAS target identifier
     * (e.g. "DAS JTAG AURDUINO 46YPEADW").  Without -p the first target is
     * used (the previous behaviour).  The list is always logged so a wrong
     * or ambiguous setup is visible in the agent log. */
    sel = 0;
    if (aurix_device_name != NULL && aurix_device_name[0] != 0) {
        int found = -1;
        for (i = 0; i < g_ntargets; i++) {
            const char * id = g_targets[i].identifier;
            if (strcasecmp(id, aurix_device_name) == 0 ||
                strstr(id, aurix_device_name) != NULL ||
                strstr(aurix_device_name, id) != NULL) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            trace(LOG_ALWAYS, "AURIX: requested target '%s' not found (%d visible):",
                  aurix_device_name, g_ntargets);
            for (i = 0; i < g_ntargets; i++)
                trace(LOG_ALWAYS, "AURIX:   [%d] '%s'", i, g_targets[i].identifier);
            return;
        }
        sel = found;
    }
    for (i = 0; i < g_ntargets; i++)
        trace(LOG_ALWAYS, "AURIX: target [%d] '%s'%s", i, g_targets[i].identifier,
              i == sel ? "   <- selected" : "");

    if (tas_session_start(&g_tas, g_targets[sel].identifier, &g_con, &e) != 0 ||
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
          g_targets[sel].identifier, (unsigned)dtype,
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
        g_core_ctx[i] = ctx;
        send_context_created_event(ctx);

        /* ContextReset: the extension sends type "reset" (lowercase) */
        add_reset(ctx, "reset", "Device reset (reset and halt, TAS DC 0x0003)",
                  aurix_reset);

        /* report the halted state and request the intercept */
        ctx->stopped = 1;
        ctx->pending_intercept = 1;
        send_context_stopped_event(ctx);
    }
    trace(LOG_ALWAYS, "AURIX: %d core contexts created", AURIX_MAX_CORES);

    /* start the core state monitor (asynchronous stop detection) */
    post_event_with_delay(aurix_monitor, NULL, AURIX_MONITOR_INTERVAL);
}

void init_contexts_sys_dep(void) {
    context_extension_offset = context_extension(sizeof(ContextExtensionAurix));
    trace(LOG_ALWAYS, "AURIX: init_contexts_sys_dep: posting init event");
    post_event(aurix_init_event, NULL);
}

#endif /* ENABLE_DebugContext */
