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
    }
    cls = device_class(g.con_connected.device_type);
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
/* Trace recording (M23-2 stubs)                                       */
/* ------------------------------------------------------------------ */

mcdsl_return_et mcdsrl_set_mcdsconfig(const mcdsl_mcdsc_st *mcdsc,
                                      const mcdsl_mcdsc_st *mcdsc_cleanup)
{
    (void)mcdsc;
    (void)mcdsc_cleanup;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_set_trace_buf(uint8_t *trace_buf,
                                     uint32_t max_trace_size_kb)
{
    (void)trace_buf;
    (void)max_trace_size_kb;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_open_trace_file(const mcd_char_t *trace_file_name)
{
    (void)trace_file_name;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_start_recording(const mcdsrl_trace_state_st *stop_at)
{
    (void)stop_at;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_get_recstate(mcdsrl_recstate_et *recstate,
                                    mcdsrl_trace_state_st *trace_state)
{
    (void)recstate;
    (void)trace_state;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_get_data_from_fifo(uint8_t *trace_buf,
                                          uint32_t trace_size_kb_max,
                                          uint32_t *trace_size_kb)
{
    (void)trace_buf;
    (void)trace_size_kb_max;
    (void)trace_size_kb;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}

mcdsl_return_et mcdsrl_stop_recording(mcdsrl_trace_state_st *trace_state)
{
    (void)trace_state;
    return err_set(MCDSL_RET_FUNCTION_NOT_SUPPORTED,
                   "Function is not supported by library");
}
