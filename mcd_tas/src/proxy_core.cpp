/* proxy_core.cpp - proxy DLL core: original DLL loading + logging. */
#include "proxy_common.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <mutex>

static HMODULE    g_orig = NULL;
static FILE      *g_log = NULL;
static std::mutex g_mtx;
static bool       g_init_done = false;

static void get_own_dir(char *dir, size_t n)
{
    HMODULE hm = NULL;
    char path[MAX_PATH];
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)&get_own_dir, &hm);
    if (!GetModuleFileNameA(hm, path, MAX_PATH))
        path[0] = 0;
    strncpy(dir, path, n - 1);
    dir[n - 1] = 0;
    char *sl = strrchr(dir, '\\');
    if (sl)
        *(sl + 1) = 0;
}

static void proxy_init_locked(void)
{
    if (g_init_done)
        return;
    g_init_done = true;

    char dir[MAX_PATH];
    get_own_dir(dir, sizeof(dir));

    char logp[MAX_PATH + 32];
    _snprintf(logp, sizeof(logp), "%sproxy_log.txt", dir);
    g_log = fopen(logp, "a");

    char origp[MAX_PATH + 32];
    _snprintf(origp, sizeof(origp), "%smcdxdas_orig.dll", dir);
    g_orig = LoadLibraryA(origp);

    if (g_log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_log,
                "=== proxy session %04d-%02d-%02d %02d:%02d:%02d "
                "orig=%s handle=%p ===\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                origp, (void *)g_orig);
        fflush(g_log);
    }
}

void proxy_log(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    proxy_init_locked();
    if (!g_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void proxy_log_hex(const char *tag, const void *p, unsigned n)
{
    char buf[3 * 32 + 1];
    const unsigned char *b = (const unsigned char *)p;
    unsigned i, m = n > 32 ? 32 : n;
    char *w = buf;
    buf[0] = 0;
    for (i = 0; i < m; i++)
        w += sprintf(w, "%02X ", b[i]);
    proxy_log("%s [%u bytes]: %s", tag, n, buf);
}

void *proxy_orig_proc(const char *name)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    proxy_init_locked();
    if (!g_orig) {
        if (g_log) {
            fprintf(g_log, "FATAL: mcdxdas_orig.dll not loaded (%s)\n", name);
            fflush(g_log);
        }
        return NULL;
    }
    FARPROC p = GetProcAddress(g_orig, name);
    if (g_log) {
        fprintf(g_log, "resolve %s -> %p\n", name, (void *)p);
        fflush(g_log);
    }
    return (void *)p;
}
