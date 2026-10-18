/* halt_test.cpp - M17 follow-up experiment:
 * Can "reset and run" be implemented as DC(0x0003) (reset+halt) followed by
 * a DBGSR run write (0x4)?  That would leave DBGSR.DE == 1, so a later stop
 * (DBGSR halt write 0x6) works.  DC(0x0001) (reset, run) leaves DE == 0 and
 * the DBGSR halt write is ignored by hardware.
 *
 * Read-only except: DC options and DBGSR writes.  Ends with the target
 * running (as found). */
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include "tas_client.h"

static int rd32(tas_conn *c, uint32_t addr, uint32_t *v, uint32_t maxrw)
{
    tas_rw_item it;
    uint8_t pe = 0;
    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.data = (uint8_t *)v;
    if (tas_rw_exec(c, &it, 1, maxrw, &pe) != 0 || !it.ok) {
        printf("   (rd32 @0x%08X failed, pl0_err 0x%02x)\n", addr, (unsigned)pe);
        return -1;
    }
    return 0;
}

static int wr32(tas_conn *c, uint32_t addr, uint32_t v, uint32_t maxrw)
{
    tas_rw_item it;
    uint8_t pe = 0;
    memset(&it, 0, sizeof(it));
    it.addr_map = TAS_AM0;
    it.addr = addr;
    it.num_bytes = 4;
    it.is_write = 1;
    it.data = (uint8_t *)&v;
    if (tas_rw_exec(c, &it, 1, maxrw, &pe) != 0 || !it.ok) {
        printf("   (wr32 @0x%08X=0x%08X failed, pl0_err 0x%02x)\n",
               addr, v, (unsigned)pe);
        return -1;
    }
    return 0;
}

static void show_dbgsr(tas_conn *c, const char *tag, uint32_t maxrw)
{
    uint32_t v = 0;
    if (rd32(c, 0xF881FD00u, &v, maxrw) == 0)
        printf("%s DBGSR=0x%08X (DE=%u HALT=%u SIH=%u EVTSRC=%u)\n",
               tag, v, v & 1u, (v >> 1) & 3u, (v >> 3) & 1u, (v >> 8) & 0x1Fu);
}

int main(void)
{
    tas_conn c;
    tas_server_info si;
    tas_target t[4];
    int nt = 0;
    uint8_t te = 0;
    tas_con_info ci;
    uint16_t feat = 0;
    uint32_t dt = 0;
    char err[256];

    if (tas_connect(&c, "localhost", 24817, err, sizeof(err)) != 0) {
        printf("connect failed: %s\n", err);
        return 1;
    }
    if (tas_server_connect(&c, "mcd_tas halt_test", "halt_test",
                           GetCurrentProcessId(), &si, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("SERVER_CONNECT failed te=0x%02X\n", (unsigned)te);
        return 1;
    }
    if (tas_get_targets(&c, t, 4, &nt, &te) != 0 || nt < 1) {
        printf("GET_TARGETS failed te=0x%02X nt=%d\n", (unsigned)te, nt);
        return 1;
    }
    if (tas_session_start(&c, t[0].identifier, &ci, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("SESSION_START failed te=0x%02X\n", (unsigned)te);
        return 1;
    }
    if (tas_set_device_key(&c, TAS_ULO_HOT_ATTACH, NULL, 0, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("SET_DEVICE_KEY failed te=0x%02X\n", (unsigned)te);
        return 1;
    }
    if (tas_device_connect(&c, 0x0000, &feat, &dt, &te) != 0 ||
        te != TAS_ERR_OK) {
        printf("DC(hot attach) failed te=0x%02X\n", (unsigned)te);
        return 1;
    }
    printf("attached (device 0x%08X, max_rw %u)\n", dt,
           (unsigned)ci.pl0_max_num_rw);
    show_dbgsr(&c, "start   :", ci.pl0_max_num_rw);

    /* A: reset and halt */
    feat = 0;
    if (tas_device_connect(&c, 0x0003, &feat, &dt, &te) != 0 ||
        te != TAS_ERR_OK)
        printf("DC(0x0003) failed te=0x%02X\n", (unsigned)te);
    else
        printf("DC(0x0003) feat_used=0x%04X\n", feat);
    Sleep(200);
    show_dbgsr(&c, "rst+hlt :", ci.pl0_max_num_rw);

    /* B: run write (clear halt) */
    printf("wr32 DBGSR <- 0x4 ...\n");
    wr32(&c, 0xF881FD00u, 0x00000004u, ci.pl0_max_num_rw);
    show_dbgsr(&c, "run     :", ci.pl0_max_num_rw);

    /* C: stop write (set halt) — stop-after-reset simulation */
    printf("wr32 DBGSR <- 0x6 ...\n");
    wr32(&c, 0xF881FD00u, 0x00000006u, ci.pl0_max_num_rw);
    show_dbgsr(&c, "stop    :", ci.pl0_max_num_rw);

    /* D: restore run (as found) */
    printf("wr32 DBGSR <- 0x4 (restore) ...\n");
    wr32(&c, 0xF881FD00u, 0x00000004u, ci.pl0_max_num_rw);
    show_dbgsr(&c, "restore :", ci.pl0_max_num_rw);

    tas_disconnect(&c);
    printf("done\n");
    return 0;
}
