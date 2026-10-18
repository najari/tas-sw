/*
 * context-aurix.c - AURIX TriCore TCF context interface over TAS.
 *
 * M25-1 prototype: the TCF ContextIf (19 functions) mapped to our TAS
 * wire client (the M1-M22 sequences calibrated against mcdxdas.dll).
 * This is the "missing 10%" that Infineon keeps closed in
 * aurix-debugger.exe - implemented here on top of the open Eclipse TCF
 * agent framework.
 *
 * Status: draft skeleton - the structure and the TAS call mapping are
 * complete; the TCF framework API details (context creation, events,
 * memory contexts) are marked TODO and get finalized during the first
 * compile iteration against the upstream tree.
 *
 * SPDX-License-Identifier: MIT (project code only; the TCF framework in
 * this tree keeps its original EPL/EDL license).
 */
#include <tcf/config.h>

#if ENABLE_ContextMux

#include <tcf/framework/context.h>
#include <tcf/framework/cpudefs.h>
#include <tcf/framework/context-dispatcher.h>
#include <tcf/framework/events.h>
#include <tcf/framework/myalloc.h>
#include <tcf/framework/trace.h>
#include <tcf/services/memorymap.h>
#include <tcf/services/runctrl.h>
#include <errno.h>
#include <string.h>

#include "aurix-tas.h"

/* ------------------------------------------------------------------ */
/* the agent state                                                     */
/* ------------------------------------------------------------------ */

#define AURIX_MAX_CORES     3
#define AURIX_CSFR_CPU0     0xF8810000u
#define AURIX_CSFR_STRIDE   0x20000u
#define AURIX_CSFR_PC_OFF   0xFE08u
#define AURIX_CSFR_DBGSR_OFF 0xFD00u

/* the run/step sequences (calibrated in M16/M21/M22 against mcdxdas) */
#define AURIX_ADAPTER_CPU_CTRL  0xF000047Cu   /* the access HW mode reg  */
#define AURIX_ADAPTER_STEP_ON   0xC0u
#define AURIX_ADAPTER_STEP_OFF  0x40u
#define AURIX_ADAPTER_RUN_CMD   0xFA006000u   /* the run command         */
#define AURIX_DBGSR_WR_RUN      0x00000100u   /* DBGSR: HALT=00b, DE=1   */
#define AURIX_DBGSR_WR_HALT     0x00000102u   /* DBGSR: HALT=01b, DE=1   */

typedef struct {
    Context *   ctx;
    int         core_index;
    int         attached;
} AurixCore;

static tas_conn          g_tas;
static int               g_tas_open = 0;
static tas_server_info   g_srv;
static tas_con_info      g_con;
static tas_target        g_targets[TAS_MAX_TARGETS];
static int               g_ntargets = 0;
static AurixCore         g_cores[AURIX_MAX_CORES];

