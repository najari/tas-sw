/*
 * disassembler-aurix-tricore.c - TriCore disassembler for the TCF
 * Disassembly service (M25-3).
 *
 * Instruction length rule: the LSB of the first (lowest addressed) byte
 * is 0 for 16-bit instructions and 1 for 32-bit instructions.
 *
 * Field layouts were derived empirically with the HighTec toolchain
 * (tricore-elf-as / tricore-elf-objdump) - see the M25-3 document.
 * Common compiler output is covered; anything else is emitted as
 * .half / .word so that decoding stays aligned.
 *
 * SPDX-License-Identifier: MIT (project code only).
 */
#include <tcf/config.h>

#if SERVICE_Disassembly

#include <stdio.h>
#include <string.h>
#include <tcf/framework/context.h>
#include <tcf/framework/myalloc.h>
#include <tcf/services/disassembly.h>
#include <machine/aurix/tcf/disassembler-aurix-tricore.h>

#define INSN_BUF_SIZE 96

static char insn_buf[INSN_BUF_SIZE];
static DisassemblyResult dr;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static const char * dn(int n) {
    static const char * names[16] = {
        "%d0", "%d1", "%d2", "%d3", "%d4", "%d5", "%d6", "%d7",
        "%d8", "%d9", "%d10", "%d11", "%d12", "%d13", "%d14", "%d15"
    };
    return names[n & 15];
}

static const char * an(int n) {
    static const char * names[16] = {
        "%a0", "%a1", "%a2", "%a3", "%a4", "%a5", "%a6", "%a7",
        "%a8", "%a9", "%sp", "%a11", "%a12", "%a13", "%a14", "%a15"
    };
    return names[n & 15];
}

static DisassemblyResult * complete(ContextAddress size) {
    dr.text = insn_buf;
    dr.size = size;
    dr.incomplete = 0;
    return &dr;
}

static DisassemblyResult * emit_unknown(uint16_t h, uint32_t v, ContextAddress size) {
    if (size == 2)
        snprintf(insn_buf, INSN_BUF_SIZE, ".half 0x%04x", h);
    else
        snprintf(insn_buf, INSN_BUF_SIZE, ".word 0x%08x", v);
    return complete(size);
}

/* TriCore 'disp16' style branch target: target = addr + disp * 2 */
static unsigned branch_target(ContextAddress addr, int32_t disp) {
    return (unsigned)(addr + disp * 2);
}

/* ------------------------------------------------------------------ */
/* 16-bit instructions                                                 */
/* ------------------------------------------------------------------ */

