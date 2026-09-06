/* gpib.dll: Windows CE 2.11 stream interface driver for the NI PCMCIA-GPIB (prefix GPB).
 *
 * Loaded by device.exe when the card is inserted (HKLM\Drivers\PCMCIA\<PnP ID>). Init reads
 * the socket from the active key, binds Card Services, claims the card's 32-byte I/O window
 * from the CIS configuration table, powers and configures the card and brings the TNT4882C
 * up as system controller. Applications talk to it through the IOCTLs in gpib_ioctl.h; the
 * work happens on the caller's thread under one critical section. No interrupts are used.
 */
#include "ce/ce_api.h"
#include "ce/ce_cardserv.h"
#include "gpib/cis.h"
#include "gpib/drv_log.h"
#include "gpib/gpib488.h"
#include "gpib/gpib_ioctl.h"
#include "gpib/tnt4882.h"
#include "rt/rt.h"

#define MAX_CFTABLE_ENTRIES 8
#define GPIB_WINDOW_BYTES 32
#define DEFAULT_PAD 0
#define MAX_XFER_INLINE 65536
#define DELAY_REGISTER TNT_CSR       /* reading the signature register has no side effects */

typedef struct gpib_device {
    cs_table cs;
    CS_SOCKET sock;
    CARD_CLIENT_HANDLE client;
    CARD_WINDOW_HANDLE window;
    volatile UINT8 *io;
    UINT32 granularity;
    UINT32 io_base, io_length, config_index, window_16bit, io_access;
    BOOL configured;
    BOOL card_present;
    BOOL chip_ok;
    UINT32 window_8bit_ok, window_16bit_ok;
    cis_capture cis;
    tnt_probe_result probe;
    tnt_io tio;
    tnt_chip chip;
    gpib_ctl ctl;
    gpib_config config;
    CRITICAL_SECTION lock;
    DWORD opens;
} gpib_device;

typedef struct gpib_open {
    gpib_device *dev;
    gpib_address current;       /* used by ReadFile/WriteFile */
} gpib_open;

static int check_ready(gpib_device *d);
static int ensure_cic(gpib_device *d);

/* ---- register access for the chip driver ------------------------------------------- */

static UINT8 io_read8(void *ctx, unsigned off)
{
    gpib_device *d = ctx;
    return d->io[off];
}

static void io_write8(void *ctx, unsigned off, UINT8 v)
{
    gpib_device *d = ctx;
    d->io[off] = v;
}

static void io_delay_us(void *ctx, unsigned us)
{
    gpib_device *d = ctx;
    volatile UINT8 sink;
    unsigned i;
    /* Each PC Card I/O cycle is several hundred nanoseconds on the HD64461; two reads per
     * microsecond requested is a conservative floor. */
    for (i = 0; i < us * 2; i++) {
        sink = d->io[DELAY_REGISTER];
    }
    (void)sink;
}

static uint32_t io_now_ms(void *ctx)
{
    (void)ctx;
    return GetTickCount();
}

static void io_yield(void *ctx)
{
    (void)ctx;
    Sleep(0);
}

/* ---- Card Services ------------------------------------------------------------------ */

static STATUS card_callback(CARD_EVENT ev, CS_SOCKET sock, PCARD_EVENT_PARMS p)
{
    gpib_device *d = (gpib_device *)p->uClientData;
    (void)sock;
    if (d == NULL) {
        return CERR_SUCCESS;
    }
    if (ev == CE_CARD_REMOVAL) {
        CARD_STATUS st;
        st.hSocket.uSocket = CS_SOCKET_NUMBER(d->sock);
        st.hSocket.uFunction = CS_SOCKET_FUNCTION(d->sock);
        st.fCardState = 0;
        st.fSocketState = 0;
        if (cs_GetStatus(&d->cs, &st) != CERR_SUCCESS || !(st.fCardState & EVENT_MASK_CARD_DETECT)) {
            d->card_present = FALSE;
            log_printf(L"card removed");
        }
    }
    return CERR_SUCCESS;
}

