/* mcdsrl_impl.cpp - MCDS Recorder Library (mcds_recorder.dll) prototype over TAS.
 *
 * M23-1 scope: the library core and the device/RW functions of the
 * official mcds_lib_recorder.h API (22 exports):
 *   lib_init / lib_version / get_error
 *   qry_devices / connect_device / device_connected / disconnect_device /
 *   reset_device
 *   read / write / read_list / write_list / read_block / write_block /
 *   read_timestamped
 * The trace recording functions (set_mcdsconfig / set_trace_buf /
 * open_trace_file / start_recording / stop_recording / get_recstate /
 * get_data_from_fifo) follow in M23-2.
 *
 * References (calibration targets):
 *   - C:\Program Files\DAS64\include\mcds_libs\mcds_lib_recorder.h  (API)
 *   - Ghidra decompile of the reference mcds_recorder.dll 3.3.0.0
 *   - tas_client.cpp (the TAS wire client calibrated in mcd_tas)
 *
 * SPDX-License-Identifier: MIT (project code only; the SPRINT headers in
 * include/ keep their original license).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "mcds_lib.h"
#include "mcds_lib_recorder.h"
#include "mcd_api.h"
#include "tas_client.h"

/* client/session names observed for the reference recorder (M14/M22) */
#define MCDSRL_CLIENT_NAME   "MCDS recorder (MCDSRL-TAS prototype)"
#define MCDSRL_QRY_NAME      "DeviceQry (MCDSRL-TAS prototype)"

/* MCDS free running 64 bit timestamp counter (used by read_timestamped;
 * captured from the reference implementation) */
#define MCDSRL_TIMESTAMP_ADDR 0x50000090ull

/* device classes seen in the reference strings */
#define MCDSRL_DEV_TC2XX_MINIMCDS 1
#define MCDSRL_DEV_TC3XX_MINIMCDS 2

/* ------------------------------------------------------------------ */
typedef struct {
    int                  initialized;
    int                  device_connected;
    tas_conn             conn;
    tas_server_info      srv;
    tas_con_info         con;
    tas_target           targets[TAS_MAX_TARGETS];
    int                  n_targets;
    mcd_core_con_info_st con_connected;
    mcdsrl_cplty_st      cplty;
    char                 last_err[256];
    /* M23-2: the recording state */
    uint32_t             mcds_ctrl_base;   /* the MCDS control registers  */
    uint32_t             mcds_status_base; /* the FIFO/status registers   */
    uint32_t             mcds_emem_base;   /* the EMEM (class 1 devices)  */
    uint32_t             fifo_base;        /* the FIFO bottom (from config) */
    uint32_t             fifo_top;         /* the FIFO top (from config)  */
    uint32_t             trace_max_kb;     /* the trace memory size       */
    int                  mcds_present;     /* the MCDS-ID marker seen     */
    int                  device_class;     /* the class from the device table */
    uint32_t             device_id_short;  /* the table device id (0x310) */
    float                freq_hz;          /* the measured MCDS clock     */
    int                  cfg_set;
    mcdsl_mcdsc_st       mcdsc;          /* stored config (start writes it) */
    mcdsl_mcdsc_st       mcdsc_cleanup;  /* stored cleanup config */
    int                  cleanup_set;
    uint8_t             *trace_buf;      /* the caller's buffer (or NULL) */
    uint32_t             trace_buf_kb;
    FILE                *trace_file;     /* the open .mcds file (or NULL) */
    char                 trace_file_name[260];
    int                  started;
    int                  need_stop;      /* a stop is pending (0xf1)    */
    int                  stop_done;      /* the cached stop result      */
    __time64_t           rec_time;
    uint64_t             start_ms;
    uint32_t             elapsed_ms;
    uint32_t             trace_size_kb;  /* the cached progress */
    uint32_t             stop_size_kb;
    uint32_t             stop_time_ms;
    uint32_t             stop_gaps;
} mcdsrl_ctx_t;

static mcdsrl_ctx_t g;

static const mcdsl_version_st g_version = { { { 3, 3, 0 } } };

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

/* The reference auto-starts the TasServer on the local machine if the
 * mutex is absent (see M14 analysis). */
static void ensure_tas_server(void)
{
    HANDLE m;
    int i;

    m = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\only_one_TasServer_on_host");
    if (m != NULL) {
        CloseHandle(m);
        return;
    }
    ShellExecuteA(NULL, "open",
                  "C:\\Windows\\System32\\start_tas_server.bat", NULL, NULL,
                  SW_SHOWMINNOACTIVE);
    for (i = 0; i < 10; i++) {
        Sleep(500);
        m = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\only_one_TasServer_on_host");
        if (m != NULL) {
            CloseHandle(m);
            return;
        }
    }
}

/* Device class from the JTAG idcode (calibrated against the reference;
 * the values are refined in the smoke comparison). */
static int device_class(uint32_t device_type)
{
    uint32_t id = device_type & 0x00FFFFFFu;

    if ((id & 0x00F00000u) == 0x00100000u)     /* TC2xx family (0x1DA083) */
        return MCDSRL_DEV_TC2XX_MINIMCDS;
    if ((id & 0x00F00000u) == 0x00200000u)     /* TC3xx family (0x21E083) */
        return MCDSRL_DEV_TC3XX_MINIMCDS;
    return 0;
}

/* Device name table (short names from the reference decompile:
 * device_id_to_name_long; the "ED" suffix marks the Emulation Device
 * variants). */
static const char *device_short_name(uint32_t device_type, int ed)
{
    uint32_t id = device_type & 0x0FFFFFFFu;

    switch (id) {
    case 0x1DA083u: return ed ? "TC27xED" : "TC27x";
    case 0x1E8083u: return ed ? "TC26xED" : "TC26x";
    case 0x1E9083u: return ed ? "TC29xED" : "TC29x";
    case 0x200083u: return ed ? "TC23xED" : "TC23x";
    case 0x205083u: return "TC39x";
    case 0x206083u: return "TC38x";
    case 0x207083u: return "TC37x";
    case 0x208083u: return "TC37xED";
    case 0x20A083u: return "TC35x";
    case 0x20B083u: return "TC33x/32x";
    case 0x20C083u: return "TC33xED";
    case 0x21E083u: return "TC49xA";
    case 0x21D083u: return "TC48x";
    default:        return "UNKNOWN";
    }
}

