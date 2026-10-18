/*
 * disassembler-aurix-tricore.h - TriCore disassembler (M25-3).
 *
 * SPDX-License-Identifier: MIT (project code only).
 */
#ifndef D_disassembler_aurix_tricore
#define D_disassembler_aurix_tricore

#include <tcf/services/disassembly.h>

extern DisassemblyResult * disassemble_tricore(uint8_t * code, ContextAddress addr,
                                               ContextAddress size, DisassemblerParams * params);

#endif /* D_disassembler_aurix_tricore */