static BOOL read_socket_from_registry(LPCWSTR active_key, CS_SOCKET *sock)
{
    HKEY hk = NULL;
    DWORD type = 0;
    DWORD value = 0;
    DWORD len = sizeof value;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, active_key, 0, 0, &hk);
    if (rc != ERROR_SUCCESS) {
        log_printf(L"RegOpenKeyEx(%s) failed: %d", active_key, rc);
        return FALSE;
    }
    rc = RegQueryValueExW(hk, DEVLOAD_SOCKET_VALNAME, NULL, &type, (LPBYTE)&value, &len);
    RegCloseKey(hk);
    if (rc != ERROR_SUCCESS) {
        log_printf(L"RegQueryValueEx(Sckt) failed: %d", rc);
        return FALSE;
    }
    *sock = value & 0xFFFF;
    return TRUE;
}

/* Choose the configuration entry to use. The PCMCIA-GPIB describes its I/O space only by
 * address lines (5 lines = 32 bytes, any base the host likes), which Card Services reports
 * as an I/O interface entry with no explicit ranges, so both forms are accepted. */
static BOOL pick_io_window(gpib_device *d)
{
    PARSED_CFTABLE cft[MAX_CFTABLE_ENTRIES];
    UINT32 n = MAX_CFTABLE_ENTRIES;
    UINT32 i;
    STATUS st;
    int best = -1;

    memset(cft, 0, sizeof cft);
    st = cs_GetParsedTuple(&d->cs, d->sock, CISTPL_CFTABLE_ENTRY, cft, &n);
    if (st != CERR_SUCCESS) {
        log_printf(L"CardGetParsedTuple(CFTABLE_ENTRY) failed: 0x%x", st);
        return FALSE;
    }
    for (i = 0; i < n; i++) {
        UINT32 lines_len = cft[i].NumIOAddrLines > 0 && cft[i].NumIOAddrLines < 16 ? 1u << cft[i].NumIOAddrLines : 0;
        log_printf(L"cftable[%u]: index 0x%x defaults %u iface %u/%u io entries %u base 0x%x len %u access %u lines %u vcc %u",
                   i, (UINT32)cft[i].ConfigIndex, (UINT32)cft[i].ContainsDefaults,
                   (UINT32)cft[i].IFacePresent, (UINT32)cft[i].IFaceType,
                   (UINT32)cft[i].NumIOEntries, cft[i].IOBase[0], cft[i].IOLength[0],
                   (UINT32)cft[i].IOAccess, (UINT32)cft[i].NumIOAddrLines,
                   (UINT32)cft[i].VccDescr.NominalV);
        if (best >= 0) {
            continue;
        }
        if (cft[i].NumIOEntries > 0 && cft[i].IOLength[0] >= GPIB_WINDOW_BYTES) {
            best = (int)i;
            d->io_base = cft[i].IOBase[0];
            d->io_length = cft[i].IOLength[0];
        } else if (cft[i].NumIOEntries == 0 && lines_len >= GPIB_WINDOW_BYTES &&
                   (!cft[i].IFacePresent || cft[i].IFaceType == 1)) {
            best = (int)i;
            d->io_base = 0;
            d->io_length = lines_len;
        }
    }
    if (best < 0) {
        log_printf(L"no configuration entry with a %u-byte I/O window", GPIB_WINDOW_BYTES);
        return FALSE;
    }
    d->config_index = cft[best].ConfigIndex;
    d->io_access = cft[best].IOAccess;
    log_printf(L"using entry %d: config index 0x%x, I/O base 0x%x, %u bytes, access %u", best,
               d->config_index, d->io_base, d->io_length, d->io_access);
    return TRUE;
}

static CARD_WINDOW_HANDLE try_window(gpib_device *d, UINT16 attrs, LPCWSTR label)
{
    CARD_WINDOW_PARMS wp;
    CARD_WINDOW_HANDLE w;
    memset(&wp, 0, sizeof wp);
    wp.hSocket.uSocket = CS_SOCKET_NUMBER(d->sock);
    wp.hSocket.uFunction = CS_SOCKET_FUNCTION(d->sock);
    wp.fAttributes = attrs;
    wp.uWindowSize = d->io_length;
    wp.fAccessSpeed = WIN_SPEED_USE_WAIT;
    w = cs_RequestWindow(&d->cs, d->client, &wp);
    log_printf(L"CardRequestWindow(%s, %u bytes): %S (error %u)", label, d->io_length,
               w != NULL ? "granted" : "refused", w != NULL ? 0 : GetLastError());
    return w;
}