/* The reference distinguishes ED (Emulation Device) variants of the same
 * JTAG idcode: the ED devices carry the EMEM/MCDS.  Probe the EMEM
 * window (0x50000000): a successful read means the EMEM (and thus the
 * on-chip trace hardware) is present. */
static int probe_ed(void)
{
    tas_rw_item it;
    uint8_t pe = 0;
    uint32_t v = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = 0x50000000u;
    it.num_bytes = 4;
    it.data = (uint8_t *)&v;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    return (rc == 0 && it.ok) ? 1 : 0;
}

static void fill_connected_info(void)
{
    mcd_core_con_info_st *c = &g.con_connected;

    memset(c, 0, sizeof(*c));
    strncpy(c->host, "localhost", sizeof(c->host) - 1);
    c->server_port = TAS_DEFAULT_PORT;
    strncpy(c->system, "Real HW", sizeof(c->system) - 1);
    if (g.n_targets > 0)
        strncpy(c->acc_hw, g.targets[0].identifier, sizeof(c->acc_hw) - 1);
    c->device_type = g.con.device_type;
    c->device_id = g.con.device_id[0];
    strncpy(c->device, "TC27x", sizeof(c->device) - 1);
}

/* ------------------------------------------------------------------ */
/* MCDSL core                                                          */
/* ------------------------------------------------------------------ */

mcdsl_return_et mcdsrl_lib_init(uint8_t version_major)
{
    WSADATA wsa;

    if (g.initialized) {
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsrl_lib_init() only once");
    }
    if (version_major != MCDSRL_VERSION_MAJOR) {
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "Unsupported library version %u (expected %u)",
                       (unsigned)version_major, (unsigned)MCDSRL_VERSION_MAJOR);
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return err_set(MCDSL_RET_UNKNOWN_ERROR, "WSAStartup failed");
    }
    memset(&g.conn, 0, sizeof(g.conn));
    g.initialized = 1;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

const mcdsl_version_st *mcdsrl_lib_version(void)
{
    return &g_version;
}

const mcd_char_t *mcdsrl_get_error(void)
{
    return (g.last_err[0] != 0) ? g.last_err : NULL;
}

/* ------------------------------------------------------------------ */
/* Device discovery and connection                                     */
/* ------------------------------------------------------------------ */

mcdsl_return_et mcdsrl_qry_devices(const mcd_char_t *host,
                                   uint32_t max_num_devices,
                                   uint32_t *num_devices,
                                   mcd_core_con_info_st *con_device)
{
    const char *h = (host != NULL && host[0] != 0) ? host : "localhost";
    tas_conn c;
    char detail[160];
    uint8_t e = 0;
    uint32_t i;

    if (!g.initialized)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsrl_lib_init() first");
    if (num_devices == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "num_devices is NULL");

    if (max_num_devices == 0) {
        *num_devices = 1;    /* one access HW in this setup */
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    if (con_device == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "con_device is NULL");

    ensure_tas_server();
    detail[0] = 0;
    if (tas_connect(&c, h, TAS_DEFAULT_PORT, detail, sizeof(detail)) != 0)
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "Could not connect to TasServer (%s)", detail);
    if (tas_server_connect(&c, MCDSRL_QRY_NAME, "admin",
                           GetCurrentProcessId(), &g.srv, &e) != 0 ||
        e != TAS_ERR_OK) {
        tas_disconnect(&c);
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "SERVER_CONNECT rejected (tas_err=0x%02x)", e);
    }
    g.n_targets = 0;
    if (tas_get_targets(&c, g.targets, TAS_MAX_TARGETS, &g.n_targets, &e) != 0 ||
        e != TAS_ERR_OK) {
        tas_disconnect(&c);
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "GET_TARGETS rejected (tas_err=0x%02x)", e);
    }
    tas_disconnect(&c);

    for (i = 0; i < (uint32_t)g.n_targets && i < max_num_devices; i++) {
        mcd_core_con_info_st *d = &con_device[i];

        memset(d, 0, sizeof(*d));
        strncpy(d->host, h, sizeof(d->host) - 1);
        d->server_port = TAS_DEFAULT_PORT;
        strncpy(d->system, "Real HW", sizeof(d->system) - 1);
        strncpy(d->acc_hw, g.targets[i].identifier, sizeof(d->acc_hw) - 1);
        d->device_type = g.targets[i].device_type;
        d->device_id = g.targets[i].device_id[0];
    }
    *num_devices = (uint32_t)g.n_targets;
    if (*num_devices > max_num_devices)
        *num_devices = max_num_devices;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_connect_device(const mcd_core_con_info_st *con_requested,
                                      mcd_core_con_info_st *con_connected)
{
    char detail[160];
    uint8_t e = 0;
    uint16_t feat = 0;
    uint32_t dtype = 0;
    int idx = 0;
    int cls;

    if (!g.initialized)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsrl_lib_init() first");
    if (g.device_connected)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsrl_disconnect_device() before connecting "
                       "another device");
    if (con_connected == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "con_connected is NULL");

    ensure_tas_server();
    detail[0] = 0;
    if (tas_connect(&g.conn, "localhost", TAS_DEFAULT_PORT, detail,
                    sizeof(detail)) != 0)
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "Could not connect to TasServer (%s)", detail);
    if (tas_server_connect(&g.conn, MCDSRL_CLIENT_NAME, "admin",
                           GetCurrentProcessId(), &g.srv, &e) != 0 ||
        e != TAS_ERR_OK) {
        tas_disconnect(&g.conn);
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "SERVER_CONNECT rejected (tas_err=0x%02x)", e);
    }
    g.n_targets = 0;
    if (tas_get_targets(&g.conn, g.targets, TAS_MAX_TARGETS, &g.n_targets,
                        &e) != 0 || e != TAS_ERR_OK) {
        tas_disconnect(&g.conn);
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "GET_TARGETS rejected (tas_err=0x%02x)", e);
    }
    if (g.n_targets < 1) {
        tas_disconnect(&g.conn);
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION, "No target found");
    }
    if (con_requested != NULL && con_requested->acc_hw[0] != 0) {
        int k;
        for (k = 0; k < g.n_targets; k++) {
            if (strcmp(g.targets[k].identifier, con_requested->acc_hw) == 0) {
                idx = k;
                break;
            }
        }
    }
    if (tas_session_start(&g.conn, g.targets[idx].identifier, &g.con,
                          &e) != 0 || e != TAS_ERR_OK) {
        tas_disconnect(&g.conn);
        return err_set(MCDSL_RET_DEVICE_CONNECTION,
                       "SESSION_START rejected (tas_err=0x%02x)", e);
    }
    /* hot attach (the reference declares the unlock option first) */
    tas_set_device_key(&g.conn, TAS_ULO_HOT_ATTACH, NULL, 0, &e);
    if (tas_device_connect(&g.conn, 0 /* hot attach */, &feat, &dtype,
                           &e) != 0 || e != TAS_ERR_OK) {
        tas_disconnect(&g.conn);
        return err_set(MCDSL_RET_DEVICE_CONNECTION,
                       "DEVICE_CONNECT rejected (tas_err=0x%02x)", e);
    }

    fill_connected_info();
    if (dtype != 0)
        g.con_connected.device_type = dtype;
    {
        int ed = probe_ed();

        strncpy(g.con_connected.device,
                device_short_name(g.con_connected.device_type, ed),
                sizeof(g.con_connected.device) - 1);
        /* The MCDS parameter set for the TC2xx miniMCDS devices (from the
         * reference device table and FUN_180005c40): the control register
         * window, the FIFO/status window and the EMEM base. */
        g.mcds_ctrl_base = ed ? 0xF90E6000u : 0u;
        g.mcds_status_base = ed ? 0xF90E8000u : 0u;
        g.mcds_emem_base = ed ? 0x9F000000u : 0u;
    }
    g.device_id_short = 0;
    g.device_class = 0;
    g.mcds_present = 0;
    g.freq_hz = 0.0f;
    g.fifo_base = 0;
    g.fifo_top = 0;
    g.trace_max_kb = 0;
    g.cfg_set = 0;
    g.cleanup_set = 0;
    g.need_stop = 0;
    g.stop_done = 0;
    g.trace_buf = NULL;
    g.trace_buf_kb = 0;
    g.trace_file = NULL;
    g.started = 0;
    g.trace_size_kb = 0;
    cls = device_class(g.con_connected.device_type);
    g.device_class = cls;
    /* the table device id (0x310 for the TC27xED; used by the reference
     * 0x310/0x30F special sequences) */
    if ((g.con_connected.device_type & 0x0FFFFFFFu) == 0x1DA083u)
        g.device_id_short = 0x310;
    /* Calibrated against the reference for this setup (DAS miniWiggler
     * class tool on a Windows PC): continuous trace is possible but
     * unreliable. */
    g.cplty.cplty = (cls != 0) ? MCDSRL_CPLTY_CONT_TRACE_UNRELIABLE
                               : MCDSRL_CPLTY_NO_TRACE;
    g.cplty.bandwidth_kb = 0;
    g.cplty.trace_buf_kb = 0;
    g.device_connected = 1;

    *con_connected = g.con_connected;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

