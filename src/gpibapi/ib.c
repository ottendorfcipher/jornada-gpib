/* NI-488.2 style API over the gpib.dll IOCTL interface. See include/gpib/ib.h. */
#include "gpib/ib.h"

#include "ce/ce_api.h"
#include "gpib/gpib_ioctl.h"
#include "rt/rt.h"

int ibsta;
int iberr;
long ibcnt;
long ibcntl;

typedef struct ib_device {
    int in_use;
    int pad, sad;
    unsigned timeout_ms;
    int eot;
    int eos;                 /* NI packed value */
} ib_device;

static HANDLE driver = INVALID_HANDLE_VALUE;
static ib_device devices[IB_MAX_DEVICES];

static const unsigned TIMEOUT_TABLE_MS[] = {
    0, 1, 1, 1, 1, 1, 3, 10, 30, 100, 300, 1000, 3000, 10000, 30000, 100000, 300000, 1000000,
};

unsigned ib_timeout_ms(int tmo)
{
    if (tmo < 0 || tmo > IB_T1000s) {
        return 10000;
    }
    return TIMEOUT_TABLE_MS[tmo];
}

const char *ib_error_name(int err)
{
    switch (err) {
    case IB_EDVR: return "EDVR system error";
    case IB_ECIC: return "ECIC not controller in charge";
    case IB_ENOL: return "ENOL no listener";
    case IB_EADR: return "EADR bad address";
    case IB_EARG: return "EARG invalid argument";
    case IB_ESAC: return "ESAC not system controller";
    case IB_EABO: return "EABO timeout";
    case IB_ENEB: return "ENEB no board";
    case IB_EBUS: return "EBUS bus error";
    case IB_ETAB: return "ETAB table problem";
    default: return "unknown";
    }
}

HANDLE ib_driver_handle(void)
{
    return driver;
}

static int fail(int err, long count)
{
    iberr = err;
    ibcnt = count;
    ibcntl = count;
    ibsta = IB_ERR | IB_CMPL;
    return ibsta;
}

static int ok(long count, int extra)
{
    iberr = 0;
    ibcnt = count;
    ibcntl = count;
    ibsta = IB_CMPL | extra;
    return ibsta;
}

static int map_status(int status)
{
    switch (status) {
    case GPIB_ST_OK: return 0;
    case GPIB_ST_TIMEOUT: return IB_EABO;
    case GPIB_ST_NOLISTENER: return IB_ENOL;
    case GPIB_ST_NOTCIC: return IB_ECIC;
    case GPIB_ST_NOTSC: return IB_ESAC;
    case GPIB_ST_INVAL: return IB_EARG;
    case GPIB_ST_NOCARD: return IB_ENEB;
    default: return IB_EBUS;
    }
}