/* Probe what the socket driver grants, then keep an 8-bit window (v1 uses byte access only). */
static BOOL request_window(gpib_device *d)
{
    CARD_WINDOW_HANDLE w16 = try_window(d, WIN_ATTR_IO_SPACE | WIN_ATTR_16BIT, L"16-bit I/O");
    d->window_16bit_ok = w16 != NULL;
    if (w16 != NULL) {
        cs_ReleaseWindow(&d->cs, w16);
    }
    d->window = try_window(d, WIN_ATTR_IO_SPACE, L"8-bit I/O");
    d->window_8bit_ok = d->window != NULL;
    d->window_16bit = 0;
    return d->window != NULL;
}

static BOOL configure_card(gpib_device *d)
{
    CARD_CONFIG_INFO ci;
    STATUS st;
    memset(&ci, 0, sizeof ci);
    ci.hSocket.uSocket = CS_SOCKET_NUMBER(d->sock);
    ci.hSocket.uFunction = CS_SOCKET_FUNCTION(d->sock);
    ci.fAttributes = CFG_ATTR_VALID_CLIENT;
    ci.fInterfaceType = CFG_IFACE_MEMORY_IO;
    ci.uVcc = 50;
    ci.fRegisters = CFG_REGISTER_CONFIG | CFG_REGISTER_STATUS;
    ci.uConfigReg = (UINT8)d->config_index;
    ci.uStatusReg = FCR_FCSR_REQUIRED_BITS;
    st = cs_RequestConfiguration(&d->cs, d->client, &ci);
    if (st != CERR_SUCCESS) {
        log_printf(L"CardRequestConfiguration failed: 0x%x", st);
        return FALSE;
    }
    d->configured = TRUE;
    return TRUE;
}

static void chip_bringup(gpib_device *d)
{
    int rc;
    tnt_bind(&d->chip, &d->tio);
    gpib_bind(&d->ctl, &d->chip);
    rc = tnt_probe(&d->chip, &d->probe);
    log_printf(L"probe: csr 0x%02x sts1 0x%02x sts2 0x%02x isr3 0x%02x adsr 0x%02x isr0 0x%02x -> %S",
               (UINT32)d->probe.csr, (UINT32)d->probe.sts1, (UINT32)d->probe.sts2,
               (UINT32)d->probe.isr3, (UINT32)d->probe.adsr, (UINT32)d->probe.isr0,
               rc == TNT_OK ? "match" : "MISMATCH");
    rc = tnt_init(&d->chip, (UINT8)d->config.pad);
    tnt_set_timeout(&d->chip, d->config.timeout_ms);
    d->chip_ok = rc == TNT_OK;
    log_printf(L"tnt_init: %d (system controller %S)", rc, (d->chip.io->read8(d, TNT_STS1) & TNT_S_SC) ? "yes" : "no");
    if (d->chip_ok) {
        /* Take charge of the bus right away, as a system controller does at power-up. */
        rc = gpib_interface_clear(&d->ctl);
        log_printf(L"interface clear at load: %d, CIC %S", rc, tnt_is_cic(&d->chip) ? "yes" : "no");
    }
}

/* Bus operations need the card to be controller in charge; after a fresh load, a resume or a
 * bus reset by another device it may not be. Re-assert IFC once when that happens. */
static int ensure_cic(gpib_device *d)
{
    int rc = check_ready(d);
    if (rc != GPIB_ST_OK) {
        return rc;
    }
    if (tnt_is_cic(&d->chip)) {
        return GPIB_ST_OK;
    }
    rc = gpib_interface_clear(&d->ctl);
    log_printf(L"not controller in charge: interface clear -> %d", rc);
    return rc == TNT_OK ? GPIB_ST_OK : rc;
}

static void teardown(gpib_device *d)
{
    if (d->chip_ok) {
        tnt_shutdown(&d->chip);
        d->chip_ok = FALSE;
    }
    if (d->configured) {
        cs_ReleaseConfiguration(&d->cs, d->client, d->sock);
        d->configured = FALSE;
    }
    if (d->window != NULL) {
        cs_ReleaseWindow(&d->cs, d->window);
        d->window = NULL;
    }
    if (d->client != NULL) {
        cs_DeregisterClient(&d->cs, d->client);
        d->client = NULL;
    }
    cs_unbind(&d->cs);
}

