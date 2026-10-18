/* smoke.c - loads mcd_tas.dll dynamically (the way an MCD tool would) and
 * exercises the implemented subset against a running tas_server.
 * By default the test is fully read-only (enumeration only).
 * With --attach it additionally performs mcd_open_core_f, which
 * attaches to the target (TAS DEVICE_CONNECT: hot attach, no reset or
 * halt request) and executes read-only transaction lists. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Prevent __declspec(dllexport) on the API declarations (we never link to
 * the DLL directly, all calls go through GetProcAddress). */
#undef WIN32
#include "mcd_api.h"

typedef mcd_return_et (*pfn_initialize)(const mcd_api_version_st *, mcd_impl_version_info_st *);
typedef void           (*pfn_exit)(void);
typedef mcd_return_et (*pfn_qry_servers)(const mcd_char_t *, mcd_bool_t, uint32_t, uint32_t *, mcd_server_info_st *);
typedef mcd_return_et (*pfn_open_server)(const mcd_char_t *, const mcd_char_t *, mcd_server_st **);
typedef mcd_return_et (*pfn_close_server)(const mcd_server_st *);
typedef mcd_return_et (*pfn_qry_systems)(uint32_t, uint32_t *, mcd_core_con_info_st *);
typedef mcd_return_et (*pfn_qry_devices)(const mcd_core_con_info_st *, uint32_t, uint32_t *, mcd_core_con_info_st *);
typedef mcd_return_et (*pfn_qry_cores)(const mcd_core_con_info_st *, uint32_t, uint32_t *, mcd_core_con_info_st *);
typedef void           (*pfn_qry_error)(const mcd_core_st *, mcd_error_info_st *);
typedef mcd_return_et (*pfn_run)(const mcd_core_st *, mcd_bool_t);
typedef mcd_return_et (*pfn_stop)(const mcd_core_st *, mcd_bool_t);
typedef mcd_return_et (*pfn_rst)(const mcd_core_st *, uint32_t, mcd_bool_t);
typedef mcd_return_et (*pfn_createtrig)(const mcd_core_st *, void *, uint32_t *);
typedef mcd_return_et (*pfn_qrytrigset)(const mcd_core_st *, uint32_t, uint32_t *, uint32_t *);
typedef mcd_return_et (*pfn_qrytrigstate)(const mcd_core_st *, uint32_t, mcd_trig_state_st *);
typedef mcd_return_et (*pfn_removetrig)(const mcd_core_st *, uint32_t);
typedef mcd_return_et (*pfn_qrytraces)(const mcd_core_st *, uint32_t, uint32_t *, mcd_trace_info_st *);
typedef mcd_return_et (*pfn_qrytracestate)(const mcd_core_st *, uint32_t, mcd_trace_state_st *);
typedef mcd_return_et (*pfn_settracestate)(const mcd_core_st *, uint32_t, mcd_trace_state_st *);
typedef mcd_return_et (*pfn_readtrace)(const mcd_core_st *, uint32_t, uint64_t, uint32_t *, uint32_t, void *);
typedef mcd_return_et (*pfn_qrycoremodes)(const mcd_core_st *, uint32_t, uint32_t *, mcd_core_mode_info_st *);
typedef mcd_return_et (*pfn_qryoverlays)(const mcd_core_st *, uint32_t, uint32_t *, uint32_t *);
typedef mcd_return_et (*pfn_qryregcompound)(const mcd_core_st *, uint32_t, uint32_t, uint32_t *, uint32_t *);
typedef mcd_return_et (*pfn_qryctrigs)(const mcd_core_st *, uint32_t, uint32_t *, mcd_ctrig_info_st *);
typedef mcd_return_et (*pfn_qrycurrenttime)(const mcd_core_st *, uint64_t *);
typedef mcd_return_et (*pfn_qrydevdesc)(const mcd_core_st *, mcd_char_t *, uint32_t *);
typedef mcd_return_et (*pfn_qrymaxpayload)(const mcd_core_st *, uint32_t *);
typedef mcd_return_et (*pfn_qrytriginfo)(const mcd_core_st *, mcd_trig_info_st *);
typedef mcd_return_et (*pfn_open_core)(const mcd_core_con_info_st *, mcd_core_st **);
typedef mcd_return_et (*pfn_close_core)(const mcd_core_st *);
typedef mcd_return_et (*pfn_qry_mem_spaces)(const mcd_core_st *, uint32_t, uint32_t *, mcd_memspace_st *);
typedef mcd_return_et (*pfn_qry_mem_blocks)(const mcd_core_st *, uint32_t, uint32_t, uint32_t *, mcd_memblock_st *);
typedef mcd_return_et (*pfn_qry_rst_classes)(const mcd_core_st *, uint32_t *);
typedef mcd_return_et (*pfn_qry_rst_class_info)(const mcd_core_st *, uint8_t, mcd_rst_info_st *);
typedef mcd_return_et (*pfn_execute_txlist)(const mcd_core_st *, mcd_txlist_st *);
typedef mcd_return_et (*pfn_qry_state)(const mcd_core_st *, mcd_core_state_st *);