static DisassemblyResult * dis_16(uint16_t h, ContextAddress addr) {
    unsigned op = h & 0xFF;
    unsigned d = (h >> 8) & 0xF;       /* [11:8] */
    unsigned s2 = (h >> 12) & 0xF;     /* [15:12] */
    unsigned c8 = ((h >> 12) & 0xF) << 4 | ((h >> 8) & 0xF);   /* [15:8] */
    int32_t disp;

    /* opcode 0x00 family uses [15:8] */
    if (op == 0x00) {
        switch (h) {
        case 0x0000: snprintf(insn_buf, INSN_BUF_SIZE, "nop");  return complete(2);
        case 0x8000: snprintf(insn_buf, INSN_BUF_SIZE, "rfe");  return complete(2);
        case 0x9000: snprintf(insn_buf, INSN_BUF_SIZE, "ret");  return complete(2);
        case 0xA000: snprintf(insn_buf, INSN_BUF_SIZE, "debug"); return complete(2);
        default: break;
        }
        return emit_unknown(h, h, 2);
    }

    switch (op) {
    case 0x02: snprintf(insn_buf, INSN_BUF_SIZE, "mov %s, %s", dn(d), dn(s2)); return complete(2);
    case 0x82: { /* mov d, const4 (signed) */
        int32_t c4 = (int32_t)(s2 << 28) >> 28;
        snprintf(insn_buf, INSN_BUF_SIZE, "mov %s, %d", dn(d), c4); return complete(2);
    }
    case 0xDA: snprintf(insn_buf, INSN_BUF_SIZE, "mov %%d15, %u", (h >> 8) & 0xFF); return complete(2);
    case 0xD4: snprintf(insn_buf, INSN_BUF_SIZE, "ld.a %s, [%s]", an(d), an(s2)); return complete(2);
    case 0x16: snprintf(insn_buf, INSN_BUF_SIZE, "and %%d15, %u", (h >> 8) & 0xFF); return complete(2);
    case 0x96: snprintf(insn_buf, INSN_BUF_SIZE, "or %%d15, %u", (h >> 8) & 0xFF); return complete(2);
    case 0x40: snprintf(insn_buf, INSN_BUF_SIZE, "mov.aa %s, %s", an(d), an(s2)); return complete(2);
    case 0x80: snprintf(insn_buf, INSN_BUF_SIZE, "mov.d %s, %s", dn(d), an(s2)); return complete(2);
    case 0x60: snprintf(insn_buf, INSN_BUF_SIZE, "mov.a %s, %s", an(d), dn(s2)); return complete(2);
    case 0x42: snprintf(insn_buf, INSN_BUF_SIZE, "add %s, %s", dn(d), dn(s2)); return complete(2);
    case 0xC2: { /* add d, const4 (signed) */
        int32_t c4 = (int32_t)(s2 << 28) >> 28;
        snprintf(insn_buf, INSN_BUF_SIZE, "add %s, %d", dn(d), c4); return complete(2);
    }
    case 0xA2: snprintf(insn_buf, INSN_BUF_SIZE, "sub %s, %s", dn(d), dn(s2)); return complete(2);
    case 0x26: snprintf(insn_buf, INSN_BUF_SIZE, "and %s, %s", dn(d), dn(s2)); return complete(2);
    case 0xA6: snprintf(insn_buf, INSN_BUF_SIZE, "or %s, %s", dn(d), dn(s2)); return complete(2);
    case 0xC6: snprintf(insn_buf, INSN_BUF_SIZE, "xor %s, %s", dn(d), dn(s2)); return complete(2);
    case 0x46: snprintf(insn_buf, INSN_BUF_SIZE, "not %s", dn(d)); return complete(2);
    case 0xE2: snprintf(insn_buf, INSN_BUF_SIZE, "mul %s, %s", dn(d), dn(s2)); return complete(2);
    case 0x06: { /* sh d, const4 (signed) */
        int32_t c4 = (int32_t)(s2 << 28) >> 28;
        snprintf(insn_buf, INSN_BUF_SIZE, "sh %s, %d", dn(d), c4); return complete(2);
    }
    case 0x86: { /* sha d, const4 (signed) */
        int32_t c4 = (int32_t)(s2 << 28) >> 28;
        snprintf(insn_buf, INSN_BUF_SIZE, "sha %s, %d", dn(d), c4); return complete(2);
    }
    case 0x20: snprintf(insn_buf, INSN_BUF_SIZE, "sub.a %%sp, %u", c8); return complete(2);
    case 0x30: snprintf(insn_buf, INSN_BUF_SIZE, "add.a %s, %s", an(d), an(s2)); return complete(2);
    case 0xB0: { /* add.a a, const4 (signed) */
        int32_t c4 = (int32_t)(s2 << 28) >> 28;
        snprintf(insn_buf, INSN_BUF_SIZE, "add.a %s, %d", an(d), c4); return complete(2);
    }
    case 0xE0: snprintf(insn_buf, INSN_BUF_SIZE, "bisr %u", c8); return complete(2);

    /* [a] addressing mode */
    case 0x14: snprintf(insn_buf, INSN_BUF_SIZE, "ld.bu %s, [%s]", dn(d), an(s2)); return complete(2);
    case 0x34: snprintf(insn_buf, INSN_BUF_SIZE, "st.b [%s], %s", an(s2), dn(d)); return complete(2);
    case 0x54: snprintf(insn_buf, INSN_BUF_SIZE, "ld.w %s, [%s]", dn(d), an(s2)); return complete(2);
    case 0x74: snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%s], %s", an(s2), dn(d)); return complete(2);
    case 0x94: snprintf(insn_buf, INSN_BUF_SIZE, "ld.h %s, [%s]", dn(d), an(s2)); return complete(2);
    case 0xB4: snprintf(insn_buf, INSN_BUF_SIZE, "st.h [%s], %s", an(s2), dn(d)); return complete(2);

    /* [a+] post-increment addressing mode */
    case 0x04: snprintf(insn_buf, INSN_BUF_SIZE, "ld.bu %s, [%s+]", dn(d), an(s2)); return complete(2);
    case 0x24: snprintf(insn_buf, INSN_BUF_SIZE, "st.b [%s+], %s", an(s2), dn(d)); return complete(2);
    case 0x44: snprintf(insn_buf, INSN_BUF_SIZE, "ld.w %s, [%s+]", dn(d), an(s2)); return complete(2);
    case 0x64: snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%s+], %s", an(s2), dn(d)); return complete(2);
    case 0x84: snprintf(insn_buf, INSN_BUF_SIZE, "ld.h %s, [%s+]", dn(d), an(s2)); return complete(2);
    case 0xA4: snprintf(insn_buf, INSN_BUF_SIZE, "st.h [%s+], %s", an(s2), dn(d)); return complete(2);

    /* a15-relative / general base / sp-relative with 4-bit scaled offset */
    case 0x48: snprintf(insn_buf, INSN_BUF_SIZE, "ld.w %s, [%%a15]%u", dn(d), s2 * 4); return complete(2);
    case 0x4C: snprintf(insn_buf, INSN_BUF_SIZE, "ld.w %%d15, [%s]%u", an(d), s2 * 4); return complete(2);
    case 0x68: snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%%a15]%u, %s", s2 * 4, dn(d)); return complete(2);
    case 0x6C: snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%s]%u, %%d15", an(d), s2 * 4); return complete(2);
    case 0x78: snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%%sp]%u, %%d15", d * 4); return complete(2);

    case 0xDC: snprintf(insn_buf, INSN_BUF_SIZE, "ji %s", an(d)); return complete(2);

    case 0x3C:  /* j disp8 */
    case 0x5C:  /* call disp8 */
        disp = (int32_t)(int8_t)((h >> 8) & 0xFF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s 0x%08x",
                 op == 0x5C ? "call" : "j", branch_target(addr, disp));
        return complete(2);

    case 0x6E:  /* jz d15, disp8 (signed; d15 implied) */
        disp = (int32_t)(int8_t)((h >> 8) & 0xFF);
        snprintf(insn_buf, INSN_BUF_SIZE, "jz %%d15, 0x%08x", branch_target(addr, disp));
        return complete(2);

    case 0x76:  /* jz  d, disp4 (unsigned, forward-only) */
    case 0xF6:  /* jnz d, disp4 */
        disp = (int32_t)((h >> 8) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, 0x%08x",
                 op == 0xF6 ? "jnz" : "jz", dn(s2), branch_target(addr, disp));
        return complete(2);

    case 0x5E:  /* jeq d15, d, disp4 */
    case 0x7E:  /* jne d15, d, disp4 */
        disp = (int32_t)((h >> 8) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %%d15, %s, 0x%08x",
                 op == 0x7E ? "jne" : "jeq", dn(s2), branch_target(addr, disp));
        return complete(2);

    case 0x1E:  /* jeq d15, const4, disp4 */
    case 0x9E:  /* jne d15, const4, disp4 */
        disp = (int32_t)((h >> 8) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %%d15, %u, 0x%08x",
                 op == 0x9E ? "jne" : "jeq", s2, branch_target(addr, disp));
        return complete(2);

    case 0x2E:  /* jz.t d15, const4, disp4 */
    case 0xAE:  /* jnz.t d15, const4, disp4 */
        disp = (int32_t)((h >> 8) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %%d15, %u, 0x%08x",
                 op == 0xAE ? "jnz.t" : "jz.t", s2, branch_target(addr, disp));
        return complete(2);

    default: break;
    }
    return emit_unknown(h, h, 2);
}

