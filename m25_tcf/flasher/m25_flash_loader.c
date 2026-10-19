/*
 * m25_flash_loader.c - M25-4b: our own TC37x PFLASH flash loader.
 *
 * Runs on CPU0 from PSPR (linked at 0x70100000), driven by the host (TCF agent)
 * through a mailbox in DSPR at 0x7000F000.
 *
 * Flash command sequences derived from the Infineon iLLD IfxFlash driver for
 * TC3xx (distributed under the Boost Software License 1.0):
 *   - command interface : 0xAF000000 | {0x5554, 0xAA50, 0xAA58, 0xAAA8, 0x55F0}
 *   - DMU status / errsr: 0xF8040010 / 0xF8040034  (P0BUSY = bit2, P1BUSY = bit3)
 *   - safety ENDINIT    : SCU WDTS_CON0 @ 0xF00362A8 (ENDINIT[0], LCK[1], PW[15:2])
 *
 * Mailbox protocol (32-bit words at 0x7000F000):
 *   [0] magic    host:0 -> loader:0x4D32354D ("M25M") when ready
 *   [1] cmd      0=idle 1=erase 2=program 3=exit
 *   [2] arg0     erase: sector addr (0xA0xxxxxx) ; program: page addr
 *   [3] arg1     erase: sector count               ; program: byte count (n*32)
 *   [4] arg2     program: source addr in DSPR
 *   [5] status   0=ok else DMU_ERRSR value
 *   [6] done     0=busy 1=command finished
 *   [7] progress units processed (sectors / pages)
 */

typedef unsigned int u32;

#define MB            ((volatile u32 *)0x7000F000u)
#define CMD_BASE      0xAF000000u
#define DMU_STATUS    (*(volatile u32 *)0xF8040010u)
#define DMU_ERRSR     (*(volatile u32 *)0xF8040034u)
#define WDTS_CON0     (*(volatile u32 *)0xF00362A8u)
#define WDTS_CON1     (*(volatile u32 *)0xF00362ACu)
#define WDTCPU0_CON0  (*(volatile u32 *)0xF003624Cu)
#define WDTCPU0_CON1  (*(volatile u32 *)0xF0036250u)

#define CMD_IDLE      0u
#define CMD_ERASE     1u
#define CMD_PROGRAM   2u
#define CMD_EXIT      3u

#define MAGIC         0x4D32354Du

#define MB_MAGIC      0
#define MB_CMD        1
#define MB_ARG0       2
#define MB_ARG1       3
#define MB_ARG2       4
#define MB_STATUS     5
#define MB_DONE       6
#define MB_PROGRESS   7

#define PFLASH_SECTOR_SIZE 0x4000u   /* 16 KiB logical sector */
#define PFLASH_PAGE_SIZE   32u       /* 32 bytes per program page */

static void dsync(void)
{
    __asm__ volatile ("dsync");
}

/* ---- flash command interface (iLLD sequences) ---- */

static void flash_cmd(u32 v)
{
    *(volatile u32 *)(CMD_BASE | 0x5554u) = v;
    dsync();
}

static void flash_cmd2(u32 p1, u32 p2, u32 c1, u32 c2)
{
    *(volatile u32 *)(CMD_BASE | 0xAA50u) = p1;
    *(volatile u32 *)(CMD_BASE | 0xAA58u) = p2;
    *(volatile u32 *)(CMD_BASE | 0xAAA8u) = c1;
    *(volatile u32 *)(CMD_BASE | 0xAAA8u) = c2;
    dsync();
}

static void load_page(u32 w0, u32 w1)
{
    volatile u32 *a = (volatile u32 *)(CMD_BASE | 0x55F0u);
    a[0] = w0;
    dsync();               /* purge store buffers: avoid 64-bit merging */
    a[1] = w1;
    dsync();
}

static void wait_unbusy(void)
{
    /* wait until both program flash banks are idle */
    while (DMU_STATUS & ((1u << 2) | (1u << 3))) { }
    dsync();
}

/* ---- watchdog handling (standard flasher practice) ---- */

/* clear ENDINIT of a WDT CON0 (returns password used) */
static u32 wdt_clear_endinit(volatile u32 *con0_reg)
{
    u32 con0 = *con0_reg;
    u32 pw   = (con0 >> 2) & 0x3FFFu;
    u32 rel  = con0 & 0xFFFF0000u;
    if (con0 & (1u << 1))                       /* LCK set: password access pattern */
        *con0_reg = (1u << 0) | (0u << 1) | (pw << 2) | rel;
    *con0_reg = (0u << 0) | (1u << 1) | (pw << 2) | rel;   /* ENDINIT=0, LCK=1 */
    dsync();
    while (*con0_reg & 1u) { }                  /* wait until ENDINIT is cleared */
    return pw;
}

static void wdt_set_endinit(volatile u32 *con0_reg, u32 pw)
{
    u32 rel = *con0_reg & 0xFFFF0000u;
    *con0_reg = (1u << 0) | (1u << 1) | (pw << 2) | rel;   /* ENDINIT=1, LCK=1 */
    dsync();
}

