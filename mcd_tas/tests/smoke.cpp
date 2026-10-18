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
    int runstop = 0;
    int rst = 0;
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
    }

    printf("== mcd_tas smoke test ==\n");
    printf("dll: %s\ncfg: %s\nmode: %s%s%s\n\n", dll, cfg ? cfg : "(default)",
           attach ? "with core attach (--attach)" : "read-only",
           runstop ? " + run/stop (--runstop)" : "",
           rst ? " + reset (--rst)" : "");

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

        r = f_open(NULL, cfg, &srv);
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

                r = f_closecore(core);
                CHK(r == MCD_RET_ACT_NONE, "mcd_close_core_f");
            }
        }

        /* 7. invalid-handle path: mcd_run_f(NULL) must report an error
         * (mcd_run_f itself is implemented since M16; see --runstop) */
        {
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
