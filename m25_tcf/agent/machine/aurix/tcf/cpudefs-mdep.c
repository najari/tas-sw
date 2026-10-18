/*
 * cpudefs-mdep.c - TriCore register definitions for the TCF agent (M25).
 *
 * The table below is the register set of the reference MCD stack
 * ("CPU Registers", M7 calibration) with the TriCore EABI DWARF register
 * numbers.  Every register is memory mapped in the per-core CSFR window
 * (CPU0 base 0xF8810000, +0x20000 per core); the register values are
 * read and written by context-aurix.c through the TAS wire client.
 *
 * The framework (tcf/framework/cpudefs.c via cpudefs-ext.h) provides
 * get_reg_definitions(), get_reg_by_id(), read/write_reg_bytes() and
 * get_break_instruction() on top of regs_index and BREAK_INST.
 *
 * SPDX-License-Identifier: MIT (project code only; the TCF framework in
 * this tree keeps its original EPL/EDL license).
 */
#include <tcf/config.h>

#include <stddef.h>
#include <tcf/framework/context.h>
#include <tcf/framework/cpudefs.h>
#include <tcf/framework/errors.h>

#include <tcf/cpudefs-mdep.h>

#define REG_OFFSET(name) offsetof(REG_SET, name)
#define D_OFF(i)         (offsetof(REG_SET, d) + 4 * (i))
#define A_OFF(i)         (offsetof(REG_SET, a) + 4 * (i))

/* CSFR offsets (TC27x UM): the register addresses of the reference map. */
#define CSFR_PCXI   0xFE00u
#define CSFR_PSW    0xFE04u
#define CSFR_PC     0xFE08u
#define CSFR_BIV    0xFE20u
#define CSFR_BTV    0xFE24u
#define CSFR_ISP    0xFE28u
#define CSFR_ICR    0xFE2Cu
#define CSFR_FCX    0xFE38u
#define CSFR_D0     0xFF00u  /* D0-D15: 0xFF00..0xFF3C */
#define CSFR_A0     0xFF80u  /* A0-A15: 0xFF80..0xFFBC */

#define CSFR_BASE   0xF8810000u  /* CPU0 CSFR window */

/*
 * The register set: PC, D0-D15, A0-A15, PSW, PCXI, FCX, ISP, ICR, BIV, BTV
 * (the order of the reference register map).
 *   dwarf_id / eh_frame_id: TriCore EABI table 4-9
 *   memory_address: absolute CSFR address in the CPU0 window
 *   role: "PC" / "SP" / "FP" / "RA" (informational, used by clients)
 */
