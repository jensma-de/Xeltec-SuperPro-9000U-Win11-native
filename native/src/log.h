#ifndef XELTEK_LOG_H
#define XELTEK_LOG_H
#include <windows.h>

BOOL xlog_init(HMODULE module, BOOL module_in_bin, const char *component);
const wchar_t *xlog_path(void);
BOOL xlog_write(const char *level, DWORD error, const char *format, ...);
BOOL xlog_write_w(const char *level, DWORD error, const wchar_t *message);
#endif