const mcd_core_con_info_st *mcdsrl_device_connected(mcdsrl_cplty_st *cplty)
{
    if (!g.device_connected)
        return NULL;
    if (cplty != NULL)
        *cplty = g.cplty;
    return &g.con_connected;
}

mcdsl_return_et mcdsrl_disconnect_device(void)
{
    if (!g.device_connected)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    if (g.trace_file != NULL) {
        fclose(g.trace_file);
        g.trace_file = NULL;
    }
    g.started = 0;
    g.cfg_set = 0;
    g.cleanup_set = 0;
    tas_disconnect(&g.conn);
    g.device_connected = 0;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_reset_device(mcd_bool_t halt_after_reset)
{
    uint8_t e = 0;
    uint16_t feat = 0;
    uint32_t dtype = 0;

    if (!g.device_connected)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    /* DEVICE_CONNECT with RESET_AND_HALT (3) keeps the debug enabled
     * (M17b finding in mcd_tas); plain RESET (1) may leave DE=0. */
    if (tas_device_connect(&g.conn, halt_after_reset ? 3 : 1, &feat, &dtype,
                           &e) != 0 || e != TAS_ERR_OK)
        return err_set(MCDSL_RET_DEVICE_CONNECTION,
                       "Reset (DEVICE_CONNECT) rejected (tas_err=0x%02x)", e);
    err_clear();
    return MCDSL_RET_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Register and memory access                                          */
/* ------------------------------------------------------------------ */

/* Map a tas_rw_exec result to the MCDSL return code. */
static mcdsl_return_et map_rw_result(int rc, uint8_t pl0_err,
                                     const char *what)
{
    if (rc == 0)
        return MCDSL_RET_DEVICE_DATA;
    if (rc == -1 || rc == -2)
        return err_set(MCDSL_RET_TOOL_HW_CONNECTION,
                       "%s: transport error (rc %d)", what, rc);
    if (rc == -3)
        return err_set(MCDSL_RET_DEVICE_DATA,
                       "%s: PL0 frame rejected (pl0_err=0x%02x)", what,
                       (unsigned)pl0_err);
    return err_set(MCDSL_RET_DEVICE_DATA, "%s: encode error (rc %d)", what,
                   rc);
}

static mcdsl_return_et rw_check(void)
{
    if (!g.device_connected)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_read(uint32_t addr, uint32_t *value)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (value == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY, "value is NULL");

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.data = (uint8_t *)value;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    return map_rw_result((rc == 0) ? 0 : rc, pe, "mcdsrl_read");
}

mcdsl_return_et mcdsrl_write(uint32_t addr, uint32_t value)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.is_write = 1;
    it.data = (uint8_t *)&value;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    return map_rw_result((rc == 0) ? 0 : rc, pe, "mcdsrl_write");
}

mcdsl_return_et mcdsrl_read_list(const uint32_t *addr, uint32_t *value,
                                 uint16_t num, uint16_t *num_ok)
{
    tas_rw_item items[MCDSRL_MAX_LIST_SIZE];
    uint8_t pe = 0;
    int rc;
    uint32_t i;
    uint16_t ok = 0;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (addr == NULL || value == NULL || num == 0 || num > MCDSRL_MAX_LIST_SIZE)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid list parameters");

    memset(items, 0, sizeof(items));
    for (i = 0; i < num; i++) {
        items[i].addr_map = TAS_AM0;
        items[i].addr = addr[i];
        items[i].num_bytes = 4;
        items[i].data = (uint8_t *)&value[i];
    }
    rc = tas_rw_exec(&g.conn, items, num, g.con.pl0_max_num_rw, &pe);
    for (i = 0; i < num; i++)
        if (items[i].ok)
            ok++;
    if (num_ok != NULL)
        *num_ok = ok;
    if (rc == 0 && ok == num) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    if (rc == 0) {
        err_set(MCDSL_RET_DEVICE_DATA,
                "mcdsrl_read_list: %u of %u accesses failed (pl0_err=0x%02x)",
                (unsigned)(num - ok), (unsigned)num, (unsigned)pe);
        return MCDSL_RET_DEVICE_DATA;
    }
    return map_rw_result(rc, pe, "mcdsrl_read_list");
}

mcdsl_return_et mcdsrl_write_list(const uint32_t *addr, const uint32_t *value,
                                  uint16_t num, uint16_t *num_ok)
{
    tas_rw_item items[MCDSRL_MAX_LIST_SIZE];
    uint8_t pe = 0;
    int rc;
    uint32_t i;
    uint16_t ok = 0;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (addr == NULL || value == NULL || num == 0 || num > MCDSRL_MAX_LIST_SIZE)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid list parameters");

    memset(items, 0, sizeof(items));
    for (i = 0; i < num; i++) {
        items[i].addr_map = TAS_AM0;
        items[i].addr = addr[i];
        items[i].num_bytes = 4;
        items[i].is_write = 1;
        items[i].data = (uint8_t *)&value[i];
    }
    rc = tas_rw_exec(&g.conn, items, num, g.con.pl0_max_num_rw, &pe);
    for (i = 0; i < num; i++)
        if (items[i].ok)
            ok++;
    if (num_ok != NULL)
        *num_ok = ok;
    if (rc == 0 && ok == num) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    if (rc == 0) {
        err_set(MCDSL_RET_DEVICE_DATA,
                "mcdsrl_write_list: %u of %u accesses failed (pl0_err=0x%02x)",
                (unsigned)(num - ok), (unsigned)num, (unsigned)pe);
        return MCDSL_RET_DEVICE_DATA;
    }
    return map_rw_result(rc, pe, "mcdsrl_write_list");
}

mcdsl_return_et mcdsrl_read_block(uint32_t addr, void *data,
                                  uint32_t num_bytes, uint32_t *num_bytes_ok)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (data == NULL || num_bytes == 0)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid block parameters");

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = num_bytes;
    it.data = (uint8_t *)data;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (num_bytes_ok != NULL)
        *num_bytes_ok = it.num_bytes_ok;
    if (rc == 0 && it.ok) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    return map_rw_result((rc == 0) ? 0 : rc, pe, "mcdsrl_read_block");
}

