/* mcdsrl_smoke.cpp - loads an mcds_recorder implementation DLL (ours or the
 * reference) and exercises the M23-1 core API; the outputs of both DLLs
 * are diffed for cross-calibration.
 *
 * usage: mcdsrl_smoke.exe [dll_path]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mcds_lib.h"
#include "mcds_lib_recorder.h"
#include "mcd_api.h"

typedef mcdsl_return_et (*pfn_lib_init)(uint8_t);
typedef const mcdsl_version_st *(*pfn_lib_version)(void);
typedef const mcd_char_t *(*pfn_get_error)(void);
typedef mcdsl_return_et (*pfn_qry_devices)(const mcd_char_t *, uint32_t,
                                           uint32_t *, mcd_core_con_info_st *);
typedef mcdsl_return_et (*pfn_connect_device)(const mcd_core_con_info_st *,
                                              mcd_core_con_info_st *);
typedef const mcd_core_con_info_st *(*pfn_device_connected)(mcdsrl_cplty_st *);
typedef mcdsl_return_et (*pfn_disconnect_device)(void);
typedef mcdsl_return_et (*pfn_reset_device)(mcd_bool_t);
typedef mcdsl_return_et (*pfn_read)(uint32_t, uint32_t *);
typedef mcdsl_return_et (*pfn_write)(uint32_t, uint32_t);
typedef mcdsl_return_et (*pfn_read_list)(const uint32_t *, uint32_t *,
                                         uint16_t, uint16_t *);
typedef mcdsl_return_et (*pfn_write_list)(const uint32_t *, const uint32_t *,
                                          uint16_t, uint16_t *);
typedef mcdsl_return_et (*pfn_read_block)(uint32_t, void *, uint32_t,
                                          uint32_t *);
typedef mcdsl_return_et (*pfn_write_block)(uint32_t, const void *, uint32_t,
                                           uint32_t *);
typedef mcdsl_return_et (*pfn_read_timestamped)(uint32_t, uint32_t *,
                                                uint64_t *, uint64_t *);

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
                          ? argv[1] : "mcdsrl_tas.dll";
    HMODULE h;
    mcdsl_return_et r;
    uint32_t i;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== mcdsrl smoke test (M23-1 core) ==\n");
    printf("dll: %s\n\n", dll);

    h = LoadLibraryA(dll);
    if (h == NULL) {
        printf("LoadLibraryA(%s) failed: %lu\n", dll, GetLastError());
        return 2;
    }
    LOADFN(h, pfn_lib_init,         f_init,      "mcdsrl_lib_init");
    LOADFN(h, pfn_lib_version,      f_version,   "mcdsrl_lib_version");
    LOADFN(h, pfn_get_error,        f_error,     "mcdsrl_get_error");
    LOADFN(h, pfn_qry_devices,      f_qrydev,    "mcdsrl_qry_devices");
    LOADFN(h, pfn_connect_device,   f_connect,   "mcdsrl_connect_device");
    LOADFN(h, pfn_device_connected, f_connected, "mcdsrl_device_connected");
    LOADFN(h, pfn_disconnect_device, f_disconnect, "mcdsrl_disconnect_device");
    LOADFN(h, pfn_reset_device,     f_reset,     "mcdsrl_reset_device");
    LOADFN(h, pfn_read,             f_read,      "mcdsrl_read");
    LOADFN(h, pfn_write,            f_write,     "mcdsrl_write");
    LOADFN(h, pfn_read_list,        f_read_list, "mcdsrl_read_list");
    LOADFN(h, pfn_write_list,       f_write_list, "mcdsrl_write_list");
    LOADFN(h, pfn_read_block,       f_read_block, "mcdsrl_read_block");
    LOADFN(h, pfn_write_block,      f_write_block, "mcdsrl_write_block");
    LOADFN(h, pfn_read_timestamped, f_rdts,      "mcdsrl_read_timestamped");
    printf("all exports resolved\n\n");

    /* 1. library init and version */
    {
        const mcdsl_version_st *v;
        r = f_init(3);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_lib_init(3)");
        if (r != MCDSL_RET_SUCCESS)
            printf("       error: %s\n", f_error() ? f_error() : "(none)");
        v = f_version();
        CHK(v != NULL, "mcdsrl_lib_version");
        if (v != NULL)
            printf("       version %u.%u.%u\n", (unsigned)v->v.major,
                   (unsigned)v->v.minor, (unsigned)v->v.patch);
    }

    /* 2. device query */
    {
        mcd_core_con_info_st devs[4];
        uint32_t n = 0;

        memset(devs, 0, sizeof(devs));
        r = f_qrydev(NULL, 4, &n, devs);
        printf("       qry_devices rc 0x%08x n %u\n", (unsigned)r,
               (unsigned)n);
        for (i = 0; i < n && i < 4; i++) {
            printf("       dev[%u] acc_hw '%s' type 0x%08X id 0x%08X "
                   "system '%s' port %u\n", (unsigned)i, devs[i].acc_hw,
                   (unsigned)devs[i].device_type, (unsigned)devs[i].device_id,
                   devs[i].system, (unsigned)devs[i].server_port);
        }
    }

    /* 3. connect */
    {
        mcd_core_con_info_st con;
        mcdsrl_cplty_st cplty;
        const mcd_core_con_info_st *pc;

        memset(&con, 0, sizeof(con));
        memset(&cplty, 0, sizeof(cplty));
        r = f_connect(NULL, &con);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_connect_device");
        if (r != MCDSL_RET_SUCCESS)
            printf("       error: %s\n", f_error() ? f_error() : "(none)");
        else
            printf("       connected: acc_hw '%s' device '%s' type 0x%08X "
                   "id 0x%08X\n", con.acc_hw, con.device,
                   (unsigned)con.device_type, (unsigned)con.device_id);

        pc = f_connected(&cplty);
        CHK(pc != NULL, "mcdsrl_device_connected");
        if (pc != NULL)
            printf("       cplty 0x%X bandwidth %u trace_buf %u\n",
                   (unsigned)cplty.cplty, (unsigned)cplty.bandwidth_kb,
                   (unsigned)cplty.trace_buf_kb);
    }

    /* 4. register access (DSPR scratch cell 0x70000000, restored) */
    {
        uint32_t orig = 0, v = 0, v2 = 0;
        const uint32_t pat = 0x12345678u;

        r = f_read(0x70000000u, &orig);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_read (0x70000000)");
        printf("       orig 0x%08X\n", (unsigned)orig);

        r = f_write(0x70000000u, pat);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_write (pattern)");
        r = f_read(0x70000000u, &v);
        CHK(r == MCDSL_RET_SUCCESS && v == pat, "read back pattern");
        printf("       readback 0x%08X\n", (unsigned)v);

        r = f_write(0x70000000u, orig);
        r = f_read(0x70000000u, &v2);
        CHK(r == MCDSL_RET_SUCCESS && v2 == orig, "restore original");
    }

    /* 5. list access */
    {
        uint32_t addr[3] = { 0x70000000u, 0x70000010u, 0x70000020u };
        uint32_t val[3] = { 0, 0, 0 };
        uint16_t ok = 0;

        r = f_read_list(addr, val, 3, &ok);
        CHK(r == MCDSL_RET_SUCCESS && ok == 3, "mcdsrl_read_list (3)");
        printf("       values 0x%08X 0x%08X 0x%08X (ok %u)\n",
               (unsigned)val[0], (unsigned)val[1], (unsigned)val[2],
               (unsigned)ok);
    }

    /* 6. block access (16 bytes, restored) */
    {
        uint8_t orig[16], buf[16];
        uint32_t nok = 0;

        memset(orig, 0, sizeof(orig));
        memset(buf, 0, sizeof(buf));
        r = f_read_block(0x70000030u, orig, 16, &nok);
        CHK(r == MCDSL_RET_SUCCESS && nok == 16, "mcdsrl_read_block (16)");
        memcpy(buf, orig, 16);
        buf[0] ^= 0xFF;
        r = f_write_block(0x70000030u, buf, 16, &nok);
        CHK(r == MCDSL_RET_SUCCESS && nok == 16, "mcdsrl_write_block (16)");
        r = f_write_block(0x70000030u, orig, 16, &nok);
        CHK(r == MCDSL_RET_SUCCESS, "restore block");
    }

    /* 7. timestamped read (MCDS timestamp counter) */
    {
        uint32_t v = 0;
        uint64_t tb = 0, ta = 0;

        r = f_rdts(0x70000000u, &v, &tb, &ta);
        CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_read_timestamped");
        printf("       value 0x%08X ts_before 0x%llX ts_after 0x%llX\n",
               (unsigned)v, (unsigned long long)tb,
               (unsigned long long)ta);
    }

    /* 8. disconnect */
    r = f_disconnect();
    CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_disconnect_device");

    FreeLibrary(h);
    printf("\n== %s (%d failure(s)) ==\n",
           g_fails ? "SMOKE FAILED" : "SMOKE OK", g_fails);
    return g_fails ? 1 : 0;
}