static RegisterDefinition regs_def[] = {
    { "PC",   REG_OFFSET(pc), 4, 42, 42, .memory_address = CSFR_BASE + CSFR_PC,
      .role = "PC", .description = "Program counter" },

    { "D0",   D_OFF(0),  4,  0,  0, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 0 },
    { "D1",   D_OFF(1),  4,  1,  1, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 1 },
    { "D2",   D_OFF(2),  4,  2,  2, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 2 },
    { "D3",   D_OFF(3),  4,  3,  3, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 3 },
    { "D4",   D_OFF(4),  4,  4,  4, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 4 },
    { "D5",   D_OFF(5),  4,  5,  5, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 5 },
    { "D6",   D_OFF(6),  4,  6,  6, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 6 },
    { "D7",   D_OFF(7),  4,  7,  7, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 7 },
    { "D8",   D_OFF(8),  4,  8,  8, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 8 },
    { "D9",   D_OFF(9),  4,  9,  9, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 9 },
    { "D10",  D_OFF(10), 4, 10, 10, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 10 },
    { "D11",  D_OFF(11), 4, 11, 11, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 11 },
    { "D12",  D_OFF(12), 4, 12, 12, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 12 },
    { "D13",  D_OFF(13), 4, 13, 13, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 13 },
    { "D14",  D_OFF(14), 4, 14, 14, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 14 },
    { "D15",  D_OFF(15), 4, 15, 15, .memory_address = CSFR_BASE + CSFR_D0 + 4 * 15 },

    { "A0",   A_OFF(0),  4, 16, 16, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 0 },
    { "A1",   A_OFF(1),  4, 17, 17, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 1 },
    { "A2",   A_OFF(2),  4, 18, 18, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 2 },
    { "A3",   A_OFF(3),  4, 19, 19, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 3 },
    { "A4",   A_OFF(4),  4, 20, 20, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 4 },
    { "A5",   A_OFF(5),  4, 21, 21, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 5 },
    { "A6",   A_OFF(6),  4, 22, 22, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 6 },
    { "A7",   A_OFF(7),  4, 23, 23, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 7 },
    { "A8",   A_OFF(8),  4, 24, 24, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 8 },
    { "A9",   A_OFF(9),  4, 25, 25, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 9 },
    { "A10",  A_OFF(10), 4, 26, 26, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 10,
      .role = "SP", .description = "Stack pointer" },
    { "A11",  A_OFF(11), 4, 27, 27, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 11,
      .role = "RA", .description = "Return address" },
    { "A12",  A_OFF(12), 4, 28, 28, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 12 },
    { "A13",  A_OFF(13), 4, 29, 29, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 13 },
    { "A14",  A_OFF(14), 4, 30, 30, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 14,
      .role = "FP", .description = "Frame pointer" },
    { "A15",  A_OFF(15), 4, 31, 31, .memory_address = CSFR_BASE + CSFR_A0 + 4 * 15 },

    { "PSW",  REG_OFFSET(psw),  4, 40, 40, .memory_address = CSFR_BASE + CSFR_PSW,
      .description = "Program status word" },
    { "PCXI", REG_OFFSET(pcxi), 4, 41, 41, .memory_address = CSFR_BASE + CSFR_PCXI,
      .description = "Previous context information" },
    { "FCX",  REG_OFFSET(fcx),  4, 43, 43, .memory_address = CSFR_BASE + CSFR_FCX,
      .description = "Free CSA list head pointer" },
    { "ISP",  REG_OFFSET(isp),  4, 45, 45, .memory_address = CSFR_BASE + CSFR_ISP,
      .description = "Interrupt stack pointer" },
    { "ICR",  REG_OFFSET(icr),  4, 46, 46, .memory_address = CSFR_BASE + CSFR_ICR,
      .description = "Interrupt control register" },
    { "BIV",  REG_OFFSET(biv),  4, 48, 48, .memory_address = CSFR_BASE + CSFR_BIV,
      .description = "Interrupt vector table base" },
    { "BTV",  REG_OFFSET(btv),  4, 49, 49, .memory_address = CSFR_BASE + CSFR_BTV,
      .description = "Trap vector table base" },

    { NULL }
};

RegisterDefinition * regs_index = NULL;

/* TriCore 'debug' instruction, bytes in memory order (little-endian) */
unsigned char BREAK_INST[2] = { 0x00, 0xa0 };

void ini_cpudefs_mdep(void) {
    regs_index = regs_def;
}

RegisterDefinition * get_PC_definition(Context * ctx) {
    static RegisterDefinition * reg_def = NULL;

    if (!context_has_state(ctx)) return NULL;
    if (reg_def == NULL) {
        RegisterDefinition * defs = get_reg_definitions(ctx);
        if (defs != NULL) {
            RegisterDefinition * r;
            for (r = defs; r->name != NULL; r++) {
                if (r->offset == offsetof(REG_SET, pc)) {
                    reg_def = r;
                    break;
                }
            }
        }
    }
    return reg_def;
}

/*
 * Stack crawl is not implemented yet (M25-3): the TriCore unwinding uses
 * the CSA chain (PCXI/FCX/LCX).  DWARF based unwinding (dwarfframe.c)
 * already works for frames covered by .debug_frame.
 */
int crawl_stack_frame(StackFrame * frame, StackFrame * down) {
    (void)frame;
    (void)down;
    errno = ERR_UNSUPPORTED;
    return -1;
}