mcdsl_return_et mcdsrl_write_block(uint32_t addr, const void *data,
                                   uint32_t num_bytes, uint32_t *num_bytes_ok)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (data == NULL || num_bytes == 0)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid block parameters");

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = num_bytes;
    it.is_write = 1;
    it.data = (uint8_t *)data;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (num_bytes_ok != NULL)
        *num_bytes_ok = it.num_bytes_ok;
    if (rc == 0 && it.ok) {
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    return map_rw_result((rc == 0) ? 0 : rc, pe, "mcdsrl_write_block");
}

mcdsl_return_et mcdsrl_read_timestamped(uint32_t addr, uint32_t *value,
                                        uint64_t *ts_before,
                                        uint64_t *ts_after)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    mcdsl_return_et r;

    r = rw_check();
    if (r != MCDSL_RET_SUCCESS)
        return r;
    if (value == NULL || ts_before == NULL || ts_after == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid timestamped read parameters");

    /* Reference sequence (captured): three SEPARATE frames, each with a
     * single address map - RD64 @ MCDS timestamp counter (map 15, the
     * server internal view), RD32 @ addr (map 0, target access), RD64 @
     * MCDS timestamp counter again.  A mixed-map single frame is rejected
     * by the server (PL0 0x8D on the map switch). */
    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM15;
    it.addr = MCDSRL_TIMESTAMP_ADDR;
    it.num_bytes = 8;
    it.data = (uint8_t *)ts_before;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc != 0 || !it.ok)
        return map_rw_result((rc == 0) ? 0 : rc, pe,
                             "mcdsrl_read_timestamped (ts_before)");

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.data = (uint8_t *)value;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc != 0 || !it.ok)
        return map_rw_result((rc == 0) ? 0 : rc, pe,
                             "mcdsrl_read_timestamped (value)");

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM15;
    it.addr = MCDSRL_TIMESTAMP_ADDR;
    it.num_bytes = 8;
    it.data = (uint8_t *)ts_after;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc != 0 || !it.ok)
        return map_rw_result((rc == 0) ? 0 : rc, pe,
                             "mcdsrl_read_timestamped (ts_after)");

    err_clear();
    return MCDSL_RET_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Trace recording (M23-2)                                             */
/* ------------------------------------------------------------------ */

/* MCDS register offsets (reference decompile; the TC2xx miniMCDS class):
 *   ctrl_base + 0x004     : the OCS register (must read 0 before start)
 *   ctrl_base + 0x210     : the trace control (stop: write 0x8400)
 *   ctrl_base + 0x100000  : the trace read port (fixed address)
 *   ctrl_base + 0x101030  : the final trace size (32 byte paragraphs)
 *   status_base + 0x200   : the FIFO fill level (progress)
 */