/* disable the CPU0 watchdog and the safety watchdog (like professional flashers) */
static void wdt_disable(void)
{
    u32 pw;
    pw = wdt_clear_endinit(&WDTCPU0_CON0);
    WDTCPU0_CON1 = WDTCPU0_CON1 | (1u << 3);   /* DR = 1 (disable request) */
    wdt_set_endinit(&WDTCPU0_CON0, pw);
    pw = wdt_clear_endinit(&WDTS_CON0);
    WDTS_CON1 = WDTS_CON1 | (1u << 3);         /* DR = 1 */
    wdt_set_endinit(&WDTS_CON0, pw);
}

/* ---- safety ENDINIT handling (SCU WDTS) ---- */

static void clear_endinit(void)
{
    u32 con0 = WDTS_CON0;
    u32 pw   = (con0 >> 2) & 0x3FFFu;
    u32 rel  = con0 & 0xFFFF0000u;
    if (con0 & (1u << 1))                       /* LCK set: password access pattern */
        WDTS_CON0 = (1u << 0) | (0u << 1) | (pw << 2) | rel;
    WDTS_CON0 = (0u << 0) | (1u << 1) | (pw << 2) | rel;   /* ENDINIT=0, LCK=1 */
    dsync();
    while (WDTS_CON0 & 1u) { }                  /* wait until ENDINIT is cleared */
}

static void set_endinit(void)
{
    u32 con0 = WDTS_CON0;
    u32 pw   = (con0 >> 2) & 0x3FFFu;
    u32 rel  = con0 & 0xFFFF0000u;
    WDTS_CON0 = (1u << 0) | (1u << 1) | (pw << 2) | rel;   /* ENDINIT=1, LCK=1 */
    dsync();
}

/* ---- operations ---- */

static u32 erase_one_sector(u32 sector_addr)
{
    u32 err;
    flash_cmd(0xFA);                                  /* clear status */
    flash_cmd2(sector_addr, 1u, 0x80u, 0x50u);        /* erase sector */
    wait_unbusy();
    err = DMU_ERRSR;
    flash_cmd(0xF0);                                  /* reset to read mode */
    return err;
}

static u32 program_one_page(u32 page_addr, const u32 *src)
{
    u32 err;
    flash_cmd(0xFA);                                  /* clear status */
    if ((page_addr & 0xFF000000u) == 0xA0000000u)
        flash_cmd(0x50);                              /* enter page mode (PFLASH) */
    else
        flash_cmd(0x5D);                              /* enter page mode (DFLASH) */
    load_page(src[0], src[1]);
    load_page(src[2], src[3]);
    load_page(src[4], src[5]);
    load_page(src[6], src[7]);
    flash_cmd2(page_addr, 0u, 0xA0u, 0xAAu);          /* write page */
    wait_unbusy();
    err = DMU_ERRSR;
    flash_cmd(0xF0);                                  /* reset to read mode */
    return err;
}

/* ---- entry point ---- */

void _start(void);

void _start(void)
{
    MB[MB_STATUS]   = 0u;
    MB[MB_DONE]     = 0u;
    MB[MB_PROGRESS] = 0u;
    MB[MB_CMD]      = 0u;
    wdt_disable();                 /* stop the app watchdogs before flashing */
    MB[MB_MAGIC]    = MAGIC;

    for (;;) {
        u32 cmd = MB[MB_CMD];
        u32 st  = 0u;

        if (cmd == CMD_IDLE) {
            continue;
        }

        clear_endinit();

        if (cmd == CMD_ERASE) {
            u32 addr = MB[MB_ARG0];
            u32 n    = MB[MB_ARG1];
            u32 i;
            for (i = 0u; i < n; i++) {
                st = erase_one_sector(addr + i * PFLASH_SECTOR_SIZE);
                MB[MB_PROGRESS] = i + 1u;
                if (st != 0u) {
                    break;
                }
            }
        }
        else if (cmd == CMD_PROGRAM) {
            u32 addr  = MB[MB_ARG0];
            u32 size  = MB[MB_ARG1];
            u32 src   = MB[MB_ARG2];
            u32 pages = (size + PFLASH_PAGE_SIZE - 1u) / PFLASH_PAGE_SIZE;
            u32 i;
            for (i = 0u; i < pages; i++) {
                st = program_one_page(addr + i * PFLASH_PAGE_SIZE,
                                      (const u32 *)(src + i * PFLASH_PAGE_SIZE));
                MB[MB_PROGRESS] = i + 1u;
                if (st != 0u) {
                    break;
                }
            }
        }
        else if (cmd == CMD_EXIT) {
            set_endinit();
            MB[MB_STATUS] = 0u;
            MB[MB_DONE]   = 1u;
            MB[MB_CMD]    = 0u;
            for (;;) { }                                  /* idle forever */
        }
        else {
            st = 0xFFFFFFFFu;                             /* unknown command */
        }

        set_endinit();
        MB[MB_STATUS] = st;
        MB[MB_DONE]   = 1u;
        MB[MB_CMD]    = 0u;
    }
}
