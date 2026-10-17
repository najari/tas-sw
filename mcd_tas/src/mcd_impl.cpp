/* mcd_impl.c - MCD API v1.1 implementation over the TAS wire protocol.
 *
 * mcd_tas prototype, milestone M1 (2026-10-17):
 *   implemented: initialize / exit / query+open+close server (TAS
 *                SERVER_CONNECT) / query systems, devices, cores
 *                (TAS GET_TARGETS) / error info / server config query.
 *   stubbed    : everything else (target access, transactions, triggers,
 *                channels, trace, reset ...) returning
 *                MCD_ERR_FN_UNIMPLEMENTED with a descriptive message.
 *
 * Design notes:
 *   - The DLL is the "MCD API over TAS" pendant of mcdxdas.dll: tools
 *     (MCD clients) load it and get an MCD v1.1 implementation whose
 *     backend is a tas_server listetning on TCP 24817.
 *   - Only read-only operations are performed in M1.  No target state
 *     is changed.
 *   - All public entry points are serialized with one critical section.
 *
 * SPDX-License-Identifier: MIT (project code only; mcd_api.h/mcd_types.h
 * remain under the SPRINT license, see include/).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "mcd_api.h"
#include "tas_client.h"

#define MCD_TAS_IMPL_VER_MAJOR  0
#define MCD_TAS_IMPL_VER_MINOR  2
#define MCD_TAS_IMPL_VER_BUILD  0
#define MCD_TAS_IMPL_VENDOR     "mcd_tas prototype"
#define MCD_TAS_CLIENT_NAME     "mcd_tas (MCD-TAS prototype)"
#define MCD_TAS_PROBE_NAME      "mcd_tas probe"

#define MCD_TAS_MEM_SPACE_ID    1u      /* single AM0 memory space      */
#define MCD_TAS_REG_GROUP_CPU   1u      /* M4 calibration register group */

/* CPU0 CSFR window (0xF8810000-0xF881FFFF, TC27x UM Table 3-3) register
 * offsets: PC @FE08 (Table 5-19), DBGSR @FD00 (Table 5-24 / TC1.6 UM
 * Vol.1 sec.12.9). */
#define MCD_TAS_CSFR_CPU0_ADDR  0xF8810000ull
#define MCD_TAS_DBGSR_ADDR      (MCD_TAS_CSFR_CPU0_ADDR + 0xFD00ull)
#define MCD_MAX_CORE_SLOTS      4
#define MCD_TAS_MAX_PAYLOAD     0x10000u

/* ------------------------------------------------------------------ */
/* context                                                             */
/* ------------------------------------------------------------------ */

/* One opened core: owns its own TAS TCP connection and session. */
typedef struct {
    int                  used;
    int                  target;        /* index into g.targets          */
    int                  core_index;    /* core within the device        */
    tas_conn             conn;
    int                  attached;      /* DEVICE_CONNECT succeeded      */
    tas_con_info         con;           /* SESSION_START/PING snapshot   */
    char                 no_device_note[64]; /* diag text for errors     */
    /* qry_state: DBGSR (Debug Status Register) snapshot - read once per
     * attach (read-only), then cached for the session. */
    int                  st_valid;           /* 1 = st_dbgsr/st_state set */
    uint32_t             st_dbgsr;           /* raw DBGSR value           */
    mcd_core_state_et    st_state;           /* state derived from DBGSR  */
    mcd_core_st          core_obj;
    mcd_core_con_info_st core_ci;
} mcd_core_slot_t;

typedef struct {
    CRITICAL_SECTION  lock;

    int               initialized;
    tas_conn          conn;
    int               server_open;

    char              host[MCD_HOSTNAME_LEN];
    uint32_t          port;
    tas_server_info   srv;
    int               n_targets;
    tas_target        targets[TAS_MAX_TARGETS];

    mcd_server_st     server_obj;
    char              server_host[MCD_HOSTNAME_LEN];
    char              server_cfg[256];

    mcd_core_slot_t   cores[MCD_MAX_CORE_SLOTS];

    mcd_error_info_st last_err;
} mcd_ctx_t;

static mcd_ctx_t  g;
static INIT_ONCE  g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK ctx_init(PINIT_ONCE once, PVOID p, PVOID *c)
{
    int k;
    (void)once; (void)p; (void)c;
    InitializeCriticalSection(&g.lock);
    memset(&g.conn, 0, sizeof(g.conn));
    g.conn.sock = INVALID_SOCKET;
    for (k = 0; k < MCD_MAX_CORE_SLOTS; k++)
        g.cores[k].conn.sock = INVALID_SOCKET;
    g.last_err.error_code = MCD_ERR_NONE;
    return TRUE;
}

