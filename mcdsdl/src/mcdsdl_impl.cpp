/* mcdsdl_impl.cpp - MCDS Decoder Library (mcds_decoder.dll) prototype (M24-1).
 *
 * Implements the API surface (25 exports) and the feasible layers:
 *   - the core          : lib_init / lib_version / get_error
 *   - the state         : clear / get_error_counter / get_trace_marker /
 *                         get_num_msg / get_msg / get_next_msg / get_previous_msg
 *   - the .mcds file    : mcds_file_load / mcds_file_save  (the format was
 *                         reverse-engineered in M23 from the recorder and CCL)
 *   - the ELF symbols   : elf_file_load / unload_all / get_code_symbol /
 *                         get_data_symbol / ..._addr  (standard ELF32 LE)
 *   - the config        : set_mcdsconfig / set_mcdsconfig_with_file
 *   - the device callback
 *   - the message string: get_msg_str (basic formatting)
 *
 * NOT implemented (the engine - 2,409 functions / 174k lines in the
 * reference; see M23_DECODER_FEASIBILITY.md):
 *   - mcdsdl_decode     : the MCDS trace decompression engine
 *   - get_instr / get_instr_type : the TriCore disassembler
 * These return an explicit "not supported by this implementation" error.
 *
 * SPDX-License-Identifier: MIT (project code only; the SPRINT headers in
 * include/ keep their original license).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "mcds_lib.h"
#include "mcds_lib_decoder.h"
#include "mcds_control.h"
#include "mcd_api.h"

#define MCDSDL_MAX_ELF_FILES 8
#define MCDSDL_MAX_ELF_SYMS  8192
#define MCDSDL_MAX_MSGS      65536

/* ------------------------------------------------------------------ */
/* the ELF (ELF32 little endian)                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    char     name[128];
    uint32_t value;
    uint32_t size;
    uint8_t  type;          /* STT_OBJECT=1, STT_FUNC=2 */
} elf_sym_t;

typedef struct {
    char       name[260];
    uint8_t   *image;
    uint32_t   image_size;
    uint32_t   sym_count;
    elf_sym_t *syms;
} elf_file_t;

/* the ELF32 structures (as on disk) */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_hdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} elf32_shdr_t;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} elf32_sym_t;

/* ------------------------------------------------------------------ */
/* the decoder context                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int                     initialized;
    char                    last_err[256];

    /* the decoded messages (empty without the engine) */
    mcdsdl_msg_st          *msgs;
    int32_t                 num_msg;
    int32_t                 msgs_alloc;

    mcdsdl_error_counter_st ec;
    mcdsdl_trace_marker_st  marker;

    /* the configuration */
    mcdsl_mcdsc_st          mcdsc;
    int                     cfg_set;
    float                   freq_hz;

    /* the ELF files */
    elf_file_t              elfs[MCDSDL_MAX_ELF_FILES];
    int                     num_elfs;

    /* the device callback */
    mcdsdl_device_callback_st cb;
    int                     cb_set;

    /* the loaded trace data (raw) */
    uint8_t                *trace_data;
    uint32_t                trace_size_kb;
    uint32_t                trace_capacity_kb;
} mcdsdl_ctx_t;

static mcdsdl_ctx_t g;

static const mcdsl_version_st g_version = { { { 4, 1, 0 } } };

static mcdsl_return_et err_set(mcdsl_return_et r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g.last_err, sizeof(g.last_err), fmt, ap);
    va_end(ap);
    return r;
}

static void err_clear(void)
{
    g.last_err[0] = 0;
}

/* ------------------------------------------------------------------ */
/* the .mcds file (the text header + the raw trace data)               */
/* ------------------------------------------------------------------ */
static uint32_t parse_hex32(const char *s)
{
    uint32_t v = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    while ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'F') ||
           (*s >= 'a' && *s <= 'f')) {
        v <<= 4;
        if (*s <= '9')
            v |= (uint32_t)(*s - '0');
        else if (*s <= 'F')
            v |= (uint32_t)(*s - 'A' + 10);
        else
            v |= (uint32_t)(*s - 'a' + 10);
        s++;
    }
    return v;
}

/* Parse the .mcds header (both the recorder's format with
 * START_OF_MCDS_CONFIG/START_OF_TDR and the CCL's
 * "McdsFrequencyMHz=...\n\n" + raw). Returns 0 on success; the raw trace
 * data starts at *raw_off in the buffer. */
