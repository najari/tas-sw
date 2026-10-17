/* ftdi_jtag_scan.cpp - direct FTDI (D2XX) JTAG IDCODE scan, bypassing DAS/TAS.
 *
 * Opens the "DAS JTAG AURDUINO" FTDI channel with D2XX, switches to async
 * bit-bang mode and performs a plain 1149.1 TAP reset + IDCODE shift.
 * Read-only: no IR loads, no target reset. Used to decide whether the
 * adapter's JTAG output stage + ribbon + target TAP are electrically alive
 * independently of the DAS/TAS server stack.
 *
 * usage: ftdi_jtag_scan.exe [--channel A|B] [--tck N] [--tdi N] [--tdo N]
 *                           [--tms N] [--passes N] [--list]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *FT_HANDLE;

typedef struct _FT_DEVICE_LIST_INFO_NODE {
    DWORD Flags;
    DWORD Type;
    DWORD Id;
    DWORD LocId;
    char  SerialNumber[16];
    char  Description[64];
    FT_HANDLE ftHandle;
} FT_DEVICE_LIST_INFO_NODE;

typedef DWORD (WINAPI *PFN_CreateDeviceInfoList)(LPDWORD);
typedef DWORD (WINAPI *PFN_GetDeviceInfoList)(FT_DEVICE_LIST_INFO_NODE *, LPDWORD);
typedef DWORD (WINAPI *PFN_OpenEx)(PVOID, DWORD, FT_HANDLE *);
typedef DWORD (WINAPI *PFN_Close)(FT_HANDLE);
typedef DWORD (WINAPI *PFN_ResetDevice)(FT_HANDLE);
typedef DWORD (WINAPI *PFN_Purge)(FT_HANDLE, DWORD);
typedef DWORD (WINAPI *PFN_SetTimeouts)(FT_HANDLE, DWORD, DWORD);
typedef DWORD (WINAPI *PFN_SetLatencyTimer)(FT_HANDLE, UCHAR);
typedef DWORD (WINAPI *PFN_SetBitMode)(FT_HANDLE, UCHAR, UCHAR);
typedef DWORD (WINAPI *PFN_Write)(FT_HANDLE, LPVOID, DWORD, LPDWORD);
typedef DWORD (WINAPI *PFN_GetBitMode)(FT_HANDLE, PUCHAR);

static HMODULE g_mod;
static FT_HANDLE g_h;
static PFN_CreateDeviceInfoList FT_CreateDeviceInfoList;
static PFN_GetDeviceInfoList    FT_GetDeviceInfoList;
static PFN_OpenEx               FT_OpenEx;
static PFN_Close                FT_Close;
static PFN_ResetDevice          FT_ResetDevice;
static PFN_Purge                FT_Purge;
static PFN_SetTimeouts          FT_SetTimeouts;
static PFN_SetLatencyTimer      FT_SetLatencyTimer;
static PFN_SetBitMode           FT_SetBitMode;
static PFN_Write                FT_Write;
static PFN_GetBitMode           FT_GetBitMode;

static int g_tck = 0, g_tdi = 1, g_tdo = 2, g_tms = 3;
static unsigned char g_out;

static int load_d2xx(void)
{
    g_mod = LoadLibraryA("ftd2xx.dll");
    if (!g_mod) g_mod = LoadLibraryA("C:\\Program Files\\DAS64\\others\\driver\\ftdi\\amd64\\ftd2xx64.dll");
    if (!g_mod) { printf("  [x] cannot load ftd2xx.dll (GetLastError=%lu)\n", GetLastError()); return -1; }
#define GET(n, t) do { n = reinterpret_cast<t>(GetProcAddress(g_mod, #n)); if (!n) { printf("  [x] missing export: %s\n", #n); return -1; } } while (0)
    GET(FT_CreateDeviceInfoList, PFN_CreateDeviceInfoList);
    GET(FT_GetDeviceInfoList,    PFN_GetDeviceInfoList);
    GET(FT_OpenEx,               PFN_OpenEx);
    GET(FT_Close,                PFN_Close);
    GET(FT_ResetDevice,          PFN_ResetDevice);
    GET(FT_Purge,                PFN_Purge);
    GET(FT_SetTimeouts,          PFN_SetTimeouts);
    GET(FT_SetLatencyTimer,      PFN_SetLatencyTimer);
    GET(FT_SetBitMode,           PFN_SetBitMode);
    GET(FT_Write,                PFN_Write);
    GET(FT_GetBitMode,           PFN_GetBitMode);
#undef GET
    return 0;
}

static int ft_write1(unsigned char v)
{
    DWORD n = 0;
    g_out = v;
    if (FT_Write(g_h, &g_out, 1, &n) != 0 || n != 1) return -1;
    return 0;
}

/* one TCK cycle: setup TMS/TDI on falling edge, sample TDO after rising edge */
static int jtag_clk(int tms, int tdi)
{
    unsigned char v = 0, pins = 0;
    if (tms) v = (unsigned char)(v | (1u << g_tms));
    if (tdi) v = (unsigned char)(v | (1u << g_tdi));
    if (ft_write1(v) != 0) return -2;                                  /* TCK low  */
    if (ft_write1((unsigned char)(v | (1u << g_tck))) != 0) return -3; /* TCK high */
    if (FT_GetBitMode(g_h, &pins) != 0) return -4;
    if (ft_write1(v) != 0) return -5;                                  /* TCK low  */
    return (int)pins;
}