static void mcd_lock(void)
{
    InitOnceExecuteOnce(&g_once, ctx_init, NULL, NULL);
    EnterCriticalSection(&g.lock);
}
static void mcd_unlock(void)
{
    LeaveCriticalSection(&g.lock);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void dbg(const char *fmt, ...);

static mcd_return_et err_set(mcd_return_et ret, mcd_error_code_et code,
                             const char *fmt, ...)
{
    va_list ap;
    g.last_err.return_status = ret;
    g.last_err.error_code = code;
    g.last_err.error_events = MCD_ERR_EVT_NONE;
    va_start(ap, fmt);
    vsnprintf(g.last_err.error_str, sizeof(g.last_err.error_str), fmt, ap);
    va_end(ap);
    dbg("mcd_tas: FAIL: %s", g.last_err.error_str);
    return ret;
}

static void err_clear(void)
{
    g.last_err.return_status = MCD_RET_ACT_NONE;
    g.last_err.error_code = MCD_ERR_NONE;
    g.last_err.error_events = MCD_ERR_EVT_NONE;
    g.last_err.error_str[0] = 0;
}

static void dbg(const char *fmt, ...)
{
    va_list ap;
    if (getenv("MCD_TAS_LOG") == NULL)
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static const char *mcd_user(void)
{
    static char u[16];
    static int done = 0;
    if (!done) {
        const char *e = getenv("USERNAME");
        strncpy(u, (e && e[0]) ? e : "user", 15);
        u[15] = 0;
        done = 1;
    }
    return u;
}

/* Copy a C string with guaranteed termination. */
static void copy_str(char *dst, size_t dstlen, const char *src)
{
    if (dstlen == 0)
        return;
    if (src == NULL)
        src = "";
    strncpy(dst, src, dstlen - 1);
    dst[dstlen - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* provisional device knowledge                                        */
/* ------------------------------------------------------------------ */
/*
 * device_type is the IEEE 1149.1 idcode of the device.  EM_TRICORE (ELF
 * predefined architecture) == 44.  The values marked "prov." were taken
 * from the UI of the real stack (Core Info dialog of the MCD Basic
 * Client for this exact target); they need re-calibration against the
 * original mcdxdas.dll in a later milestone.
 */
typedef struct {
    uint32_t    device_type;
    const char *device_name;
    int         n_cores;
    const char *core_name_fmt;   /* two %d: core index, CPU index      */
    uint32_t    core_type;
    uint32_t    core_id;         /* prov.                              */
} mcd_devdb_entry_t;

static const mcd_devdb_entry_t g_devdb[] = {
    { 0x401DA083u /* prov. */, "TC27x" /* prov. */, 3,
      "Core %d: CPU%d TriCore 1.6E" /* prov. */, 44u, 0x00B7C002u },
};
#define MCD_DEVDB_COUNT (sizeof(g_devdb) / sizeof(g_devdb[0]))

static const mcd_devdb_entry_t *devdb_lookup(uint32_t device_type)
{
    size_t i;
    for (i = 0; i < MCD_DEVDB_COUNT; i++)
        if (g_devdb[i].device_type == device_type)
            return &g_devdb[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* config string parsing ("McdHostName=... McdServerPort=...")         */
/* ------------------------------------------------------------------ */

static int cfg_get(const char *cfg, const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = cfg;
    if (cfg == NULL || outlen == 0)
        return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (_strnicmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t n = 0;
            if (*v == '"') {
                v++;
                while (v[n] && v[n] != '"' && n < outlen - 1) {
                    out[n] = v[n];
                    n++;
                }
            } else {
                while (v[n] && v[n] != ' ' && v[n] != '\t' &&
                       v[n] != '\r' && v[n] != '\n' && n < outlen - 1) {
                    out[n] = v[n];
                    n++;
                }
            }
            out[n] = 0;
            return 1;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
    }
    return 0;
}

static void parse_server_config(const char *cfg, char *host, size_t hostlen,
                                uint16_t *port)
{
    char v[128];
    copy_str(host, hostlen, "localhost");
    *port = TAS_DEFAULT_PORT;

    if (cfg_get(cfg, "McdHostName", v, sizeof(v))) {
        copy_str(host, hostlen, v);
    } else if (cfg != NULL && cfg[0] != 0 && strchr(cfg, '=') == NULL) {
        /* convenience form: "host" or "host:port" */
        const char *colon = strchr(cfg, ':');
        if (colon != NULL) {
            size_t n = (size_t)(colon - cfg);
            if (n > hostlen - 1)
                n = hostlen - 1;
            memcpy(host, cfg, n);
            host[n] = 0;
            *port = (uint16_t)atoi(colon + 1);
        } else {
            copy_str(host, hostlen, cfg);
        }
        if (*port == 0)
            *port = TAS_DEFAULT_PORT;
    }
    if (cfg_get(cfg, "McdServerPort", v, sizeof(v))) {
        int pv = atoi(v);
        if (pv > 0 && pv < 65536)
            *port = (uint16_t)pv;
    }
}

/* ------------------------------------------------------------------ */
/* TAS helpers                                                         */
/* ------------------------------------------------------------------ */

/* One-shot handshake probe; returns 0 when a TAS server answered. */
static int probe_tas_server(const char *host, uint16_t port,
                            tas_server_info *si, char *acc_hw, size_t acclen,
                            char *detail, size_t detaillen)
{
    tas_conn c;
    uint8_t e = 0;
    tas_server_info tmp;
    tas_target t;
    int n = 0;

    if (tas_connect(&c, host, port, detail, detaillen) != 0)
        return -1;
    if (tas_server_connect(&c, MCD_TAS_PROBE_NAME, mcd_user(),
                           GetCurrentProcessId(), &tmp, &e) != 0 ||
        e != TAS_ERR_OK) {
        if (detail)
            snprintf(detail, detaillen, "SERVER_CONNECT rejected (tas_err=0x%02x)", e);
        tas_disconnect(&c);
        return -1;
    }
    if (si != NULL)
        *si = tmp;
    if (acc_hw != NULL && acclen > 0) {
        acc_hw[0] = 0;
        if (tas_get_targets(&c, &t, 1, &n, &e) == 0 && e == TAS_ERR_OK && n > 0)
            copy_str(acc_hw, acclen, t.identifier);
    }
    tas_disconnect(&c);
    return 0;
}

static void refresh_targets(void)
{
    uint8_t e = 0;
    int n = 0;
    if (tas_get_targets(&g.conn, g.targets, TAS_MAX_TARGETS, &n, &e) == 0 &&
        e == TAS_ERR_OK)
        g.n_targets = n;
    else
        g.n_targets = 0;
}

static void fill_device_con_info(mcd_core_con_info_st *o, const tas_target *t)
{
    const mcd_devdb_entry_t *db = devdb_lookup(t->device_type);
    memset(o, 0, sizeof(*o));
    copy_str(o->host, sizeof(o->host), g.host);
    o->server_port = g.port;
    copy_str(o->system, sizeof(o->system), "Real HW");
    copy_str(o->acc_hw, sizeof(o->acc_hw), t->identifier);
    o->device_type = t->device_type;
    copy_str(o->device, sizeof(o->device), db ? db->device_name : "Unknown device");
    o->device_id = t->device_id[0];   /* mapping of the 16B id is provisional */
}

/* ------------------------------------------------------------------ */
/* implemented API (M1)                                                */
/* ------------------------------------------------------------------ */

/* M2 session helpers are defined further below; forward declaration */
static void close_tas_session(mcd_core_slot_t *cs);

mcd_return_et mcd_initialize_f(const mcd_api_version_st *version_req,
                               mcd_impl_version_info_st *impl_info)
{
    mcd_return_et r;

    mcd_lock();
    err_clear();

    if (version_req == NULL || impl_info == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_initialize_f: NULL argument");
    } else if (version_req->v_api_major != MCD_API_VER_MAJOR) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_initialize_f: unsupported API major version %u.%u",
                    (unsigned)version_req->v_api_major, (unsigned)version_req->v_api_minor);
    } else if (version_req->v_api_minor > MCD_API_VER_MINOR) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_initialize_f: client requests newer minor version %u.%u",
                    (unsigned)version_req->v_api_major, (unsigned)version_req->v_api_minor);
    } else if (strcmp(version_req->author, "SPRINT Release") != 0 &&
               strcmp(version_req->author, "Lauterbach Release") != 0) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_initialize_f: unexpected API author '%s'",
                    version_req->author);
    } else {
        memset(impl_info, 0, sizeof(*impl_info));
        impl_info->v_api.v_api_major = MCD_API_VER_MAJOR;
        impl_info->v_api.v_api_minor = MCD_API_VER_MINOR;
        copy_str(impl_info->v_api.author, sizeof(impl_info->v_api.author),
                 "SPRINT Release");
        impl_info->v_imp_major = MCD_TAS_IMPL_VER_MAJOR;
        impl_info->v_imp_minor = MCD_TAS_IMPL_VER_MINOR;
        impl_info->v_imp_build = MCD_TAS_IMPL_VER_BUILD;
        copy_str(impl_info->vendor, sizeof(impl_info->vendor), MCD_TAS_IMPL_VENDOR);
        copy_str(impl_info->date, sizeof(impl_info->date), __DATE__);
        g.initialized = 1;
        dbg("mcd_tas: initialized (client requested %u.%u '%s')",
            (unsigned)version_req->v_api_major, (unsigned)version_req->v_api_minor,
            version_req->author);
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

void mcd_exit_f(void)
{
    int k;
    mcd_lock();
    err_clear();
    for (k = 0; k < MCD_MAX_CORE_SLOTS; k++) {
        if (g.cores[k].used)
            close_tas_session(&g.cores[k]);
    }
    if (g.server_open) {
        tas_disconnect(&g.conn);
        g.server_open = 0;
        g.n_targets = 0;
        dbg("mcd_tas: exit, server closed");
    }
    g.initialized = 0;
    mcd_unlock();
}

mcd_return_et mcd_qry_servers_f(const mcd_char_t *host, mcd_bool_t running,
                                uint32_t start_index, uint32_t *num_servers,
                                mcd_server_info_st *server_info)
{
    const char *h;
    uint32_t want;
    mcd_return_et r = MCD_RET_ACT_NONE;
    char acc[64];
    char detail[160];
    tas_server_info si;

    mcd_lock();
    err_clear();

    if (num_servers == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_servers_f: NULL num_servers");
        mcd_unlock();
        return r;
    }
    h = (host != NULL && host[0] != 0) ? host : "localhost";
    want = *num_servers;

    if (!running) {
        /* "installed servers" list is not provided by this prototype */
        *num_servers = 0;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }

    detail[0] = 0;
    if (probe_tas_server(h, TAS_DEFAULT_PORT, &si, acc, sizeof(acc),
                         detail, sizeof(detail)) != 0) {
        *num_servers = 0;   /* empty list is not an error */
        dbg("mcd_tas: qry_servers(%s): no TAS server (%s)", h, detail);
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }

    if (want == 0) {
        *num_servers = 1;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    if (start_index >= 1 || server_info == NULL) {
        *num_servers = 0;
        if (server_info == NULL && start_index == 0)
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_servers_f: NULL server_info");
        mcd_unlock();
        return r;
    }

    memset(&server_info[0], 0, sizeof(server_info[0]));
    copy_str(server_info[0].server, sizeof(server_info[0].server),
             si.server[0] ? si.server : "TasServer");
    /* real hardware server: acc_hw carries the access hardware id,
       system_instance stays empty (per MCD API convention) */
    copy_str(server_info[0].acc_hw, sizeof(server_info[0].acc_hw), acc);
    *num_servers = 1;
    dbg("mcd_tas: qry_servers(%s): 1 server '%s' acc_hw '%s'",
        h, server_info[0].server, server_info[0].acc_hw);
    mcd_unlock();
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_open_server_f(const mcd_char_t *system_key,
                                const mcd_char_t *config_string,
                                mcd_server_st **server)
{
    char host[MCD_HOSTNAME_LEN];
    uint16_t port;
    char detail[160];
    uint8_t e = 0;
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)system_key;   /* no server keys in this prototype */

    mcd_lock();
    err_clear();

    if (server == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_open_server_f: NULL server");
        mcd_unlock();
        return r;
    }
    *server = NULL;
    if (!g.initialized) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_USAGE,
                    "mcd_open_server_f: mcd_initialize_f not called");
        mcd_unlock();
        return r;
    }
    /* tolerate repeated opens of the same endpoint: the MCD client polls
       open/qry/close in a loop and later re-opens for the real session */
    parse_server_config(config_string, host, sizeof(host), &port);
    if (g.server_open) {
        if (strcmp(host, g.host) == 0 && port == g.port) {
            *server = &g.server_obj;   /* same endpoint: reuse handle */
            mcd_unlock();
            return MCD_RET_ACT_NONE;
        }
        tas_disconnect(&g.conn);   /* different endpoint: switch over */
        g.server_open = 0;
    }
    detail[0] = 0;

    if (tas_connect(&g.conn, host, port, detail, sizeof(detail)) != 0) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                    "mcd_open_server_f: %s", detail);
        mcd_unlock();
        return r;
    }
    if (tas_server_connect(&g.conn, MCD_TAS_CLIENT_NAME, mcd_user(),
                           GetCurrentProcessId(), &g.srv, &e) != 0 ||
        e != TAS_ERR_OK) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                    "mcd_open_server_f: TAS SERVER_CONNECT rejected "
                    "(tas_err=0x%02x)", e);
        tas_disconnect(&g.conn);
        mcd_unlock();
        return r;
    }

    copy_str(g.host, sizeof(g.host), host);
    g.port = port;
    refresh_targets();
    copy_str(g.server_host, sizeof(g.server_host), host);
    snprintf(g.server_cfg, sizeof(g.server_cfg),
             "McdHostName=%s McdServerPort=%u", host, (unsigned)port);
    memset(&g.server_obj, 0, sizeof(g.server_obj));
    g.server_obj.instance = &g;
    g.server_obj.host = g.server_host;
    g.server_obj.config_string = g.server_cfg;
    g.server_open = 1;
    *server = &g.server_obj;

    dbg("mcd_tas: server '%s' opened at %s:%u (%d targets)",
        g.srv.server, host, (unsigned)port, g.n_targets);
    mcd_unlock();
    return r;
}

