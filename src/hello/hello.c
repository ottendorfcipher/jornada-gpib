/* Toolchain validation program for the HP Jornada 680e.
 *
 * Exercises everything the driver will depend on: calls into coredll.dll with up to seven
 * arguments (CreateFileW), five arguments (WriteFile), structure-returning APIs
 * (GetSystemInfo, GetVersionExW), our own varargs formatter, integer division helpers and
 * 16-bit string literals. It writes a report to \hello.txt and shows a message box.
 *
 * Read the report back with:  jornada get '\hello.txt'
 */
#include "ce/ce_api.h"
#include "rt/rt.h"

static const WCHAR REPORT_PATH[] = L"\\hello.txt";

static BOOL write_line(HANDLE h, const char *line)
{
    DWORD written = 0;
    DWORD len = rt_strlen(line);
    if (!WriteFile(h, line, len, &written, NULL) || written != len) {
        return FALSE;
    }
    return WriteFile(h, "\r\n", 2, &written, NULL) && written == 2;
}

static void report(HANDLE h, const WCHAR *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    rt_vfmt8(line, sizeof line, fmt, ap);
    va_end(ap);
    write_line(h, line);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    SYSTEM_INFO si;
    OSVERSIONINFOW vi;
    HANDLE h;
    DWORD t0 = GetTickCount();
    unsigned int quotient = 1000000u / 7u;   /* exercises __udivsi3 */
    int remainder = -1000003 % 10;           /* exercises __modsi3 */
    WCHAR caption[64];

    (void)hPrev;
    (void)nCmdShow;

    h = CreateFileW(REPORT_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        MessageBoxW(NULL, L"CreateFileW failed", L"hello", MB_OK | MB_ICONERROR);
        return 1;
    }

    memset(&si, 0, sizeof si);
    GetSystemInfo(&si);
    memset(&vi, 0, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
    GetVersionExW(&vi);

    report(h, L"jornada-gpib hello: toolchain validation");
    report(h, L"hInstance=%p cmdline=[%s]", (void *)hInstance, lpCmdLine);
    report(h, L"os version %u.%u build %u platform %u", vi.dwMajorVersion, vi.dwMinorVersion,
           vi.dwBuildNumber, vi.dwPlatformId);
    report(h, L"page size %u, processor type %u, arch %u", si.dwPageSize, si.dwProcessorType,
           (UINT32)si.u.s.wProcessorArchitecture);
    report(h, L"min app address %p, max app address %p", si.lpMinimumApplicationAddress,
           si.lpMaximumApplicationAddress);
    report(h, L"division: 1000000/7=%u (expect 142857), -1000003%%10=%d (expect -3)", quotient,
           remainder);
    report(h, L"hex %08X %x, string %S, char %c, width [%5d] [%-5d]", 0xDEADBEEFu, 255u,
           "narrow", 'Z', 42, 42);
    report(h, L"tick %u, elapsed %u ms", t0, GetTickCount() - t0);
    report(h, L"done");
    CloseHandle(h);

    rt_fmt(caption, 64, L"hello from GCC (CE %u.%u)", vi.dwMajorVersion, vi.dwMinorVersion);
    MessageBoxW(NULL, L"The cross toolchain works. Report written to \\hello.txt", caption,
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    return 0;
}