static int g_fails = 0;
#define CHK(cond, msg) do { \
    if (cond) printf("  [ok]   %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fails++; } \
} while (0)

#define LOADFN(h, type, var, name) \
    type var = (type)GetProcAddress(h, name); \
    if (var == NULL) { printf("missing export: %s\n", name); return 3; }

/* Read one 32-bit word through the read-only txlist path (used by the
 * --runstop / --rst state-change tests).  Returns 0 on success. */
static int read_word32(pfn_execute_txlist f_txlist, const mcd_core_st *core,
                       uint64_t addr, uint32_t *out)
{
    mcd_tx_st tx;
    uint8_t b[4];
    mcd_txlist_st tl;

    memset(&tx, 0, sizeof(tx));
    memset(b, 0, sizeof(b));
    tx.addr.mem_space_id = 1;
    tx.addr.address = addr;
    tx.access_type = MCD_TX_AT_R;
    tx.access_width = 4;
    tx.num_bytes = 4;
    tx.data = b;
    tl.tx = &tx;
    tl.num_tx = 1;
    tl.num_tx_ok = 0;
    *out = 0;
    if (f_txlist(core, &tl) != MCD_RET_ACT_NONE || tl.num_tx_ok != 1)
        return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE h;
    const char *dll  = (argc > 1 && argv[1][0] && argv[1][0] != '-') ? argv[1] : "mcd_tas.dll";
    const char *cfg  = (argc > 2 && argv[2][0] && argv[2][0] != '-') ? argv[2] : NULL;
    int attach = 0;

    /* unbuffered output: a crashing DLL must not hide prior progress */
    setvbuf(stdout, NULL, _IONBF, 0);
    int runstop = 0;
    int rst = 0;
    int trig = 0;
    int trace = 0;
    int spr = 0;
    int sprw = 0;
    mcd_return_et r;
    uint32_t n;
    uint32_t i;

    for (i = 1; i < (uint32_t)argc; i++) {
        if (strcmp(argv[i], "--attach") == 0)
            attach = 1;
        if (strcmp(argv[i], "--runstop") == 0)
            runstop = 1;
        if (strcmp(argv[i], "--rst") == 0)
            rst = 1;
        if (strcmp(argv[i], "--trig") == 0)
            trig = 1;
        if (strcmp(argv[i], "--trace") == 0)
            trace = 1;
        if (strcmp(argv[i], "--spr") == 0)
            spr = 1;
        if (strcmp(argv[i], "--sprw") == 0)
            sprw = 1;
    }

    printf("== mcd_tas smoke test ==\n");
    printf("dll: %s\ncfg: %s\nmode: %s%s%s%s%s%s%s\n\n", dll, cfg ? cfg : "(default)",
           attach ? "with core attach (--attach)" : "read-only",
           runstop ? " + run/stop (--runstop)" : "",
           rst ? " + reset (--rst)" : "",
           trig ? " + trigger (--trig)" : "",
           trace ? " + trace probe (--trace)" : "",
           spr ? " + SPR translation (--spr)" : "",
           sprw ? " + SPR write check (--sprw)" : "");

    h = LoadLibraryA(dll);
    if (h == NULL) {
        printf("LoadLibraryA(%s) failed: %lu\n", dll, GetLastError());
        return 2;
    }

    LOADFN(h, pfn_initialize,   f_init,    "mcd_initialize_f");
    LOADFN(h, pfn_exit,         f_exit,    "mcd_exit_f");
    LOADFN(h, pfn_qry_servers,  f_qrysvr,  "mcd_qry_servers_f");
    LOADFN(h, pfn_open_server,  f_open,    "mcd_open_server_f");
    LOADFN(h, pfn_close_server, f_close,   "mcd_close_server_f");
    LOADFN(h, pfn_qry_systems,  f_systems, "mcd_qry_systems_f");
    LOADFN(h, pfn_qry_devices,  f_devices, "mcd_qry_devices_f");
    LOADFN(h, pfn_qry_cores,    f_cores,   "mcd_qry_cores_f");
    LOADFN(h, pfn_qry_error,    f_err,     "mcd_qry_error_info_f");
    LOADFN(h, pfn_run,          f_run,     "mcd_run_f");
    LOADFN(h, pfn_stop,         f_stop,    "mcd_stop_f");
    LOADFN(h, pfn_rst,          f_rst,     "mcd_rst_f");
    LOADFN(h, pfn_createtrig,   f_createtrig,   "mcd_create_trig_f");
    LOADFN(h, pfn_qrytrigset,   f_qrytrigset,   "mcd_qry_trig_set_f");
    LOADFN(h, pfn_qrytrigstate, f_qrytrigstate, "mcd_qry_trig_state_f");
    LOADFN(h, pfn_removetrig,   f_removetrig,   "mcd_remove_trig_f");
    LOADFN(h, pfn_qrytraces,    f_qrytraces,    "mcd_qry_traces_f");
    LOADFN(h, pfn_qrytracestate,f_qrytracestate,"mcd_qry_trace_state_f");
    LOADFN(h, pfn_settracestate,f_settracestate,"mcd_set_trace_state_f");
    LOADFN(h, pfn_readtrace,    f_readtrace,    "mcd_read_trace_f");
    LOADFN(h, pfn_qrycoremodes, f_qrycoremodes, "mcd_qry_core_modes_f");
    LOADFN(h, pfn_qryoverlays,  f_qryoverlays,  "mcd_qry_active_overlays_f");
    LOADFN(h, pfn_qryregcompound, f_qryregcompound, "mcd_qry_reg_compound_f");
    LOADFN(h, pfn_qryctrigs,    f_qryctrigs,    "mcd_qry_ctrigs_f");
    LOADFN(h, pfn_qrycurrenttime, f_qrycurrenttime, "mcd_qry_current_time_f");
    LOADFN(h, pfn_qrydevdesc,   f_qrydevdesc,   "mcd_qry_device_description_f");
    LOADFN(h, pfn_qrymaxpayload, f_qrymaxpayload, "mcd_qry_max_payload_size_f");
    LOADFN(h, pfn_qrytriginfo,  f_qrytriginfo,  "mcd_qry_trig_info_f");
    LOADFN(h, pfn_open_core,    f_opencore,   "mcd_open_core_f");
    LOADFN(h, pfn_close_core,   f_closecore,  "mcd_close_core_f");
    LOADFN(h, pfn_qry_mem_spaces, f_memspaces, "mcd_qry_mem_spaces_f");
    LOADFN(h, pfn_qry_mem_blocks, f_memblocks, "mcd_qry_mem_blocks_f");
    LOADFN(h, pfn_qry_rst_classes, f_rstclasses, "mcd_qry_rst_classes_f");
    LOADFN(h, pfn_qry_rst_class_info, f_rstclassinfo, "mcd_qry_rst_class_info_f");
    LOADFN(h, pfn_execute_txlist, f_txlist,   "mcd_execute_txlist_f");
    LOADFN(h, pfn_qry_state,    f_state,      "mcd_qry_state_f");
    printf("all exports resolved\n\n");

    /* 1. initialize (the client acts as a v1.1 "SPRINT Release" tool) */
    {
        mcd_api_version_st req;
        mcd_impl_version_info_st impl;
        memset(&req, 0, sizeof(req));
        req.v_api_major = MCD_API_VER_MAJOR;
        req.v_api_minor = MCD_API_VER_MINOR;
        strncpy(req.author, MCD_API_VER_AUTHOR, sizeof(req.author) - 1);

        r = f_init(&req, &impl);
        CHK(r == MCD_RET_ACT_NONE, "mcd_initialize_f");
        if (r == MCD_RET_ACT_NONE) {
            printf("       impl: api %u.%u '%s', vendor '%s', build %u.%u.%u, date %s\n",
                   (unsigned)impl.v_api.v_api_major, (unsigned)impl.v_api.v_api_minor,
                   impl.v_api.author, impl.vendor,
                   (unsigned)impl.v_imp_major, (unsigned)impl.v_imp_minor,
                   (unsigned)impl.v_imp_build, impl.date);
        }
    }

    /* 2. query running servers */
    {
        mcd_server_info_st svr[4];
        n = 0;
        r = f_qrysvr("localhost", TRUE, 0, &n, NULL);
        CHK(r == MCD_RET_ACT_NONE, "mcd_qry_servers_f (count)");
        printf("       running servers on localhost: %u\n", (unsigned)n);
        if (n > 0) {
            uint32_t cap = 4;
            memset(svr, 0, sizeof(svr));
            r = f_qrysvr("localhost", TRUE, 0, &cap, svr);
            CHK(r == MCD_RET_ACT_NONE && cap >= 1, "mcd_qry_servers_f (fetch)");
            for (i = 0; i < cap && i < 4; i++)
                printf("       server[%u]: '%s' acc_hw '%s' system_instance '%s'\n",
                       i, svr[i].server, svr[i].acc_hw, svr[i].system_instance);
        }
    }

    /* 3. open server (read-only handshake + target enumeration) */
    {
        mcd_server_st *srv = NULL;
        mcd_core_con_info_st system_ci;
        mcd_core_con_info_st devs[8];
        mcd_core_con_info_st cores[8];
        uint32_t nsys, ndev, ncore;

        /* The reference demo passes an empty string (never NULL) for the
         * system key; the original mcdxdas.dll crashes on NULL. */
        r = f_open((const mcd_char_t *)"", cfg, &srv);
        CHK(r == MCD_RET_ACT_NONE && srv != NULL, "mcd_open_server_f");
        if (r != MCD_RET_ACT_NONE || srv == NULL) {
            mcd_error_info_st ei;
            f_err(NULL, &ei);
            printf("       error 0x%08x: %s\n", (unsigned)ei.error_code, ei.error_str);
            f_exit();
            FreeLibrary(h);
            return 1;
        }

        /* 4. systems */
        nsys = 4;
        memset(&system_ci, 0, sizeof(system_ci));
        r = f_systems(0, &nsys, &system_ci);
        CHK(r == MCD_RET_ACT_NONE && nsys == 1, "mcd_qry_systems_f");
        printf("       system '%s' instance '%s' host %s:%u\n",
               system_ci.system, system_ci.system_instance,
               system_ci.host, (unsigned)system_ci.server_port);

        /* 5. devices */
        ndev = 8;
        memset(devs, 0, sizeof(devs));
        r = f_devices(&system_ci, 0, &ndev, devs);
        CHK(r == MCD_RET_ACT_NONE && ndev >= 1, "mcd_qry_devices_f");
        for (i = 0; i < ndev && i < 8; i++)
            printf("       device[%u]: acc_hw '%s' device '%s' type 0x%08X id 0x%08X\n",
                   i, devs[i].acc_hw, devs[i].device,
                   (unsigned)devs[i].device_type, (unsigned)devs[i].device_id);

        /* 6. cores of the first device */
        if (ndev >= 1) {
            ncore = 8;
            memset(cores, 0, sizeof(cores));
            r = f_cores(&devs[0], 0, &ncore, cores);
            CHK(r == MCD_RET_ACT_NONE && ncore >= 1, "mcd_qry_cores_f");
            for (i = 0; i < ncore && i < 8; i++)
                printf("       core[%u]: '%s' core_type %u core_id 0x%08X (acc_hw '%s')\n",
                       i, cores[i].core, (unsigned)cores[i].core_type,
                       (unsigned)cores[i].core_id, cores[i].acc_hw);
        }

        /* 6b. M2: core attach + memory access (only with --attach) */
        if (!attach) {
            printf("  [skip] M2 core attach test (use --attach to include TAS DEVICE_CONNECT)\n");
        } else if (ndev < 1 || ncore < 1) {
            printf("  [skip] M2 core attach test (no core enumerated)\n");
        } else {
            mcd_core_st *core = NULL;

            printf("  NOTE: --attach performs TAS DEVICE_CONNECT (JTAG hot attach, no reset/halt)\n");
            r = f_opencore(&cores[0], &core);
            CHK(r == MCD_RET_ACT_NONE && core != NULL, "mcd_open_core_f (session + attach)");
            if (r != MCD_RET_ACT_NONE || core == NULL) {
                mcd_error_info_st ei;
                f_err(NULL, &ei);
                printf("       error 0x%08x: %s\n", (unsigned)ei.error_code, ei.error_str);
            } else {
                /* memory spaces */
                {
                    mcd_memspace_st sp;
                    uint32_t nm = 1;
                    memset(&sp, 0, sizeof(sp));
                    r = f_memspaces(core, 0, &nm, &sp);
                    CHK(r == MCD_RET_ACT_NONE && nm == 1, "mcd_qry_mem_spaces_f");
                    if (r == MCD_RET_ACT_NONE && nm == 1)
                        printf("       mem space id %u '%s' type 0x%x range 0x%llX..0x%llX opts 0x%X\n",
                               (unsigned)sp.mem_space_id, sp.mem_space_name,
                               (unsigned)sp.mem_type,
                               (unsigned long long)sp.min_addr,
                               (unsigned long long)sp.max_addr,
                               (unsigned)sp.supported_access_options);
                }

                /* memory blocks (reference reports unimplemented) */
                {
                    uint32_t nb = 0;
                    mcd_error_info_st ei;
                    memset(&ei, 0, sizeof(ei));
                    r = f_memblocks(core, 0, 0, &nb, NULL);
                    CHK(r == MCD_RET_ACT_HANDLE_ERROR,
                        "mcd_qry_mem_blocks_f returns HANDLE_ERROR (reference: unimplemented)");
                    f_err(NULL, &ei);
                    printf("       mem blocks rc %d: code 0x%08x '%s'\n",
                           (int)r, (unsigned)ei.error_code, ei.error_str);
                }

                /* reset classes (reference advertises class 0..2) */
                {
                    uint32_t vec = 0, b;
                    r = f_rstclasses(core, &vec);
                    CHK(r == MCD_RET_ACT_NONE, "mcd_qry_rst_classes_f");
                    printf("       rst class vector 0x%08X\n", (unsigned)vec);
                    for (b = 0; b < 8; b++) {
                        if (vec & (1u << b)) {
                            mcd_rst_info_st ri;
                            memset(&ri, 0, sizeof(ri));
                            r = f_rstclassinfo(core, (uint8_t)b, &ri);
                            if (r == MCD_RET_ACT_NONE)
                                printf("       rst_info[%u]: class_vector 0x%08X '%s'\n",
                                       (unsigned)b,
                                       (unsigned)ri.class_vector, ri.info_str);
                        }
                    }
                }

                /* enumeration should now show the identified device */
                {
                    uint32_t nd2 = 8;
                    mcd_core_con_info_st dv[8];
                    memset(dv, 0, sizeof(dv));
                    if (f_devices(&system_ci, 0, &nd2, dv) == MCD_RET_ACT_NONE && nd2 >= 1)
                        printf("       after attach: device '%s' type 0x%08X id 0x%08X\n",
                               dv[0].device, (unsigned)dv[0].device_type,
                               (unsigned)dv[0].device_id);
                }

                /* read-only txlist: 0xF0000480 is the first address the
                 * reference MCD client reads after attaching */
                {
                    mcd_tx_st tx[2];
                    uint8_t  b0[4], b1[16];
                    mcd_txlist_st tl;

                    memset(tx, 0, sizeof(tx));
                    memset(b0, 0, sizeof(b0));
                    memset(b1, 0, sizeof(b1));

                    tx[0].addr.mem_space_id = 1;
                    tx[0].addr.address = 0xF0000480ull;
                    tx[0].access_type = MCD_TX_AT_R;
                    tx[0].access_width = 4;
                    tx[0].num_bytes = 4;
                    tx[0].data = b0;

                    tx[1].addr.mem_space_id = 1;
                    tx[1].addr.address = 0xF0000480ull;
                    tx[1].access_type = MCD_TX_AT_R;
                    tx[1].access_width = 0;
                    tx[1].num_bytes = 16;
                    tx[1].data = b1;

                    tl.tx = tx;
                    tl.num_tx = 2;
                    tl.num_tx_ok = 0;

                    r = f_txlist(core, &tl);
                    CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 2,
                        "mcd_execute_txlist_f (2 read transactions)");
                    if (r == MCD_RET_ACT_NONE && tl.num_tx_ok == 2) {
                        printf("       rd32  @0xF0000480 ->");
                        for (i = 0; i < 4; i++) printf(" %02X", b0[i]);
                        printf("\n       rd16B @0xF0000480 ->");
                        for (i = 0; i < 16; i++) printf(" %02X", b1[i]);
                        printf("\n");
                    } else {
                        mcd_error_info_st ei;
                        f_err(NULL, &ei);
                        printf("       error 0x%08x: %s (num_tx_ok %u)\n",
                               (unsigned)ei.error_code, ei.error_str,
                               (unsigned)tl.num_tx_ok);
                    }
                }

                /* M20: local SPR window translation (--spr).  The
                 * reference implementation resolves the core-local
                 * scratchpad windows for the addressed core; TAS only
                 * knows the global bus view.  Verify that the local
                 * addresses reach the same cells as the core's bus
                 * aliases (read-only, CPU0):
                 *   DSPR local 0xD0000000 == bus 0x70000000
                 *   PSPR local 0xC0000000 == bus 0x70100000 */
                if (spr) {
                    static const struct {
                        unsigned long long loc, bus;
                        const char *name;
                    } pairs[2] = {
                        { 0xD0000000ull, 0x70000000ull, "DSPR" },
                        { 0xC0000000ull, 0x70100000ull, "PSPR" },
                    };
                    unsigned k;

                    printf("  -- M20 SPR translation check --\n");
                    for (k = 0; k < 2; k++) {
                        uint8_t bl[4], bb[4];
                        mcd_tx_st tx[2];
                        mcd_txlist_st tl;

                        memset(tx, 0, sizeof(tx));
                        memset(bl, 0, sizeof(bl));
                        memset(bb, 0, sizeof(bb));
                        tx[0].addr.mem_space_id = 0;
                        tx[0].addr.address = pairs[k].loc;
                        tx[0].access_type = MCD_TX_AT_R;
                        tx[0].num_bytes = 4;
                        tx[0].data = bl;
                        tx[1].addr.mem_space_id = 0;
                        tx[1].addr.address = pairs[k].bus;
                        tx[1].access_type = MCD_TX_AT_R;
                        tx[1].num_bytes = 4;
                        tx[1].data = bb;
                        tl.tx = tx;
                        tl.num_tx = 2;
                        tl.num_tx_ok = 0;

                        r = f_txlist(core, &tl);
                        printf("       %s local 0x%08llX -> %02X %02X %02X %02X"
                               " | bus 0x%08llX -> %02X %02X %02X %02X"
                               " (rc %d, ok %u)\n",
                               pairs[k].name, pairs[k].loc,
                               bl[0], bl[1], bl[2], bl[3],
                               pairs[k].bus,
                               bb[0], bb[1], bb[2], bb[3],
                               (int)r, (unsigned)tl.num_tx_ok);
                        if (r != MCD_RET_ACT_NONE || tl.num_tx_ok != 2) {
                            mcd_error_info_st ei;
                            f_err(NULL, &ei);
                            printf("       error 0x%08x: %s\n",
                                   (unsigned)ei.error_code, ei.error_str);
                        }
                        CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 2 &&
                                memcmp(bl, bb, 4) == 0,
                            "local SPR reads match the bus alias");
                    }
                }

                /* M20: SPR write-path check (--sprw, STATE-CHANGING).
                 * Write a pattern through the LOCAL DSPR address, verify
                 * it through the bus alias, then restore the original
                 * cell content (read first) - the M12/M15 procedure. */
                if (spr && sprw) {
                    const uint32_t pat = 0x5AA55AA5u;
                    uint32_t orig = 0, v1 = 0, v2 = 0, v3 = 0;
                    int ok_bus, ok_loc;
                    mcd_tx_st tx;
                    mcd_txlist_st tl;

                    printf("  -- M20 SPR write-path check (--sprw) --\n");
                    memset(&tx, 0, sizeof(tx));
                    tl.tx = &tx;
                    tl.num_tx = 1;
                    tx.addr.mem_space_id = 0;
                    tx.num_bytes = 4;

                    /* 1. original content via the bus alias */
                    tx.access_type = MCD_TX_AT_R;
                    tx.addr.address = 0x70000000ull;
                    tx.data = (uint8_t *)&orig;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1,
                        "read original DSPR[0] content (bus)");

                    /* 2. write pattern via the LOCAL address */
                    tx.access_type = MCD_TX_AT_W;
                    tx.addr.address = 0xD0000000ull;
                    tx.data = (uint8_t *)&pat;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1,
                        "write pattern via local DSPR address");

                    /* 3. pattern must be visible through the bus alias */
                    tx.access_type = MCD_TX_AT_R;
                    tx.addr.address = 0x70000000ull;
                    tx.data = (uint8_t *)&v1;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1 && v1 == pat,
                        "pattern visible through the bus alias");

                    /* 4. restore the original content via the bus */
                    tx.access_type = MCD_TX_AT_W;
                    tx.addr.address = 0x70000000ull;
                    tx.data = (uint8_t *)&orig;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    CHK(r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1,
                        "restore original content (bus)");

                    /* 5. restore verified through both views */
                    tx.access_type = MCD_TX_AT_R;
                    tx.addr.address = 0x70000000ull;
                    tx.data = (uint8_t *)&v2;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    ok_bus = (r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1);
                    tx.addr.address = 0xD0000000ull;
                    tx.data = (uint8_t *)&v3;
                    tl.num_tx_ok = 0;
                    r = f_txlist(core, &tl);
                    ok_loc = (r == MCD_RET_ACT_NONE && tl.num_tx_ok == 1);
                    printf("       orig 0x%08X | pattern readback 0x%08X | "
                           "restored bus 0x%08X local 0x%08X\n",
                           (unsigned)orig, (unsigned)v1,
                           (unsigned)v2, (unsigned)v3);
                    CHK(ok_bus && ok_loc && v2 == orig && v3 == orig,
                        "DSPR cell restored (bus and local view)");
                }

                /* state query */
                {
                    mcd_core_state_st st;
                    memset(&st, 0, sizeof(st));
                    r = f_state(core, &st);
                    CHK(r == MCD_RET_ACT_NONE, "mcd_qry_state_f");
                    if (r == MCD_RET_ACT_NONE)
                        printf("       state 0x%x (%s) info '%s'\n",
                               (unsigned)st.state,
                               st.state == MCD_CORE_STATE_UNKNOWN ? "unknown" :
                               st.state == MCD_CORE_STATE_HALTED ? "halted" :
                               st.state == MCD_CORE_STATE_RUNNING ? "running" : "other",
                               st.info_str);
                }

                /* M16: run/stop verification (--runstop).  Changes the
                 * target state; the sequence ends with the core halted
                 * again (the state found at attach). */
                if (runstop) {
                    mcd_core_state_st st2;
                    uint32_t pc0 = 0, pc1 = 0;
                    uint32_t state_before = MCD_CORE_STATE_UNKNOWN;

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    state_before = st2.state;
                    printf("       [runstop] before: state 0x%x event 0x%x\n",
                           (unsigned)st2.state, (unsigned)st2.event);

                    r = f_run(core, FALSE);
                    CHK(r == MCD_RET_ACT_NONE, "mcd_run_f (run)");
                    Sleep(300);
                    read_word32(f_txlist, core, 0xF881FD00ull, &pc0);
                    printf("       [runstop] running: DBGSR=0x%08X\n",
                           (unsigned)pc0);
                    CHK((pc0 & 0x6u) == 0x0u, "DBGSR HALT=00b while running");

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    CHK(r == MCD_RET_ACT_NONE &&
                        st2.state == MCD_CORE_STATE_RUNNING,
                        "state RUNNING after run");

                    r = f_stop(core, FALSE);
                    CHK(r == MCD_RET_ACT_NONE, "mcd_stop_f (stop)");
                    read_word32(f_txlist, core, 0xF881FD00ull, &pc1);
                    printf("       [runstop] stopped: DBGSR=0x%08X\n",
                           (unsigned)pc1);
                    CHK((pc1 & 0x6u) == 0x2u, "DBGSR HALT=01b while halted");

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    CHK(r == MCD_RET_ACT_NONE &&
                        st2.state == MCD_CORE_STATE_DEBUG,
                        "state DEBUG after stop");
                    CHK(st2.event == MCD_CORE_EVENT_STOPPED,
                        "one-shot STOPPED event after stop");
                    printf("       [runstop] after: state 0x%x event 0x%x\n",
                           (unsigned)st2.state, (unsigned)st2.event);

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    CHK(r == MCD_RET_ACT_NONE &&
                        st2.event == MCD_CORE_EVENT_NONE,
                        "STOPPED event cleared on next poll");

                    /* safety invariant: restore the state found at entry */
                    if (state_before == MCD_CORE_STATE_RUNNING) {
                        r = f_run(core, FALSE);
                        CHK(r == MCD_RET_ACT_NONE,
                            "restore RUNNING (state found at entry)");
                        printf("       [runstop] restored: RUNNING (as found)\n");
                    } else {
                        printf("       [runstop] restored: DEBUG (as found)\n");
                    }
                }

                /* M17: reset verification (--rst).  Triggers the TAS
                 * DEVICE_CONNECT reset (reset and halt): the device boots
                 * and stays halted at the reset vector. */
                if (rst) {
                    mcd_core_state_st st2;
                    uint32_t pc0 = 0, pc1 = 0, dbgsr = 0;
                    uint32_t state_before = MCD_CORE_STATE_UNKNOWN;

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    state_before = st2.state;
                    read_word32(f_txlist, core, 0xF881FE08ull, &pc0);
                    printf("       [rst] before: state 0x%x PC=0x%08X\n",
                           (unsigned)st2.state, (unsigned)pc0);

                    r = f_rst(core, 0x7u, TRUE);
                    CHK(r == MCD_RET_ACT_NONE,
                        "mcd_rst_f (class vector 0x7, reset and halt)");

                    Sleep(400);
                    read_word32(f_txlist, core, 0xF881FE08ull, &pc1);
                    read_word32(f_txlist, core, 0xF881FD00ull, &dbgsr);
                    printf("       [rst] after: PC=0x%08X DBGSR=0x%08X\n",
                           (unsigned)pc1, (unsigned)dbgsr);
                    CHK((dbgsr & 0x6u) == 0x2u,
                        "DBGSR HALT=01b after reset and halt");
                    CHK(pc1 != pc0, "PC changed after reset (reset vector)");

                    memset(&st2, 0, sizeof(st2));
                    r = f_state(core, &st2);
                    CHK(r == MCD_RET_ACT_NONE &&
                        st2.state == MCD_CORE_STATE_DEBUG,
                        "state DEBUG after reset and halt");

                    /* M17b: reset-and-run must leave DBGSR.DE == 1 so a
                     * later stop works (the plain RESET option leaves
                     * DE == 0 and the halt write is ignored by hardware). */
                    r = f_rst(core, 0x7u, FALSE);
                    CHK(r == MCD_RET_ACT_NONE,
                        "mcd_rst_f (class vector 0x7, reset and run)");
                    Sleep(300);
                    read_word32(f_txlist, core, 0xF881FD00ull, &dbgsr);
                    CHK((dbgsr & 0x1u) == 0x1u,
                        "DBGSR DE=1 after reset and run");
                    CHK((dbgsr & 0x6u) == 0x0u,
                        "DBGSR HALT=00b after reset and run");

                    r = f_stop(core, FALSE);
                    CHK(r == MCD_RET_ACT_NONE,
                        "mcd_stop_f after reset and run");
                    read_word32(f_txlist, core, 0xF881FD00ull, &dbgsr);
                    CHK((dbgsr & 0x6u) == 0x2u,
                        "DBGSR HALT=01b after stop (reset-and-run case)");

                    /* safety invariant: restore the state found at entry */
                    if (state_before == MCD_CORE_STATE_RUNNING) {
                        r = f_run(core, FALSE);
                        CHK(r == MCD_RET_ACT_NONE,
                            "restore RUNNING (state found at entry)");
                        printf("       [rst] restored: RUNNING (as found)\n");
                    } else {
                        printf("       [rst] restored: DEBUG (as found)\n");
                    }
                }

                /* M18: IP trigger (breakpoint) verification (--trig). */
                if (trig) {
                    mcd_trig_simple_core_st tdef;
                    uint32_t tid = 0, dbgsr2 = 0, pc2 = 0;
                    uint32_t ids[8];
                    uint32_t nids;
                    mcd_trig_state_st tst;
                    mcd_core_state_st st3;
                    uint32_t state_before = MCD_CORE_STATE_UNKNOWN;
                    int hit = 0, k;

                    memset(&st3, 0, sizeof(st3));
                    r = f_state(core, &st3);
                    state_before = st3.state;
                    printf("       [trig] before: state 0x%x\n",
                           (unsigned)st3.state);

                    /* breakpoint at a PC inside the firmware's running loop */
                    memset(&tdef, 0, sizeof(tdef));
                    tdef.struct_size = sizeof(tdef);
                    tdef.type = MCD_TRIG_TYPE_IP;
                    tdef.option = MCD_TRIG_OPT_DEFAULT;
                    tdef.action = MCD_TRIG_ACTION_DBG_DEBUG;
                    tdef.addr_start.mem_space_id = 1;
                    tdef.addr_start.address = 0x8003A160ull;
                    tdef.addr_range = 0;

                    r = f_createtrig(core, &tdef, &tid);
                    CHK(r == MCD_RET_ACT_NONE && tid != 0,
                        "mcd_create_trig_f (IP breakpoint @0x8003A160)");
                    printf("       [trig] created id %u\n", (unsigned)tid);

                    nids = 8;
                    r = f_qrytrigset(core, 0, &nids, ids);
                    CHK(r == MCD_RET_ACT_NONE && nids == 1 && ids[0] == tid,
                        "mcd_qry_trig_set_f lists the trigger");

                    if (state_before == MCD_CORE_STATE_RUNNING) {
                        printf("       [trig] core already running; the armed "
                               "trigger halts it by itself\n");
                    } else {
                        r = f_run(core, FALSE);
                        if (r != MCD_RET_ACT_NONE)
                            printf("       [trig] run returned %d (trigger may "
                                   "have halted the core during the run "
                                   "verification)\n", (int)r);
                    }

                    /* poll up to ~2 s for the breakpoint halt */
                    for (k = 0; k < 40; k++) {
                        Sleep(50);
                        read_word32(f_txlist, core, 0xF881FD00ull, &dbgsr2);
                        if (((dbgsr2 >> 1) & 3u) == 1u) {
                            hit = 1;
                            break;
                        }
                    }
                    read_word32(f_txlist, core, 0xF881FE08ull, &pc2);
                    printf("       [trig] after run: DBGSR=0x%08X PC=0x%08X "
                           "(EVTSRC=%u)\n", (unsigned)dbgsr2, (unsigned)pc2,
                           (unsigned)((dbgsr2 >> 8) & 0x1Fu));
                    CHK(hit, "core halted on the IP trigger");
                    CHK(pc2 == 0x8003A160u,
                        "halted PC equals the breakpoint address");

                    memset(&tst, 0, sizeof(tst));
                    r = f_qrytrigstate(core, tid, &tst);
                    CHK(r == MCD_RET_ACT_NONE && tst.captured,
                        "mcd_qry_trig_state_f reports captured");

                    r = f_removetrig(core, tid);
                    CHK(r == MCD_RET_ACT_NONE, "mcd_remove_trig_f");
                    nids = 8;
                    r = f_qrytrigset(core, 0, &nids, ids);
                    CHK(r == MCD_RET_ACT_NONE && nids == 0,
                        "trigger set empty after removal");

                    /* restore the state found at entry */
                    if (state_before == MCD_CORE_STATE_RUNNING) {
                        printf("       [trig] restored: RUNNING (as found)\n");
                    } else {
                        r = f_stop(core, FALSE);
                        CHK(r == MCD_RET_ACT_NONE,
                            "restore DEBUG (state found at entry)");
                        printf("       [trig] restored: DEBUG (as found)\n");
                    }
                }

                /* M19: MCD trace API probing (--trace; read-only + no-op set). */
                if (trace) {
                    mcd_trace_info_st ti;
                    uint32_t ntr = 4;
                    mcd_error_info_st ei;

                    memset(&ti, 0, sizeof(ti));
                    memset(&ei, 0, sizeof(ei));
                    r = f_qrytraces(core, 0, &ntr, &ti);
                    f_err(core, &ei);
                    printf("       [trace] qry_traces rc %d num %u code 0x%08x "
                           "'%s'\n", (int)r, (unsigned)ntr,
                           (unsigned)ei.error_code, ei.error_str);
                    if (r == MCD_RET_ACT_NONE && ntr > 0) {
                        mcd_trace_state_st ts;
                        memset(&ts, 0, sizeof(ts));
                        printf("       [trace] trace[0] id %u name '%s' "
                               "type 0x%x format 0x%x modes 0x%x\n",
                               (unsigned)ti.trace_id, ti.trace_name,
                               (unsigned)ti.trace_type,
                               (unsigned)ti.trace_format,
                               (unsigned)ti.trace_modes);
                        r = f_qrytracestate(core, ti.trace_id, &ts);
                        f_err(core, &ei);
                        printf("       [trace] qry_trace_state rc %d state 0x%x "
                               "frames %llu size %llu code 0x%08x '%s'\n",
                               (int)r, (unsigned)ts.state,
                               (unsigned long long)ts.frames,
                               (unsigned long long)ts.size,
                               (unsigned)ei.error_code, ei.error_str);
                        r = f_settracestate(core, ti.trace_id, &ts);
                        f_err(core, &ei);
                        printf("       [trace] set_trace_state(same) rc %d "
                               "code 0x%08x '%s'\n", (int)r,
                               (unsigned)ei.error_code, ei.error_str);
                        {
                            uint32_t frames = 4;
                            uint8_t tbuf[256];
                            memset(tbuf, 0, sizeof(tbuf));
                            r = f_readtrace(core, ti.trace_id, 0, &frames,
                                            sizeof(tbuf), tbuf);
                            f_err(core, &ei);
                            printf("       [trace] read_trace rc %d frames %u "
                                   "code 0x%08x '%s'\n", (int)r,
                                   (unsigned)frames,
                                   (unsigned)ei.error_code, ei.error_str);
                        }
                    }
                    {
                        mcd_trace_state_st ts2;
                        uint32_t frames = 1;
                        uint8_t tbuf[64];

                        memset(&ts2, 0, sizeof(ts2));
                        memset(tbuf, 0, sizeof(tbuf));
                        r = f_qrytracestate(core, 0x1234u, &ts2);
                        f_err(core, &ei);
                        printf("       [trace] qry_trace_state(id 0x1234) "
                               "rc %d code 0x%08x '%s'\n", (int)r,
                               (unsigned)ei.error_code, ei.error_str);

                        r = f_settracestate(core, 0x1234u, &ts2);
                        f_err(core, &ei);
                        printf("       [trace] set_trace_state(id 0x1234, "
                               "zero state) rc %d code 0x%08x '%s'\n", (int)r,
                               (unsigned)ei.error_code, ei.error_str);

                        r = f_readtrace(core, 0x1234u, 0, &frames,
                                        sizeof(tbuf), tbuf);
                        f_err(core, &ei);
                        printf("       [trace] read_trace(id 0x1234) rc %d "
                               "frames %u code 0x%08x '%s'\n", (int)r,
                               (unsigned)frames,
                               (unsigned)ei.error_code, ei.error_str);
                    }

                    /* M19: parity probes for the remaining fixed-response
                     * functions (all read-only; real buffers — the
                     * original crashes on NULL buffers). */
                    {
                        mcd_core_mode_info_st mbuf[4];
                        uint32_t nm = 4;
                        uint32_t mi;
                        memset(mbuf, 0, sizeof(mbuf));
                        r = f_qrycoremodes(core, 0, &nm, mbuf);
                        f_err(core, &ei);
                        printf("       [misc] qry_core_modes rc %d num %u "
                               "code 0x%08x '%s'\n", (int)r, (unsigned)nm,
                               (unsigned)ei.error_code, ei.error_str);
                        for (mi = 0; mi < nm && mi < 4; mi++)
                            printf("       [misc]   mode[%u] id %u name '%s'\n",
                                   (unsigned)mi,
                                   (unsigned)mbuf[mi].core_mode,
                                   mbuf[mi].name);
                    }
                    {
                        uint32_t obuf[4];
                        uint32_t nov = 4;
                        memset(obuf, 0, sizeof(obuf));
                        r = f_qryoverlays(core, 0, &nov, obuf);
                        f_err(core, &ei);
                        printf("       [misc] qry_active_overlays rc %d num %u "
                               "code 0x%08x '%s'\n", (int)r, (unsigned)nov,
                               (unsigned)ei.error_code, ei.error_str);
                    }
                    {
                        uint8_t cbuf[4 * 128];
                        uint32_t ncr = 4;
                        memset(cbuf, 0, sizeof(cbuf));
                        r = f_qryctrigs(core, 0, &ncr,
                                        (mcd_ctrig_info_st *)cbuf);
                        f_err(core, &ei);
                        printf("       [misc] qry_ctrigs rc %d num %u "
                               "code 0x%08x '%s'\n", (int)r, (unsigned)ncr,
                               (unsigned)ei.error_code, ei.error_str);
                    }
                    {
                        uint32_t rbuf[4];
                        uint32_t nrc = 4;
                        memset(rbuf, 0, sizeof(rbuf));
                        r = f_qryregcompound(core, 0, 0, &nrc, rbuf);
                        f_err(core, &ei);
                        printf("       [misc] qry_reg_compound rc %d num %u "
                               "code 0x%08x '%s'\n", (int)r, (unsigned)nrc,
                               (unsigned)ei.error_code, ei.error_str);
                    }
                    {
                        uint64_t t = 0;
                        r = f_qrycurrenttime(core, &t);
                        f_err(core, &ei);
                        printf("       [misc] qry_current_time rc %d time %llu "
                               "code 0x%08x '%s'\n", (int)r,
                               (unsigned long long)t,
                               (unsigned)ei.error_code, ei.error_str);
                    }
                    {
                        uint32_t len = 0;
                        r = f_qrydevdesc(core, NULL, &len);
                        f_err(core, &ei);
                        printf("       [misc] qry_device_description(NULL) "
                               "rc %d len %u code 0x%08x '%s'\n", (int)r,
                               (unsigned)len, (unsigned)ei.error_code,
                               ei.error_str);
                        if (r == MCD_RET_ACT_NONE && len > 0 && len < 256) {
                            char ubuf[256];
                            uint32_t len2 = len;
                            memset(ubuf, 0, sizeof(ubuf));
                            r = f_qrydevdesc(core, ubuf, &len2);
                            f_err(core, &ei);
                            printf("       [misc] qry_device_description "
                                   "rc %d len %u '%s'\n", (int)r,
                                   (unsigned)len2, ubuf);
                        }
                    }
                    {
                        uint32_t mp = 0;
                        r = f_qrymaxpayload(core, &mp);
                        f_err(core, &ei);
                        printf("       [misc] qry_max_payload_size rc %d "
                               "max %u code 0x%08x '%s'\n", (int)r,
                               (unsigned)mp, (unsigned)ei.error_code,
                               ei.error_str);
                    }
                    {
                        mcd_trig_info_st tinfo;
                        memset(&tinfo, 0, sizeof(tinfo));
                        r = f_qrytriginfo(core, &tinfo);
                        f_err(core, &ei);
                        printf("       [misc] qry_trig_info rc %d type 0x%x "
                               "opt 0x%x act 0x%x num %u state %u cnt %u swbp %u "
                               "code 0x%08x '%s'\n", (int)r,
                               (unsigned)tinfo.type, (unsigned)tinfo.option,
                               (unsigned)tinfo.action,
                               (unsigned)tinfo.trig_number,
                               (unsigned)tinfo.state_number,
                               (unsigned)tinfo.counter_number,
                               (unsigned)tinfo.sw_breakpoints,
                               (unsigned)ei.error_code, ei.error_str);
                    }
                }

                r = f_closecore(core);
                CHK(r == MCD_RET_ACT_NONE, "mcd_close_core_f");
            }
        }

        /* 7. invalid-handle path: mcd_run_f(NULL) must report an error
         * (mcd_run_f itself is implemented since M16; see --runstop).
         * Skipped in --trace mode: the original mcdxdas.dll crashes on a
         * NULL core handle instead of returning an error. */
        if (!trace) {
            mcd_error_info_st ei;
            r = f_run(NULL, TRUE);
            CHK(r == MCD_RET_ACT_HANDLE_ERROR, "mcd_run_f(NULL) returns HANDLE_ERROR");
            f_err(NULL, &ei);
            printf("       error 0x%08x: %s\n", (unsigned)ei.error_code, ei.error_str);
        }

        /* 8. close server + exit */
        r = f_close(srv);
        CHK(r == MCD_RET_ACT_NONE, "mcd_close_server_f");
    }

    f_exit();
    FreeLibrary(h);

    printf("\n== %s (%d failure(s)) ==\n", g_fails ? "SMOKE FAILED" : "SMOKE OK", g_fails);
    return g_fails ? 1 : 0;
}
