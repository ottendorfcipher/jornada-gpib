/* DLL entry point for Windows CE DLLs built with this toolchain.
 *
 * The loader calls the PE entry point with (HINSTANCE, DWORD reason, LPVOID reserved); three
 * words, so no thunk is needed. Nothing in this project needs attach/detach notifications,
 * so the entry point simply accepts every call.
 */
#include "ce/ce_api.h"

BOOL DllMainCRTStartup(HINSTANCE hInstance, DWORD reason, LPVOID reserved)
{
    (void)hInstance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
