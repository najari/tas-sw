/*
 * stack-crawl-tricore.c - TriCore CSA (Context Save Area) stack crawl.
 *
 * TriCore has no frame pointer chain in the traditional sense: on every
 * call the hardware saves the caller's upper context (A10, A11, D8-D11,
 * A12-A15, D12-D15, PSW and PCXI) into a 64-byte CSA and links it into a
 * list through the saved PCXI values.  The return address of the called
 * function lives in A11 (the TriCore return address register), so walking
 * the chain starting from the live PCXI yields the callers' PCs.
 *
 * PCXI/FCX address format (verified on TC375 hardware):
 *   bits [15:0]  = CSA address bits [21:6]
 *   bits [19:16] = CSA address bits [31:28]
 *   addr = ((pcxi & 0xFFFF) << 6) | ((pcxi & 0xF0000) << 12)
 *
 * CSA word layout (verified on TC375 hardware):
 *   word 0: PCXI (previous context pointer)   word 8-11:  A12-A15
 *   word 1: PSW                               word 12-15: D12-D15
 *   word 2: A10 (SP)
 *   word 3: A11 (return address of the callee)
 *   word 4-7: D8-D11
 *
 * This is used by the framework stack tracer when DWARF frame information
 * is not available (no ELF mapped) - see services/stacktrace.c.
 *
 * SPDX-License-Identifier: MIT (project code only).
 */
#include <tcf/config.h>

#if ENABLE_DebugContext

#include <string.h>
#include <tcf/framework/context.h>
#include <tcf/framework/cpudefs.h>
#include <tcf/framework/errors.h>
#include <machine/aurix/tcf/stack-crawl-tricore.h>

/* TriCore EABI register numbers (see cpudefs-mdep.c) */
#define TC_REG_D8    8
#define TC_REG_D9    9
#define TC_REG_D10  10
#define TC_REG_D11  11
#define TC_REG_D12  12
#define TC_REG_D13  13
#define TC_REG_D14  14
#define TC_REG_D15  15
#define TC_REG_A10  26
#define TC_REG_A11  27
#define TC_REG_A12  28
#define TC_REG_A13  29
#define TC_REG_A14  30
#define TC_REG_A15  31
#define TC_REG_PSW  40
#define TC_REG_PCXI 41
#define TC_REG_PC   42
#define TC_REG_FCX  43

#define CSA_SIZE 64
#define CSA_WORDS (CSA_SIZE / 4)

/* PCXI/FCX -> CSA address */
static ContextAddress csa_address(uint32_t pcxi) {
    return (ContextAddress)((((uint32_t)pcxi & 0xFFFFu) << 6) |
                            (((uint32_t)pcxi & 0xF0000u) << 12));
}

static RegisterDefinition * find_reg(RegisterDefinition * defs, int dwarf_id) {
    RegisterDefinition * def;
    for (def = defs; def != NULL && def->name != NULL; def++) {
        if (def->dwarf_id == dwarf_id) return def;
    }
    return NULL;
}

int crawl_stack_frame_tricore(StackFrame * frame, StackFrame * down) {
    Context * ctx = frame->ctx;
    RegisterDefinition * defs = get_reg_definitions(ctx);
    RegisterDefinition * def_pcxi, *def_pc, *def_a10, *def_a11, *def_fcx;
    uint64_t pcxi = 0, sp = 0, ra = 0, fcx = 0;
    ContextAddress csa, fcx_addr;
    uint8_t buf[CSA_SIZE];
    uint32_t w[CSA_WORDS];
    unsigned i;

    if (defs == NULL) {
        set_errno(ERR_OTHER, "No register definitions");
        return -1;
    }

    def_pcxi = find_reg(defs, TC_REG_PCXI);
    def_pc = find_reg(defs, TC_REG_PC);
    def_a10 = find_reg(defs, TC_REG_A10);
    def_a11 = find_reg(defs, TC_REG_A11);
    def_fcx = find_reg(defs, TC_REG_FCX);
    if (def_pcxi == NULL || def_pc == NULL || def_a10 == NULL || def_a11 == NULL) {
        set_errno(ERR_OTHER, "Required registers not found");
        return -1;
    }

    /* The canonical frame address of the current frame is its stack pointer. */
    if (read_reg_value(frame, def_a10, &sp) == 0) frame->fp = (ContextAddress)sp;

    /* The caller frame's PC is the current frame's A11 (TriCore keeps the
     * return address in A11; it is not part of the saved context). */
    if (read_reg_value(frame, def_a11, &ra) < 0) return -1;
    if (ra == 0) return 0;                          /* end of the chain */

    /* Read the link to the caller's saved context. */
    if (read_reg_value(frame, def_pcxi, &pcxi) < 0) return -1;
    if (pcxi == 0) return 0;                        /* end of the chain */
    csa = csa_address((uint32_t)pcxi);
    if ((csa >> 28) != 0xD) return 0;               /* not a DSPR CSA: stop */

    /* The used CSA chain ends where the free list starts (live FCX). */
    if (def_fcx != NULL && read_reg_value(frame, def_fcx, &fcx) == 0) {
        fcx_addr = csa_address((uint32_t)fcx);
        if (csa == fcx_addr) return 0;
    }

    if (context_read_mem(ctx, csa, buf, CSA_SIZE) < 0) return -1;
    for (i = 0; i < CSA_WORDS; i++) {
        w[i] = (uint32_t)buf[i * 4] | ((uint32_t)buf[i * 4 + 1] << 8) |
               ((uint32_t)buf[i * 4 + 2] << 16) | ((uint32_t)buf[i * 4 + 3] << 24);
    }

    if (w[3] == 0) return 0;                        /* null return address: end */

    /* Fill the caller frame: PC from the current A11, the rest from the CSA. */
#define WR(did, val) do { \
        RegisterDefinition * d = find_reg(defs, did); \
        if (d != NULL && write_reg_value(down, d, (val)) < 0) return -1; \
    } while (0)

    WR(TC_REG_PC,  ra);             /* return address kept in the caller's A11 */
    WR(TC_REG_A10, w[2]);
    WR(TC_REG_A11, w[3]);
    WR(TC_REG_D8,  w[4]);
    WR(TC_REG_D9,  w[5]);
    WR(TC_REG_D10, w[6]);
    WR(TC_REG_D11, w[7]);
    WR(TC_REG_A12, w[8]);
    WR(TC_REG_A13, w[9]);
    WR(TC_REG_A14, w[10]);
    WR(TC_REG_A15, w[11]);
    WR(TC_REG_D12, w[12]);
    WR(TC_REG_D13, w[13]);
    WR(TC_REG_D14, w[14]);
    WR(TC_REG_D15, w[15]);
    WR(TC_REG_PSW, w[1]);
    WR(TC_REG_PCXI, w[0]);          /* the next link in the chain */
#undef WR

    down->has_reg_data = 1;
    return 0;
}

#endif /* ENABLE_DebugContext */