static gpib_device *bringup(LPCWSTR active_key)
{
    gpib_device *d = LocalAlloc(LPTR, sizeof *d);
    CARD_REGISTER_PARMS rp;
    if (d == NULL) {
        return NULL;
    }
    d->config.timeout_ms = TNT_DEFAULT_TIMEOUT_MS;
    d->config.t1_ns = 2000;
    d->config.pad = DEFAULT_PAD;
    d->config.sad = -1;
    d->tio.ctx = d;
    d->tio.read8 = io_read8;
    d->tio.write8 = io_write8;
    d->tio.delay_us = io_delay_us;
    d->tio.now_ms = io_now_ms;
    d->tio.yield = io_yield;
    InitializeCriticalSection(&d->lock);

    if (!read_socket_from_registry(active_key, &d->sock)) {
        goto fail;
    }
    log_printf(L"socket %u function %u", (UINT32)CS_SOCKET_NUMBER(d->sock), (UINT32)CS_SOCKET_FUNCTION(d->sock));
    if (!cs_bind(&d->cs)) {
        log_printf(L"cannot bind pcmcia.dll Card Services");
        goto fail;
    }
    rp.fAttributes = CLIENT_ATTR_IO_DRIVER | CLIENT_ATTR_NOTIFY_SHARED | CLIENT_ATTR_NOTIFY_EXCLUSIVE;
    rp.fEventMask = EVENT_MASK_CARD_DETECT;
    rp.uClientData = (UINT32)d;
    d->card_present = TRUE;
    d->client = cs_RegisterClient(&d->cs, card_callback, &rp);
    if (d->client == NULL) {
        log_printf(L"CardRegisterClient failed: %u", GetLastError());
        goto fail;
    }
    cis_capture_socket(&d->cs, d->sock, &d->cis);
    if (!pick_io_window(d) || !request_window(d)) {
        goto fail;
    }
    cs_ResetFunction(&d->cs, d->client, d->sock);
    d->io = cs_MapWindow(&d->cs, d->window, d->io_base, d->io_length, &d->granularity);
    if (d->io == NULL) {
        log_printf(L"CardMapWindow(0x%x, %u) failed: %u", d->io_base, d->io_length, GetLastError());
        goto fail;
    }
    log_printf(L"window mapped at %p granularity %u", (void *)d->io, d->granularity);
    if (!configure_card(d)) {
        goto fail;
    }
    chip_bringup(d);
    return d;
fail:
    teardown(d);
    DeleteCriticalSection(&d->lock);
    LocalFree(d);
    return NULL;
}

/* ---- stream interface ---------------------------------------------------------------- */

DWORD GPB_Init(DWORD dwContext)
{
    gpib_device *d;
    log_printf(L"GPB_Init(%s)", (LPCWSTR)dwContext);
    d = bringup((LPCWSTR)dwContext);
    log_printf(L"GPB_Init -> %p", (void *)d);
    return (DWORD)d;
}

BOOL GPB_Deinit(DWORD hDeviceContext)
{
    gpib_device *d = (gpib_device *)hDeviceContext;
    log_printf(L"GPB_Deinit");
    if (d == NULL) {
        return FALSE;
    }
    EnterCriticalSection(&d->lock);
    teardown(d);
    LeaveCriticalSection(&d->lock);
    DeleteCriticalSection(&d->lock);
    LocalFree(d);
    return TRUE;
}

DWORD GPB_Open(DWORD hDeviceContext, DWORD AccessCode, DWORD ShareMode)
{
    gpib_device *d = (gpib_device *)hDeviceContext;
    gpib_open *o;
    (void)AccessCode;
    (void)ShareMode;
    if (d == NULL) {
        return 0;
    }
    o = LocalAlloc(LPTR, sizeof *o);
    if (o == NULL) {
        return 0;
    }
    o->dev = d;
    o->current.pad = -1;
    o->current.sad = -1;
    EnterCriticalSection(&d->lock);
    d->opens++;
    LeaveCriticalSection(&d->lock);
    return (DWORD)o;
}

BOOL GPB_Close(DWORD hOpenContext)
{
    gpib_open *o = (gpib_open *)hOpenContext;
    if (o == NULL) {
        return FALSE;
    }
    EnterCriticalSection(&o->dev->lock);
    o->dev->opens--;
    LeaveCriticalSection(&o->dev->lock);
    LocalFree(o);
    return TRUE;
}

