/* mcdsrl_record.cpp - M23-2 recording smoke test.
 *
 * Mirrors the CCL reference flow (CclDevice.record()):
 *   [the reference configurator generates the MCDS config]
 *   -> reset (HAR) -> open_trace_file -> set_mcdsconfig -> start_recording
 *   -> poll get_recstate -> stop_recording -> the .mcds file.
 *
 * usage: mcdsrl_record.exe <recorder_dll> <out.mcds> [reset:0|1]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mcds_lib.h"
#include "mcds_lib_recorder.h"
#include "mcd_api.h"
#include "mcds_lib_configurator.h"

/* the recorder API under test */
typedef mcdsl_return_et (*pfn_lib_init)(uint8_t);
typedef const mcd_char_t *(*pfn_get_error)(void);
typedef mcdsl_return_et (*pfn_connect)(const mcd_core_con_info_st *,
                                       mcd_core_con_info_st *);
typedef mcdsl_return_et (*pfn_disconnect)(void);
typedef mcdsl_return_et (*pfn_reset)(mcd_bool_t);
typedef mcdsl_return_et (*pfn_open_file)(const mcd_char_t *);
typedef mcdsl_return_et (*pfn_set_cfg)(const mcdsl_mcdsc_st *,
                                       const mcdsl_mcdsc_st *);
typedef mcdsl_return_et (*pfn_start)(const mcdsrl_trace_state_st *);
typedef mcdsl_return_et (*pfn_recstate)(mcdsrl_recstate_et *,
                                        mcdsrl_trace_state_st *);
typedef mcdsl_return_et (*pfn_stop)(mcdsrl_trace_state_st *);

/* the reference configurator (always the original DLL) */
typedef mcdsl_return_et (*pfn_c_lib_init)(uint8_t);
typedef mcdsl_return_et (*pfn_c_init)(uint32_t);
typedef mcdsl_return_et (*pfn_c_getcfg)(mcdsl_crsv_et,
                                        const mcdsl_mcdsc_st **,
                                        const mcdsl_mcdsc_st **);
typedef const mcd_char_t *(*pfn_c_error)(void);

