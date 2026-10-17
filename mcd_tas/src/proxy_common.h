/* proxy_common.h - shared decls for the mcdxdas proxy DLL */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve a function in the original mcdxdas_orig.dll (loaded lazily). */
void *proxy_orig_proc(const char *name);

/* Append one line to proxy_log.txt (next to the DLL). */
void proxy_log(const char *fmt, ...);

/* Hex dump helper: logs "tag: XX XX ..." (up to n bytes). */
void proxy_log_hex(const char *tag, const void *p, unsigned n);

#ifdef __cplusplus
}
#endif
