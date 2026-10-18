/* map_probe.cpp - reads a matrix of TAS address-map/address combinations
 * to determine the MCDS access paths of the current server/adapter setup.
 *
 * The reference configurator emits the MCDS configuration for the TC27xED
 * at map 1 (0xF00E6000/0xF00E8000 windows, EMEM 0xAF000000); the server
 * responded 0x8D (bus error) to those on this setup. This probe checks
 * all candidate paths: map 1 (raw MCDS), map 15 (server internal view,
 * e.g. the timestamp counter at 0x50000090) and map 0 (target view).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "tas_client.h"

static void probe(tas_conn *c, uint8_t map, uint32_t addr, uint32_t maxrw)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    uint32_t v = 0xDEADBEEFu;

    memset(&it, 0, sizeof(it));
    it.addr_map = map;
    it.addr = addr;
    it.num_bytes = 4;
    it.data = (uint8_t *)&v;
    rc = tas_rw_exec(c, &it, 1, maxrw, &pe);
    printf("  map %2u @ 0x%08X : rc %2d ok %d pl0 0x%02X value 0x%08X\n",
           (unsigned)map, (unsigned)addr, rc, it.ok, (unsigned)pe,
           (unsigned)v);
}

static void probe64(tas_conn *c, uint8_t map, uint32_t addr, uint32_t maxrw)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;
    uint64_t v = 0xDEADBEEFDEADBEEFull;

    memset(&it, 0, sizeof(it));
    it.addr_map = map;
    it.addr = addr;
    it.num_bytes = 8;
    it.data = (uint8_t *)&v;
    rc = tas_rw_exec(c, &it, 1, maxrw, &pe);
    printf("  map %2u @ 0x%08X (RD64): rc %2d ok %d pl0 0x%02X value 0x%016llX\n",
           (unsigned)map, (unsigned)addr, rc, it.ok, (unsigned)pe,
           (unsigned long long)v);
}

static void probe_wr32(tas_conn *c, uint8_t map, uint32_t addr, uint32_t val,
                       uint32_t maxrw)
{
    tas_rw_item it;
    uint8_t pe = 0;
    int rc;

    memset(&it, 0, sizeof(it));
    it.addr_map = map;
    it.addr = addr;
    it.num_bytes = 4;
    it.is_write = 1;
    it.data = (uint8_t *)&val;
    rc = tas_rw_exec(c, &it, 1, maxrw, &pe);
    printf("  WR map %2u @ 0x%08X <- 0x%08X : rc %2d ok %d pl0 0x%02X\n",
           (unsigned)map, (unsigned)addr, (unsigned)val, rc, it.ok,
           (unsigned)pe);
}

int main(void)
{
    tas_conn c;
    tas_server_info srv;
    tas_con_info con;
    tas_target targets[TAS_MAX_TARGETS];
    int n = 0;
    uint8_t e = 0;
    uint16_t feat = 0;
    uint32_t dtype = 0;
    char detail[160];

    setvbuf(stdout, NULL, _IONBF, 0);

    /* the reference/smoke flows first run a separate "DeviceQry" session
     * (the MCDSRL qry_devices) before the main connection - test if that
     * session is what initializes the server side MCDS path */
    {
        tas_conn qc;
        tas_server_info qsrv;
        tas_target qt[TAS_MAX_TARGETS];
        int qn = 0;
        uint8_t qe = 0;

        detail[0] = 0;
        if (tas_connect(&qc, "localhost", TAS_DEFAULT_PORT, detail,
                        sizeof(detail)) == 0 &&
            tas_server_connect(&qc, "DeviceQry", "admin",
                               GetCurrentProcessId(), &qsrv, &qe) == 0 &&
            tas_get_targets(&qc, qt, TAS_MAX_TARGETS, &qn, &qe) == 0)
            printf("DeviceQry pre-session ok (targets %d)\n", qn);
        else
            printf("DeviceQry pre-session failed (tas_err 0x%02X)\n",
                   (unsigned)qe);
        tas_disconnect(&qc);
        Sleep(200);
    }

    detail[0] = 0;
    if (tas_connect(&c, "localhost", TAS_DEFAULT_PORT, detail, sizeof(detail)) != 0) {
        printf("connect failed: %s\n", detail);
        return 1;
    }
    if (tas_server_connect(&c, "MCDS recorder (MCDSRL-TAS prototype)", "admin", GetCurrentProcessId(), &srv, &e) != 0) {
        printf("server connect failed\n");
        return 1;
    }
    if (tas_get_targets(&c, targets, TAS_MAX_TARGETS, &n, &e) != 0 || n < 1) {
        printf("no targets\n");
        return 1;
    }
    if (tas_session_start(&c, targets[0].identifier, &con, &e) != 0) {
        printf("session failed (tas_err 0x%02X)\n", (unsigned)e);
        return 1;
    }
    printf("session ok, pl0_max_num_rw %u\n", (unsigned)con.pl0_max_num_rw);
    tas_set_device_key(&c, TAS_ULO_HOT_ATTACH, NULL, 0, &e);
    if (tas_device_connect(&c, 0, &feat, &dtype, &e) != 0) {
        printf("DC failed (tas_err 0x%02X)\n", (unsigned)e);
        return 1;
    }
    printf("connected, device_type 0x%08X\n\n", (unsigned)dtype);

    /* hypothesis: the very FIRST register access must be a target (map 0)
     * read of the MCDS area (0x50000000) - any earlier MCDS map (1/15)
     * access latches the MCDS path as broken for the session */
    printf("== first access: map 0 @ 0x50000000 ==\n");
    probe(&c, 0, 0x50000000u, con.pl0_max_num_rw);
    printf("-- map 15 with RD64 (the counter pattern): --\n");
    probe64(&c, 15, 0x50000090u, con.pl0_max_num_rw);/* the counter (RD64) */
    probe64(&c, 15, 0x50000000u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50000004u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50006000u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50006020u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50006034u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50008000u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50008018u, con.pl0_max_num_rw);
    printf("-- map 1 with RD64: --\n");
    probe64(&c, 1, 0xF00E6000u, con.pl0_max_num_rw);
    probe64(&c, 1, 0xF00E8000u, con.pl0_max_num_rw);
    printf("-- EMEM cross views (map 0): --\n");
    probe(&c, 0, 0xAF000000u, con.pl0_max_num_rw);
    probe(&c, 0, 0x50100000u, con.pl0_max_num_rw);
    probe(&c, 0, 0x50000000u, con.pl0_max_num_rw);

    /* the decisive test: the MCDS enable write (the first pair of the
     * reference config: 0xF00E6000 <- 0x00010000) via the map-15 view
     * (0x50006000), which is the working path for the timestamp counter */
    printf("\n== MCDS enable write via map 15 (0x50006000 <- 0x10000) ==\n");
    probe_wr32(&c, 15, 0x50006000u, 0x00010000u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50006000u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50006000u, con.pl0_max_num_rw);
    probe64(&c, 15, 0x50006034u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E6000u, con.pl0_max_num_rw);

    /* restore the device state (the MCDS configuration is volatile and is
     * cleared by the reset) */
    printf("\n== restore: reset (DC option 3) ==\n");
    {
        uint8_t e2 = 0;
        uint16_t f2 = 0;
        uint32_t d2 = 0;

        if (tas_device_connect(&c, 3, &f2, &d2, &e2) == 0)
            printf("  reset+halt ok\n");
        else
            printf("  reset failed (tas_err 0x%02X)\n", (unsigned)e2);
    }

    printf("\n== map 1 (MCDS registers per the reference config) ==\n");
    probe(&c, 1, 0xF00E6000u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E6004u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E6020u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E6034u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E8000u, con.pl0_max_num_rw);
    probe(&c, 1, 0xF00E8018u, con.pl0_max_num_rw);
    probe(&c, 1, 0xAF000000u, con.pl0_max_num_rw);

    printf("\n== map 15 (server internal MCDS view) ==\n");
    probe(&c, 15, 0x50000000u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50000004u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50000090u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50100000u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50006000u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50008000u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50006020u, con.pl0_max_num_rw);
    probe(&c, 15, 0x50006034u, con.pl0_max_num_rw);
    probe(&c, 15, 0x000C0100u, con.pl0_max_num_rw);

    printf("\n== map 0 (target view) ==\n");
    probe(&c, 0, 0xF00E6000u, con.pl0_max_num_rw);
    probe(&c, 0, 0x50000000u, con.pl0_max_num_rw);
    probe(&c, 0, 0xAF000000u, con.pl0_max_num_rw);
    probe(&c, 0, 0x9F000000u, con.pl0_max_num_rw);

    tas_disconnect(&c);
    return 0;
}