static int check_ready(gpib_device *d)
{
    if (!d->card_present) {
        return GPIB_ST_NOCARD;
    }
    if (!d->chip_ok) {
        return GPIB_ST_IO;
    }
    return GPIB_ST_OK;
}

DWORD GPB_Read(DWORD hOpenContext, LPVOID pBuffer, DWORD Count)
{
    gpib_open *o = (gpib_open *)hOpenContext;
    unsigned got = 0;
    int end = 0;
    int rc;
    if (o == NULL || o->current.pad < 0) {
        return (DWORD)-1;
    }
    EnterCriticalSection(&o->dev->lock);
    rc = ensure_cic(o->dev);
    if (rc == GPIB_ST_OK) {
        rc = gpib_read(&o->dev->ctl, o->current.pad, o->current.sad, pBuffer, Count, &got, &end);
    }
    LeaveCriticalSection(&o->dev->lock);
    return rc == GPIB_ST_OK ? got : (DWORD)-1;
}

DWORD GPB_Write(DWORD hOpenContext, LPCVOID pBuffer, DWORD Count)
{
    gpib_open *o = (gpib_open *)hOpenContext;
    unsigned sent = 0;
    int rc;
    if (o == NULL || o->current.pad < 0) {
        return (DWORD)-1;
    }
    EnterCriticalSection(&o->dev->lock);
    rc = ensure_cic(o->dev);
    if (rc == GPIB_ST_OK) {
        rc = gpib_write(&o->dev->ctl, o->current.pad, o->current.sad, pBuffer, Count, 1, &sent);
    }
    LeaveCriticalSection(&o->dev->lock);
    return rc == GPIB_ST_OK ? sent : (DWORD)-1;
}

DWORD GPB_Seek(DWORD hOpenContext, long Amount, DWORD Type)
{
    (void)hOpenContext;
    (void)Amount;
    (void)Type;
    return (DWORD)-1;
}

void GPB_PowerDown(DWORD hDeviceContext)
{
    (void)hDeviceContext;
}

void GPB_PowerUp(DWORD hDeviceContext)
{
    (void)hDeviceContext;
}

/* ---- IOCTL dispatch --------------------------------------------------------------- */

static void fill_regs(gpib_device *d, gpib_regs *g);

static BOOL put_result(PBYTE out, DWORD outlen, PDWORD actual, int status, UINT32 count, UINT32 end)
{
    gpib_result r;
    r.status = status;
    r.count = count;
    r.end = end;
    if (out == NULL || outlen < sizeof r) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(out, &r, sizeof r);
    if (actual != NULL) {
        *actual = sizeof r;
    }
    return TRUE;
}

static BOOL get_address(PBYTE in, DWORD inlen, gpib_address *a)
{
    if (in == NULL || inlen < sizeof *a) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(a, in, sizeof *a);
    return TRUE;
}

static void apply_config(gpib_device *d, const gpib_config *c)
{
    d->config = *c;
    if (d->chip_ok) {
        tnt_set_timeout(&d->chip, c->timeout_ms);
        tnt_set_t1(&d->chip, c->t1_ns);
        tnt_set_address(&d->chip, (UINT8)c->pad, c->sad);
        tnt_set_eos(&d->chip, (UINT8)c->eos, c->eos_flags);
    }
}

static BOOL ioctl_get_info(gpib_device *d, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_info info;
    if (out == NULL || outlen < sizeof info) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memset(&info, 0, sizeof info);
    info.version = GPIB_IOCTL_VERSION;
    info.card_present = d->card_present;
    info.socket = d->sock;
    info.io_base = d->io_base;
    info.io_length = d->io_length;
    info.config_index = d->config_index;
    info.window_granularity = d->granularity;
    info.window_16bit = d->window_16bit;
    info.probe.ok = (UINT32)d->probe.ok;
    info.probe.csr = d->probe.csr;
    info.probe.sts1 = d->probe.sts1;
    info.probe.sts2 = d->probe.sts2;
    info.probe.isr3 = d->probe.isr3;
    info.probe.adsr = d->probe.adsr;
    info.probe.isr0 = d->probe.isr0;
    info.config = d->config;
    memcpy(out, &info, sizeof info);
    if (actual != NULL) {
        *actual = sizeof info;
    }
    return TRUE;
}

