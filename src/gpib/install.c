/* Self-installation through the H/PC "Unidentified PCCard Adapter" dialog.
 *
 * When a card has no registry entry, device.exe asks the user for a driver name, loads that
 * DLL and calls its Install_Driver export with the card's PnP ID. We create
 * HKLM\Drivers\PCMCIA\<PnP ID> with Dll and Prefix values and return the key path, which
 * device.exe then uses to load us immediately. Later insertions find the key directly.
 */
#include "ce/ce_api.h"
#include "ce/ce_cardserv.h"
#include "gpib/drv_log.h"
#include "gpib/gpib_ioctl.h"
#include "rt/rt.h"

#define REG_PATH_CHARS 128

static BOOL set_string(HKEY hk, LPCWSTR name, LPCWSTR value)
{
    DWORD bytes = (rt_wcslen(value) + 1) * sizeof(WCHAR);
    return RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE *)value, bytes) == ERROR_SUCCESS;
}

/* Creates the driver key for pnp_id; writes its HKLM-relative path into out. */
BOOL gpib_install_registry(LPCWSTR pnp_id, LPWSTR out, DWORD out_chars)
{
    HKEY hk = NULL;
    DWORD disposition = 0;
    LONG rc;
    BOOL ok;
    int n = rt_fmt(out, out_chars, L"%s\\%s", DEVLOAD_PCMCIA_KEY, pnp_id);
    if (n < 0 || (DWORD)n >= out_chars) {
        return FALSE;
    }
    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, out, 0, NULL, REG_OPTION_NON_VOLATILE, 0, NULL, &hk, &disposition);
    if (rc != ERROR_SUCCESS) {
        log_printf(L"RegCreateKeyEx(%s) failed: %d", out, rc);
        return FALSE;
    }
    ok = set_string(hk, DEVLOAD_DLLNAME_VALNAME, GPIB_DRIVER_DLL) &&
         set_string(hk, DEVLOAD_PREFIX_VALNAME, GPIB_DRIVER_PREFIX) &&
         set_string(hk, L"FriendlyName", L"NI PCMCIA-GPIB (jornada-gpib)");
    RegCloseKey(hk);
    return ok;
}

LPWSTR Install_Driver(LPWSTR lpPnpId, LPWSTR lpRegPath, DWORD cRegPathLen)
{
    DWORD chars = cRegPathLen / sizeof(WCHAR);
    log_printf(L"Install_Driver(%s, buffer %u bytes)", lpPnpId, cRegPathLen);
    if (lpPnpId == NULL || lpRegPath == NULL || chars < 2) {
        return NULL;
    }
    if (chars > REG_PATH_CHARS) {
        chars = REG_PATH_CHARS;
    }
    if (!gpib_install_registry(lpPnpId, lpRegPath, chars)) {
        return NULL;
    }
    log_printf(L"installed %s", lpRegPath);
    return lpRegPath;
}