static Context * find_core(int core_index) {
    int i;
    for (i = 0; i < AURIX_MAX_CORES; i++) {
        if (g_cores[i].ctx != NULL && g_cores[i].core_index == core_index)
            return g_cores[i].ctx;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* the TAS helpers                                                     */
/* ------------------------------------------------------------------ */

static int aurix_tas_rw(uint32_t addr, void *buf, size_t size, int is_write) {
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = (uint32_t)size;
    it.is_write = is_write;
    it.data = (uint8_t *)buf;
    rc = tas_rw_exec(&g_tas, &it, 1, g_con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok)
        return 0;
    errno = EIO;
    return -1;
}

static int aurix_read_mem(uint32_t addr, void *buf, size_t size) {
    return aurix_tas_rw(addr, buf, size, 0);
}

static int aurix_write_mem(uint32_t addr, const void *buf, size_t size) {
    return aurix_tas_rw(addr, (void *)buf, size, 1);
}

static int aurix_read32(uint32_t addr, uint32_t *v) {
    return aurix_tas_rw(addr, v, 4, 0);
}

static int aurix_write32(uint32_t addr, uint32_t v) {
    return aurix_tas_rw(addr, &v, 4, 1);
}

static int aurix_dbgsr(int core_index, uint32_t *v) {
    return aurix_read32(AURIX_CSFR_CPU0 +
                        AURIX_CSFR_STRIDE * (uint32_t)core_index +
                        AURIX_CSFR_DBGSR_OFF, v);
}

/* the M22b run sequence: [step-on] [DBGSR run] [step-off] [DBGSR run]
 * [run command] - also releases cores suspended by a prior step. */
static int aurix_core_run(int core_index) {
    uint32_t dbgsr_addr = AURIX_CSFR_CPU0 +
                          AURIX_CSFR_STRIDE * (uint32_t)core_index +
                          AURIX_CSFR_DBGSR_OFF;

    if (aurix_write32(AURIX_ADAPTER_CPU_CTRL, AURIX_ADAPTER_STEP_ON) != 0)
        return -1;
    if (aurix_write32(dbgsr_addr, AURIX_DBGSR_WR_RUN) != 0)
        return -1;
    if (aurix_write32(AURIX_ADAPTER_CPU_CTRL, AURIX_ADAPTER_STEP_OFF) != 0)
        return -1;
    if (aurix_write32(dbgsr_addr, AURIX_DBGSR_WR_RUN) != 0)
        return -1;
    return aurix_write32(AURIX_ADAPTER_RUN_CMD, 2u);
}

/* the M16/M21 halt: DBGSR HALT=01b (+ the verify read - M17b) */
static int aurix_core_halt(int core_index) {
    uint32_t dbgsr_addr = AURIX_CSFR_CPU0 +
                          AURIX_CSFR_STRIDE * (uint32_t)core_index +
                          AURIX_CSFR_DBGSR_OFF;
    uint32_t v = 0;

    if (aurix_write32(dbgsr_addr, AURIX_DBGSR_WR_HALT) != 0)
        return -1;
    if (aurix_dbgsr(core_index, &v) != 0)
        return -1;
    if (((v >> 1) & 3u) != 1u) {        /* HALT field */
        errno = EIO;
        return -1;
    }
    return 0;
}

/* the M21/M22 single step: the access HW holds the core in step mode. */
static int aurix_core_step(int core_index) {
    if (aurix_write32(AURIX_ADAPTER_CPU_CTRL, AURIX_ADAPTER_STEP_ON) != 0)
        return -1;
    return aurix_write32(AURIX_ADAPTER_CPU_CTRL, AURIX_ADAPTER_STEP_OFF);
}

/* ------------------------------------------------------------------ */
/* the ContextIf implementation (the 19 functions!)                    */
/* ------------------------------------------------------------------ */

static int sys_context_has_state(Context * ctx) {
    (void)ctx;
    return 1;                            /* the cores have HALT/run state */
}

static const char * sys_context_suspend_reason(Context * ctx) {
    (void)ctx;
    return REASON_USER_REQUEST;
}

static int sys_context_stop(Context * ctx) {
    AurixCore *c = (AurixCore *)ctx->mem;   /* TODO: the mapping */

    (void)c;
    /* TODO(M25-1): resolve the core from ctx, then: */
    /* return aurix_core_halt(core_index); */
    return 0;
}

static int sys_context_continue(Context * ctx) {
    (void)ctx;
    /* TODO(M25-1): return aurix_core_run(core_index); */
    return 0;
}

static int sys_context_resume(Context * ctx, int mode,
                              ContextAddress range_start,
                              ContextAddress range_end) {
    (void)range_start;
    (void)range_end;
    if (mode == RM_STEP_INTO || mode == RM_STEP_OVER ||
        mode == RM_STEP_OVER_LINE || mode == RM_STEP_OUT)
        return sys_context_single_step(ctx);
    return sys_context_continue(ctx);
}

static int sys_context_can_resume(Context * ctx, int mode) {
    (void)ctx;
    (void)mode;
    return 1;
}

static int sys_context_single_step(Context * ctx) {
    (void)ctx;
    /* TODO(M25-1): return aurix_core_step(core_index); */
    return 0;
}

static int sys_context_write_mem(Context * ctx, ContextAddress address,
                                 void * buf, size_t size) {
    (void)ctx;
    return aurix_write_mem((uint32_t)address, buf, size);
}

static int sys_context_read_mem(Context * ctx, ContextAddress address,
                                void * buf, size_t size) {
    (void)ctx;
    return aurix_read_mem((uint32_t)address, buf, size);
}

static unsigned sys_context_word_size(Context * ctx) {
    (void)ctx;
    return 4;                            /* the TriCore word size */
}

static int sys_context_read_reg(Context * ctx, RegisterDefinition * def,
                                unsigned offs, unsigned size, void * buf) {
    uint32_t addr;

    /* the PC lives in the CPUx CSFR window; the GPRs via the register
     * map (M4/M7 calibration).  TODO(M25-1): the full register map. */
    if (def->name != NULL && strcmp(def->name, "PC") == 0)
        addr = AURIX_CSFR_CPU0 + (uint32_t)offs + AURIX_CSFR_PC_OFF;
    else
        addr = (uint32_t)def->addr + offs;
    return aurix_read_mem(addr, buf, size);
}

static int sys_context_write_reg(Context * ctx, RegisterDefinition * def,
                                 unsigned offs, unsigned size, void * buf) {
    uint32_t addr;

    if (def->name != NULL && strcmp(def->name, "PC") == 0)
        addr = AURIX_CSFR_CPU0 + (uint32_t)offs + AURIX_CSFR_PC_OFF;
    else
        addr = (uint32_t)def->addr + offs;
    return aurix_write_mem(addr, buf, size);
}

static Context * sys_context_get_group(Context * ctx, int group) {
    (void)group;
    return ctx;                          /* TODO(M25-2): the core group */
}

static int sys_context_get_canonical_addr(Context * ctx, ContextAddress addr,
                                          Context ** canonical_ctx,
                                          ContextAddress * canonical_addr,
                                          ContextAddress * block_addr,
                                          ContextAddress * block_size) {
    /* M20: the SPR local -> the global alias translation */
    (void)ctx;
    *canonical_ctx = ctx;
    *canonical_addr = addr;
    if (block_addr != NULL) *block_addr = addr & ~0xFFFu;
    if (block_size != NULL) *block_size = 0x1000u;
    return 0;
}

static int sys_context_get_memory_map(Context * ctx, MemoryMap * map) {
    /* M8: "Code and Data" (id 0, 0x0..0xFFFFFFFF) */
    (void)ctx;
    (void)map;
    return 0;
}

static int sys_context_get_supported_bp_access_types(Context * ctx) {
    (void)ctx;
    return CTX_BP_ACCESS_INSTRUCTION | CTX_BP_ACCESS_DATA;  /* M18: IP + RW */
}

static int sys_context_plant_breakpoint(ContextBreakpoint * bp) {
    /* M18: the OCDS TR0..TR7 units (TRxEVT/TRxADR + TRIG_ACC).
     * TODO(M25-2): the full sequence. */
    (void)bp;
    return 0;
}

static int sys_context_unplant_breakpoint(ContextBreakpoint * bp) {
    (void)bp;
    return 0;
}

ContextIf sys_ctx_if = {
    sys_context_has_state,
    sys_context_suspend_reason,
    sys_context_stop,
    sys_context_continue,
    sys_context_resume,
    sys_context_can_resume,
    sys_context_single_step,
    sys_context_write_mem,
    sys_context_read_mem,
    sys_context_word_size,
    sys_context_read_reg,
    sys_context_write_reg,
    sys_context_get_group,
    sys_context_get_canonical_addr,
    sys_context_get_memory_map,
    sys_context_get_supported_bp_access_types,
    sys_context_plant_breakpoint,
    sys_context_unplant_breakpoint,
#if ENABLE_ContextStateProperties
    NULL,
#endif
#if ENABLE_ExtendedMemoryErrorReports
    NULL,
#endif
#if ENABLE_ExtendedBreakpointStatus
    NULL,
#endif
#if ENABLE_ContextMemoryProperties
    NULL,
#endif
#if ENABLE_ContextExtraProperties
    NULL,
#endif
#if ENABLE_ContextISA
    NULL,
#endif
#if ENABLE_MemoryAccessModes
    NULL,
    NULL,
#endif
#if ENABLE_ContextBreakpointCapabilities
    NULL,
#endif
};

/* ------------------------------------------------------------------ */
/* the CPU definitions (the TriCore register set - minimal M25-1)       */
/* ------------------------------------------------------------------ */

/* the TriCore GPRs: D0-D15 (data) + A0-A15 (address); PC + PSW special.
 * The full DWARF numbering is finalized in machine/aurix/tcf/. */
static RegisterDefinition sys_regs[] = {
    { "PC",  0, 4, 1, 0, 0xFE08, 0, 0 },   /* TODO: the DWARF id/reg */
    { "PSW", 0, 4, 1, 0, 0xFE04, 0, 0 },
    { "D0",  0, 4, 1, 0, 0x0000, 0, 0 },
    /* ... D1-D15, A0-A15 (TODO M25-1: fill in) ... */
    { NULL }
};

static RegisterDefinition * sys_get_reg_definitions(Context * ctx) {
    (void)ctx;
    return sys_regs;
}

static RegisterDefinition * sys_get_PC_definition(Context * ctx) {
    (void)ctx;
    return &sys_regs[0];
}

static RegisterDefinition * sys_get_reg_by_id(Context * ctx, unsigned id,
                                              RegisterIdScope * scope) {
    RegisterDefinition *r;
    (void)scope;
    for (r = sys_get_reg_definitions(ctx); r->name != NULL; r++) {
        if (r->dwarf_id == (int)id)
            return r;
    }
    return NULL;
}

CpuDefsIf sys_cpudefs_if = {
    sys_get_reg_definitions,
    sys_get_PC_definition,
    sys_get_reg_by_id,
    NULL,                                /* read_reg_bytes  - TODO M25-1 */
    NULL,                                /* write_reg_bytes - TODO M25-1 */
    NULL,                                /* get_break_instruction - TODO */
#if ENABLE_StackCrawlMux
    NULL,                                /* crawl_stack_frame - M25-3 */
#endif
#if ENABLE_StackRegisterLocations
    NULL,
#endif
};

/* ------------------------------------------------------------------ */
/* the init - the "missing 10%" (the TAS bring-up!)                    */
/* ------------------------------------------------------------------ */

void init_contexts_sys_dep(void) {
    char detail[160];
    uint8_t e = 0;
    uint16_t feat = 0;
    uint32_t dtype = 0;
    int i;

    /* the TAS bring-up: connect -> server -> targets -> session -> DC */
    detail[0] = 0;
    if (tas_connect(&g_tas, "localhost", TAS_DEFAULT_PORT, detail,
                    sizeof(detail)) != 0) {
        trace(LOG_ALWAYS, "AURIX: TAS connect failed: %s", detail);
        return;
    }
    g_tas_open = 1;
    if (tas_server_connect(&g_tas, "TCF agent (aurix)", "admin",
                           (int)getpid(), &g_srv, &e) != 0 || e != TAS_ERR_OK) {
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
    tas_set_device_key(&g_tas, TAS_ULO_HOT_ATTACH, NULL, 0, &e);
    if (tas_device_connect(&g_tas, 0, &feat, &dtype, &e) != 0 ||
        e != TAS_ERR_OK) {
        trace(LOG_ALWAYS, "AURIX: DEVICE_CONNECT failed (tas_err=0x%02x)", e);
        return;
    }
    trace(LOG_ALWAYS, "AURIX: target '%s' connected (device_type 0x%08X)",
          g_targets[0].identifier, (unsigned)dtype);

    /* the core contexts (CPU0..CPU2 - the multi-core!) */
    for (i = 0; i < AURIX_MAX_CORES; i++) {
        char id[32];
        Context *ctx;

        snprintf(id, sizeof(id), "core%d", i);
        ctx = context_alloc(id);          /* TODO(M25-1): the exact API */
        if (ctx == NULL)
            continue;
        ctx->name = loc_strdup(i == 0 ? "Core 0: CPU0 TriCore"
                                      : (i == 1 ? "Core 1: CPU1 TriCore"
                                                : "Core 2: CPU2 TriCore"));
        ctx->big_endian = 0;
        ctx->mem = ctx;
        ctx->mem_access = MEM_ACCESS_INSTRUCTION | MEM_ACCESS_DATA;
        ctx->reg_access = REG_ACCESS_RD | REG_ACCESS_WR;
        context_set_interface(ctx, &sys_ctx_if);
        g_cores[i].ctx = ctx;
        g_cores[i].core_index = i;
        g_cores[i].attached = 1;
        list_add_last(&ctx->ctxl, &context_root);
        send_context_created_event(ctx);
    }
    trace(LOG_ALWAYS, "AURIX: %d core contexts created", AURIX_MAX_CORES);
}

#endif /* ENABLE_ContextMux */