static BOOL ioctl_write(gpib_device *d, PBYTE in, DWORD inlen, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_xfer x;
    unsigned sent = 0;
    int rc;
    if (in == NULL || inlen < sizeof x) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(&x, in, sizeof x);
    if (x.length > inlen - sizeof x || x.length > MAX_XFER_INLINE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    rc = ensure_cic(d);
    if (rc == GPIB_ST_OK) {
        rc = gpib_write(&d->ctl, x.addr.pad, x.addr.sad, in + sizeof x, x.length,
                        (x.flags & GPIB_XF_EOI) != 0, &sent);
    }
    if (rc != GPIB_ST_OK) {
        gpib_regs g;
        fill_regs(d, &g);
        log_printf(L"write to %d/%d failed: status %d sent %u; isr1 %02x isr0 %02x sts1 %02x sts2 %02x adsr %02x bsr %02x sasr %02x",
                   x.addr.pad, x.addr.sad, rc, sent, g.isr1, g.isr0, g.sts1, g.sts2, g.adsr, g.bsr, g.sasr);
    }
    return put_result(out, outlen, actual, rc, sent, 0);
}

static BOOL ioctl_read(gpib_device *d, PBYTE in, DWORD inlen, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_xfer x;
    gpib_result r;
    unsigned got = 0;
    int end = 0;
    int rc;
    if (in == NULL || inlen < sizeof x || out == NULL || outlen < sizeof r) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(&x, in, sizeof x);
    if (x.length > outlen - sizeof r || x.length > MAX_XFER_INLINE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    rc = ensure_cic(d);
    if (rc == GPIB_ST_OK) {
        rc = gpib_read(&d->ctl, x.addr.pad, x.addr.sad, out + sizeof r, x.length, &got, &end);
    }
    r.status = rc;
    r.count = got;
    r.end = (UINT32)end;
    memcpy(out, &r, sizeof r);
    if (actual != NULL) {
        *actual = sizeof r + got;
    }
    return TRUE;
}

static BOOL ioctl_probe(gpib_device *d, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_probe_info p;
    if (out == NULL || outlen < sizeof p) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!d->card_present) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    chip_bringup(d);
    p.ok = (UINT32)d->probe.ok;
    p.csr = d->probe.csr;
    p.sts1 = d->probe.sts1;
    p.sts2 = d->probe.sts2;
    p.isr3 = d->probe.isr3;
    p.adsr = d->probe.adsr;
    p.isr0 = d->probe.isr0;
    memcpy(out, &p, sizeof p);
    if (actual != NULL) {
        *actual = sizeof p;
    }
    return TRUE;
}

static BOOL ioctl_register(gpib_device *d, PBYTE in, DWORD inlen, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_register_op op;
    if (in == NULL || inlen < sizeof op || out == NULL || outlen < sizeof op || !d->card_present) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(&op, in, sizeof op);
    if (op.offset >= GPIB_WINDOW_BYTES) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (op.op == GPIB_REG_WRITE) {
        d->io[op.offset] = (UINT8)op.value;
    } else {
        op.value = d->io[op.offset];
    }
    memcpy(out, &op, sizeof op);
    if (actual != NULL) {
        *actual = sizeof op;
    }
    return TRUE;
}

static BOOL ioctl_cis(gpib_device *d, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_cis_info ci;
    if (out == NULL || outlen < sizeof ci) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memset(&ci, 0, sizeof ci);
    ci.tuples = d->cis.tuples;
    ci.raw_len = d->cis.raw_len < GPIB_CIS_RAW_MAX ? d->cis.raw_len : GPIB_CIS_RAW_MAX;
    ci.manufacturer_id = d->cis.manufacturer_id;
    ci.card_id = d->cis.card_id;
    ci.function_type = d->cis.function_type;
    ci.window_8bit_ok = d->window_8bit_ok;
    ci.window_16bit_ok = d->window_16bit_ok;
    memcpy(ci.pnpid, d->cis.pnpid, sizeof ci.pnpid);
    memcpy(ci.raw, d->cis.raw, ci.raw_len);
    memcpy(out, &ci, sizeof ci);
    if (actual != NULL) {
        *actual = sizeof ci;
    }
    return TRUE;
}

static void fill_regs(gpib_device *d, gpib_regs *g)
{
    tnt_regs r;
    tnt_snapshot(&d->chip, &r);
    g->isr0 = r.isr0;
    g->isr1 = r.isr1;
    g->isr2 = r.isr2;
    g->isr3 = r.isr3;
    g->sts1 = r.sts1;
    g->sts2 = r.sts2;
    g->adsr = r.adsr;
    g->bsr = r.bsr;
    g->sasr = r.sasr;
    g->cnt0 = r.cnt0;
    g->cnt1 = r.cnt1;
}

static BOOL ioctl_snapshot(gpib_device *d, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_regs g;
    if (out == NULL || outlen < sizeof g || !d->card_present) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    fill_regs(d, &g);
    memcpy(out, &g, sizeof g);
    if (actual != NULL) {
        *actual = sizeof g;
    }
    return TRUE;
}

static BOOL ioctl_raw_out(gpib_device *d, PBYTE in, DWORD inlen, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_raw_out h;
    gpib_raw_result r;
    unsigned sent = 0;
    if (in == NULL || inlen < sizeof h || out == NULL || outlen < sizeof r) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(&h, in, sizeof h);
    if (h.length > inlen - sizeof h || h.length > MAX_XFER_INLINE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(&r, 0, sizeof r);
    r.status = check_ready(d);
    if (r.status == GPIB_ST_OK) {
        fill_regs(d, &r.before);
        r.status = tnt_transfer_out(&d->chip, in + sizeof h, h.length, h.flags, &sent);
        fill_regs(d, &r.after);
    }
    r.sent = sent;
    log_printf(L"raw out flags 0x%x len %u -> status %d sent %u; before isr1 %02x sts1 %02x sts2 %02x adsr %02x bsr %02x; after isr1 %02x isr0 %02x isr3 %02x sts1 %02x sts2 %02x adsr %02x bsr %02x sasr %02x cnt %02x%02x",
               h.flags, h.length, r.status, sent, r.before.isr1, r.before.sts1, r.before.sts2, r.before.adsr, r.before.bsr,
               r.after.isr1, r.after.isr0, r.after.isr3, r.after.sts1, r.after.sts2, r.after.adsr, r.after.bsr, r.after.sasr,
               r.after.cnt1, r.after.cnt0);
    memcpy(out, &r, sizeof r);
    if (actual != NULL) {
        *actual = sizeof r;
    }
    return TRUE;
}

static BOOL ioctl_lines(gpib_device *d, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_lines_info li;
    if (out == NULL || outlen < sizeof li || !d->card_present) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    li.bsr = d->io[TNT_BSR];
    li.adsr = d->io[TNT_ADSR];
    li.sts1 = d->io[TNT_STS1];
    li.sts2 = d->io[TNT_STS2];
    memcpy(out, &li, sizeof li);
    if (actual != NULL) {
        *actual = sizeof li;
    }
    return TRUE;
}

static BOOL dispatch(gpib_open *o, DWORD code, PBYTE in, DWORD inlen, PBYTE out, DWORD outlen, PDWORD actual)
{
    gpib_device *d = o->dev;
    gpib_address a;
    int rc;
    UINT8 stb = 0;

    switch (code) {
    case IOCTL_GPIB_GET_INFO:
        return ioctl_get_info(d, out, outlen, actual);
    case IOCTL_GPIB_SET_CONFIG: {
        gpib_config c;
        if (in == NULL || inlen < sizeof c) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        memcpy(&c, in, sizeof c);
        if (c.pad < 0 || c.pad > 30 || c.sad > 30 || c.sad < -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        apply_config(d, &c);
        return TRUE;
    }
    case IOCTL_GPIB_SET_DEVICE:
        if (!get_address(in, inlen, &a)) {
            return FALSE;
        }
        o->current = a;
        return TRUE;
    case IOCTL_GPIB_IFC:
        rc = check_ready(d);
        if (rc == GPIB_ST_OK) {
            rc = gpib_interface_clear(&d->ctl);
        }
        return put_result(out, outlen, actual, rc, 0, 0);
    case IOCTL_GPIB_REN: {
        UINT32 on = 0;
        if (in == NULL || inlen < sizeof on) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        memcpy(&on, in, sizeof on);
        rc = check_ready(d);
        if (rc == GPIB_ST_OK) {
            rc = gpib_remote_enable(&d->ctl, on != 0);
        }
        return put_result(out, outlen, actual, rc, 0, 0);
    }
    case IOCTL_GPIB_LINES:
        return ioctl_lines(d, out, outlen, actual);
    case IOCTL_GPIB_WRITE:
        return ioctl_write(d, in, inlen, out, outlen, actual);
    case IOCTL_GPIB_READ:
        return ioctl_read(d, in, inlen, out, outlen, actual);
    case IOCTL_GPIB_COMMAND:
        if (in == NULL || inlen == 0 || inlen > MAX_XFER_INLINE) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        rc = ensure_cic(d);
        if (rc == GPIB_ST_OK) {
            rc = gpib_send_commands(&d->ctl, in, inlen);
        }
        return put_result(out, outlen, actual, rc, rc == GPIB_ST_OK ? inlen : 0, 0);
    case IOCTL_GPIB_CLEAR:
    case IOCTL_GPIB_TRIGGER:
    case IOCTL_GPIB_LOCAL:
    case IOCTL_GPIB_PASS_CONTROL:
        if (!get_address(in, inlen, &a)) {
            return FALSE;
        }
        rc = ensure_cic(d);
        if (rc == GPIB_ST_OK) {
            if (code == IOCTL_GPIB_CLEAR) {
                rc = gpib_clear(&d->ctl, a.pad, a.sad);
            } else if (code == IOCTL_GPIB_TRIGGER) {
                rc = gpib_trigger(&d->ctl, a.pad, a.sad);
            } else if (code == IOCTL_GPIB_LOCAL) {
                rc = gpib_local(&d->ctl, a.pad, a.sad);
            } else {
                rc = gpib_pass_control(&d->ctl, a.pad);
            }
        }
        return put_result(out, outlen, actual, rc, 0, 0);
    case IOCTL_GPIB_LOCAL_LOCKOUT:
        rc = ensure_cic(d);
        if (rc == GPIB_ST_OK) {
            rc = gpib_local_lockout(&d->ctl);
        }
        return put_result(out, outlen, actual, rc, 0, 0);
    case IOCTL_GPIB_SERIAL_POLL:
        if (!get_address(in, inlen, &a)) {
            return FALSE;
        }
        rc = ensure_cic(d);
        if (rc == GPIB_ST_OK) {
            rc = gpib_serial_poll(&d->ctl, a.pad, a.sad, &stb);
        }
        return put_result(out, outlen, actual, rc, stb, 0);
    case IOCTL_GPIB_PROBE:
        return ioctl_probe(d, out, outlen, actual);
    case IOCTL_GPIB_CIS:
        return ioctl_cis(d, out, outlen, actual);
    case IOCTL_GPIB_SNAPSHOT:
        return ioctl_snapshot(d, out, outlen, actual);
    case IOCTL_GPIB_RAW_OUT:
        return ioctl_raw_out(d, in, inlen, out, outlen, actual);
    case IOCTL_GPIB_ATN: {
        UINT32 on = 0;
        if (in == NULL || inlen < sizeof on) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        memcpy(&on, in, sizeof on);
        rc = check_ready(d);
        if (rc == GPIB_ST_OK) {
            rc = on ? tnt_take_control(&d->chip, 0) : tnt_go_to_standby(&d->chip);
        }
        return put_result(out, outlen, actual, rc, 0, 0);
    }
    case IOCTL_GPIB_REGISTER:
        return ioctl_register(d, in, inlen, out, outlen, actual);
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}

/* Called through the export thunk (seven arguments cross the calling-convention boundary). */
BOOL GPB_IOControl_impl(DWORD hOpenContext, DWORD dwCode, PBYTE pBufIn, DWORD dwLenIn,
                        PBYTE pBufOut, DWORD dwLenOut, PDWORD pdwActualOut)
{
    gpib_open *o = (gpib_open *)hOpenContext;
    BOOL ok;
    if (o == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&o->dev->lock);
    ok = dispatch(o, dwCode, pBufIn, dwLenIn, pBufOut, dwLenOut, pdwActualOut);
    LeaveCriticalSection(&o->dev->lock);
    return ok;
}