static int jtag_scan_once(unsigned long word[8])
{
    unsigned int i; int k; int r;
    for (k = 0; k < 8; k++) word[k] = 0;
    for (i = 0; i < 8; i++) {                       /* TAP reset (TMS=1 x8)  */
        r = jtag_clk(1, 1); if (r < 0) return r;
    }
    r = jtag_clk(0, 1); if (r < 0) return r;        /* Run-Test/Idle         */
    r = jtag_clk(1, 1); if (r < 0) return r;        /* Select-DR             */
    r = jtag_clk(0, 1); if (r < 0) return r;        /* Capture-DR            */
    r = jtag_clk(0, 0); if (r < 0) return r;        /* Shift-DR              */
    for (i = 0; i < 32; i++) {                      /* shift, capture all 8 pins */
        r = jtag_clk(0, 0);
        if (r < 0) return r;
        for (k = 0; k < 8; k++) word[k] |= (unsigned long)((r >> k) & 1) << i;
    }
    jtag_clk(1, 0); jtag_clk(1, 0); jtag_clk(0, 0); /* Exit1 -> Update -> RTI */
    return 0;
}

int main(int argc, char **argv)
{
    int i, list_only = 0, passes = 3;
    char chan = 0;
    FT_DEVICE_LIST_INFO_NODE nodes[32];
    DWORD num = 0, n = 0, st;
    int sel = -1;
    unsigned int pass;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) list_only = 1;
        else if (!strcmp(argv[i], "--channel") && i + 1 < argc) chan = argv[++i][0];
        else if (!strcmp(argv[i], "--tck") && i + 1 < argc) g_tck = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tdi") && i + 1 < argc) g_tdi = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tdo") && i + 1 < argc) g_tdo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tms") && i + 1 < argc) g_tms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--passes") && i + 1 < argc) passes = atoi(argv[++i]);
    }
    if (passes < 1) passes = 1;
    if (passes > 8) passes = 8;

    printf("== ftdi_jtag_scan: direct D2XX bit-bang JTAG IDCODE scan ==\n");
    if (load_d2xx() != 0) return 4;

    st = FT_CreateDeviceInfoList(&num);
    if (st != 0 || num == 0) {
        printf("  [x] FT_CreateDeviceInfoList st=%lu num=%lu (no FTDI devices?)\n", st, num);
        return 4;
    }
    n = num > 32 ? 32 : num;
    st = FT_GetDeviceInfoList(nodes, &n);
    if (st != 0) { printf("  [x] FT_GetDeviceInfoList st=%lu\n", st); return 4; }
    printf("  devices (%lu):\n", n);
    for (i = 0; i < (int)n; i++) {
        printf("    [%d] desc='%s' serial='%s' loc=0x%04lX type=%lu flags=0x%lX\n",
               i, nodes[i].Description, nodes[i].SerialNumber,
               (unsigned long)nodes[i].LocId, (unsigned long)nodes[i].Type,
               (unsigned long)nodes[i].Flags);
    }
    if (list_only) return 0;

    for (i = 0; i < (int)n; i++) {
        const char *d = nodes[i].Description;
        const char *s = nodes[i].SerialNumber;
        if (chan == 'B') {
            if (strstr(d, "COM") ||
                (strlen(s) && (s[strlen(s) - 1] == 'B' || s[strlen(s) - 1] == 'b'))) { sel = i; break; }
        } else {
            if (strstr(d, "Debug") || strstr(d, "debug") || strstr(d, "DEBUG")) { sel = i; break; }
        }
    }
    if (sel < 0 && chan == 0) {
        for (i = 0; i < (int)n; i++) {
            const char *s = nodes[i].SerialNumber;
            if (strlen(s) && (s[strlen(s) - 1] == 'A' || s[strlen(s) - 1] == 'a')) { sel = i; break; }
        }
    }
    if (sel < 0) sel = 0;
    printf("  selected: [%d] '%s'\n", sel, nodes[sel].Description);

    st = FT_OpenEx((PVOID)(ULONG_PTR)nodes[sel].LocId, 4 /*FT_OPEN_BY_LOCATION*/, &g_h);
    if (st != 0) {
        printf("  open-by-location st=%lu, trying description...\n", st);
        st = FT_OpenEx((PVOID)nodes[sel].Description, 2 /*FT_OPEN_BY_DESCRIPTION*/, &g_h);
    }
    if (st != 0) {
        printf("  open-by-description st=%lu, trying serial...\n", st);
        st = FT_OpenEx((PVOID)nodes[sel].SerialNumber, 1 /*FT_OPEN_BY_SERIAL_NUMBER*/, &g_h);
    }
    if (st != 0) {
        printf("  [x] FT_OpenEx failed, st=%lu (2=busy/opened elsewhere, 3=not found)\n", st);
        return 4;
    }
    printf("  D2XX open OK (nobody else holds this channel)\n");

    FT_ResetDevice(g_h);
    FT_Purge(g_h, 3);
    FT_SetTimeouts(g_h, 500, 500);
    FT_SetLatencyTimer(g_h, 1);
    FT_SetBitMode(g_h, 0, 0);
    Sleep(30);
    {
        unsigned char o1 = 0, o2 = 0;
        st = FT_SetBitMode(g_h, 0x00, 1);   /* all inputs - observe idle levels */
        Sleep(50);
        FT_GetBitMode(g_h, &o1);
        Sleep(50);
        FT_GetBitMode(g_h, &o2);
        printf("  idle observe (all ADBUS inputs, st=%lu): 0x%02X / 0x%02X\n", st, o1, o2);
        FT_SetBitMode(g_h, 0, 0);
    }
    {
        unsigned char mask = (unsigned char)((1u << g_tck) | (1u << g_tdi) | (1u << g_tms));
        st = FT_SetBitMode(g_h, mask, 1);   /* async bit-bang */
        printf("  bitbang st=%lu  (TCK=p%d TDI=p%d TDO=p%d TMS=p%d, outmask=0x%02X)\n",
               st, g_tck, g_tdi, g_tdo, g_tms, mask);
        if (st != 0) { FT_Close(g_h); return 4; }
    }
    Sleep(30);

    {
        unsigned char rb = 0, rb2 = 0;
        ft_write1((unsigned char)((1u << g_tck) | (1u << g_tdi) | (1u << g_tms)));
        Sleep(30);
        FT_GetBitMode(g_h, &rb);
        ft_write1(0x00);
        Sleep(30);
        FT_GetBitMode(g_h, &rb2);
        printf("  bitbang liveness: drive-high readback=0x%02X, drive-low readback=0x%02X (outputs = p%d/p%d/p%d)\n",
               rb, rb2, g_tck, g_tdi, g_tms);
    }

    {
        unsigned long w[8] = {0}, prev[8] = {0};
        int got = 0, r_all = 0, k;
        for (pass = 0; pass < (unsigned int)passes; pass++) {
            int r = jtag_scan_once(w);
            if (r < 0) {
                printf("  [x] USB/bitbang error during pass %u (r=%d) - USB path sick?\n", pass + 1, r);
                r_all = r;
                break;
            }
            printf("  pass %u pin words (ADBUS0..7, sampled during Shift-DR):\n", pass + 1);
            for (k = 0; k < 8; k++) {
                const char *tag = "";
                if (k == g_tck) tag = " <TCK driven";
                else if (k == g_tdi) tag = " <TDI driven";
                else if (k == g_tms) tag = " <TMS driven";
                else if (k == g_tdo) tag = " <TDO assumed";
                printf("    p%d: 0x%08lX%s%s\n", k, w[k], tag,
                       (got > 0 && w[k] != prev[k]) ? "  *CHANGED*" : "");
            }
            memcpy(prev, w, sizeof(prev));
            got++;
        }
        if (got >= 2) {
            unsigned long v = w[g_tdo];
            if (v != 0xFFFFFFFFul && v != 0x00000000ul)
                printf("  => p%d shows structured data 0x%08lX - check it (maybe TDO/mapping!)\n", g_tdo, v);
            else
                printf("  => p%d (assumed TDO) static 0x%08lX; inspect table above for other structured pins\n", g_tdo, v);
        } else if (got == 1 && r_all == 0) {
            printf("  => only one pass completed, inconclusive\n");
        }
        if (r_all) printf("  (note: USB-level errors - consider hub/controller wedge / PC reboot)\n");
    }

    FT_SetBitMode(g_h, 0, 0);
    FT_ResetDevice(g_h);
    FT_Close(g_h);
    printf("== done ==\n");
    return 0;
}