mcd_return_et mcd_close_server_f(const mcd_server_st *server)
{
    mcd_return_et r;

    mcd_lock();
    err_clear();

    if (server == NULL || server != &g.server_obj) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_close_server_f: unknown server instance");
    } else if (!g.server_open) {
        r = MCD_RET_ACT_NONE;   /* idempotent close (reference behaviour) */
    } else {
        /* Only the enumeration connection is closed here.  Attached core
           sessions use their own connections and stay open: the MCD client
           closes the server between its periodic poll cycles. */
        tas_disconnect(&g.conn);
        g.server_open = 0;
        g.n_targets = 0;
        dbg("mcd_tas: server closed");
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_set_server_config_f(const mcd_server_st *server,
                                      const mcd_char_t *config_string)
{
    mcd_return_et r;

    (void)config_string;

    mcd_lock();
    err_clear();
    if (server == NULL || server != &g.server_obj || !g.server_open) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_set_server_config_f: unknown server instance");
    } else {
        /* prototype: configuration changes are accepted and ignored
           (host port and access parameters are fixed for now) */
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_server_config_f(const mcd_server_st *server,
                                      uint32_t *max_len,
                                      mcd_char_t *config_string)
{
    mcd_return_et r;
    size_t need;

    mcd_lock();
    err_clear();

    if (server == NULL || server != &g.server_obj || !g.server_open) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_server_config_f: unknown server instance");
        mcd_unlock();
        return r;
    }
    if (max_len == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_server_config_f: NULL max_len");
        mcd_unlock();
        return r;
    }

    need = strlen(g.server_cfg) + 1;
    if (config_string == NULL) {
        *max_len = (uint32_t)need;   /* just report the required size */
        r = MCD_RET_ACT_NONE;
    } else if (*max_len < need) {
        uint32_t given = *max_len;
        *max_len = (uint32_t)need;
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_RESULT_TOO_LONG,
                    "mcd_qry_server_config_f: buffer too small (%u < %u)",
                    (unsigned)given, (unsigned)need);
    } else {
        copy_str(config_string, (size_t)*max_len, g.server_cfg);
        *max_len = (uint32_t)need;
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_systems_f(uint32_t start_index, uint32_t *num_systems,
                                mcd_core_con_info_st *system_con_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (num_systems == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_systems_f: NULL num_systems");
        mcd_unlock();
        return r;
    }
    if (!g.server_open) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_qry_systems_f: no server opened");
        mcd_unlock();
        return r;
    }
    if (*num_systems == 0) {
        *num_systems = 1;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    if (start_index >= 1 || system_con_info == NULL) {
        *num_systems = 0;
        if (system_con_info == NULL && start_index == 0)
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_systems_f: NULL system_con_info");
        mcd_unlock();
        return r;
    }

    memset(&system_con_info[0], 0, sizeof(system_con_info[0]));
    copy_str(system_con_info[0].host, sizeof(system_con_info[0].host), g.host);
    system_con_info[0].server_port = g.port;
    copy_str(system_con_info[0].system, sizeof(system_con_info[0].system), "Real HW");
    *num_systems = 1;
    mcd_unlock();
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_qry_devices_f(const mcd_core_con_info_st *system_con_info,
                                uint32_t start_index, uint32_t *num_devices,
                                mcd_core_con_info_st *device_con_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    uint32_t total, count, i;

    (void)system_con_info;   /* single system; selection is not needed */

    mcd_lock();
    err_clear();

    if (num_devices == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_devices_f: NULL num_devices");
        mcd_unlock();
        return r;
    }
    if (!g.server_open) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_qry_devices_f: no server opened");
        mcd_unlock();
        return r;
    }
    total = (uint32_t)g.n_targets;
    if (*num_devices == 0) {
        *num_devices = total;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    if (start_index >= total || device_con_info == NULL) {
        *num_devices = 0;
        if (device_con_info == NULL && start_index == 0)
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_devices_f: NULL device_con_info");
        mcd_unlock();
        return r;
    }

    count = *num_devices;
    if (count > total - start_index)
        count = total - start_index;
    for (i = 0; i < count; i++)
        fill_device_con_info(&device_con_info[i], &g.targets[start_index + i]);
    *num_devices = count;
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_cores_f(const mcd_core_con_info_st *connection_info,
                              uint32_t start_index, uint32_t *num_cores,
                              mcd_core_con_info_st *core_con_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    const tas_target *t = NULL;
    const mcd_devdb_entry_t *db;
    uint32_t n, count, i;
    int k;

    mcd_lock();
    err_clear();

    if (num_cores == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_cores_f: NULL num_cores");
        mcd_unlock();
        return r;
    }
    if (!g.server_open) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_qry_cores_f: no server opened");
        mcd_unlock();
        return r;
    }

    if (connection_info != NULL && connection_info->acc_hw[0] != 0) {
        for (k = 0; k < g.n_targets; k++) {
            if (strcmp(g.targets[k].identifier, connection_info->acc_hw) == 0) {
                t = &g.targets[k];
                break;
            }
        }
        if (t == NULL) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_cores_f: unknown acc_hw '%s'",
                        connection_info->acc_hw);
            mcd_unlock();
            return r;
        }
    } else if (g.n_targets > 0) {
        t = &g.targets[0];
    }

    db = (t != NULL) ? devdb_lookup(t->device_type) : NULL;
    n = (t == NULL) ? 0u : (db != NULL ? (uint32_t)db->n_cores : 1u);

    if (*num_cores == 0) {
        *num_cores = n;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    if (start_index >= n || core_con_info == NULL) {
        *num_cores = 0;
        if (core_con_info == NULL && start_index == 0 && n > 0)
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_cores_f: NULL core_con_info");
        mcd_unlock();
        return r;
    }

    count = *num_cores;
    if (count > n - start_index)
        count = n - start_index;
    for (i = 0; i < count; i++) {
        uint32_t idx = start_index + i;
        mcd_core_con_info_st *o = &core_con_info[i];
        fill_device_con_info(o, t);
        if (db != NULL)
            snprintf(o->core, sizeof(o->core), db->core_name_fmt, (int)idx, (int)idx);
        else
            snprintf(o->core, sizeof(o->core), "Core %d", (int)idx);
        o->core_type = (db != NULL) ? db->core_type : 44u;
        o->core_id = (db != NULL) ? db->core_id : 0u;
    }
    *num_cores = count;
    mcd_unlock();
    return r;
}

void mcd_qry_error_info_f(const mcd_core_st *core, mcd_error_info_st *error_info)
{
    (void)core;   /* prototype keeps one global error record */
    mcd_lock();
    dbg("mcd_tas: CALL qry_error_info ('%s')", g.last_err.error_str);
    if (error_info != NULL)
        *error_info = g.last_err;
    mcd_unlock();
}

/* ------------------------------------------------------------------ */
/* M2: TAS session helpers for core access                             */
/* ------------------------------------------------------------------ */

static int core_index_from_name(const char *name)
{
    if (name != NULL && strncmp(name, "Core ", 5) == 0 &&
        name[5] >= '0' && name[5] <= '9')
        return name[5] - '0';
    return -1;
}

static mcd_error_code_et tas_err_to_mcd(uint8_t te)
{
    switch (te) {
    case TAS_PL1_ERR_SERVER_LOCKED: return MCD_ERR_SERVER_LOCKED;
    case TAS_PL1_ERR_DEV_RESET:     return MCD_ERR_DEVICE_ACCESS;
    case TAS_PL1_ERR_DEV_LOCKED:    return MCD_ERR_DEVICE_LOCKED;
    case TAS_PL1_ERR_DEV_ACCESS:    return MCD_ERR_DEVICE_ACCESS;
    default:                        return MCD_ERR_CONNECTION;
    }
}

/* Open a dedicated TAS connection + session for one core and attach to
 * the target.  The message order replicates the observed reference MCD
 * client: SERVER_CONNECT -> SESSION_START -> PING -> SET_DEVICE_KEY
 * (hot attach, no key) -> DEVICE_CONNECT (option 0: no reset, no halt).
 * Returns MCD_ERR_NONE on success, otherwise an MCD error code with a
 * description in err. */
static mcd_error_code_et open_tas_session(mcd_core_slot_t *cs,
                                          char *err, size_t errlen)
{
    uint8_t  te = 0;
    uint16_t feat_used = 0;
    uint32_t device_type = 0;
    int tgt = cs->target;

    if (tgt < 0 || tgt >= g.n_targets) {
        snprintf(err, errlen, "target index %d is not available", tgt);
        return MCD_ERR_PARAM;
    }

    if (tas_connect(&cs->conn, g.host, (uint16_t)g.port, err, errlen) != 0)
        return MCD_ERR_CONNECTION;
    if (tas_server_connect(&cs->conn, MCD_TAS_CLIENT_NAME, mcd_user(),
                           GetCurrentProcessId(), &g.srv, &te) != 0 ||
        te != TAS_ERR_OK) {
        snprintf(err, errlen, "TAS SERVER_CONNECT rejected (tas_err=0x%02x)", te);
        tas_disconnect(&cs->conn);
        return tas_err_to_mcd(te);
    }
    if (tas_session_start(&cs->conn, g.targets[tgt].identifier,
                          &cs->con, &te) != 0 || te != TAS_ERR_OK) {
        snprintf(err, errlen, "TAS SESSION_START rejected for '%s' (tas_err=0x%02x)",
                 g.targets[tgt].identifier, te);
        tas_disconnect(&cs->conn);
        return tas_err_to_mcd(te);
    }
    if (tas_ping_con(&cs->conn, &cs->con, &te) != 0 || te != TAS_ERR_OK) {
        snprintf(err, errlen, "TAS PING rejected (tas_err=0x%02x)", te);
        tas_disconnect(&cs->conn);
        return tas_err_to_mcd(te);
    }

    /* Reference clients read the server internal device descriptor blocks
     * (AM15) before attaching.  Keep that for parity; it is also our
     * read-only indicator whether the server has an identified device. */
    {
        static const uint64_t addrs[3] = { 0xD000ull, 0x0000ull, 0x8000ull };
        static const uint32_t lens[3]  = { 104, 96, 168 };
        uint8_t buf[256];
        uint8_t devtype_from_desc = 0;
        uint32_t desc_devtype = 0;
        int k;

        for (k = 0; k < 3; k++) {
            tas_rw_item it;
            uint8_t pe = 0;
            memset(&it, 0, sizeof(it));
            memset(buf, 0, sizeof(buf));
            it.addr_map = TAS_AM15;
            it.addr = addrs[k];
            it.num_bytes = lens[k];
            it.data = buf;
            if (tas_rw_exec(&cs->conn, &it, 1, cs->con.pl0_max_num_rw, &pe) != 0 ||
                !it.ok) {
                dbg("mcd_tas: AM15 info read @0x%llX (%u B) failed (pl0_err 0x%02x)",
                    (unsigned long long)addrs[k], (unsigned)lens[k], (unsigned)pe);
            } else if (k == 0) {
                desc_devtype = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                               ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
            }
        }
        if (desc_devtype == 0)
            devtype_from_desc = 1;   /* server has no identified device */
        cs->con.device_type = cs->con.device_type ? cs->con.device_type : desc_devtype;
        if (devtype_from_desc)
            strncpy(cs->no_device_note, " (server reports no identified device)",
                    sizeof(cs->no_device_note) - 1);
    }

    if (tas_set_device_key(&cs->conn, TAS_ULO_HOT_ATTACH, NULL, 0, &te) != 0 ||
        te != TAS_ERR_OK) {
        snprintf(err, errlen, "TAS SET_DEVICE_KEY rejected (tas_err=0x%02x)", te);
        tas_disconnect(&cs->conn);
        return tas_err_to_mcd(te);
    }
    if (tas_device_connect(&cs->conn, 0x0000, &feat_used, &device_type, &te) != 0 ||
        te != TAS_ERR_OK) {
        snprintf(err, errlen, "TAS DEVICE_CONNECT rejected (tas_err=0x%02x)%s",
                 te, cs->no_device_note);
        tas_disconnect(&cs->conn);
        return tas_err_to_mcd(te);
    }

    if (device_type != 0)
        cs->con.device_type = device_type;   /* known after attach */
    cs->st_valid = 0;
    cs->st_dbgsr = 0;
    cs->st_state = MCD_CORE_STATE_UNKNOWN;
    cs->attached = 1;
    dbg("mcd_tas: session attached to '%s' (device 0x%08X, feat 0x%04x, max_rw %u)",
        g.targets[tgt].identifier, (unsigned)cs->con.device_type,
        (unsigned)feat_used, (unsigned)cs->con.pl0_max_num_rw);
    return MCD_ERR_NONE;
}

static void close_tas_session(mcd_core_slot_t *cs)
{
    if (cs->conn.sock != INVALID_SOCKET)
        tas_disconnect(&cs->conn);
    cs->used = 0;
    cs->attached = 0;
    cs->conn.sock = INVALID_SOCKET;
}

static mcd_core_slot_t *core_slot_from(const mcd_core_st *core)
{
    int k;
    if (core == NULL)
        return NULL;
    for (k = 0; k < MCD_MAX_CORE_SLOTS; k++)
        if (g.cores[k].used && core == &g.cores[k].core_obj)
            return &g.cores[k];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* stubs (M2+ work)                                                    */
/* ------------------------------------------------------------------ */

#define STUB_ENTER(name)                                          \
    mcd_return_et r_;                                             \
    mcd_lock();                                                   \
    r_ = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_FN_UNIMPLEMENTED, \
                 name ": not implemented by mcd_tas prototype"); \
    mcd_unlock();                                                 \
    return r_;

mcd_return_et mcd_qry_core_modes_f(const mcd_core_st *core,
                                   uint32_t start_index, uint32_t *num_modes,
                                   mcd_core_mode_info_st *core_mode_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)start_index; (void)core_mode_info;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_core_modes_f: unknown core handle");
    } else if (num_modes == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_core_modes_f: NULL num_modes");
    } else {
        /* The prototype does not expose core mode switching. */
        dbg("mcd_tas: CALL qry_core_modes start=%u", (unsigned)start_index);
        *num_modes = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_open_core_f(const mcd_core_con_info_st *core_con_info,
                              mcd_core_st **core)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    mcd_core_slot_t *cs = NULL;
    const mcd_devdb_entry_t *db;
    char detail[192];
    char ident[65];
    int k, tgt = -1, cidx = 0, ncores;

    mcd_lock();
    err_clear();

    if (core_con_info == NULL || core == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_open_core_f: NULL argument");
        mcd_unlock();
        return r;
    }
    *core = NULL;
    dbg("mcd_tas: open_core request: acc_hw='%s' core='%s' host='%s' port=%u",
        core_con_info->acc_hw, core_con_info->core,
        core_con_info->host, (unsigned)core_con_info->server_port);
    if (!g.server_open) {
        /* The MCD client may call open_core directly with a fully
           specified connection info (restored last session).  Open the
           server implicitly, like the reference implementation does. */
        char cfg[160];
        mcd_server_st *srv = NULL;
        const char *h = (core_con_info->host[0] != 0)
                        ? core_con_info->host : "localhost";
        uint32_t p = (core_con_info->server_port != 0)
                     ? core_con_info->server_port : TAS_DEFAULT_PORT;
        dbg("mcd_tas: open_core: no server open, auto-open %s:%u",
            h, (unsigned)p);
        snprintf(cfg, sizeof(cfg), "McdHostName=%s McdServerPort=%u",
                 h, (unsigned)p);
        r = mcd_open_server_f(NULL, cfg, &srv);
        if (r != MCD_RET_ACT_NONE) {
            mcd_unlock();
            return r;   /* failure reason set by open_server */
        }
    }

    /* locate the target by access HW name */
    if (core_con_info->acc_hw[0] != 0) {
        for (k = 0; k < g.n_targets; k++) {
            if (strcmp(g.targets[k].identifier, core_con_info->acc_hw) == 0) {
                tgt = k;
                break;
            }
        }
        if (tgt < 0) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_open_core_f: unknown acc_hw '%s'",
                        core_con_info->acc_hw);
            mcd_unlock();
            return r;
        }
    } else if (g.n_targets > 0) {
        tgt = 0;
    } else {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_open_core_f: no target available");
        mcd_unlock();
        return r;
    }

    db = devdb_lookup(g.targets[tgt].device_type);
    ncores = (db != NULL) ? (int)db->n_cores : 1;
    cidx = core_index_from_name(core_con_info->core);
    if (cidx < 0)
        cidx = 0;   /* unspecified core -> first core */
    if (cidx >= ncores) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_open_core_f: unknown core '%s' (device has %d)",
                    core_con_info->core, ncores);
        mcd_unlock();
        return r;
    }

    /* reuse an already open slot for the same target + core */
    for (k = 0; k < MCD_MAX_CORE_SLOTS; k++) {
        if (g.cores[k].used && g.cores[k].target == tgt &&
            g.cores[k].core_index == cidx) {
            *core = &g.cores[k].core_obj;
            mcd_unlock();
            return MCD_RET_ACT_NONE;
        }
    }

    /* free slot */
    for (k = 0; k < MCD_MAX_CORE_SLOTS; k++) {
        if (!g.cores[k].used) {
            cs = &g.cores[k];
            break;
        }
    }
    if (cs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                    "mcd_open_core_f: too many open cores (max %d)",
                    MCD_MAX_CORE_SLOTS);
        mcd_unlock();
        return r;
    }

    memset(cs, 0, sizeof(*cs));
    cs->conn.sock = INVALID_SOCKET;
    cs->target = tgt;
    cs->core_index = cidx;

    {
        mcd_error_code_et ec = open_tas_session(cs, detail, sizeof(detail));
        if (ec != MCD_ERR_NONE) {
            close_tas_session(cs);
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, ec,
                        "mcd_open_core_f: %s", detail);
            mcd_unlock();
            return r;
        }
    }

    /* The device may only be identified after the attach: refresh the
     * target list so device type / id become visible everywhere. */
    copy_str(ident, sizeof(ident), g.targets[tgt].identifier);
    refresh_targets();
    for (k = 0; k < g.n_targets; k++) {
        if (strcmp(g.targets[k].identifier, ident) == 0) {
            tgt = k;
            break;
        }
    }

    cs->used = 1;
    cs->target = tgt;

    db = devdb_lookup(g.targets[tgt].device_type);
    fill_device_con_info(&cs->core_ci, &g.targets[tgt]);
    if (cs->core_ci.device_type == 0 && cs->con.device_type != 0) {
        const mcd_devdb_entry_t *cdb = devdb_lookup(cs->con.device_type);
        cs->core_ci.device_type = cs->con.device_type;
        copy_str(cs->core_ci.device, sizeof(cs->core_ci.device),
                 cdb ? cdb->device_name : "Unknown device");
        if (db == NULL)
            db = cdb;
    }
    if (db != NULL) {
        snprintf(cs->core_ci.core, sizeof(cs->core_ci.core),
                 db->core_name_fmt, cidx, cidx);
        cs->core_ci.core_type = db->core_type;
        cs->core_ci.core_id = db->core_id;
    } else {
        snprintf(cs->core_ci.core, sizeof(cs->core_ci.core),
                 "Core %d", cidx);
    }
    cs->core_obj.instance = cs;
    cs->core_obj.core_con_info = &cs->core_ci;
    *core = &cs->core_obj;

    dbg("mcd_tas: core %d of '%s' opened (device '%s' type 0x%08X)",
        cidx, cs->core_ci.acc_hw, cs->core_ci.device,
        (unsigned)cs->core_ci.device_type);
    mcd_unlock();
    return r;
}

