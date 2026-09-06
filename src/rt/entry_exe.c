/* Process entry point for Windows CE executables built with this toolchain.
 *
 * The kernel starts the primary thread at the PE entry point with the same four arguments
 * Microsoft's CRT hands to WinMain: (HINSTANCE, HINSTANCE previous, LPWSTR command line,
 * int show). Four words fit in R4..R7, so no thunk is needed. Returning ends the thread; the
 * process ends when its last thread does.
 */
#include "ce/ce_api.h"

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow);

int WinMainCRTStartup(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    return WinMain(hInstance, hPrev, lpCmdLine, nCmdShow);
}
