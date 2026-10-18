/* mcdsdl_smoke.cpp - loads an mcds_decoder implementation DLL (ours or the
 * reference) and exercises the M24-1 API surface; the outputs of both DLLs
 * are compared for cross-calibration.
 *
 * usage: mcdsdl_smoke.exe [dll_path]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mcds_lib.h"
#include "mcds_lib_decoder.h"
#include "mcds_control.h"
#include "mcd_api.h"

typedef int32_t (*pfn_lib_init)(uint8_t);
typedef const mcdsl_version_st *(*pfn_lib_version)(void);
typedef const mcd_char_t *(*pfn_get_error)(void);
typedef void (*pfn_clear)(void);
typedef const mcdsdl_error_counter_st *(*pfn_get_ec)(void);
typedef const mcdsdl_trace_marker_st *(*pfn_get_marker)(void);
typedef int32_t (*pfn_get_num_msg)(void);
typedef int32_t (*pfn_get_msg)(int32_t, mcdsdl_msg_st *);
typedef int32_t (*pfn_get_next)(const mcdsdl_msg_st *, mcdsdl_msg_st *);
typedef int32_t (*pfn_get_prev)(const mcdsdl_msg_st *, mcdsdl_msg_st *);
typedef int32_t (*pfn_mcds_load)(const mcd_char_t *);
typedef int32_t (*pfn_mcds_save)(const mcd_char_t *, mcd_bool_t, mcd_bool_t);
typedef int32_t (*pfn_elf_load)(const mcd_char_t *, mcd_bool_t);
typedef void (*pfn_elf_unload)(void);
typedef mcd_bool_t (*pfn_get_csym)(mctrl_opoint_et, uint32_t,
                                   const mcd_char_t **, uint32_t *, uint32_t *);
typedef mcd_bool_t (*pfn_get_dsym)(mctrl_opoint_et, uint32_t,
                                   const mcd_char_t **, uint32_t *, uint32_t *);
typedef mcd_bool_t (*pfn_get_csym_addr)(mctrl_opoint_et, const mcd_char_t *,
                                        uint32_t *, uint32_t *);
typedef mcd_bool_t (*pfn_get_dsym_addr)(mctrl_opoint_et, const mcd_char_t *,
                                        uint32_t *, uint32_t *);
typedef int32_t (*pfn_set_cfg)(const mcdsl_mcdsc_st *, float);
typedef int32_t (*pfn_set_cfg_file)(const mcd_char_t *);
typedef void (*pfn_set_cb)(mctrl_rcfd_et, const mcdsdl_device_callback_st *);
typedef int32_t (*pfn_decode)(const uint8_t *, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*pfn_get_msg_str)(const mcdsdl_msg_st *,
                                    mcdsdl_msg_str_option_et, uint32_t,
                                    mcdsdl_msg_str_st *);
typedef int32_t (*pfn_get_instr)(mctrl_opoint_et, uint32_t, uint32_t *,
                                 uint32_t *, const mcd_char_t **);
typedef mcdsdl_flow_instr_type_et (*pfn_get_instr_type)(uint32_t, int32_t *,
                                                        mcd_bool_t *);

static int g_fails = 0;
#define CHK(cond, msg) do { \
    if (cond) printf("  [ok]   %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fails++; } \
} while (0)

#define LOADFN(h, type, var, name) \
    type var = (type)GetProcAddress(h, name); \
    if (var == NULL) { printf("missing export: %s\n", name); return 3; }

int main(int argc, char **argv)
{
    const char *dll = (argc > 1 && argv[1][0] && argv[1][0] != '-')
                          ? argv[1] : "mcdsdl.dll";
    HMODULE h;
    int32_t r;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== mcdsdl smoke test (M24-1 API surface) ==\n");
    printf("dll: %s\n\n", dll);

    h = LoadLibraryA(dll);
    if (h == NULL) {
        printf("LoadLibraryA(%s) failed: %lu\n", dll, GetLastError());
        return 2;
    }
    LOADFN(h, pfn_lib_init,    f_init,    "mcdsdl_lib_init");
    LOADFN(h, pfn_lib_version, f_version, "mcdsdl_lib_version");
    LOADFN(h, pfn_get_error,   f_error,   "mcdsdl_get_error");
    LOADFN(h, pfn_clear,       f_clear,   "mcdsdl_clear");
    LOADFN(h, pfn_get_ec,      f_ec,      "mcdsdl_get_error_counter");
    LOADFN(h, pfn_get_marker,  f_marker,  "mcdsdl_get_trace_marker");
    LOADFN(h, pfn_get_num_msg, f_num,     "mcdsdl_get_num_msg");
    LOADFN(h, pfn_get_msg,     f_msg,     "mcdsdl_get_msg");
    LOADFN(h, pfn_get_next,    f_next,    "mcdsdl_get_next_msg");
    LOADFN(h, pfn_get_prev,    f_prev,    "mcdsdl_get_previous_msg");
    LOADFN(h, pfn_mcds_load,   f_load,    "mcdsdl_mcds_file_load");
    LOADFN(h, pfn_mcds_save,   f_save,    "mcdsdl_mcds_file_save");
    LOADFN(h, pfn_elf_load,    f_elf,     "mcdsdl_elf_file_load");
    LOADFN(h, pfn_elf_unload,  f_unload,  "mcdsdl_elf_file_unload_all");
    LOADFN(h, pfn_get_csym,    f_csym,    "mcdsdl_get_code_symbol");
    LOADFN(h, pfn_get_dsym,    f_dsym,    "mcdsdl_get_data_symbol");
    LOADFN(h, pfn_get_csym_addr, f_csym_a, "mcdsdl_get_code_symbol_addr");
    LOADFN(h, pfn_get_dsym_addr, f_dsym_a, "mcdsdl_get_data_symbol_addr");
    LOADFN(h, pfn_set_cfg,     f_setc,    "mcdsdl_set_mcdsconfig");
    LOADFN(h, pfn_set_cfg_file, f_setcf,  "mcdsdl_set_mcdsconfig_with_file");
    LOADFN(h, pfn_set_cb,      f_setcb,   "mcdsdl_set_device_callback");
    LOADFN(h, pfn_decode,      f_decode,  "mcdsdl_decode");
    LOADFN(h, pfn_get_msg_str, f_msgstr,  "mcdsdl_get_msg_str");
    LOADFN(h, pfn_get_instr,   f_instr,   "mcdsdl_get_instr");
    LOADFN(h, pfn_get_instr_type, f_itype, "mcdsdl_get_instr_type");
    printf("all 25 exports resolved\n\n");

    /* 1. the core */
    {
        const mcdsl_version_st *v;

        r = f_init(4);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsdl_lib_init(4)");
        if (r != MCDSL_RET_SUCCESS)
            printf("       error: %s\n", f_error() ? f_error() : "(none)");
        v = f_version();
        CHK(v != NULL, "mcdsdl_lib_version");
        if (v != NULL)
            printf("       version %u.%u.%u\n", (unsigned)v->v.major,
                   (unsigned)v->v.minor, (unsigned)v->v.patch);
    }

    /* 2. the empty state */
    f_clear();
    CHK(f_num() == 0, "get_num_msg == 0 (no decode)");
    {
        mcdsdl_msg_st m;

        memset(&m, 0, sizeof(m));
        r = f_msg(0, &m);
        printf("       get_msg(0) rc 0x%08X err '%s'\n", (unsigned)r,
               f_error() ? f_error() : "(none)");
        printf("       error_counter sum 0x%X\n",
               (unsigned)f_ec()->sum_of_all);
        printf("       marker num_msg %d\n", (int)f_marker()->num_msg);
    }

    /* 3. the .mcds file (the recorder output from the M23 session!) */
    {
        const char *mcds = "C:\\work\\tas_server_analysis\\m23_rec_orig.mcds";

        r = f_load(mcds);
        printf("       mcds_file_load('%s') rc 0x%08X\n", mcds, (unsigned)r);
        printf("       error: %s\n", f_error() ? f_error() : "(none)");
        printf("       num_msg after load: %d\n", (int)f_num());
    }

    /* 4. the synthetic ELF (the symbol tables!) */
    {
        const char *elf = "C:\\work\\mcdsdl\\tests\\test_elf.elf";
        const mcd_char_t *sym = NULL;
        uint32_t sa = 0, ss = 0;
        mcd_bool_t ok;

        r = f_elf(elf, 1);
        CHK(r == MCDSL_RET_SUCCESS, "elf_file_load(test_elf.elf)");
        if (r != MCDSL_RET_SUCCESS)
            printf("       error: %s\n", f_error() ? f_error() : "(none)");

        ok = f_csym((mctrl_opoint_et)0, 0x80000100u, &sym, &sa, &ss);
        printf("       code_symbol(0x80000100): ok %d name '%s' start 0x%08X "
               "size %u\n", (int)ok, (ok && sym) ? sym : "(none)",
               (unsigned)sa, (unsigned)ss);

        ok = f_dsym((mctrl_opoint_et)0, 0x70000004u, &sym, &sa, &ss);
        printf("       data_symbol(0x70000004): ok %d name '%s' start 0x%08X "
               "size %u\n", (int)ok, (ok && sym) ? sym : "(none)",
               (unsigned)sa, (unsigned)ss);

        ok = f_csym_a((mctrl_opoint_et)0, "test_func_a", &sa, &ss);
        printf("       code_symbol_addr('test_func_a'): ok %d start 0x%08X "
               "size %u\n", (int)ok, (unsigned)sa, (unsigned)ss);

        ok = f_dsym_a((mctrl_opoint_et)0, "test_data_x", &sa, &ss);
        printf("       data_symbol_addr('test_data_x'): ok %d start 0x%08X "
               "size %u\n", (int)ok, (unsigned)sa, (unsigned)ss);

        f_unload();
        printf("       elf_file_unload_all done\n");
    }

    /* 5. the engine stubs (the honest behaviour!) */
    {
        uint32_t instr = 0xDEADBEEF, isz = 0xDEADBEEF;
        const mcd_char_t *istr = (const mcd_char_t *)0xDEADBEEF;
        int32_t off = 0xDEADBEEF;
        mcd_bool_t abs = 0xDEADBEEF;

        r = f_decode(NULL, 0, 0x80, 0xF0);
        printf("       decode(NULL,0,KEEP,RAW) rc 0x%08X err '%s'\n",
               (unsigned)r, f_error() ? f_error() : "(none)");

        r = f_instr((mctrl_opoint_et)0, 0x80000000u, &instr, &isz, &istr);
        printf("       get_instr rc 0x%08X instr 0x%X size %u str %s\n",
               (unsigned)r, (unsigned)instr, (unsigned)isz,
               (istr == NULL) ? "NULL" : "set");

        printf("       get_instr_type(0x1D000000) = %d (0=UNKNOWN)\n",
               (int)f_itype(0x1D000000u, &off, &abs));
    }

    /* 6. the save (the header round-trip!) */
    {
        const char *out = "C:\\work\\mcdsdl\\tests\\test_save.mcds";

        r = f_save(out, 0, 0);
        printf("       mcds_file_save('%s') rc 0x%08X\n", out, (unsigned)r);
        r = f_load(out);
        printf("       reload rc 0x%08X num_msg %d\n", (unsigned)r,
               (int)f_num());
    }

    FreeLibrary(h);
    printf("\n== %s (%d failure(s)) ==\n",
           g_fails ? "SMOKE FAILED" : "SMOKE OK", g_fails);
    return g_fails ? 1 : 0;
}