mcd_return_et mcd_close_core_f(const mcd_core_st *core)
{
    mcd_return_et r;
    mcd_core_slot_t *cs;

    mcd_lock();
    err_clear();

    cs = core_slot_from(core);
    if (cs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_close_core_f: unknown core handle");
    } else {
        dbg("mcd_tas: core %d of '%s' closed", cs->core_index,
            cs->core_ci.acc_hw);
        close_tas_session(cs);
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_device_description_f(const mcd_core_st *core,
                                           mcd_char_t *url, uint32_t *url_length)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    dbg("mcd_tas: CALL qry_device_description");

    if (url_length == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_device_description_f: NULL url_length");
    } else if (url == NULL) {
        *url_length = 1;   /* required buffer size incl. terminating zero */
    } else if (*url_length == 0) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_device_description_f: zero size url buffer");
    } else {
        /* The prototype has no IP-XACT description document. */
        url[0] = '\0';
        *url_length = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_max_payload_size_f(const mcd_core_st *core,
                                         uint32_t *max_payload)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_max_payload_size_f: unknown core handle");
    } else if (max_payload == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_max_payload_size_f: NULL max_payload");
    } else {
        /* The TAS transport is chunked internally; larger transaction
         * sizes are split into PL0 items automatically. */
        *max_payload = MCD_TAS_MAX_PAYLOAD;
        dbg("mcd_tas: CALL qry_max_payload_size (-> %u)",
            (unsigned)MCD_TAS_MAX_PAYLOAD);
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_input_handle_f(const mcd_core_st *core,
                                     uint32_t *input_handle)
{
    (void)core; (void)input_handle;
    STUB_ENTER("mcd_qry_input_handle_f")
}

mcd_return_et mcd_qry_mem_spaces_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_mem_spaces,
                                   mcd_memspace_st *mem_spaces)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    mcd_memspace_st *sp;

    mcd_lock();
    err_clear();

    dbg("mcd_tas: CALL qry_mem_spaces start=%u", (unsigned)start_index);

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_mem_spaces_f: unknown core handle");
        mcd_unlock();
        return r;
    }
    if (num_mem_spaces == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_mem_spaces_f: NULL num_mem_spaces");
        mcd_unlock();
        return r;
    }
    if (*num_mem_spaces == 0) {
        *num_mem_spaces = 1;
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    if (start_index > 0 || mem_spaces == NULL) {
        *num_mem_spaces = 0;
        if (mem_spaces == NULL && start_index == 0)
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                        "mcd_qry_mem_spaces_f: NULL mem_spaces");
        mcd_unlock();
        return r;
    }

    /* The prototype exposes the TriCore default device access path
     * (TAS address map 0) as a single physical memory space. */
    sp = &mem_spaces[0];
    memset(sp, 0, sizeof(*sp));
    sp->mem_space_id = MCD_TAS_MEM_SPACE_ID;
    copy_str(sp->mem_space_name, sizeof(sp->mem_space_name), "AM0");
    sp->mem_type = MCD_MEM_SPACE_IS_PHYSICAL;
    sp->bits_per_mau = 8;
    sp->invariance = 1;
    sp->endian = MCD_ENDIAN_LITTLE;
    sp->min_addr = 0;
    sp->max_addr = 0xFFFFFFFFull;
    sp->num_mem_blocks = 0;
    sp->supported_access_options = 0;
    sp->core_mode_mask_read = 0;
    sp->core_mode_mask_write = 0;
    *num_mem_spaces = 1;

    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_mem_blocks_f(const mcd_core_st *core,
                                   uint32_t mem_space_id, uint32_t start_index,
                                   uint32_t *num_mem_blocks,
                                   mcd_memblock_st *mem_blocks)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)mem_space_id; (void)start_index; (void)mem_blocks;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_mem_blocks_f: unknown core handle");
    } else if (num_mem_blocks == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_mem_blocks_f: NULL num_mem_blocks");
    } else {
        /* No named memory blocks are exposed by the prototype. */
        dbg("mcd_tas: CALL qry_mem_blocks space=%u start=%u",
            (unsigned)mem_space_id, (unsigned)start_index);
        *num_mem_blocks = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_active_overlays_f(const mcd_core_st *core,
                                        uint32_t start_index,
                                        uint32_t *num_active_overlays,
                                        uint32_t *active_overlays)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)start_index; (void)active_overlays;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_active_overlays_f: unknown core handle");
    } else if (num_active_overlays == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_active_overlays_f: NULL num_active_overlays");
    } else {
        /* No overlapping overlay regions are exposed by the prototype. */
        dbg("mcd_tas: CALL qry_active_overlays start=%u",
            (unsigned)start_index);
        *num_active_overlays = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_reg_groups_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_reg_groups,
                                   mcd_register_group_st *reg_groups)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)reg_groups;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_groups_f: unknown core handle");
    } else if (num_reg_groups == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_groups_f: NULL num_reg_groups");
    } else {
        /* M4 calibration: expose a single "CPU" register group so the
         * basic client's register discovery path becomes observable. */
        dbg("mcd_tas: CALL qry_reg_groups start=%u (-> 1 group)",
            (unsigned)start_index);
        *num_reg_groups = 1;
        if (start_index == 0 && reg_groups != NULL) {
            mcd_register_group_st *g = &reg_groups[0];
            memset(g, 0, sizeof(*g));
            g->reg_group_id = MCD_TAS_REG_GROUP_CPU;
            copy_str(g->reg_group_name, sizeof(g->reg_group_name), "CPU");
            g->n_registers = 1;
        }
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_reg_map_f(const mcd_core_st *core, uint32_t reg_group_id,
                                uint32_t start_index, uint32_t *num_regs,
                                mcd_register_info_st *reg_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_map_f: unknown core handle");
    } else if (num_regs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_map_f: NULL num_regs");
    } else {
        uint32_t cap = *num_regs;
        /* M4: the reference stack exposes the TriCore debug PC register;
         * the basic client requests it once (group id 0) and then polls
         * it for the Instruction Pointer display.  A single 32 bit AM0
         * register reproduces the reference poll cycle.  The number of
         * entries written must never exceed the caller's capacity. */
        (void)start_index;
        dbg("mcd_tas: CALL qry_reg_map group=%u cap=%u",
            (unsigned)reg_group_id, (unsigned)cap);
        if ((reg_group_id != 0 && reg_group_id != MCD_TAS_REG_GROUP_CPU) ||
            start_index != 0) {
            *num_regs = 0;
        } else if (cap == 0 || reg_info == NULL) {
            *num_regs = 1;              /* count query */
        } else {
            mcd_register_info_st *ri = &reg_info[0];
            *num_regs = 1;
            memset(ri, 0, sizeof(*ri));
            ri->addr.mem_space_id = MCD_TAS_MEM_SPACE_ID;
            ri->addr.address = 0xF881FE08ull;
            ri->reg_group_id = reg_group_id;
            copy_str(ri->regname, sizeof(ri->regname), "PC");
            ri->regsize = 32;
            ri->reg_type = MCD_REG_TYPE_SIMPLE;
        }
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_reg_compound_f(const mcd_core_st *core,
                                     uint32_t compound_reg_id,
                                     uint32_t start_index,
                                     uint32_t *num_reg_ids,
                                     uint32_t *reg_id_array)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)compound_reg_id; (void)start_index; (void)reg_id_array;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_compound_f: unknown core handle");
    } else if (num_reg_ids == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_reg_compound_f: NULL num_reg_ids");
    } else {
        /* No compound registers are exposed by the prototype. */
        *num_reg_ids = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_trig_info_f(const mcd_core_st *core,
                                  mcd_trig_info_st *trig_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_info_f: unknown core handle");
    } else if (trig_info == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_info_f: NULL trig_info");
    } else {
        /* No trigger types are supported by the prototype. */
        dbg("mcd_tas: CALL qry_trig_info");
        memset(trig_info, 0, sizeof(*trig_info));
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_ctrigs_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_ctrigs,
                               mcd_ctrig_info_st *ctrig_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)start_index; (void)ctrig_info;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_ctrigs_f: unknown core handle");
    } else if (num_ctrigs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_ctrigs_f: NULL num_ctrigs");
    } else {
        /* No custom triggers are exposed by the prototype. */
        *num_ctrigs = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_create_trig_f(const mcd_core_st *core, void *trig,
                                uint32_t *trig_id)
{
    (void)core; (void)trig; (void)trig_id;
    STUB_ENTER("mcd_create_trig_f")
}

mcd_return_et mcd_qry_trig_f(const mcd_core_st *core, uint32_t trig_id,
                             uint32_t max_trig_size, void *trig)
{
    (void)core; (void)trig_id; (void)max_trig_size; (void)trig;
    STUB_ENTER("mcd_qry_trig_f")
}

mcd_return_et mcd_remove_trig_f(const mcd_core_st *core, uint32_t trig_id)
{
    (void)core; (void)trig_id;
    STUB_ENTER("mcd_remove_trig_f")
}

mcd_return_et mcd_qry_trig_state_f(const mcd_core_st *core, uint32_t trig_id,
                                   mcd_trig_state_st *trig_state)
{
    (void)core; (void)trig_id; (void)trig_state;
    STUB_ENTER("mcd_qry_trig_state_f")
}

mcd_return_et mcd_activate_trig_set_f(const mcd_core_st *core)
{
    (void)core;
    STUB_ENTER("mcd_activate_trig_set_f")
}

mcd_return_et mcd_remove_trig_set_f(const mcd_core_st *core)
{
    (void)core;
    STUB_ENTER("mcd_remove_trig_set_f")
}

mcd_return_et mcd_qry_trig_set_f(const mcd_core_st *core, uint32_t start_index,
                                 uint32_t *num_trigs, uint32_t *trig_ids)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)start_index; (void)trig_ids;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_set_f: unknown core handle");
    } else if (num_trigs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_set_f: NULL num_trigs");
    } else {
        /* No triggers are in the active set (none can be created). */
        *num_trigs = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_trig_set_state_f(const mcd_core_st *core,
                                       mcd_trig_set_state_st *trig_state)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_set_state_f: unknown core handle");
    } else if (trig_state == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trig_set_state_f: NULL trig_state");
    } else {
        /* Empty trigger set: no hits, no active triggers. */
        memset(trig_state, 0, sizeof(*trig_state));
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_execute_txlist_f(const mcd_core_st *core,
                                   mcd_txlist_st *txlist)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    mcd_core_slot_t *cs;
    tas_rw_item *items = NULL;
    uint32_t *tx_start = NULL;
    uint32_t cap = 0, n = 0, i, j;
    uint8_t frame_err = 0;
    int trc;

    mcd_lock();
    err_clear();

    cs = core_slot_from(core);
    if (cs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_execute_txlist_f: unknown core handle");
        mcd_unlock();
        return r;
    }
    if (!cs->attached) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                    "mcd_execute_txlist_f: no TAS device session");
        mcd_unlock();
        return r;
    }
    if (txlist == NULL || (txlist->num_tx > 0 && txlist->tx == NULL)) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_execute_txlist_f: invalid txlist");
        mcd_unlock();
        return r;
    }

    txlist->num_tx_ok = 0;
    if (txlist->num_tx == 0) {
        mcd_unlock();
        return MCD_RET_ACT_NONE;
    }
    for (i = 0; i < txlist->num_tx; i++)
        txlist->tx[i].num_bytes_ok = 0;

    /* upper bound on the number of PL0 items (worst case 1 byte each) */
    for (i = 0; i < txlist->num_tx; i++) {
        uint32_t nb = txlist->tx[i].num_bytes;
        cap += nb / 8 + 2;
        if (cap > 0x1000000) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                        "mcd_execute_txlist_f: transaction list too large");
            mcd_unlock();
            return r;
        }
    }
    items = (tas_rw_item *)calloc(cap, sizeof(*items));
    tx_start = (uint32_t *)malloc((size_t)txlist->num_tx * sizeof(uint32_t));
    if (items == NULL || tx_start == NULL) {
        free(items);
        free(tx_start);
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_GENERAL,
                    "mcd_execute_txlist_f: out of memory");
        mcd_unlock();
        return r;
    }

    /* --- split the transactions into PL0 items --- */
    for (i = 0; i < txlist->num_tx; i++) {
        mcd_tx_st *tx = &txlist->tx[i];
        uint64_t a = tx->addr.address;
        uint32_t nb = tx->num_bytes;
        uint8_t w = tx->access_width;
        uint32_t off = 0;

        tx_start[i] = n;

        dbg("mcd_tas: tx[%u] %s space=%u addr=0x%llX bytes=%u width=%u",
            i, (tx->access_type == MCD_TX_AT_W) ? "W" : "R",
            (unsigned)tx->addr.mem_space_id,
            (unsigned long long)tx->addr.address,
            (unsigned)tx->num_bytes, (unsigned)tx->access_width);

        if (tx->access_type != MCD_TX_AT_R && tx->access_type != MCD_TX_AT_W) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: tx[%u] access type 0x%x is not supported",
                        i, (unsigned)tx->access_type);
            goto fail;
        }
        if (tx->addr.addr_space_type != MCD_NOTUSED_ID ||
            tx->addr.addr_space_id != 0) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: tx[%u] address space type/id not supported",
                        i);
            goto fail;
        }
        if (tx->addr.mem_space_id != 0 &&
            tx->addr.mem_space_id != MCD_TAS_MEM_SPACE_ID) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: tx[%u] unknown memory space %u",
                        i, (unsigned)tx->addr.mem_space_id);
            goto fail;
        }
        if (nb == 0)
            continue;
        if (tx->data == NULL) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: tx[%u] NULL data buffer", i);
            goto fail;
        }
        if (w != 0 && (w > 8 || (w & (w - 1)) != 0)) {
            /* Not a valid power-of-two width.  The reference MCD
             * implementation never reads this field, and some clients
             * leave it uninitialized; treat it like 0 ("does not
             * matter") and fall back to alignment-driven chunking. */
            static int warned_invalid_width = 0;
            if (!warned_invalid_width) {
                warned_invalid_width = 1;
                dbg("mcd_tas: note: access width %u at tx[%u] is not a "
                    "valid power of two; treating as 'does not matter'",
                    (unsigned)w, i);
            }
            w = 0;
        }
        if (w != 0 && ((nb % w) != 0 || (a % w) != 0)) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: tx[%u] size/alignment mismatch for access width %u",
                        i, (unsigned)w);
            goto fail;
        }

        while (off < nb) {
            uint32_t chunk;
            if (w != 0) {
                chunk = w;
            } else {
                /* largest power-of-two chunk (<= 8) fitting alignment */
                chunk = 1;
                while (chunk < 8 && chunk * 2 <= nb - off &&
                       ((a + off) % (chunk * 2)) == 0)
                    chunk *= 2;
            }
            items[n].addr_map = TAS_AM0;
            items[n].addr = a + off;
            items[n].num_bytes = chunk;
            items[n].is_write = (tx->access_type == MCD_TX_AT_W);
            items[n].data = tx->data + off;
            items[n].ok = 0;
            items[n].num_bytes_ok = 0;
            items[n].pl_err = 0;
            n++;
            off += chunk;
        }
    }

    /* --- execute (frames built inside tas_rw_exec) --- */
    trc = tas_rw_exec(&cs->conn, items, n, cs->con.pl0_max_num_rw, &frame_err);
    if (trc != 0) {
        if (trc == -3) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, tas_err_to_mcd(frame_err),
                        "mcd_execute_txlist_f: PL0 frame rejected (tas_err=0x%02x)",
                        frame_err);
        } else if (trc == -4) {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_TXLIST_TX,
                        "mcd_execute_txlist_f: transaction could not be encoded");
        } else {
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                        "mcd_execute_txlist_f: TAS transport error (%d)", trc);
        }
        goto fail;
    }

    /* --- aggregate per transaction (abort report at first failure) --- */
    for (i = 0; i < txlist->num_tx; i++) {
        mcd_tx_st *tx = &txlist->tx[i];
        uint32_t beg = tx_start[i];
        uint32_t end = (i + 1 < txlist->num_tx) ? tx_start[i + 1] : n;
        uint32_t okb = 0;
        int bad = 0;
        uint8_t p_err = 0;

        for (j = beg; j < end; j++) {
            if (items[j].ok) {
                okb += items[j].num_bytes;
            } else {
                bad = 1;
                p_err = items[j].pl_err;
                break;
            }
        }
        tx->num_bytes_ok = bad ? okb : tx->num_bytes;
        if (bad) {
            mcd_error_code_et ec = (tx->access_type == MCD_TX_AT_R)
                                       ? MCD_ERR_TXLIST_READ
                                       : MCD_ERR_TXLIST_WRITE;
            /* Keep diagnostic detail in our log; the client-facing string must
             * match the original mcdxdas.dll's per-code text so that
             * qry_error_info reports the canonical message. */
            dbg("mcd_tas: tx[%u] %s failed at 0x%llX (PL0 err 0x%02x)",
                i, (tx->access_type == MCD_TX_AT_R) ? "read" : "write",
                (unsigned long long)tx->addr.address, (unsigned)p_err);
            r = err_set(MCD_RET_ACT_HANDLE_ERROR, ec,
                        (ec == MCD_ERR_TXLIST_READ)
                            ? "Read failure for a transaction of the txlist"
                            : "Write failure for a transaction of the txlist");
            goto fail;
        }
        txlist->num_tx_ok = i + 1;
    }

    free(items);
    free(tx_start);
    mcd_unlock();
    return MCD_RET_ACT_NONE;

