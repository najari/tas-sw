/*
 * cpudefs-mdep.h - TriCore CPU definitions for the TCF agent (M25 prototype).
 *
 * This is the AURIX (TriCore) counterpart of machine/<host>/tcf/cpudefs-mdep.h.
 * The register set matches the register map of the reference MCD stack
 * (mcdxdas.dll "CPU Registers", 40 entries, M7 calibration):
 *   PC, D0-D15, A0-A15, PSW, PCXI, FCX, ISP, ICR, BIV, BTV
 * All of them are accessible through the per-core CSFR window
 * (CPU0 base 0xF8810000, +0x20000 per core - M20b calibration).
 *
 * DWARF register numbers follow the TriCore EABI (Table 4-9):
 *   D0-D15 = 0-15, A0-A15 = 16-31, PSW = 40, PCXI = 41, PC = 42,
 *   FCX = 43, LCX = 44, ISP = 45, ICR = 46, PIPN = 47, BIV = 48,
 *   BTV = 49.
 *
 * SPDX-License-Identifier: MIT (project code only; the TCF framework in
 * this tree keeps its original EPL/EDL license).
 */
#ifndef D_cpudefs_mdep_aurix
#define D_cpudefs_mdep_aurix

/*
 * REG_SET - a structure that can hold values of target CPU registers.
 * The layout is used by the register cache code in cpudefs.c, so the
 * RegisterDefinition offsets must be relative to this structure.
 */
typedef struct {
    uint32_t pc;
    uint32_t d[16];
    uint32_t a[16];
    uint32_t psw;
    uint32_t pcxi;
    uint32_t fcx;
    uint32_t isp;
    uint32_t icr;
    uint32_t biv;
    uint32_t btv;
} REG_SET;

extern RegisterDefinition * regs_index;

/* TriCore 'debug' instruction (bytes as stored in memory, little-endian) */
extern unsigned char BREAK_INST[2];

#if !defined(ENABLE_ini_cpudefs_mdep)
#define ENABLE_ini_cpudefs_mdep 1
extern void ini_cpudefs_mdep(void);
#endif

#endif /* D_cpudefs_mdep_aurix */