static BOOL ioctl(DWORD code, const void *in, DWORD inlen, void *out, DWORD outlen, DWORD *actual)
{
    DWORD got = 0;
    BOOL r;
    if (driver == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    r = DeviceIoControl(driver, code, (LPVOID)in, inlen, out, outlen, &got, NULL);
    if (actual != NULL) {
        *actual = got;
    }
    return r;
}

static int simple_ioctl(DWORD code, const void *in, DWORD inlen)
{
    gpib_result r;
    if (!ioctl(code, in, inlen, &r, sizeof r, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    if (r.status != GPIB_ST_OK) {
        return fail(map_status(r.status), 0);
    }
    return ok((long)r.count, 0);
}

static ib_device *device(int ud)
{
    if (ud <= 0 || ud >= IB_MAX_DEVICES || !devices[ud].in_use) {
        return NULL;
    }
    return &devices[ud];
}

int ibfind_board(void)
{
    if (driver != INVALID_HANDLE_VALUE) {
        return ok(0, 0) >= 0 ? 0 : -1;
    }
    driver = CreateFileW(GPIB_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (driver == INVALID_HANDLE_VALUE) {
        fail(IB_ENEB, (long)GetLastError());
        return -1;
    }
    ok(0, 0);
    return 0;
}

int ibconfig_board(int pad, int sad, unsigned timeout_ms, unsigned t1_ns)
{
    gpib_config c;
    memset(&c, 0, sizeof c);
    c.timeout_ms = timeout_ms;
    c.t1_ns = t1_ns;
    c.pad = pad;
    c.sad = sad;
    c.eos = 0;
    c.eos_flags = 0;
    if (!ioctl(IOCTL_GPIB_SET_CONFIG, &c, sizeof c, NULL, 0, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    return ok(0, 0);
}

static int apply_device_settings(const ib_device *d)
{
    gpib_config c;
    gpib_info info;
    if (!ioctl(IOCTL_GPIB_GET_INFO, NULL, 0, &info, sizeof info, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    c = info.config;
    c.timeout_ms = d->timeout_ms;
    c.eos = (UINT32)(d->eos & 0xFF);
    c.eos_flags = 0;
    if (d->eos & IB_REOS) {
        c.eos_flags |= GPIB_EOS_REOS;
    }
    if (d->eos & IB_XEOS) {
        c.eos_flags |= GPIB_EOS_XEOS;
    }
    if (d->eos & IB_BIN) {
        c.eos_flags |= GPIB_EOS_BIN;
    }
    if (!ioctl(IOCTL_GPIB_SET_CONFIG, &c, sizeof c, NULL, 0, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    return 0;
}

int ibsic(int ud)
{
    (void)ud;
    return simple_ioctl(IOCTL_GPIB_IFC, NULL, 0);
}

int ibsre(int ud, int v)
{
    UINT32 on = v != 0;
    (void)ud;
    return simple_ioctl(IOCTL_GPIB_REN, &on, sizeof on);
}

int iblines(int ud, short *lines)
{
    gpib_lines_info li;
    (void)ud;
    if (!ioctl(IOCTL_GPIB_LINES, NULL, 0, &li, sizeof li, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    *lines = (short)(li.bsr & 0xFF);
    return ok(0, (li.adsr & 0x80) ? IB_CIC : 0);
}

int ibdev(int board, int pad, int sad, int tmo, int eot, int eos)
{
    int ud;
    if (board != 0 || pad < 0 || pad > 30 || sad > 30 || sad < -1) {
        fail(IB_EARG, 0);
        return -1;
    }
    if (driver == INVALID_HANDLE_VALUE && ibfind_board() < 0) {
        return -1;
    }
    for (ud = 1; ud < IB_MAX_DEVICES; ud++) {
        if (!devices[ud].in_use) {
            devices[ud].in_use = 1;
            devices[ud].pad = pad;
            devices[ud].sad = sad;
            devices[ud].timeout_ms = ib_timeout_ms(tmo);
            devices[ud].eot = eot;
            devices[ud].eos = eos;
            ok(0, 0);
            return ud;
        }
    }
    fail(IB_ETAB, 0);
    return -1;
}

int ibonl(int ud, int v)
{
    ib_device *d = device(ud);
    if (d == NULL) {
        return fail(IB_EARG, 0);
    }
    if (v == 0) {
        d->in_use = 0;
    }
    return ok(0, 0);
}

int ibwrt(int ud, const void *buf, long cnt)
{
    ib_device *d = device(ud);
    gpib_xfer *x;
    gpib_result r;
    BOOL good;
    if (d == NULL || buf == NULL || cnt < 0) {
        return fail(IB_EARG, 0);
    }
    if (apply_device_settings(d) != 0) {
        return ibsta;
    }
    x = LocalAlloc(LPTR, sizeof *x + (UINT)cnt);
    if (x == NULL) {
        return fail(IB_EDVR, 0);
    }
    x->addr.pad = d->pad;
    x->addr.sad = d->sad;
    x->flags = d->eot ? GPIB_XF_EOI : 0;
    x->length = (UINT32)cnt;
    memcpy(x + 1, buf, (UINT)cnt);
    good = ioctl(IOCTL_GPIB_WRITE, x, sizeof *x + (DWORD)cnt, &r, sizeof r, NULL);
    LocalFree(x);
    if (!good) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    if (r.status != GPIB_ST_OK) {
        return fail(map_status(r.status), (long)r.count);
    }
    return ok((long)r.count, 0);
}

int ibrd(int ud, void *buf, long cnt)
{
    ib_device *d = device(ud);
    gpib_xfer x;
    gpib_result *r;
    DWORD actual = 0;
    BOOL good;
    if (d == NULL || buf == NULL || cnt < 0) {
        return fail(IB_EARG, 0);
    }
    if (apply_device_settings(d) != 0) {
        return ibsta;
    }
    r = LocalAlloc(LPTR, sizeof *r + (UINT)cnt);
    if (r == NULL) {
        return fail(IB_EDVR, 0);
    }
    x.addr.pad = d->pad;
    x.addr.sad = d->sad;
    x.flags = 0;
    x.length = (UINT32)cnt;
    good = ioctl(IOCTL_GPIB_READ, &x, sizeof x, r, sizeof *r + (DWORD)cnt, &actual);
    if (!good) {
        LocalFree(r);
        return fail(IB_EDVR, (long)GetLastError());
    }
    if (r->count > (UINT32)cnt) {
        r->count = (UINT32)cnt;
    }
    memcpy(buf, r + 1, r->count);
    if (r->status != GPIB_ST_OK) {
        int e = map_status(r->status);
        long n = (long)r->count;
        LocalFree(r);
        fail(e, n);
        if (e == IB_EABO) {
            ibsta |= IB_TIMO;
        }
        return ibsta;
    }
    {
        long n = (long)r->count;
        int end = r->end ? IB_END : 0;
        LocalFree(r);
        return ok(n, end);
    }
}

static int addressed(int ud, DWORD code)
{
    ib_device *d = device(ud);
    gpib_address a;
    if (d == NULL) {
        return fail(IB_EARG, 0);
    }
    a.pad = d->pad;
    a.sad = d->sad;
    return simple_ioctl(code, &a, sizeof a);
}

int ibclr(int ud)
{
    return addressed(ud, IOCTL_GPIB_CLEAR);
}

int ibtrg(int ud)
{
    return addressed(ud, IOCTL_GPIB_TRIGGER);
}

int ibloc(int ud)
{
    return addressed(ud, IOCTL_GPIB_LOCAL);
}

int ibpct(int ud)
{
    return addressed(ud, IOCTL_GPIB_PASS_CONTROL);
}

int ibllo(int ud)
{
    (void)ud;
    return simple_ioctl(IOCTL_GPIB_LOCAL_LOCKOUT, NULL, 0);
}

int ibrsp(int ud, char *spr)
{
    ib_device *d = device(ud);
    gpib_address a;
    gpib_result r;
    if (d == NULL || spr == NULL) {
        return fail(IB_EARG, 0);
    }
    a.pad = d->pad;
    a.sad = d->sad;
    if (apply_device_settings(d) != 0) {
        return ibsta;
    }
    if (!ioctl(IOCTL_GPIB_SERIAL_POLL, &a, sizeof a, &r, sizeof r, NULL)) {
        return fail(IB_EDVR, (long)GetLastError());
    }
    if (r.status != GPIB_ST_OK) {
        return fail(map_status(r.status), 0);
    }
    *spr = (char)r.count;
    return ok(0, 0);
}

int ibtmo_ms(int ud, unsigned ms)
{
    ib_device *d = device(ud);
    if (d == NULL) {
        return fail(IB_EARG, 0);
    }
    d->timeout_ms = ms;
    return ok(0, 0);
}

int ibtmo(int ud, int v)
{
    ib_device *d = device(ud);
    if (d == NULL || v < 0 || v > IB_T1000s) {
        return fail(IB_EARG, 0);
    }
    d->timeout_ms = ib_timeout_ms(v);
    return ok(0, 0);
}

int ibeot(int ud, int v)
{
    ib_device *d = device(ud);
    if (d == NULL) {
        return fail(IB_EARG, 0);
    }
    d->eot = v != 0;
    return ok(0, 0);
}

int ibeos(int ud, int v)
{
    ib_device *d = device(ud);
    if (d == NULL) {
        return fail(IB_EARG, 0);
    }
    d->eos = v;
    return ok(0, 0);
}