#define MCDSRL_OFF_OCS          0x000004u
#define MCDSRL_OFF_TRACE_CTRL   0x000210u
#define MCDSRL_OFF_TRACE_PORT   0x100000u
#define MCDSRL_OFF_TRACE_SIZE   0x101030u
#define MCDSRL_OFF_FIFO_FILL    0x000200u
#define MCDSRL_TRACE_STOP_VAL   0x8400u
#define MCDSRL_FREQ_REG         0xC0100u
#define MCDSRL_FREQ_DEFAULT     80000000u
#define MCDSRL_FREQ_MIN         2000000u
#define MCDSRL_CPU0_START_A     0xF0000490u
#define MCDSRL_CPU0_START_B     0xFA101010u

/* the long device name table (faithful reconstruction of the reference
 * device_id_to_name_long(); ed = the ED variant flag) */
static const char *device_id_to_name_long(uint32_t device_type, int ed)
{
    uint32_t v = device_type & 0x0FFFFFFFu;

    if (v > 0x219083u) {
        if (v > 0x22F083u) {
            if (v > 0xA00ED83u) {
                if (v == 0xA20DD83u)
                    return "TC3XX_MINIMCDS";
                if (v != 0xA20ED83u)
                    return (v == 0xA30ED83u) ? "TC4XX" : "UNKNOWN";
                return "TC3XX_ED";
            }
            if (v == 0xA00ED83u)
                return "TC2XX_ED";
            if (v != 0x230083u) {
                if (v != 0x231083u)
                    return (v == 0xA00DD83u) ? "TC2XX_MINIMCDS" : "UNKNOWN";
                return "TC4Ex";
            }
            return "TC4Zx";
        }
        if (v > 0x21D083u) {
            if (v == 0x21E083u)
                return "TC49xA";
            if (v != 0x225083u)
                return (v == 0x22B083u) ? "TC49xN" : "UNKNOWN";
            return "TC4Dx";
        }
        if (v == 0x21D083u)
            return "TC48x";
        if (v != 0x21A083u) {
            if (v != 0x21B083u)
                return (v == 0x21C083u) ? "TC46x" : "UNKNOWN";
            return "TC45x";
        }
        return "TC44x";
    }
    if (v == 0x219083u)
        return "TC42x";
    if (v > 0x206083u) {
        if (v > 0x20A083u) {
            if (v == 0x20B083u)
                return "TC33x/32x";
            if (v != 0x20C083u)
                return (v == 0x215083u) ? "TC3Ex" : "UNKNOWN";
            return "TC33xED";
        }
        if (v == 0x20A083u)
            return "TC35x";
        if (v != 0x207083u) {
            if (v != 0x208083u)
                return (v == 0x209083u) ? "TC36x" : "UNKNOWN";
            return "TC37xED";
        }
        return "TC37x";
    }
    if (v == 0x206083u)
        return "TC38x";
    if (v > 0x200083u) {
        if (v == 0x201083u)
            return "TC22x";
        if (v != 0x202083u)
            return (v == 0x205083u) ? "TC39x" : "UNKNOWN";
        return "TC21x";
    }
    if (v == 0x200083u)
        return ed ? "TC23xED" : "TC23x";
    if (v != 0x1DA083u) {
        if (v == 0x1E8083u)
            return ed ? "TC26xED" : "TC26x";
        if (v == 0x1E9083u)
            return ed ? "TC29xED" : "TC29x";
        return "UNKNOWN";
    }
    return ed ? "TC27xED" : "TC27x";
}

/* lean device access helpers (no error string churn) */
static mcdsl_return_et dev_read(uint32_t addr, uint32_t *v)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.data = (uint8_t *)v;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok)
        return MCDSL_RET_SUCCESS;
    if (rc == 0)
        return MCDSL_RET_DEVICE_DATA;
    return (rc == -1 || rc == -2) ? MCDSL_RET_TOOL_HW_CONNECTION
                                  : MCDSL_RET_DEVICE_DATA;
}

static mcdsl_return_et dev_write(uint32_t addr, uint32_t v)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.is_write = 1;
    it.data = (uint8_t *)&v;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok)
        return MCDSL_RET_SUCCESS;
    if (rc == 0)
        return MCDSL_RET_DEVICE_DATA;
    return (rc == -1 || rc == -2) ? MCDSL_RET_TOOL_HW_CONNECTION
                                  : MCDSL_RET_DEVICE_DATA;
}

static mcdsl_return_et dev_read_block(uint32_t addr, uint8_t *buf, uint32_t n)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = n;
    it.data = buf;
    rc = tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    if (rc == 0 && it.ok)
        return MCDSL_RET_SUCCESS;
    if (rc == 0)
        return MCDSL_RET_DEVICE_DATA;
    return (rc == -1 || rc == -2) ? MCDSL_RET_TOOL_HW_CONNECTION
                                  : MCDSL_RET_DEVICE_DATA;
}

static void close_trace_file(void)
{
    if (g.trace_file != NULL) {
        fclose(g.trace_file);
        g.trace_file = NULL;
    }
}

/* the MCDS clock measurement (reference recorder_measure_mcds_freq: read
 * 0xC0100 via the server internal map 15; a value below 2 MHz is replaced
 * by the 80 MHz default). */
static void measure_mcds_freq(void)
{
    tas_rw_item it;
    uint8_t pe = 0;
    uint32_t f = 0;

    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM15;
    it.addr = MCDSRL_FREQ_REG;
    it.num_bytes = 4;
    it.data = (uint8_t *)&f;
    if (tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe) != 0 || !it.ok)
        return;
    if (f < MCDSRL_FREQ_MIN) {
        f = MCDSRL_FREQ_DEFAULT;
        memset(&it, 0, sizeof(it));
        it.addr_map = TAS_AM15;
        it.addr = MCDSRL_FREQ_REG;
        it.num_bytes = 4;
        it.is_write = 1;
        it.data = (uint8_t *)&f;
        tas_rw_exec(&g.conn, &it, 1, g.con.pl0_max_num_rw, &pe);
    }
    g.freq_hz = (float)f;
}