fail:
    free(items);
    free(tx_start);
    mcd_unlock();
    return r;
}

mcd_return_et mcd_run_f(const mcd_core_st *core, mcd_bool_t global)
{
    (void)core; (void)global;
    STUB_ENTER("mcd_run_f")
}

mcd_return_et mcd_stop_f(const mcd_core_st *core, mcd_bool_t global)
{
    (void)core; (void)global;
    STUB_ENTER("mcd_stop_f")
}

mcd_return_et mcd_run_until_f(const mcd_core_st *core, mcd_bool_t global,
                              mcd_bool_t absolute_time, uint64_t run_until_time)
{
    (void)core; (void)global; (void)absolute_time; (void)run_until_time;
    STUB_ENTER("mcd_run_until_f")
}

mcd_return_et mcd_qry_current_time_f(const mcd_core_st *core,
                                     uint64_t *current_time)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_current_time_f: unknown core handle");
    } else if (current_time == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_current_time_f: NULL current_time");
    } else {
        /* Target runtime is not tracked by the prototype. */
        dbg("mcd_tas: CALL qry_current_time");
        *current_time = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_step_f(const mcd_core_st *core, mcd_bool_t global,
                         mcd_core_step_type_et step_type, uint32_t n_steps)
{
    (void)core; (void)global; (void)step_type; (void)n_steps;
    STUB_ENTER("mcd_step_f")
}

mcd_return_et mcd_set_global_f(const mcd_core_st *core, mcd_bool_t enable)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)enable;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_set_global_f: unknown core handle");
    } else {
        /* Run/stop are not implemented; accepting the request is benign
         * (there is no global stop/run activity to enable or disable). */
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_state_f(const mcd_core_st *core, mcd_core_state_st *state)
{
    mcd_return_et r = MCD_RET_ACT_NONE;
    mcd_core_slot_t *cs;

    mcd_lock();
    err_clear();

    cs = core_slot_from(core);
    if (cs == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_state_f: unknown core handle");
    } else if (state == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_state_f: NULL state");
    } else if (!cs->attached) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_CONNECTION,
                    "mcd_qry_state_f: no TAS device session");
    } else {
        /* Real state: derive it from the TC27x Debug Status Register
         * (CPU0 CSFR window + FD00H; TC27x UM Table 5-24, TC1.6 UM
         * Vol.1 sec.12.9).  HALT[2:1] == 01b => core halted (debug
         * control), 00b => running.  The register is read once per
         * attach (read-only; matches the reference DLL's single DBGSR
         * read) and the decoded result is cached for the session. */
        if (!cs->st_valid) {
            tas_rw_item it;
            uint8_t  pe = 0;
            uint32_t v = 0;
            int      rc;

            memset(&it, 0, sizeof(it));
            it.addr_map = TAS_AM0;
            it.addr = MCD_TAS_DBGSR_ADDR;
            it.num_bytes = 4;
            it.data = (uint8_t *)&v;

            rc = tas_rw_exec(&cs->conn, &it, 1, cs->con.pl0_max_num_rw, &pe);
            if (rc == 0 && it.ok) {
                uint32_t de     = (v >> 0) & 0x1u;
                uint32_t halt   = (v >> 1) & 0x3u;
                uint32_t sih    = (v >> 3) & 0x1u;
                uint32_t susp   = (v >> 4) & 0x1u;
                uint32_t pevt   = (v >> 7) & 0x1u;
                uint32_t evtsrc = (v >> 8) & 0x1Fu;

                cs->st_dbgsr = v;
                if (halt == 0x1u) {
                    cs->st_state = MCD_CORE_STATE_DEBUG;   /* halted */
                } else if (halt == 0x0u) {
                    cs->st_state = MCD_CORE_STATE_RUNNING;
                } else {
                    /* 10b/11b are write-side halt set/clear codes */
                    cs->st_state = MCD_CORE_STATE_UNKNOWN;
                }
                cs->st_valid = 1;
                dbg("mcd_tas: qry_state: DBGSR @0x%llX = 0x%08X "
                    "(DE=%u HALT[2:1]=%u SIH=%u SUSP=%u PEVT=%u EVTSRC=%u) "
                    "-> %s (cached for session)",
                    (unsigned long long)MCD_TAS_DBGSR_ADDR, (unsigned)v,
                    (unsigned)de, (unsigned)halt, (unsigned)sih,
                    (unsigned)susp, (unsigned)pevt, (unsigned)evtsrc,
                    (cs->st_state == MCD_CORE_STATE_DEBUG)   ? "DEBUG" :
                    (cs->st_state == MCD_CORE_STATE_RUNNING) ? "RUNNING"
                                                             : "UNKNOWN");
            } else {
                /* Keep the client State/IP update path alive (M4
                 * behavior); retry the read on the next call. */
                static int st_fail_logged = 0;
                if (!st_fail_logged) {
                    st_fail_logged = 1;
                    dbg("mcd_tas: qry_state: DBGSR read failed (rc %d, "
                        "pl0_err 0x%02x, item_err 0x%02x) - DEBUG fallback, "
                        "retrying on next call",
                        rc, (unsigned)pe, (unsigned)it.pl_err);
                }
            }
        }

        memset(state, 0, sizeof(*state));
        if (cs->st_valid) {
            state->state = cs->st_state;
            if (cs->st_dbgsr != 0u) {
                uint32_t evtsrc = (cs->st_dbgsr >> 8) & 0x1Fu;
                if (evtsrc >= 16u && evtsrc <= 23u) {
                    state->trig_id = evtsrc - 16u;   /* TRnEVT -> trigger n */
                    snprintf(state->stop_str, sizeof(state->stop_str),
                             "Trigger %u (TR%uEVT)", (unsigned)state->trig_id,
                             (unsigned)state->trig_id);
                } else if (evtsrc == 0u) {
                    copy_str(state->stop_str, sizeof(state->stop_str),
                             "External Event (EXEVT)");
                } else if (evtsrc == 1u) {
                    copy_str(state->stop_str, sizeof(state->stop_str),
                             "Core Register Access Event (CREVT)");
                } else if (evtsrc == 2u) {
                    copy_str(state->stop_str, sizeof(state->stop_str),
                             "Software Debug Event (SWEVT)");
                }
            }
            snprintf(state->info_str, sizeof(state->info_str),
                     "mcd_tas (DBGSR=0x%08X)", (unsigned)cs->st_dbgsr);
            dbg("mcd_tas: CALL qry_state -> %s (dbgsr 0x%08X)",
                (cs->st_state == MCD_CORE_STATE_DEBUG)   ? "DEBUG" :
                (cs->st_state == MCD_CORE_STATE_RUNNING) ? "RUNNING"
                                                         : "UNKNOWN",
                (unsigned)cs->st_dbgsr);
        } else {
            /* Fallback while the DBGSR read keeps failing: report DEBUG
             * like the previous calibration so the basic client's UI
             * update path stays alive. */
            state->state = MCD_CORE_STATE_DEBUG;
            copy_str(state->info_str, sizeof(state->info_str),
                     "mcd_tas prototype (fallback)");
            dbg("mcd_tas: CALL qry_state -> DEBUG (fallback; DBGSR not read)");
        }
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_execute_command_f(const mcd_core_st *core,
                                    const mcd_char_t *command_string,
                                    uint32_t result_string_size,
                                    mcd_char_t *result_string)
{
    (void)core; (void)command_string; (void)result_string_size; (void)result_string;
    STUB_ENTER("mcd_execute_command_f")
}

/* Reset classes: the prototype advertises the strongest class only
 * (class 0, "System Reset", per the mcd_api.h recommendation).  Issuing
 * the reset itself is a pending milestone (mcd_rst_f stays a stub). */
mcd_return_et mcd_qry_rst_classes_f(const mcd_core_st *core,
                                    uint32_t *rst_class_vector)
{
    mcd_return_et r;

    (void)core;   /* single-device prototype: core value is not needed */
    mcd_lock();
    err_clear();

    if (rst_class_vector == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_rst_classes_f: NULL rst_class_vector");
    } else {
        *rst_class_vector = 1u << 0;
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_rst_class_info_f(const mcd_core_st *core,
                                       uint8_t rst_class,
                                       mcd_rst_info_st *rst_info)
{
    mcd_return_et r;

    (void)core;
    mcd_lock();
    err_clear();

    if (rst_info == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_rst_class_info_f: NULL rst_info");
    } else if (rst_class != 0) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_rst_class_info_f: reset class %u does not exist",
                    (unsigned)rst_class);
    } else {
        memset(rst_info, 0, sizeof(*rst_info));
        rst_info->class_vector = 1u << 0;
        copy_str(rst_info->info_str, sizeof(rst_info->info_str),
                 "System Reset");
        r = MCD_RET_ACT_NONE;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_rst_f(const mcd_core_st *core, uint32_t rst_class_vector,
                        mcd_bool_t rst_and_halt)
{
    (void)core; (void)rst_class_vector; (void)rst_and_halt;
    STUB_ENTER("mcd_rst_f")
}

mcd_return_et mcd_chl_open_f(const mcd_core_st *core, mcd_chl_st *channel)
{
    (void)core; (void)channel;
    STUB_ENTER("mcd_chl_open_f")
}

mcd_return_et mcd_send_msg_f(const mcd_core_st *core, const mcd_chl_st *channel,
                             uint32_t msg_len, const uint8_t *msg)
{
    (void)core; (void)channel; (void)msg_len; (void)msg;
    STUB_ENTER("mcd_send_msg_f")
}

mcd_return_et mcd_receive_msg_f(const mcd_core_st *core,
                                const mcd_chl_st *channel, uint32_t timeout,
                                uint32_t *msg_len, uint8_t *msg)
{
    (void)core; (void)channel; (void)timeout; (void)msg_len; (void)msg;
    STUB_ENTER("mcd_receive_msg_f")
}

mcd_return_et mcd_chl_reset_f(const mcd_core_st *core,
                              const mcd_chl_st *channel)
{
    (void)core; (void)channel;
    STUB_ENTER("mcd_chl_reset_f")
}

mcd_return_et mcd_chl_close_f(const mcd_core_st *core,
                              const mcd_chl_st *channel)
{
    (void)core; (void)channel;
    STUB_ENTER("mcd_chl_close_f")
}

mcd_return_et mcd_qry_traces_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_traces,
                               mcd_trace_info_st *trace_info)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)start_index; (void)trace_info;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_traces_f: unknown core handle");
    } else if (num_traces == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_traces_f: NULL num_traces");
    } else {
        /* No traces are exposed by the prototype. */
        *num_traces = 0;
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_qry_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state)
{
    mcd_return_et r = MCD_RET_ACT_NONE;

    (void)trace_id;
    mcd_lock();
    err_clear();

    if (core_slot_from(core) == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trace_state_f: unknown core handle");
    } else if (state == NULL) {
        r = err_set(MCD_RET_ACT_HANDLE_ERROR, MCD_ERR_PARAM,
                    "mcd_qry_trace_state_f: NULL state");
    } else {
        /* No traces exist: report an idle/disabled trace state. */
        memset(state, 0, sizeof(*state));
    }
    mcd_unlock();
    return r;
}

mcd_return_et mcd_set_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state)
{
    (void)core; (void)trace_id; (void)state;
    STUB_ENTER("mcd_set_trace_state_f")
}

mcd_return_et mcd_read_trace_f(const mcd_core_st *core, uint32_t trace_id,
                               uint64_t start_index, uint32_t *num_frames,
                               uint32_t trace_data_size, void *trace_data)
{
    (void)core; (void)trace_id; (void)start_index; (void)num_frames;
    (void)trace_data_size; (void)trace_data;
    STUB_ENTER("mcd_read_trace_f")
}
