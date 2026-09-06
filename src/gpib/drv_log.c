/* Append-only text log. Each line is opened, appended and closed on its own so nothing is
 * lost if device.exe dies with us inside; this is bring-up tooling, not a hot path. */
#include "gpib/drv_log.h"
#include "ce/ce_api.h"
#include "rt/rt.h"

static WCHAR log_path[MAX_PATH] = L"\\gpib.log";
static BOOL log_on = TRUE;

void log_set_path(LPCWSTR path)
{
    rt_wcscpy_n(log_path, MAX_PATH, path);
}

void log_enable(BOOL on)
{
    log_on = on;
}

static void log_write(const char *line, UINT32 len)
{
    HANDLE h;
    DWORD written = 0;
    if (!log_on) {
        return;
    }
    h = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, line, len, &written, NULL);
    WriteFile(h, "\r\n", 2, &written, NULL);
    CloseHandle(h);
}

void log_printf(LPCWSTR fmt, ...)
{
    char line[300];
    int n;
    DWORD t = GetTickCount();
    va_list ap;
    n = rt_fmt8(line, sizeof line, L"[%u.%03u] ", t / 1000u, t % 1000u);
    if (n < 0 || (UINT32)n >= sizeof line) {
        n = 0;
    }
    va_start(ap, fmt);
    n += rt_vfmt8(line + n, sizeof line - (UINT32)n, fmt, ap);
    va_end(ap);
    if ((UINT32)n >= sizeof line) {
        n = (int)sizeof line - 1;
    }
    log_write(line, (UINT32)n);
}

void log_hex(LPCWSTR label, const void *data, UINT32 len)
{
    static const char hex[] = "0123456789abcdef";
    const UINT8 *p = data;
    char line[300];
    UINT32 i;
    UINT32 n = (UINT32)rt_fmt8(line, sizeof line, L"%s (%u bytes):", label, len);
    if (n >= sizeof line) {
        n = sizeof line - 1;
    }
    for (i = 0; i < len && n + 4 < sizeof line; i++) {
        line[n++] = ' ';
        line[n++] = hex[p[i] >> 4];
        line[n++] = hex[p[i] & 15];
    }
    line[n] = 0;
    log_write(line, n);
}