/* the .mcds text header (reference trace_file_write_header) */
static void trace_file_write_header(void)
{
    struct tm tmv;
    uint32_t i;
    int ed = (g.mcdsc.hdr.mcds_device == 0xA00ED83u);

    _localtime64_s(&tmv, &g.rec_time);
    fprintf(g.trace_file, "McdsDevice=%.32s\n",
            device_id_to_name_long(g.mcdsc.hdr.mcds_device, 0));
    fprintf(g.trace_file, "McdsDeviceUsed=0x%8.8X  (%.32s)\n",
            (unsigned)g.mcdsc.hdr.device_type,
            device_id_to_name_long(g.mcdsc.hdr.device_type, ed));
    fprintf(g.trace_file, "McdsToolUsed=\"%.64s\"\n",
            (g.n_targets > 0) ? g.targets[0].identifier : "");
    if (g.freq_hz > 1000000.0f)
        fprintf(g.trace_file, "McdsFrequencyMHz=%.1f\n",
                (double)(g.freq_hz / 1000000.0f));
    fprintf(g.trace_file, "RecordingDate=%4.4d-%2.2d-%2.2d\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    fprintf(g.trace_file, "RecordingTime=%2.2d:%2.2d:%2.2d\n",
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    fprintf(g.trace_file, "START_OF_MCDS_CONFIG\n");
    for (i = 0; i < g.mcdsc.hdr.num_reg; i++)
        fprintf(g.trace_file, "%8.8X %8.8X\n",
                (unsigned)g.mcdsc.d[i],
                (unsigned)g.mcdsc.d[g.mcdsc.hdr.vsi + i]);
    fflush(g.trace_file);
}

mcdsl_return_et mcdsrl_set_mcdsconfig(const mcdsl_mcdsc_st *mcdsc,
                                      const mcdsl_mcdsc_st *mcdsc_cleanup)
{
    uint32_t i, num, vsi;

    if (mcdsc == NULL || mcdsc_cleanup == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "mcdsconfig is NULL");
    num = mcdsc->hdr.num_reg;
    vsi = mcdsc->hdr.vsi;
    if (num > MCDSL_MCDSC_MAX_NUM_REG || vsi > 2 * MCDSL_MCDSC_MAX_NUM_REG ||
        vsi + num > 2 * MCDSL_MCDSC_MAX_NUM_REG)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "mcdsconfig register arrays out of range");

    /* the reference compacts the copy: the values are placed directly
     * after the addresses (vsi := num_reg) */
    memset(&g.mcdsc, 0, sizeof(g.mcdsc));
    g.mcdsc.hdr = mcdsc->hdr;
    for (i = 0; i < num; i++) {
        g.mcdsc.d[i] = mcdsc->d[i];
        g.mcdsc.d[num + i] = mcdsc->d[vsi + i];
    }
    g.mcdsc.hdr.vsi = num;
    memset(&g.mcdsc_cleanup, 0, sizeof(g.mcdsc_cleanup));
    g.mcdsc_cleanup.hdr = mcdsc_cleanup->hdr;
    {
        uint32_t cnum = mcdsc_cleanup->hdr.num_reg;
        uint32_t cvsi = mcdsc_cleanup->hdr.vsi;

        for (i = 0; i < cnum; i++) {
            g.mcdsc_cleanup.d[i] = mcdsc_cleanup->d[i];
            g.mcdsc_cleanup.d[cnum + i] = mcdsc_cleanup->d[cvsi + i];
        }
        g.mcdsc_cleanup.hdr.vsi = cnum;
    }
    g.cleanup_set = 1;

    /* derive the MCDS parameters (reference FUN_180005c40, class 1 path:
     * the register windows and the FIFO base/top from the config) */
    g.fifo_base = 0;
    g.fifo_top = 0;
    g.trace_max_kb = 0;
    g.mcds_present = 0;
    for (i = 0; i < num; i++) {
        uint32_t a = g.mcdsc.d[i];
        uint32_t v = g.mcdsc.d[num + i];

        if ((a & 0xFFFFFF00u) == g.mcds_ctrl_base) {
            if ((a & 0xFFu) == 0x20u && v != 0)
                g.mcds_present = 1;         /* the MCDS-ID marker */
        } else if ((a & 0xFFFF8000u) == g.mcds_status_base) {
            uint32_t off = a & 0x7FFFu;

            if (off == 0x204u)
                g.fifo_base = v & 0xFFFFF000u;
            else if (off == 0x208u)
                g.fifo_top = v;
        }
    }
    if (g.fifo_top > g.fifo_base)
        g.trace_max_kb = ((g.fifo_top - g.fifo_base) + 1) >> 10;
    if (!g.mcds_present && g.trace_max_kb == 0)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "No EMEM configuration found in MCDS configuration");
    g.cfg_set = 1;
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_set_trace_buf(uint8_t *trace_buf,
                                     uint32_t max_trace_size_kb)
{
    g.trace_buf = trace_buf;
    g.trace_buf_kb = max_trace_size_kb;
    if (g.trace_file != NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Open trace file exists");
    if (max_trace_size_kb < 4)
        return err_set(MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED,
                       "Minimum buffer size is 4 kByte");
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_open_trace_file(const mcd_char_t *trace_file_name)
{
    if (trace_file_name == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "trace_file_name is NULL");
    g.trace_buf = NULL;
    g.trace_buf_kb = 0;
    close_trace_file();
    g.trace_file = fopen(trace_file_name, "wb");
    if (g.trace_file == NULL)
        return err_set(MCDSL_RET_ERROR_TRACE_FILE,
                       "Could not open trace file");
    strncpy(g.trace_file_name, trace_file_name,
            sizeof(g.trace_file_name) - 1);
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_start_recording(const mcdsrl_trace_state_st *stop_at)
{
    uint32_t n, v = 0;
    uint16_t nok = 0;
    mcdsl_return_et r;

    g.trace_size_kb = 0;
    g.elapsed_ms = 0;
    if (stop_at != NULL) {
        g.stop_size_kb = stop_at->trace_size_kb;
        g.stop_time_ms = stop_at->recording_time_ms;
        g.stop_gaps = stop_at->num_trace_gaps;
    } else {
        g.stop_size_kb = 0;
        g.stop_time_ms = 0;
        g.stop_gaps = 0;
    }

    if (!g.device_connected)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    if (!g.cfg_set)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Set mcdsconfig first");
    if (g.need_stop)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "Call mcdsrl_stop_recording() before calling "
                       "mcdsrl_start_recording again");

    /* the HAR variant: the last config pair must start CPU0 */
    if (g.mcdsc.hdr.crsv == 2 && g.mcdsc.hdr.num_reg > 0) {
        uint32_t lasta = g.mcdsc.d[g.mcdsc.hdr.num_reg - 1];

        if (lasta != MCDSRL_CPU0_START_A && lasta != MCDSRL_CPU0_START_B)
            return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                           "Reset requested but last write in MCDS config "
                           "does not start CPU0");
        /* (the reference also verifies the HAR device state here;
         * implemented as pass-through for now - calibrate in M23-3) */
    }

    /* the OCS check: the MCDS must be in the clean state */
    r = dev_read(g.mcds_ctrl_base + MCDSRL_OFF_OCS, &v);
    if (r == MCDSL_RET_SUCCESS && v != 0)
        return err_set(MCDSL_RET_DEVICE_DATA,
                       "MCDS init state is not as expected (OCS register). "
                       "Possible causes:\n1. The last session terminated "
                       "without calling mcdsrl_stop_recording()\nThis can be "
                       "resolved by a device reset.\n2. Another tool or SW "
                       "configured MCDS");

    /* the PD check (class > 2 devices only - not the TC2xx miniMCDS) */
    if (g.device_class > 2 &&
        g.con_connected.device_type != 0x21E083u) {
        r = dev_read(g.mcds_status_base + 0x101014u, &v);
        if (r == MCDSL_RET_SUCCESS && (v & 0x30000000u) == 0x30000000u)
            return err_set(MCDSL_RET_DEVICE_DATA,
                           "The device is a PD which supports only one "
                           "trace session after reset.\nPlease apply a "
                           "reset, before starting a new trace");
    }

    /* the non-continuous stop limits must be 0 */
    if (g.stop_size_kb != 0)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "trace_state_stop_at.trace_size_kb must be 0 if "
                       "trace mode is not continuous");
    if (g.stop_gaps != 0)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "trace_state_stop_at.num_trace_gaps must be 0 if "
                       "trace mode is not continuous");

    measure_mcds_freq();

    /* the 0x310/0x30F special sequence (the reference; the TC2xx ED
     * devices need the auxiliary clock ungate around the CPU start) */
    if (((g.device_id_short - 0x310u) & 0xFEFFu) == 0) {
        dev_write(0xFA006000u, 0);
        v = 0xEEEEEEEEu;
        if (dev_read(0xFA006000u, &v) == MCDSL_RET_SUCCESS && v != 0) {
            dev_write(0xF000047Cu, 0xC0u);
            dev_write(0xFA006000u, 0);
            dev_write(0xF000047Cu, 0x40u);
        }
    }

    /* the trace file header */
    if (g.trace_file != NULL) {
        _time64(&g.rec_time);
        trace_file_write_header();
    }

    /* the config write: for the HAR variant the last pair is excluded and
     * written separately (it starts the CPU0 / the trace) */
    n = g.mcdsc.hdr.num_reg;
    if (g.mcdsc.hdr.crsv == 2)
        n -= 1;
    if (n > 0) {
        r = mcdsrl_write_list(g.mcdsc.d, &g.mcdsc.d[g.mcdsc.hdr.vsi],
                              (uint16_t)n, &nok);
        if (r != MCDSL_RET_SUCCESS)
            return r;
    }
    if (g.mcdsc.hdr.crsv == 2 && g.mcdsc.hdr.num_reg > 0) {
        uint32_t last = g.mcdsc.hdr.num_reg - 1;

        r = mcdsrl_write(g.mcdsc.d[last],
                         g.mcdsc.d[g.mcdsc.hdr.vsi + last]);
        if (r != MCDSL_RET_SUCCESS)
            return r;
    }

    g.started = 1;
    g.need_stop = 1;
    g.stop_done = 0;
    g.start_ms = GetTickCount64();
    err_clear();
    return MCDSL_RET_SUCCESS;
}