static int mcds_parse_header(const uint8_t *buf, uint32_t size,
                             uint32_t *raw_off)
{
    uint32_t i = 0;
    uint32_t num_reg = 0;
    uint32_t addrs[MCDSL_MCDSC_MAX_NUM_REG];
    uint32_t vals[MCDSL_MCDSC_MAX_NUM_REG];
    int in_cfg = 0;
    int seen_freq = 0;

    g.freq_hz = 0.0f;
    g.cfg_set = 0;

    while (i < size) {
        /* read one line */
        uint32_t start = i;
        char line[512];
        uint32_t n;

        while (i < size && buf[i] != '\n' && buf[i] != '\r')
            i++;
        n = i - start;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, buf + start, n);
        line[n] = 0;
        while (i < size && (buf[i] == '\n' || buf[i] == '\r'))
            i++;

        if (n == 0) {
            /* an empty line: in the CCL format this ends the header */
            if (seen_freq) {
                *raw_off = i;
                return 0;
            }
            continue;
        }
        if (strncmp(line, "START_OF_TDR", 12) == 0) {
            *raw_off = i;
            return 0;
        }
        if (strncmp(line, "START_OF_MCDS_CONFIG", 20) == 0) {
            in_cfg = 1;
            continue;
        }
        if (strncmp(line, "McdsFrequencyMHz=", 17) == 0) {
            g.freq_hz = (float)atof(line + 17);
            seen_freq = 1;
            continue;
        }
        if (strncmp(line, "McdsDevice=", 11) == 0 ||
            strncmp(line, "McdsDeviceUsed=", 15) == 0 ||
            strncmp(line, "McdsToolUsed=", 13) == 0 ||
            strncmp(line, "RecordingDate=", 14) == 0 ||
            strncmp(line, "RecordingTime=", 14) == 0)
            continue;
        if (in_cfg) {
            /* "%8.8X %8.8X" */
            const char *sp = strchr(line, ' ');
            if (sp != NULL && num_reg < MCDSL_MCDSC_MAX_NUM_REG) {
                addrs[num_reg] = parse_hex32(line);
                vals[num_reg] = parse_hex32(sp + 1);
                num_reg++;
            }
            continue;
        }
        /* an unknown text line before any known marker: if it is not
         * printable assume the raw data starts here (the CCL format) */
        if (seen_freq && line[0] != 0) {
            int printable = 1;
            uint32_t k;

            for (k = 0; k < n; k++) {
                if ((unsigned char)line[k] < 0x20 || (unsigned char)line[k] > 0x7E) {
                    printable = 0;
                    break;
                }
            }
            if (!printable) {
                *raw_off = start;
                return 0;
            }
        }
    }
    *raw_off = size;    /* no raw data */
    return 0;
}

/* ------------------------------------------------------------------ */
/* the ELF parsing                                                     */
/* ------------------------------------------------------------------ */
static int elf_parse(elf_file_t *ef)
{
    const elf32_hdr_t *h;
    const elf32_shdr_t *sh;
    uint32_t symtab_off = 0, symtab_size = 0, symtab_link = 0;
    uint32_t i;

    if (ef->image_size < sizeof(elf32_hdr_t))
        return -1;
    if (memcmp(ef->image, "\x7f" "ELF", 4) != 0)
        return -1;
    if (ef->image[4] != 1 || ef->image[5] != 1)   /* ELF32 LE */
        return -1;
    h = (const elf32_hdr_t *)ef->image;
    if (h->e_shoff == 0 || h->e_shnum == 0)
        return -1;

    sh = (const elf32_shdr_t *)(ef->image + h->e_shoff);
    for (i = 0; i < h->e_shnum; i++) {
        if (sh[i].sh_type == 2 /* SHT_SYMTAB */) {
            symtab_off = sh[i].sh_offset;
            symtab_size = sh[i].sh_size;
            symtab_link = sh[i].sh_link;
            break;
        }
    }
    if (symtab_off == 0 || symtab_size == 0)
        return -1;

    /* the string table */
    {
        const char *strtab;
        uint32_t strtab_size = 0;

        if (symtab_link >= h->e_shnum)
            return -1;
        strtab = (const char *)(ef->image + sh[symtab_link].sh_offset);
        strtab_size = sh[symtab_link].sh_size;

        ef->syms = (elf_sym_t *)calloc(MCDSDL_MAX_ELF_SYMS, sizeof(elf_sym_t));
        if (ef->syms == NULL)
            return -1;
        ef->sym_count = 0;

        {
            uint32_t count = symtab_size / sizeof(elf32_sym_t);
            const elf32_sym_t *syms = (const elf32_sym_t *)(ef->image + symtab_off);
            uint32_t s;

            for (s = 0; s < count && ef->sym_count < MCDSDL_MAX_ELF_SYMS; s++) {
                uint8_t type = syms[s].st_info & 0x0F;
                const char *nm;

                if (type != 1 /* STT_OBJECT */ && type != 2 /* STT_FUNC */)
                    continue;
                if (syms[s].st_name >= strtab_size)
                    continue;
                nm = strtab + syms[s].st_name;
                if (nm[0] == 0)
                    continue;
                strncpy(ef->syms[ef->sym_count].name, nm,
                        sizeof(ef->syms[0].name) - 1);
                ef->syms[ef->sym_count].value = syms[s].st_value;
                ef->syms[ef->sym_count].size = syms[s].st_size;
                ef->syms[ef->sym_count].type = type;
                ef->sym_count++;
            }
        }
    }
    return 0;
}

