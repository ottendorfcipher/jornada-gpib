/* DLL entry point for Windows CE DLLs built with this toolchain.
 *
 * The loader calls the PE entry point with (HINSTANCE, DWORD reason, LPVOID reserved); three
 * words, so no thunk is needed. A DLL that wants to know about attach/detach defines
 * DllMain; the default here accepts everything.
 */
#include "ce/ce_api.h"

BOOL DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved) __attribute__((weak));

BOOL DllMainCRTStartup(HINSTANCE hInstance, DWORD reason, LPVOID reserved)
{
    if (DllMain != NULL) {
        return DllMain(hInstance, reason, reserved);
    }
    return TRUE;
}
