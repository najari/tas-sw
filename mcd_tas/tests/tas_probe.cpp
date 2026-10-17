/* tas_probe.cpp - protocol level probe for the M2 tas_client code.
 *
 * Default: READ-ONLY test.  It performs exactly the message sequence
 * that the reference MCD client performs on connection setup
 * (SERVER_CONNECT, GET_TARGETS, SESSION_START, PING) plus PL0 reads of
 * server-internal AM15 registers/descriptor blocks (the same reads the
 * reference client does at startup).  It never sends DEVICE_CONNECT and
 * never touches the target address space (AM0).
 *
 * Option --idcode adds one write-class hardware test (equivalent to the
 * official tasutil_jtag_scan with TJS_TAP_RESET): enable native JTAG
 * scan mode, TAP controller reset, capture 32 scan bits, shift zeros in
 * while reading the 32 shifted-out bits (the DR after TAP reset, i.e.
 * the IDCODE) and disable scan mode again.  It does not reset or halt
 * the target; only the JTAG TAP state machine is cycled.  Run it only
 * with the user's approval, like other hardware-touching tests. */
#define WIN32_LEAN_AND_MEAN
#include "tas_client.h"          /* winsock2 + ws2tcpip              */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fails = 0;
#define CHK(cond, msg) do { \
    if (cond) printf("  [ok]   %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fails++; } \
} while (0)

int main(int argc, char **argv)
{
    const char *host = "localhost";
    uint16_t    port = TAS_DEFAULT_PORT;
    char        err[256] = "";
    int         do_idcode = 0;
    int         do_force_devtype = 0, do_clear_devtype = 0;
    int         do_blink = 0;
    int         blink_n = 8, blink_ms = 250;
    int         ai, host_set = 0, port_set = 0;

    for (ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--idcode") == 0) { do_idcode = 1; continue; }
        if (strcmp(argv[ai], "--force-devtype") == 0) { do_force_devtype = 1; continue; }
        if (strcmp(argv[ai], "--clear-devtype") == 0) { do_clear_devtype = 1; continue; }
        if (strcmp(argv[ai], "--blink") == 0) { do_blink = 1; continue; }
        if (strcmp(argv[ai], "--blink-n") == 0 && ai + 1 < argc) { blink_n = atoi(argv[++ai]); continue; }
        if (strcmp(argv[ai], "--blink-ms") == 0 && ai + 1 < argc) { blink_ms = atoi(argv[++ai]); continue; }
        if (!host_set) { host = argv[ai]; host_set = 1; continue; }
        if (!port_set) { port = (uint16_t)atoi(argv[ai]); port_set = 1; }
    }
    if (blink_n < 1) blink_n = 1;
    if (blink_n > 2000) blink_n = 2000;
    if (blink_ms < 10) blink_ms = 10;
    if (blink_ms > 10000) blink_ms = 10000;

    tas_conn       c;
    tas_server_info srv;
    tas_target      targets[TAS_MAX_TARGETS];
    tas_con_info    con;
    int             n = 0;
    int             i;
    uint8_t         te = 0;

    printf("== mcd_tas protocol probe (read-only) ==\n");
    printf("server: %s:%u\n\n", host, (unsigned)port);

    /* 1. transport + SERVER_CONNECT */
    if (tas_connect(&c, host, port, err, sizeof(err)) != 0) {
        printf("  [FAIL] connect: %s\n", err);
        return 2;
    }
    memset(&srv, 0, sizeof(srv));
    if (tas_server_connect(&c, "mcd_tas probe", "probe", GetCurrentProcessId(),
                           &srv, &te) != 0 || te != TAS_ERR_OK) {
        printf("  [FAIL] SERVER_CONNECT (tas_err=0x%02x)\n", te);
        tas_disconnect(&c);
        return 2;
    }
    printf("  [ok]   SERVER_CONNECT: '%s' ver %u.%u protos 0x%08x date '%s'\n",
           srv.server, (unsigned)srv.ver_a, (unsigned)srv.ver_b,
           (unsigned)srv.protocols, srv.date);

    /* 2. GET_TARGETS */
    if (tas_get_targets(&c, targets, TAS_MAX_TARGETS, &n, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("  [FAIL] GET_TARGETS (tas_err=0x%02x)\n", te);
        tas_disconnect(&c);
        return 2;
    }
    printf("  [ok]   GET_TARGETS: %d target(s)\n", n);
    for (i = 0; i < n; i++) {
        printf("           [%d] '%s' devtype 0x%08X phys %u nclient %u\n",
               i, targets[i].identifier, (unsigned)targets[i].device_type,
               (unsigned)targets[i].phys, (unsigned)targets[i].nclient);
    }
    if (n < 1) {
        printf("  no target -> session tests skipped\n");
        tas_disconnect(&c);
        printf("\n== %s (%d failure(s)) ==\n", g_fails ? "PROBE FAILED" : "PROBE OK", g_fails);
        return g_fails ? 1 : 0;
    }

    /* 3. SESSION_START */
    if (tas_session_start(&c, targets[0].identifier, &con, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("  [FAIL] SESSION_START '%s' (tas_err=0x%02x)\n",
               targets[0].identifier, te);
        tas_disconnect(&c);
        return 2;
    }
    printf("  [ok]   SESSION_START '%s'\n", targets[0].identifier);
    printf("           max pl2 rq %u rsp %u, devtype 0x%08X, max_rw %u,\n"
           "           rw_mode_mask 0x%04x addr_map_mask 0x%04x, hash %s (0x%08X),\n"
           "           identifier '%s'\n",
           (unsigned)con.max_pl2rq_pkt_size, (unsigned)con.max_pl2rsp_pkt_size,
           (unsigned)con.device_type, (unsigned)con.pl0_max_num_rw,
           (unsigned)con.pl0_rw_mode_mask, (unsigned)con.pl0_addr_map_mask,
           con.hash_str, (unsigned)con.device_id_hash, con.identifier);
    printf("           device_id: %08X %08X %08X %08X\n",
           (unsigned)con.device_id[0], (unsigned)con.device_id[1],
           (unsigned)con.device_id[2], (unsigned)con.device_id[3]);

    /* 4. PING (session snapshot refresh) */
    if (tas_ping_con(&c, &con, &te) != 0 || te != TAS_ERR_OK) {
        printf("  [FAIL] PING (tas_err=0x%02x)\n", te);
        tas_disconnect(&c);
        return 2;
    }
    printf("  [ok]   PING (devtype 0x%08X)\n", (unsigned)con.device_type);

    /* 5. PL0 reads, AM15 only (server internal; no target access) */
    {
        uint8_t data[1024];
        tas_rw_item item;
        uint8_t pl0_err = 0;
        int trc;

        /* 5a. adapter hardware frequency: base 0x000C0000, off 0x0100 */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x000C0100ull;
        item.num_bytes = 4;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        CHK(trc == 0 && item.ok,
            "AM15 RD32 @0x000C0100 (ACC_HW_FREQUENCY)");
        if (trc == 0 && item.ok)
            printf("           value = %u Hz\n",
                   (unsigned)(data[0] | (data[1] << 8) | (data[2] << 16) | ((unsigned)data[3] << 24)));

        /* 5b. target descriptor block @0xD000, 104 bytes */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0xD000ull;
        item.num_bytes = 104;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        CHK(trc == 0 && item.ok, "AM15 RDBLK @0xD000 (104 bytes)");
        if (trc == 0 && item.ok) {
            uint32_t dt = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                          ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            int has_family = 0;
            for (i = 0; i + 13 <= 104; i++)
                if (memcmp(data + i, "TriCore-Family", 14) == 0)
                    has_family = 1;
            printf("           block devtype = 0x%08X, 'TriCore-Family' present: %s\n",
                   (unsigned)dt, has_family ? "yes" : "no");
        }

        /* 5c. access HW descriptor block @0x8000, 168 bytes */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x8000ull;
        item.num_bytes = 168;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        CHK(trc == 0 && item.ok, "AM15 RDBLK @0x8000 (168 bytes)");
        if (trc == 0 && item.ok) {
            char asc[169];
            for (i = 0; i < 168; i++)
                asc[i] = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
            asc[168] = 0;
            printf("           content: %.80s...\n", asc);
        }

        /* 5d-5g. adapter state diagnostics (informational, read-only AM15) */

        /* 5d. JTAG ID which the access HW has read (0 = none read yet) */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x00DD1000ull;
        item.num_bytes = 4;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && item.ok) {
            uint32_t idc = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            printf("  [info] AM15 RD32 @0x00DD1000 (DEVICE_TYPE_ACC_HW) = 0x%08X%s\n",
                   (unsigned)idc, idc ? "" : "   <- access HW has read no JTAG ID");
        } else {
            printf("  [info] AM15 RD32 @0x00DD1000 failed (trc %d pl0 0x%02x)\n", trc, pl0_err);
        }

        /* 5e. user pins (value + mask) */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x000A8000ull;
        item.num_bytes = 8;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && item.ok) {
            uint32_t up_val = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                              ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            uint32_t up_msk = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                              ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
            printf("  [info] AM15 RD64 @0x000A8000 (USERPINS) value 0x%08X mask 0x%08X\n",
                   (unsigned)up_val, (unsigned)up_msk);
        } else {
            printf("  [info] AM15 RD64 @0x000A8000 failed (trc %d pl0 0x%02x)\n", trc, pl0_err);
        }

        /* 5f. reset counters (porst, reset) */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x000A1800ull;
        item.num_bytes = 8;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && item.ok) {
            uint32_t porst = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                             ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            uint32_t rst = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                           ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
            printf("  [info] AM15 RD64 @0x000A1800 (RESET_COUNT) porst %u reset %u\n",
                   (unsigned)porst, (unsigned)rst);
        } else {
            printf("  [info] AM15 RD64 @0x000A1800 failed (trc %d pl0 0x%02x)\n", trc, pl0_err);
        }

        /* 5g. currently set device type (0 = auto from access HW) */
        memset(data, 0, sizeof(data));
        memset(&item, 0, sizeof(item));
        item.addr_map = TAS_AM15;
        item.addr = 0x00DD1080ull;
        item.num_bytes = 4;
        item.is_write = 0;
        item.data = data;
        trc = tas_rw_exec(&c, &item, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && item.ok) {
            uint32_t dst = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            printf("  [info] AM15 RD32 @0x00DD1080 (DEVICE_TYPE_SET) = 0x%08X\n", (unsigned)dst);
        } else {
            printf("  [info] AM15 RD32 @0x00DD1080 failed (trc %d pl0 0x%02x)\n", trc, pl0_err);
        }
    }

    /* 6. optional modifier: force / clear DEVICE_TYPE_SET (AM15 write).
     * DEVICE_TYPE_SET != 0 makes the TasServer operate the device as the
     * given type and keep the access HW output drivers enabled even if no
     * device is sensed.  Used to test whether the missing JTAG response
     * is a driver-enable (VTref sense) issue. */
    if (do_force_devtype || do_clear_devtype) {
        uint32_t nv = do_force_devtype ? 0x401DA083u : 0u;
        uint8_t wb[4], rb[4];
        tas_rw_item it1;
        uint8_t pl0_err = 0;
        int trc;

        wb[0] = (uint8_t)(nv & 0xff);
        wb[1] = (uint8_t)((nv >> 8) & 0xff);
        wb[2] = (uint8_t)((nv >> 16) & 0xff);
        wb[3] = (uint8_t)((nv >> 24) & 0xff);

        memset(rb, 0, sizeof(rb));
        memset(&it1, 0, sizeof(it1));
        it1.addr_map = TAS_AM15; it1.addr = 0x00DD1080ull; it1.num_bytes = 4;
        it1.is_write = 0; it1.data = rb;
        trc = tas_rw_exec(&c, &it1, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && it1.ok)
            printf("\n  [dt]   DEVICE_TYPE_SET before = 0x%02X%02X%02X%02X\n",
                   rb[3], rb[2], rb[1], rb[0]);

        memset(&it1, 0, sizeof(it1));
        it1.addr_map = TAS_AM15; it1.addr = 0x00DD1080ull; it1.num_bytes = 4;
        it1.is_write = 1; it1.data = wb;
        trc = tas_rw_exec(&c, &it1, 1, con.pl0_max_num_rw, &pl0_err);
        printf("  [dt]   write DEVICE_TYPE_SET = 0x%08X: %s (rc %d pl0 0x%02x)\n",
               (unsigned)nv, (trc == 0 && it1.ok) ? "ok" : "FAILED", trc, pl0_err);

        memset(rb, 0, sizeof(rb));
        memset(&it1, 0, sizeof(it1));
        it1.addr_map = TAS_AM15; it1.addr = 0x00DD1080ull; it1.num_bytes = 4;
        it1.is_write = 0; it1.data = rb;
        trc = tas_rw_exec(&c, &it1, 1, con.pl0_max_num_rw, &pl0_err);
        if (trc == 0 && it1.ok)
            printf("  [dt]   DEVICE_TYPE_SET after  = 0x%02X%02X%02X%02X\n",
                   rb[3], rb[2], rb[1], rb[0]);
    }

    /* 7. optional (--blink): toggle RUNLED (user pin 0x8000, low active)
     * several times.  Purely an access HW user-pin write; it does not
     * touch the target.  Purpose: a visible "the adapter executes
     * commands" indicator plus per-write ack diagnostics. */
    if (do_blink) {
        tas_rw_item bit;
        uint8_t pl0_err = 0;
        int trc, k;
        uint8_t on8[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00 };
        uint8_t off8[8] = { 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00 };

        printf("\n  [blink] RUNLED toggle test (value+mask W64 @0x000A8000, %dx %dms)\n",
               blink_n, blink_ms);
        for (k = 0; k < blink_n; k++) {
            uint8_t *b = (k & 1) ? off8 : on8;
            memset(&bit, 0, sizeof(bit));
            bit.addr_map = TAS_AM15; bit.addr = 0x000A8000ull; bit.num_bytes = 8;
            bit.is_write = 1; bit.data = b;
            trc = tas_rw_exec(&c, &bit, 1, con.pl0_max_num_rw, &pl0_err);
            printf("           toggle %d (drive %s): %s (rc %d pl0 0x%02x)\n",
                   k, (k & 1) ? "high" : "low(led on)",
                   (trc == 0 && bit.ok) ? "ok" : "FAILED", trc, pl0_err);
            Sleep(blink_ms);
        }
    }

    /* 8. optional (--idcode): native JTAG scan -> direct IDCODE read.
     * Kept strictly opt-in because it writes to the access HW (scan
     * mode / TAP state machine), although target execution state is
     * not touched. */
    if (do_idcode) {
        uint8_t  en4[4]  = { 1, 0, 0, 0 };     /* enable (value reserved) */
        uint8_t  z4[4]   = { 0, 0, 0, 0 };
        uint8_t  cap4[4] = { 32, 0, 0, 0 };    /* num_scan_bits          */
        uint64_t sh_in = 0, sh_out = 0;
        tas_rw_item it[6];
        uint8_t pl0_err = 0;
        int trc;

        printf("\n  [scan] native JTAG scan: enable, TAP reset, capture 32, shift, read, disable\n");
        memset(it, 0, sizeof(it));
        it[0].addr_map = TAS_AM15; it[0].addr = 0x000C1100ull; it[0].num_bytes = 4;
        it[0].is_write = 1; it[0].data = en4;      /* scan mode enable   */
        it[1].addr_map = TAS_AM15; it[1].addr = 0x000C1800ull; it[1].num_bytes = 4;
        it[1].is_write = 1; it[1].data = z4;       /* TAP controller rst */
        it[2].addr_map = TAS_AM15; it[2].addr = 0x000C1400ull; it[2].num_bytes = 4;
        it[2].is_write = 1; it[2].data = cap4;     /* capture 32 bits    */
        it[3].addr_map = TAS_AM15; it[3].addr = 0x000C1500ull; it[3].num_bytes = 8;
        it[3].is_write = 1; it[3].data = (uint8_t *)&sh_in;   /* start shift */
        it[4].addr_map = TAS_AM15; it[4].addr = 0x000C1500ull; it[4].num_bytes = 8;
        it[4].is_write = 0; it[4].data = (uint8_t *)&sh_out;  /* shifted out */
        it[5].addr_map = TAS_AM15; it[5].addr = 0x000C1200ull; it[5].num_bytes = 4;
        it[5].is_write = 1; it[5].data = z4;       /* scan mode disable  */

        trc = tas_rw_exec(&c, it, 6, con.pl0_max_num_rw, &pl0_err);
        printf("           trans rc %d pl0 0x%02x\n", trc, pl0_err);
        printf("           item ok/err: en %d/0x%02x  rst %d/0x%02x  cap %d/0x%02x  "
               "wr %d/0x%02x  rd %d/0x%02x  dis %d/0x%02x\n",
               it[0].ok, it[0].pl_err, it[1].ok, it[1].pl_err, it[2].ok, it[2].pl_err,
               it[3].ok, it[3].pl_err, it[4].ok, it[4].pl_err, it[5].ok, it[5].pl_err);
        if (trc == 0 && it[4].ok) {
            uint32_t idc = (uint32_t)(sh_out & 0xFFFFFFFFu);
            printf("  [scan] IDCODE = 0x%08X", (unsigned)idc);
            if (idc == 0x401DA083u)
                printf("   <- MATCH: JTAG link ALIVE (TC27x)\n");
            else if (idc == 0x00000000u || idc == 0xFFFFFFFFu)
                printf("   <- TDO static (no target driving the scan chain)\n");
            else
                printf("   <- unexpected value (raw shifted-out data)\n");
        } else {
            printf("  [scan] shift read item failed (trc %d pl0 0x%02x)\n", trc, pl0_err);
        }

        /* safety: make sure scan mode is off again (idempotent write) */
        memset(it, 0, sizeof(it));
        it[0].addr_map = TAS_AM15; it[0].addr = 0x000C1200ull; it[0].num_bytes = 4;
        it[0].is_write = 1; it[0].data = z4;
        trc = tas_rw_exec(&c, it, 1, con.pl0_max_num_rw, &pl0_err);
        printf("  [scan] scan-mode disable re-assert: %s (rc %d pl0 0x%02x)\n",
               (trc == 0 && it[0].ok) ? "ok" : "failed", trc, pl0_err);

        /* post-scan snapshot of the JTAG ID the access HW holds */
        {
            uint8_t d2[4];
            uint32_t v;
            memset(d2, 0, sizeof(d2));
            memset(it, 0, sizeof(it));
            it[0].addr_map = TAS_AM15; it[0].addr = 0x00DD1000ull; it[0].num_bytes = 4;
            it[0].is_write = 0; it[0].data = d2;
            trc = tas_rw_exec(&c, it, 1, con.pl0_max_num_rw, &pl0_err);
            if (trc == 0 && it[0].ok) {
                v = (uint32_t)d2[0] | ((uint32_t)d2[1] << 8) |
                    ((uint32_t)d2[2] << 16) | ((uint32_t)d2[3] << 24);
                printf("  [scan] post-scan DEVICE_TYPE_ACC_HW = 0x%08X\n", (unsigned)v);
            }
        }
    }

    tas_disconnect(&c);
    printf("\n== %s (%d failure(s)) ==\n",
           g_fails ? "PROBE FAILED" : "PROBE OK", g_fails);
    return g_fails ? 1 : 0;
}