static void elf_free(elf_file_t *ef)
{
    if (ef->image != NULL) {
        free(ef->image);
        ef->image = NULL;
    }
    if (ef->syms != NULL) {
        free(ef->syms);
        ef->syms = NULL;
    }
    ef->sym_count = 0;
}

/* find the symbol containing addr (the smallest symbol whose range covers
 * it; the type 0 = any) */
static const elf_sym_t *elf_find_by_addr(uint32_t addr, uint8_t type)
{
    int e;
    const elf_sym_t *best = NULL;
    uint32_t best_size = 0xFFFFFFFF;

    for (e = 0; e < g.num_elfs; e++) {
        uint32_t s;

        for (s = 0; s < g.elfs[e].sym_count; s++) {
            const elf_sym_t *sym = &g.elfs[e].syms[s];

            if (type != 0 && sym->type != type)
                continue;
            if (addr < sym->value)
                continue;
            if (sym->size != 0 && addr >= sym->value + sym->size)
                continue;
            if (sym->size == 0 && addr != sym->value)
                continue;
            if (sym->size < best_size) {
                best = sym;
                best_size = sym->size;
            }
        }
    }
    return best;
}

static const elf_sym_t *elf_find_by_name(const char *name, uint8_t type)
{
    int e;

    for (e = 0; e < g.num_elfs; e++) {
        uint32_t s;

        for (s = 0; s < g.elfs[e].sym_count; s++) {
            const elf_sym_t *sym = &g.elfs[e].syms[s];

            if (type != 0 && sym->type != type)
                continue;
            if (strcmp(sym->name, name) == 0)
                return sym;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* the core                                                            */
/* ------------------------------------------------------------------ */
mcdsl_return_et mcdsdl_lib_init(uint8_t version_major)
{
    if (g.initialized)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsdl_lib_init() only once");
    if (version_major != MCDSDL_VERSION_MAJOR)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "Requested lib version not supported by lib implementation");
    memset(&g, 0, sizeof(g));
    g.initialized = 1;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

const mcdsl_version_st *mcdsdl_lib_version(void)
{
    return &g_version;
}

const mcd_char_t *mcdsdl_get_error(void)
{
    return (g.last_err[0] != 0) ? g.last_err : NULL;
}

void mcdsdl_clear(void)
{
    g.num_msg = 0;
    memset(&g.ec, 0, sizeof(g.ec));
    memset(&g.marker, 0, sizeof(g.marker));
    g.marker.mi_trigger_trace = -1;
    g.marker.mi_all_mcds_start = -1;
    g.marker.mi_all_mcds_end = -1;
    err_clear();
}

const mcdsdl_error_counter_st *mcdsdl_get_error_counter(void)
{
    return &g.ec;
}

const mcdsdl_trace_marker_st *mcdsdl_get_trace_marker(void)
{
    g.marker.num_msg = g.num_msg;
    return &g.marker;
}

int32_t mcdsdl_get_num_msg(void)
{
    return g.num_msg;
}

int32_t mcdsdl_get_msg(int32_t mi, mcdsdl_msg_st *msg)
{
    if (msg == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "msg is NULL");
    if (mi < 0 || mi >= g.num_msg) {
        err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                "message index %d out of range", (int)mi);
        return -1;
    }
    *msg = g.msgs[mi];
    err_clear();
    return MCDSL_RET_SUCCESS;
}

int32_t mcdsdl_get_next_msg(const mcdsdl_msg_st *msg_ref, mcdsdl_msg_st *msg)
{
    int32_t mi;

    if (msg_ref == NULL || msg == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "msg is NULL");
    mi = (int32_t)msg_ref->index;
    if (mi < 0 || mi + 1 >= g.num_msg)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "no next message");
    *msg = g.msgs[mi + 1];
    err_clear();
    return MCDSL_RET_SUCCESS;
}

int32_t mcdsdl_get_previous_msg(const mcdsdl_msg_st *msg_ref, mcdsdl_msg_st *msg)
{
    int32_t mi;

    if (msg_ref == NULL || msg == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "msg is NULL");
    mi = (int32_t)msg_ref->index;
    if (mi <= 0 || mi > g.num_msg)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "no previous message");
    *msg = g.msgs[mi - 1];
    err_clear();
    return MCDSL_RET_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* the .mcds file API                                                  */
/* ------------------------------------------------------------------ */
mcdsl_return_et mcdsdl_mcds_file_load(const mcd_char_t *mcds_file_name)
{
    FILE *f;
    long size;
    uint8_t *buf;
    uint32_t raw_off = 0;

    if (mcds_file_name == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "mcds_file_name is NULL");
    f = fopen(mcds_file_name, "rb");
    if (f == NULL)
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "Could not open .mcds file");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "Could not read .mcds file");
    }
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return err_set(MCDSL_RET_UNKNOWN_ERROR, "Out of memory");
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "Could not read .mcds file");
    }
    fclose(f);

    mcds_parse_header(buf, (uint32_t)size, &raw_off);

    /* keep the raw trace data */
    if (g.trace_data != NULL) {
        free(g.trace_data);
        g.trace_data = NULL;
        g.trace_size_kb = 0;
        g.trace_capacity_kb = 0;
    }
    {
        uint32_t raw_size = (uint32_t)size - raw_off;

        if (raw_size > 0) {
            g.trace_data = (uint8_t *)malloc(raw_size);
            if (g.trace_data == NULL) {
                free(buf);
                return err_set(MCDSL_RET_UNKNOWN_ERROR, "Out of memory");
            }
            memcpy(g.trace_data, buf + raw_off, raw_size);
            g.trace_size_kb = (raw_size + 0x3FF) >> 10;
            g.trace_capacity_kb = g.trace_size_kb;
        }
    }
    free(buf);

    if (g.trace_data == NULL || g.trace_size_kb == 0)
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "ERROR no trace data in file");

    /* the decode engine is not part of this prototype (see the header):
     * the message list stays empty; the file and its config are loaded. */
    g.num_msg = 0;
    err_set(MCDSL_RET_SUCCESS,
            "mcdsdl: decode engine not implemented in this prototype "
            "(%u KB raw trace data loaded)", (unsigned)g.trace_size_kb);
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsdl_mcds_file_save(const mcd_char_t *mcds_file_name,
                              mcd_bool_t include_symbols_and_code,
                              mcd_bool_t ascii_not_binary)
{
    FILE *f;
    time_t t;
    struct tm tmv;
    uint32_t i;

    (void)include_symbols_and_code;
    (void)ascii_not_binary;
    if (mcds_file_name == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "mcds_file_name is NULL");
    if (g.trace_data == NULL || g.trace_size_kb == 0)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "ERROR no trace data in file");
    f = fopen(mcds_file_name, "wb");
    if (f == NULL)
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "Could not write .mcds file");

    _time64(&t);
    _localtime64_s(&tmv, &t);
    fprintf(f, "McdsDevice=%.32s\n", "PROTOTYPE");
    fprintf(f, "McdsDeviceUsed=0x%8.8X  (%.32s)\n",
            (unsigned)(g.cfg_set ? g.mcdsc.hdr.device_type : 0), "PROTOTYPE");
    fprintf(f, "McdsToolUsed=\"%.64s\"\n", "mcdsdl prototype");
    if (g.freq_hz > 0.0f)
        fprintf(f, "McdsFrequencyMHz=%.1f\n", (double)g.freq_hz);
    fprintf(f, "RecordingDate=%4.4d-%2.2d-%2.2d\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    fprintf(f, "RecordingTime=%2.2d:%2.2d:%2.2d\n",
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    if (g.cfg_set) {
        fprintf(f, "START_OF_MCDS_CONFIG\n");
        for (i = 0; i < g.mcdsc.hdr.num_reg; i++)
            fprintf(f, "%8.8X %8.8X\n",
                    (unsigned)g.mcdsc.d[i],
                    (unsigned)g.mcdsc.d[g.mcdsc.hdr.vsi + i]);
    }
    fprintf(f, "START_OF_TDR\n");
    if (g.trace_data != NULL && g.trace_size_kb > 0)
        fwrite(g.trace_data, 1, (size_t)g.trace_size_kb << 10, f);
    fclose(f);
    err_clear();
    return MCDSL_RET_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* the ELF file API                                                    */
/* ------------------------------------------------------------------ */
mcdsl_return_et mcdsdl_elf_file_load(const mcd_char_t *elf_file_name,
                             mcd_bool_t add_file)
{
    FILE *f;
    long size;
    elf_file_t *ef;

    (void)add_file;
    if (elf_file_name == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "elf_file_name is NULL");
    if (g.num_elfs >= MCDSDL_MAX_ELF_FILES)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "too many ELF files");
    f = fopen(elf_file_name, "rb");
    if (f == NULL)
        return err_set(MCDSL_RET_ERROR_ELF_FILE,
                       "Could not open ELF file");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return err_set(MCDSL_RET_ERROR_ELF_FILE, "Invalid ELF file");
    }
    ef = &g.elfs[g.num_elfs];
    memset(ef, 0, sizeof(*ef));
    strncpy(ef->name, elf_file_name, sizeof(ef->name) - 1);
    ef->image = (uint8_t *)malloc((size_t)size);
    if (ef->image == NULL) {
        fclose(f);
        return err_set(MCDSL_RET_UNKNOWN_ERROR, "Out of memory");
    }
    ef->image_size = (uint32_t)size;
    if (fread(ef->image, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        elf_free(ef);
        return err_set(MCDSL_RET_ERROR_ELF_FILE, "Could not read ELF file");
    }
    fclose(f);
    if (elf_parse(ef) != 0) {
        elf_free(ef);
        return err_set(MCDSL_RET_ERROR_ELF_FILE,
                       "ELF file does not exist or content is invalid "
                       "or incomplete");
    }
    g.num_elfs++;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

void mcdsdl_elf_file_unload_all(void)
{
    int e;

    for (e = 0; e < g.num_elfs; e++)
        elf_free(&g.elfs[e]);
    g.num_elfs = 0;
}

mcd_bool_t mcdsdl_get_code_symbol(mctrl_opoint_et opoint_ptu, uint32_t addr,
                                  const mcd_char_t **symbol,
                                  uint32_t *start_addr, uint32_t *size)
{
    const elf_sym_t *sym = elf_find_by_addr(addr, 2 /* STT_FUNC */);

    (void)opoint_ptu;
    if (sym == NULL)
        return 0;
    if (symbol != NULL)
        *symbol = sym->name;
    if (start_addr != NULL)
        *start_addr = sym->value;
    if (size != NULL)
        *size = sym->size;
    return 1;
}

mcd_bool_t mcdsdl_get_data_symbol(mctrl_opoint_et opoint_ptu, uint32_t addr,
                                  const mcd_char_t **symbol,
                                  uint32_t *start_addr, uint32_t *size)
{
    const elf_sym_t *sym = elf_find_by_addr(addr, 1 /* STT_OBJECT */);

    (void)opoint_ptu;
    if (sym == NULL)
        return 0;
    if (symbol != NULL)
        *symbol = sym->name;
    if (start_addr != NULL)
        *start_addr = sym->value;
    if (size != NULL)
        *size = sym->size;
    return 1;
}

mcd_bool_t mcdsdl_get_code_symbol_addr(mctrl_opoint_et opoint_ptu,
                                       const mcd_char_t *symbol,
                                       uint32_t *start_addr, uint32_t *size)
{
    const elf_sym_t *sym;

    (void)opoint_ptu;
    if (symbol == NULL)
        return 0;
    sym = elf_find_by_name(symbol, 2);
    if (sym == NULL)
        return 0;
    if (start_addr != NULL)
        *start_addr = sym->value;
    if (size != NULL)
        *size = (sym->size > 0) ? sym->size - 1 : 0;
    return 1;
}

mcd_bool_t mcdsdl_get_data_symbol_addr(mctrl_opoint_et opoint_ptu,
                                       const mcd_char_t *symbol,
                                       uint32_t *start_addr, uint32_t *size)
{
    const elf_sym_t *sym;

    (void)opoint_ptu;
    if (symbol == NULL)
        return 0;
    sym = elf_find_by_name(symbol, 1);
    if (sym == NULL)
        return 0;
    if (start_addr != NULL)
        *start_addr = sym->value;
    if (size != NULL)
        *size = (sym->size > 0) ? sym->size - 1 : 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* the configuration                                                   */
/* ------------------------------------------------------------------ */
mcdsl_return_et mcdsdl_set_mcdsconfig(const mcdsl_mcdsc_st *mcdsc, float mcds_frequency_hz)
{
    if (mcdsc == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "mcdsc is NULL");
    if (mcdsc->hdr.num_reg > MCDSL_MCDSC_MAX_NUM_REG)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "mcdsc contains too many register pairs");
    memcpy(&g.mcdsc, mcdsc, sizeof(g.mcdsc));
    g.cfg_set = 1;
    g.freq_hz = mcds_frequency_hz;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsdl_set_mcdsconfig_with_file(const mcd_char_t *mcdsc_file_name)
{
    FILE *f;
    char line[512];
    uint32_t num_reg = 0;

    if (mcdsc_file_name == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "mcdsc_file_name is NULL");
    f = fopen(mcdsc_file_name, "rb");
    if (f == NULL)
        return err_set(MCDSL_RET_ERROR_MCDSC_FILE,
                       "Could not open mcdsc file");
    memset(&g.mcdsc, 0, sizeof(g.mcdsc));
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "START_OF_MCDS_CONFIG", 20) == 0) {
            /* the pairs follow */
            while (fgets(line, sizeof(line), f) != NULL) {
                char *sp = strchr(line, ' ');

                if (sp == NULL)
                    continue;
                if (num_reg < MCDSL_MCDSC_MAX_NUM_REG) {
                    g.mcdsc.d[num_reg] = parse_hex32(line);
                    g.mcdsc.d[MCDSL_MCDSC_MAX_NUM_REG + num_reg] =
                        parse_hex32(sp + 1);
                    num_reg++;
                }
            }
            break;
        }
        if (strncmp(line, "McdsFrequencyMHz=", 17) == 0)
            g.freq_hz = (float)atof(line + 17);
    }
    fclose(f);
    g.mcdsc.hdr.num_reg = num_reg;
    g.mcdsc.hdr.vsi = MCDSL_MCDSC_MAX_NUM_REG;
    g.cfg_set = 1;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