/* the trace readback (reference FUN_1800034c0): the final size from the
 * trace size register, the readback from the fixed trace port address in
 * 1 KB blocks, then the buffer or the "START_OF_TDR" + raw into the file */
static mcdsl_return_et trace_readback(void)
{
    uint32_t v = 0, size_kb, read_kb, i, nread = 0, cap_kb = 0;
    uint8_t *buf = NULL;
    int own = 0;
    mcdsl_return_et r;

    if (g.trace_file == NULL && g.trace_buf == NULL)
        return MCDSL_RET_SUCCESS;

    r = dev_read(g.mcds_ctrl_base + MCDSRL_OFF_TRACE_SIZE, &v);
    if (r == MCDSL_RET_SUCCESS)
        size_kb = (((v & 0xFFFFu) * 0x20u) + 0x4FFu) >> 10;
    else
        size_kb = g.trace_size_kb;

    if (g.trace_file != NULL) {
        cap_kb = (g.trace_max_kb != 0) ? g.trace_max_kb : size_kb;
        if (cap_kb == 0)
            cap_kb = 4;
        buf = (uint8_t *)malloc((size_t)cap_kb << 10);
        if (buf == NULL)
            return err_set(MCDSL_RET_UNKNOWN_ERROR, "Out of memory");
        own = 1;
    } else {
        cap_kb = g.trace_buf_kb;
        buf = g.trace_buf;
    }

    /* the read size: the trace size plus the 1/32 block overhead */
    read_kb = ((size_kb + 0x1Fu) >> 5) + size_kb;
    if (read_kb > cap_kb)
        read_kb = cap_kb;
    if (read_kb == 0) {
        if (own)
            free(buf);
        close_trace_file();
        return err_set(MCDSL_RET_DEVICE_DATA, "No trace data available");
    }

    for (i = 0; i < read_kb; i++) {
        r = dev_read_block(g.mcds_ctrl_base + MCDSRL_OFF_TRACE_PORT,
                           buf + ((size_t)i << 10), 0x400u);
        if (r != MCDSL_RET_SUCCESS) {
            if (own)
                free(buf);
            close_trace_file();
            return r;
        }
        nread += 0x400u;
    }
    g.trace_size_kb = (nread + 0x3FFu) >> 10;

    if (g.trace_file != NULL) {
        fprintf(g.trace_file, "START_OF_TDR\n");
        fwrite(buf, 1, nread, g.trace_file);
        close_trace_file();
    }
    if (own)
        free(buf);
    return MCDSL_RET_SUCCESS;
}