static int g_fails = 0;
#define CHK(cond, msg) do { \
    if (cond) printf("  [ok]   %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fails++; } \
} while (0)

int main(int argc, char **argv)
{
    const char *dll = (argc > 1) ? argv[1] : "mcdsrl_tas.dll";
    const char *out = (argc > 2) ? argv[2] : "out.mcds";
    int do_reset = (argc > 3) ? atoi(argv[3]) : 1;
    HMODULE hr, hc;
    mcdsl_return_et r;
    mcd_core_con_info_st con;
    mcdsrl_trace_state_st ts;
    mcdsrl_recstate_et rs = MCDSRL_RECSTATE_UNKNOWN;
    const mcdsl_mcdsc_st *mcdsc = NULL, *cleanup = NULL;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== mcdsrl recording smoke (M23-2) ==\n");
    printf("recorder: %s\nout: %s reset: %d\n\n", dll, out, do_reset);

    /* ---- the reference configurator ---- */
    hc = LoadLibraryA("C:\\Program Files\\DAS64\\clients\\mcds_configurator.dll");
    if (hc == NULL) {
        printf("configurator load failed: %lu\n", GetLastError());
        return 2;
    }
    {
        pfn_c_lib_init c_lib_init = (pfn_c_lib_init)GetProcAddress(hc, "mcdscl_lib_init");
        pfn_c_init c_init = (pfn_c_init)GetProcAddress(hc, "mcdscl_init");
        pfn_c_getcfg c_getcfg = (pfn_c_getcfg)GetProcAddress(hc, "mcdscl_get_mcdsconfig");
        pfn_c_error c_error = (pfn_c_error)GetProcAddress(hc, "mcdscl_get_error");

        if (c_lib_init == NULL || c_init == NULL || c_getcfg == NULL) {
            printf("configurator exports missing\n");
            return 2;
        }
        r = c_lib_init(3);
        CHK(r == MCDSL_RET_SUCCESS, "mcdscl_lib_init(3)");
        if (r != MCDSL_RET_SUCCESS && c_error)
            printf("       error: %s\n", c_error() ? c_error() : "(none)");

        /* ---- the recorder under test ---- */
        hr = LoadLibraryA(dll);
        if (hr == NULL) {
            printf("recorder load failed: %lu\n", GetLastError());
            return 2;
        }
        {
            pfn_lib_init f_init = (pfn_lib_init)GetProcAddress(hr, "mcdsrl_lib_init");
            pfn_get_error f_err = (pfn_get_error)GetProcAddress(hr, "mcdsrl_get_error");
            pfn_connect f_con = (pfn_connect)GetProcAddress(hr, "mcdsrl_connect_device");
            pfn_disconnect f_dis = (pfn_disconnect)GetProcAddress(hr, "mcdsrl_disconnect_device");
            pfn_reset f_rst = (pfn_reset)GetProcAddress(hr, "mcdsrl_reset_device");
            pfn_open_file f_open = (pfn_open_file)GetProcAddress(hr, "mcdsrl_open_trace_file");
            pfn_set_cfg f_setc = (pfn_set_cfg)GetProcAddress(hr, "mcdsrl_set_mcdsconfig");
            pfn_start f_start = (pfn_start)GetProcAddress(hr, "mcdsrl_start_recording");
            pfn_recstate f_rec = (pfn_recstate)GetProcAddress(hr, "mcdsrl_get_recstate");
            pfn_stop f_stop = (pfn_stop)GetProcAddress(hr, "mcdsrl_stop_recording");

            if (f_init == NULL || f_con == NULL || f_open == NULL ||
                f_setc == NULL || f_start == NULL || f_rec == NULL ||
                f_stop == NULL) {
                printf("recorder exports missing\n");
                return 2;
            }

            r = f_init(3);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_lib_init(3)");

            memset(&con, 0, sizeof(con));
            r = f_con(NULL, &con);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_connect_device");
            if (r != MCDSL_RET_SUCCESS) {
                printf("       error: %s\n", f_err && f_err() ? f_err() : "(none)");
                return 1;
            }
            printf("       device '%s' type 0x%08X\n", con.device,
                   (unsigned)con.device_type);

            if (do_reset && f_rst != NULL) {
                r = f_rst(1);           /* reset and halt (the HAR state) */
                CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_reset_device(halt)");
                Sleep(200);
            }

            /* the reference configurator generates the config */
            r = c_init(con.device_type);
            CHK(r == MCDSL_RET_SUCCESS, "mcdscl_init(device_type)");
            if (r != MCDSL_RET_SUCCESS && c_error)
                printf("       error: %s\n", c_error() ? c_error() : "(none)");

            r = c_getcfg((mcdsl_crsv_et)(do_reset ? 2 : 1), &mcdsc, &cleanup);
            CHK(r == MCDSL_RET_SUCCESS && mcdsc != NULL && cleanup != NULL,
                "mcdscl_get_mcdsconfig");
            if (r != MCDSL_RET_SUCCESS) {
                if (c_error)
                    printf("       error: %s\n", c_error() ? c_error() : "(none)");
                return 1;
            }
            printf("       config: num_reg %u crsv %u device_type 0x%08X "
                   "mcds_device 0x%08X vsi %u\n",
                   (unsigned)mcdsc->hdr.num_reg, (unsigned)mcdsc->hdr.crsv,
                   (unsigned)mcdsc->hdr.device_type,
                   (unsigned)mcdsc->hdr.mcds_device,
                   (unsigned)mcdsc->hdr.vsi);

            r = f_open(out);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_open_trace_file");
            if (r != MCDSL_RET_SUCCESS) {
                printf("       error: %s\n", f_err && f_err() ? f_err() : "(none)");
                return 1;
            }

            r = f_setc(mcdsc, cleanup);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_set_mcdsconfig");
            if (r != MCDSL_RET_SUCCESS)
                printf("       error: %s\n", f_err && f_err() ? f_err() : "(none)");
            {
                uint32_t pk;

                printf("       pairs(all %u):\n", (unsigned)mcdsc->hdr.num_reg);
                for (pk = 0; pk < mcdsc->hdr.num_reg; pk++)
                    printf("         [%2u] 0x%08X = 0x%08X\n", (unsigned)pk,
                           (unsigned)mcdsc->d[pk],
                           (unsigned)mcdsc->d[mcdsc->hdr.vsi + pk]);
            }

            r = f_start(NULL);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_start_recording");
            if (r != MCDSL_RET_SUCCESS) {
                printf("       error: %s\n", f_err && f_err() ? f_err() : "(none)");
                return 1;
            }

            /* the poll loop (the CCL: 50 ms steps, timeout 2 s) */
            memset(&ts, 0, sizeof(ts));
            for (i = 0; i < 40; i++) {
                Sleep(50);
                r = f_rec(&rs, &ts);
                if (r != MCDSL_RET_SUCCESS) {
                    printf("       get_recstate failed: %s\n",
                           f_err && f_err() ? f_err() : "(none)");
                    break;
                }
                if (rs == MCDSRL_RECSTATE_IDLE)
                    break;
            }
            printf("       recstate 0x%X size %u KB time %u ms\n",
                   (unsigned)rs, (unsigned)ts.trace_size_kb,
                   (unsigned)ts.recording_time_ms);

            r = f_stop(&ts);
            CHK(r == MCDSL_RET_SUCCESS, "mcdsrl_stop_recording");
            if (r != MCDSL_RET_SUCCESS)
                printf("       error: %s\n", f_err && f_err() ? f_err() : "(none)");
            printf("       trace size %u KB time %u ms\n",
                   (unsigned)ts.trace_size_kb,
                   (unsigned)ts.recording_time_ms);

            f_dis();
        }
    }

    printf("\n== %s (%d failure(s)) ==\n",
           g_fails ? "RECORD FAILED" : "RECORD OK", g_fails);
    return g_fails ? 1 : 0;
}