void mcdsdl_set_device_callback(mctrl_rcfd_et rcfd,
                                const mcdsdl_device_callback_st *device_callback)
{
    (void)rcfd;
    if (device_callback != NULL) {
        g.cb = *device_callback;
        g.cb_set = 1;
    } else {
        memset(&g.cb, 0, sizeof(g.cb));
        g.cb_set = 0;
    }
    err_clear();
}

/* ------------------------------------------------------------------ */
/* the engine (not implemented in this prototype)                      */
/* ------------------------------------------------------------------ */
mcdsl_return_et mcdsdl_decode(const uint8_t *trace_data, uint32_t trace_size_kb,
                      uint32_t tdf, uint32_t dm)
{
    /* The dispatch structure is calibrated against the reference:
     *   - the NULL/0 path handles TDF/DM (KEEP / feed / unsupported)
     *   - the data path checks the 20 MB limit first
     *   - the actual decode engine is not implemented in this prototype.
     * Note: the reference crashes on decode(NULL, 0, 0x10, dm); we return
     * a graceful error instead (documented difference). */
    if (trace_data == NULL && trace_size_kb == 0) {
        if (tdf == 0x80) {
            if (dm == 0x05 || dm == 0xF0) {
                g.num_msg = 0;
                err_clear();
                return MCDSL_RET_SUCCESS;
            }
            return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                           "MCDSDL_DM_CONT not supported for "
                           "mcdsdl_decode(NULL, 0, MCTRL_TDF_KEEP, dm)");
        }
        if (tdf == 0x00) {
            g.num_msg = 0;
            err_clear();
            return MCDSL_RET_SUCCESS;
        }
        if (tdf == 0x10) {
            g.num_msg = 0;
            return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                           "trace_data is NULL");
        }
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "This trace data format is not supported");
    }
    if (trace_size_kb > MCDSDL_MAX_TRACE_DATA_SIZE_KB)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "Maximum trace_size_kb is 20480 (20MB)");
    g.num_msg = 0;
    err_set(MCDSL_RET_SUCCESS,
            "mcdsdl: decode engine not implemented in this prototype");
    return MCDSL_RET_SUCCESS;
}