/* the shared state machine (reference recorder_get_recstate_impl) */
static mcdsl_return_et recstate_impl(mcdsrl_recstate_et *recstate,
                                     mcdsrl_trace_state_st *trace_state,
                                     int stop_flag)
{
    uint32_t v = 0;
    mcdsl_return_et r;

    *recstate = MCDSRL_RECSTATE_UNKNOWN;
    if (!g.device_connected) {
        if (trace_state != NULL) {
            trace_state->trace_size_kb = 0;
            trace_state->recording_time_ms = 0;
            trace_state->num_trace_gaps = 0;
        }
        close_trace_file();
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    }
    if (trace_state != NULL) {
        trace_state->trace_size_kb = g.trace_size_kb;
        trace_state->recording_time_ms = g.elapsed_ms;
        trace_state->num_trace_gaps = 0;
    }
    if (!g.started) {
        *recstate = MCDSRL_RECSTATE_IDLE;
        close_trace_file();
        err_clear();
        return MCDSL_RET_SUCCESS;
    }

    g.elapsed_ms = (uint32_t)(GetTickCount64() - g.start_ms);

    /* the stop: the explicit flag or the recording time limit */
    if (stop_flag || (g.stop_time_ms != 0 && g.elapsed_ms >= g.stop_time_ms)) {
        r = dev_write(g.mcds_ctrl_base + MCDSRL_OFF_TRACE_CTRL,
                      MCDSRL_TRACE_STOP_VAL);
        if (r != MCDSL_RET_SUCCESS) {
            close_trace_file();
            return r;
        }
    }

    /* the progress (the FIFO fill level) */
    if (dev_read(g.mcds_status_base + MCDSRL_OFF_FIFO_FILL, &v) ==
            MCDSL_RET_SUCCESS &&
        v >= g.fifo_base)
        g.trace_size_kb = ((v - g.fifo_base) + 0x3FFu) >> 10;

    if (stop_flag || (g.stop_time_ms != 0 && g.elapsed_ms >= g.stop_time_ms)) {
        /* the stop completed: read the trace back and end the recording */
        g.started = 0;
        r = trace_readback();
        *recstate = MCDSRL_RECSTATE_IDLE;
        if (trace_state != NULL) {
            trace_state->trace_size_kb = g.trace_size_kb;
            trace_state->recording_time_ms = g.elapsed_ms;
            trace_state->num_trace_gaps = 0;
        }
        if (r != MCDSL_RET_SUCCESS)
            return r;
        err_clear();
        return MCDSL_RET_SUCCESS;
    }

    /* the running state (the simplified trigger decode; the reference
     * derives it from the MCDS status register) */
    *recstate = (g.trace_size_kb > 0) ? MCDSRL_RECSTATE_POST_TRIG
                                      : MCDSRL_RECSTATE_PRE_TRIG;
    if (trace_state != NULL) {
        trace_state->trace_size_kb = g.trace_size_kb;
        trace_state->recording_time_ms = g.elapsed_ms;
        trace_state->num_trace_gaps = 0;
    }
    err_clear();
    return MCDSL_RET_SUCCESS;
}

mcdsl_return_et mcdsrl_get_recstate(mcdsrl_recstate_et *recstate,
                                    mcdsrl_trace_state_st *trace_state)
{
    if (recstate == NULL || trace_state == NULL)
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "invalid recstate parameters");
    return recstate_impl(recstate, trace_state, 0);
}

mcdsl_return_et mcdsrl_get_data_from_fifo(uint8_t *trace_buf,
                                          uint32_t trace_size_kb_max,
                                          uint32_t *trace_size_kb)
{
    (void)trace_buf;
    (void)trace_size_kb_max;
    if (trace_size_kb != NULL)
        *trace_size_kb = 0;
    /* continuous streaming trace only; the miniMCDS does not provide it */
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_stop_recording(mcdsrl_trace_state_st *trace_state)
{
    mcdsrl_recstate_et rs = MCDSRL_RECSTATE_UNKNOWN;
    uint16_t nok = 0;
    mcdsl_return_et r;

    if (!g.device_connected) {
        if (trace_state != NULL) {
            trace_state->trace_size_kb = 0;
            trace_state->recording_time_ms = 0;
            trace_state->num_trace_gaps = 0;
        }
        close_trace_file();
        return err_set(MCDSL_RET_FUNCTION_USED_WRONGLY,
                       "No device connected");
    }
    if (!g.stop_done) {
        /* the first call does the work (the reference g_stop_called) */
        g.stop_done = 1;
        g.need_stop = 0;
        r = recstate_impl(&rs, trace_state, 1);
        if (r != MCDSL_RET_SUCCESS)
            return r;
        /* the cleanup config sequence */
        if (g.cleanup_set && g.mcdsc_cleanup.hdr.num_reg > 0) {
            r = mcdsrl_write_list(g.mcdsc_cleanup.d,
                                  &g.mcdsc_cleanup.d[g.mcdsc_cleanup.hdr.vsi],
                                  (uint16_t)g.mcdsc_cleanup.hdr.num_reg,
                                  &nok);
            if (r != MCDSL_RET_SUCCESS)
                return r;
        }
        close_trace_file();
        err_clear();
        return MCDSL_RET_SUCCESS;
    }
    /* the repeated call returns the cached state */
    if (trace_state != NULL) {
        trace_state->trace_size_kb = g.trace_size_kb;
        trace_state->recording_time_ms = g.elapsed_ms;
        trace_state->num_trace_gaps = 0;
    }
    err_clear();
    return MCDSL_RET_SUCCESS;
}