/* ------------------------------------------------------------------ */
/* 32-bit instructions                                                 */
/* ------------------------------------------------------------------ */

/* the "split" displacement used by LEA, LD.W/ST.W and the LD.B family */
static int32_t split_disp_lea(uint32_t v) {
    unsigned off = 0;

    off |= (v >> 16) & 0xF;              /* bits 3-0  <- [19:16] */
    off |= ((v >> 20) & 0x3) << 4;       /* bits 4-5  <- [23:20].b0-1 */
    off |= ((v >> 28) & 0xF) << 6;       /* bits 6-9  <- [31:28] */
    off |= ((v >> 20) & 0xC) << 8;       /* bits 10-11 <- [23:20].b2-3 */
    off |= ((v >> 24) & 0xF) << 12;      /* bits 12-15 <- [27:24] */
    off &= 0xFFFF;
    if (off >= 0x8000) off -= 0x10000;
    return (int32_t)off;
}

/* displacement of the LD.B/LD.H/LD.W family: 6-bit signed scaled by 16 plus
 * the low nibble ([27:24].b0 is the word flag, .b3 the addressing mode). */
static int32_t split_disp_ld(uint32_t v) {
    int32_t v6 = (int32_t)(((v >> 20) & 0x3) | (((v >> 28) & 0xF) << 2));

    if (v6 & 0x20) v6 -= 0x40;
    return v6 * 16 + (int32_t)((v >> 16) & 0xF);
}