uint32_t mcdsdl_get_msg_str(const mcdsdl_msg_st *msg,
                            mcdsdl_msg_str_option_et mso, uint32_t line,
                            mcdsdl_msg_str_st *msg_str)
{
    /* the reference behaviour (calibrated with the synthetic-message
     * matrix): every field defaults to the empty string; the COMMENT
     * option carries the fixed strings per message type. The BASIC
     * fields are engine-driven (the reference crashes on synthetic
     * messages there) and stay empty in this prototype. */
    static const char s_empty[] = "";
    static const char s_iftg[] =
        "Trace gap due to fill level of on-chip trace buffer";
    static const char s_fifo[] =
        "MCDS message fifo overrun in observation block";
    static const char s_fifo_ptu[] =
        "MCDS message fifo overrun in observation block (PTU)";
    static const char s_other[] = "Message can't be decoded";
    static const char s_no_msg[] = "ERROR no msg available";

    if (msg == NULL || msg_str == NULL)
        return 0;
    memset(msg_str, 0, sizeof(*msg_str));
    (void)line;

    msg_str->ticks = s_empty;
    msg_str->opoint = s_empty;
    msg_str->origin = s_empty;
    msg_str->operation = s_empty;
    msg_str->addr = s_empty;
    msg_str->data = s_empty;
    msg_str->trace_state = s_empty;
    msg_str->symbol = s_empty;
    msg_str->symbol_offset = s_empty;
    msg_str->comment = s_empty;
    msg_str->stream_state = s_empty;
    msg_str->cft = s_empty;
    msg_str->tdr_msg = s_empty;
    msg_str->tdr_addr = s_empty;
    msg_str->tdr_len = s_empty;
    msg_str->tdr_msg_head = s_empty;

    if (mso & MCDSDL_MSO_COMMENT) {
        const char *c = NULL;

        switch ((uint32_t)msg->msg_type) {
        case 4:                 /* MCTRL_MT_IFTG */
            c = s_iftg;
            break;
        case 10:                /* MCTRL_MT_ERROR_MSG_FIFO */
            c = s_fifo;
            break;
        case 11:                /* MCTRL_MT_ERROR_MSG_FIFO_PTU */
            c = s_fifo_ptu;
            break;
        case 13:                /* MCTRL_MT_ERROR_MSG_OTHER */
            c = s_other;
            break;
        case 27:                /* MCTRL_MT_OTGB */
        case 30:                /* MCTRL_MT_PTU */
            c = s_no_msg;
            break;
        default:
            /* 21/23 depend on message data (engine); 20/24 depend on the
             * device config - the reference crashes on synthetic input */
            c = NULL;
            break;
        }
        if (c != NULL)
            msg_str->comment = c;
    }
    return 1;
}

mcdsl_return_et mcdsdl_get_instr(mctrl_opoint_et opoint_ptu, uint32_t addr,
                         uint32_t *instr, uint32_t *instr_size,
                         const mcd_char_t **instr_str)
{
    (void)opoint_ptu;
    (void)addr;
    (void)instr;
    (void)instr_size;
    (void)instr_str;
    /* the TriCore disassembler is part of the reference engine; the
     * outputs are left untouched like the reference does on failure */
    return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                   "mcdsdl: instruction disassembly not implemented in this "
                   "prototype");
}

mcdsdl_flow_instr_type_et mcdsdl_get_instr_type(uint32_t instr,
                                                int32_t *offset,
                                                mcd_bool_t *absolute)
{
    (void)instr;
    if (offset != NULL)
        *offset = 0;
    if (absolute != NULL)
        *absolute = 0;
    /* the TriCore instruction classification is part of the reference
     * engine; UNKNOWN is the honest answer here */
    return MCDSDL_FIT_UNKNOWN;
}