static DisassemblyResult * dis_32(uint32_t v, ContextAddress addr) {
    unsigned op1 = v & 0xFF;
    unsigned d = (v >> 8) & 0xF;          /* [11:8] */
    unsigned s2 = (v >> 12) & 0xF;        /* [15:12] */
    unsigned dd = (v >> 28) & 0xF;        /* [31:28] */
    int32_t off16 = (int16_t)(v >> 16);

    switch (op1) {
    case 0x19:  /* ld.w d, [a]disp (split displacement) */
        snprintf(insn_buf, INSN_BUF_SIZE, "ld.w %s, [%s]%d", dn(d), an(s2), split_disp_lea(v));
        return complete(4);
    case 0x59:  /* st.w [a]disp, d */
        snprintf(insn_buf, INSN_BUF_SIZE, "st.w [%s]%d, %s", an(s2), split_disp_lea(v), dn(d));
        return complete(4);

    case 0x09:  /* ld.b/bu/h/hu/w d, [a(+)]disp */
    case 0x89: { /* st.b/h/w [a(+)]disp, d */
        static const char * ldn[4] = { "ld.b", "ld.bu", "ld.h", "ld.hu" };
        static const char * stn[4] = { "st.b", NULL, "st.h", NULL };
        unsigned type = (v >> 22) & 0x3;
        int word = (v >> 24) & 1;            /* [27:24].b0: word access */
        int post = ((v >> 27) & 1) == 0;     /* [27:24].b3: 0 = post-increment */
        int32_t off = split_disp_ld(v);
        const char * nm;
        if (word) {
            nm = (op1 == 0x89) ? "st.w" : "ld.w";
        }
        else {
            nm = (op1 == 0x89) ? stn[type] : ldn[type];
            if (nm == NULL) return emit_unknown(0, v, 4);
        }
        if (op1 == 0x89)
            snprintf(insn_buf, INSN_BUF_SIZE, "%s [%s%s]%d, %s", nm, an(s2),
                     post ? "+" : "", off, dn(d));
        else
            snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, [%s%s]%d", nm, dn(d), an(s2),
                     post ? "+" : "", off);
        return complete(4);
    }

    case 0x0B: { /* add/sub/min/max/abs (3-operand) */
        unsigned o2 = (v >> 20) & 0xF;
        unsigned o3 = (v >> 24) & 0xF;
        if (o2 == 0x0 && o3 == 0)
            snprintf(insn_buf, INSN_BUF_SIZE, "add %s, %s, %s", dn(dd), dn(d), dn(s2));
        else if (o2 == 0x8 && o3 == 0)
            snprintf(insn_buf, INSN_BUF_SIZE, "sub %s, %s, %s", dn(dd), dn(d), dn(s2));
        else if (o2 == 0x8 && o3 == 1)
            snprintf(insn_buf, INSN_BUF_SIZE, "min %s, %s, %s", dn(dd), dn(d), dn(s2));
        else if (o2 == 0xA && o3 == 1)
            snprintf(insn_buf, INSN_BUF_SIZE, "max %s, %s, %s", dn(dd), dn(d), dn(s2));
        else if (o2 == 0xC && o3 == 1)
            snprintf(insn_buf, INSN_BUF_SIZE, "abs %s, %s", dn(dd), dn(s2));
        else
            return emit_unknown(0, v, 4);
        return complete(4);
    }
    case 0x0F: { /* and/or/xor/sh/sha (3-operand) */
        unsigned o2 = (v >> 20) & 0xF;
        const char * nm = (o2 == 0x8) ? "and" : (o2 == 0xA) ? "or" : (o2 == 0xC) ? "xor" :
                          (o2 == 0x0) ? "sh" : (o2 == 0x1) ? "sha" : NULL;
        if (nm == NULL) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, %s, %s", nm, dn(dd), dn(d), dn(s2));
        return complete(4);
    }
    case 0x73: { /* mul */
        if (((v >> 16) & 0xF) != 0xA) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "mul %s, %s, %s", dn(dd), dn(d), dn(s2));
        return complete(4);
    }

    case 0x1B: { /* addi d, s2, const9 */
        int32_t c9 = (int32_t)(((v >> 12) & 0xF) | (((v >> 16) & 0xF) << 4) | (((v >> 20) & 0x1) << 8));
        if (c9 & 0x100) c9 -= 0x200;
        snprintf(insn_buf, INSN_BUF_SIZE, "addi %s, %s, %d", dn(dd), dn(d), c9);
        return complete(4);
    }
    case 0x9B: { /* addih d, s2, const9 (unsigned) */
        int32_t c9 = (int32_t)(((v >> 12) & 0xF) | (((v >> 16) & 0xF) << 4) | (((v >> 20) & 0x1) << 8));
        snprintf(insn_buf, INSN_BUF_SIZE, "addih %s, %s, %d", dn(dd), dn(d), c9);
        return complete(4);
    }

    case 0x01: { /* addsc.a a, a, d, shift ([27:24] == 6) */
        if (((v >> 24) & 0xF) != 6) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "addsc.a %s, %s, %s, %u", an(dd), an(s2),
                 dn(d), (v >> 16) & 0xF);
        return complete(4);
    }

    case 0x03:   /* madd d, s1, s2, s3 ([19:16] == 0xA) */
    case 0x23: { /* msub d, s1, s2, s3 */
        if (((v >> 16) & 0xF) != 0xA) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, %s, %s, %s", op1 == 0x23 ? "msub" : "madd",
                 dn(dd), dn((v >> 24) & 0xF), dn(d), dn(s2));
        return complete(4);
    }

    case 0x8F: { /* sh/sha d, s, const5 ([27:24] = 0 sh / 2 sha) */
        unsigned o2 = (v >> 24) & 0xF;
        unsigned c5;
        if (o2 != 0 && o2 != 2) return emit_unknown(0, v, 4);
        c5 = (((v >> 20) & 1) << 4) | ((v >> 16) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, %s, %u", o2 == 2 ? "sha" : "sh",
                 dn(dd), dn(d), c5);
        return complete(4);
    }

    case 0xA5: { /* st.w 0xf000<off16>, d */
        snprintf(insn_buf, INSN_BUF_SIZE, "st.w 0xf000%04x, %s", (unsigned)(v >> 16) & 0xFFFF, dn(d));
        return complete(4);
    }
    case 0x91: { /* movh.a a, imm16 = {[27:16], [15:12]} */
        unsigned imm = (((v >> 16) & 0xFFF) << 4) | ((v >> 12) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "movh.a %s, 0x%x", an(dd), imm);
        return complete(4);
    }
    case 0xD9: { /* lea a, [a]disp (split displacement) */
        snprintf(insn_buf, INSN_BUF_SIZE, "lea %s, [%s]%d", an(d), an(s2), split_disp_lea(v));
        return complete(4);
    }
    case 0xC5: { /* lea a, addr (absolute, same split displacement) */
        snprintf(insn_buf, INSN_BUF_SIZE, "lea %s, 0x%x", an(d), (unsigned)split_disp_lea(v) & 0xFFFF);
        return complete(4);
    }

    case 0x49: { /* lea a, [a]disp (short displacement, no [27:24] part) */
        unsigned off = 0;
        off |= (v >> 16) & 0xF;
        off |= ((v >> 20) & 0x3) << 4;
        off |= ((v >> 28) & 0xF) << 6;
        off |= ((v >> 20) & 0xC) << 8;
        if (off & 0x800) off -= 0x1000;
        snprintf(insn_buf, INSN_BUF_SIZE, "lea %s, [%s]%d", an(d), an(s2), (int32_t)off);
        return complete(4);
    }

    case 0x37: { /* extr d, s, pos, width ([27:24] == 0; width in [19:16]) */
        if (((v >> 24) & 0xF) != 0) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "extr %s, %s, %u, %u", dn(dd), dn(d), s2, (v >> 16) & 0xF);
        return complete(4);
    }

    case 0x1D:  /* j disp16 */
    case 0x6D:  /* call disp16 */
        snprintf(insn_buf, INSN_BUF_SIZE, "%s 0x%08x",
                 op1 == 0x6D ? "call" : "j", branch_target(addr, off16));
        return complete(4);

    case 0x2D:  /* calli a */
        snprintf(insn_buf, INSN_BUF_SIZE, "calli %s", an(d));
        return complete(4);

    case 0x5F: { /* jeq/jne s1, s2, disp16 ([31] = negate; s1 = [11:8]) */
        unsigned neg = (v >> 31) & 1;
        int32_t disp = (int32_t)(int16_t)(((v >> 16) & 0x7FFF) << 1) >> 1;
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, %s, 0x%08x", neg ? "jne" : "jeq",
                 dn(d), dn(s2), branch_target(addr, disp));
        return complete(4);
    }
    case 0x3F:  /* jlt/jlt.u s1, s2, disp16 ([31] = unsigned) */
    case 0x7F: { /* jge/jge.u s1, s2, disp16 */
        unsigned u = (v >> 31) & 1;
        int32_t disp = (int32_t)(int16_t)(((v >> 16) & 0x7FFF) << 1) >> 1;
        snprintf(insn_buf, INSN_BUF_SIZE, "%s%s %s, %s, 0x%08x",
                 op1 == 0x7F ? "jge" : "jlt", u ? ".u" : "", dn(d), dn(s2),
                 branch_target(addr, disp));
        return complete(4);
    }

    case 0x3B: { /* mov d, const16 = {[27:24], [23:20], [19:16], [15:12]} */
        unsigned c = (((v >> 24) & 0xF) << 12) | (((v >> 20) & 0xF) << 8) |
                     (((v >> 16) & 0xF) << 4) | ((v >> 12) & 0xF);
        snprintf(insn_buf, INSN_BUF_SIZE, "mov %s, 0x%x", dn(dd), c);
        return complete(4);
    }

    case 0xDF: { /* jeq/jne d, const4, disp16 ([31] = negate) */
        unsigned neg = (v >> 31) & 1;
        int32_t disp = (int32_t)(int16_t)(((v >> 16) & 0x7FFF) << 1) >> 1;
        snprintf(insn_buf, INSN_BUF_SIZE, "%s %s, %u, 0x%08x", neg ? "jne" : "jeq",
                 dn(d), s2, branch_target(addr, disp));
        return complete(4);
    }

    case 0xFD: { /* loop a, disp16 */
        snprintf(insn_buf, INSN_BUF_SIZE, "loop %s, 0x%08x", an(s2),
                 branch_target(addr, (int32_t)(int16_t)(v >> 16)));
        return complete(4);
    }

    case 0x4D:  /* mfcr d, csr */
        snprintf(insn_buf, INSN_BUF_SIZE, "mfcr %s, 0x%04x", dn(d),
                 (unsigned)((((v >> 16) & 0xFF) << 8) | s2));
        return complete(4);
    case 0xCD:  /* mtcr csr, d */
        snprintf(insn_buf, INSN_BUF_SIZE, "mtcr 0x%04x, %s",
                 (unsigned)((((v >> 16) & 0xFF) << 8) | s2), dn(d));
        return complete(4);

    case 0xAD:  /* syscall const */
        snprintf(insn_buf, INSN_BUF_SIZE, "syscall %u", s2);
        return complete(4);

    case 0x0D: { /* system sub-ops */
        unsigned o2 = (v >> 20) & 0xF;
        const char * nm = (o2 == 0x0) ? "svlcx" : (o2 == 0x4) ? "rslcx" :
                          (o2 == 0x1) ? "rfm" : NULL;
        if (nm == NULL) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "%s", nm);
        return complete(4);
    }

    case 0x8B: { /* lt d, s2, const4 (op2 = [19:16] == 4) */
        if (((v >> 16) & 0xF) != 0x4) return emit_unknown(0, v, 4);
        snprintf(insn_buf, INSN_BUF_SIZE, "lt %s, %s, %u", dn(dd), dn(d), s2);
        return complete(4);
    }

    default: break;
    }
    return emit_unknown(0, v, 4);
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */
/* ------------------------------------------------------------------ */

DisassemblyResult * disassemble_tricore(uint8_t * code, ContextAddress addr,
                                        ContextAddress size, DisassemblerParams * params) {
    uint16_t h;
    uint32_t v;

    (void)params;
    memset(&dr, 0, sizeof(dr));
    if (size < 2) return NULL;

    h = (uint16_t)(code[0] | (code[1] << 8));

    /* instruction length: LSB of the first byte (0 = 16-bit, 1 = 32-bit) */
    if ((code[0] & 1) == 0) return dis_16(h, addr);

    if (size < 4) return NULL;
    v = (uint32_t)(code[0] | (code[1] << 8) | (code[2] << 16) | (code[3] << 24));
    return dis_32(v, addr);
}

#endif /* SERVICE_Disassembly */
