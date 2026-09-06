/* gpibtest.exe: command-line bring-up tool for the driver, driven through jornada-link.
 *
 *   jornada run '\gpibtest.exe' 'info'            driver state, CIS window, chip probe values
 *   jornada run '\gpibtest.exe' 'probe'           re-run the reset self-test
 *   jornada run '\gpibtest.exe' 'lines'           bus line and address status
 *   jornada run '\gpibtest.exe' 'ifc'             interface clear
 *   jornada run '\gpibtest.exe' 'ren 1'           remote enable on/off
 *   jornada run '\gpibtest.exe' 'idn 5'           *IDN? to device 5 and print the answer
 *   jornada run '\gpibtest.exe' 'write 5 *RST'    write a string (EOI) to device 5
 *   jornada run '\gpibtest.exe' 'read 5 [bytes]'  read from device 5
 *   jornada run '\gpibtest.exe' 'query 5 TEXT'    write then read
 *   jornada run '\gpibtest.exe' 'spoll 5'         serial poll
 *   jornada run '\gpibtest.exe' 'clear 5|all'     selected device clear / device clear
 *   jornada run '\gpibtest.exe' 'trigger 5'       group execute trigger
 *   jornada run '\gpibtest.exe' 'local 5'         go to local
 *   jornada run '\gpibtest.exe' 'reg r 1f'        read a card register (hex offset)
 *   jornada run '\gpibtest.exe' 'reg w 1c 22'     write a card register
 *   jornada run '\gpibtest.exe' 'bench 5 N'       read N bytes as fast as possible (throughput)
 *
 * Results are appended to \gpibtest.txt. Read them with:  jornada get '\gpibtest.txt'
 */
#include "ce/ce_api.h"
#include "gpib/drv_log.h"
#include "gpib/gpib_ioctl.h"
#include "gpib/ib.h"
#include "rt/rt.h"

#define MAX_ARGS 8
#define READ_BUFFER 4096
#define TIMEOUT_CODE IB_T3s

static unsigned split(LPWSTR s, LPWSTR *argv, unsigned max)
{
    unsigned n = 0;
    while (*s != 0 && n < max) {
        while (*s == ' ') {
            s++;
        }
        if (*s == 0) {
            break;
        }
        argv[n++] = s;
        while (*s != 0 && *s != ' ') {
            s++;
        }
        if (*s == ' ') {
            *s++ = 0;
        }
    }
    return n;
}

static int parse_int(LPCWSTR s, int base, int *out)
{
    int v = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    if (*s == 0) {
        return 0;
    }
    while (*s != 0) {
        int d;
        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = *s - 'a' + 10;
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = *s - 'A' + 10;
        } else {
            return 0;
        }
        v = v * base + d;
        s++;
    }
    *out = neg ? -v : v;
    return 1;
}

static void narrow(LPCWSTR w, char *out, unsigned cap)
{
    unsigned i = 0;
    while (w[i] != 0 && i + 1 < cap) {
        out[i] = (char)(w[i] < 0x80 ? w[i] : '?');
        i++;
    }
    out[i] = 0;
}

static void report_status(LPCWSTR what)
{
    if (ibsta & IB_ERR) {
        log_printf(L"%s: ERROR ibsta 0x%04x iberr %d (%S) ibcnt %d", what, ibsta, iberr,
                   ib_error_name(iberr), ibcnt);
    } else {
        log_printf(L"%s: ok ibsta 0x%04x ibcnt %d", what, ibsta, ibcnt);
    }
}

static void log_bytes_as_text(LPCWSTR label, const char *buf, long n)
{
    char line[200];
    long i;
    unsigned k = 0;
    for (i = 0; i < n && k + 4 < sizeof line; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\n') {
            line[k++] = '\\';
            line[k++] = 'n';
        } else if (c == '\r') {
            line[k++] = '\\';
            line[k++] = 'r';
        } else if (c < 0x20 || c >= 0x7F) {
            line[k++] = '.';
        } else {
            line[k++] = (char)c;
        }
    }
    line[k] = 0;
    log_printf(L"%s (%d bytes): %S", label, n, line);
}

static int open_device(int pad)
{
    int ud = ibdev(0, pad, IB_NO_SAD, TIMEOUT_CODE, 1, 0);
    if (ud < 0) {
        report_status(L"ibdev");
    }
    return ud;
}

static void cmd_info(void)
{
    gpib_info info;
    DWORD got = 0;
    if (!DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_GET_INFO, NULL, 0, &info, sizeof info, &got, NULL)) {
        log_printf(L"GET_INFO failed: %u", GetLastError());
        return;
    }
    log_printf(L"driver version %u, card present %u, socket 0x%x", info.version, info.card_present, info.socket);
    log_printf(L"io base 0x%x length %u config index %u granularity %u 16-bit %u", info.io_base,
               info.io_length, info.config_index, info.window_granularity, info.window_16bit);
    log_printf(L"probe ok %u: csr 0x%02x sts1 0x%02x sts2 0x%02x isr3 0x%02x adsr 0x%02x isr0 0x%02x",
               info.probe.ok, info.probe.csr, info.probe.sts1, info.probe.sts2, info.probe.isr3,
               info.probe.adsr, info.probe.isr0);
    log_printf(L"config: timeout %u ms t1 %u ns pad %d sad %d eos 0x%02x flags 0x%x",
               info.config.timeout_ms, info.config.t1_ns, info.config.pad, info.config.sad,
               info.config.eos, info.config.eos_flags);
}

static void cmd_probe(void)
{
    gpib_probe_info p;
    DWORD got = 0;
    if (!DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_PROBE, NULL, 0, &p, sizeof p, &got, NULL)) {
        log_printf(L"PROBE failed: %u", GetLastError());
        return;
    }
    log_printf(L"probe %S: csr 0x%02x sts1 0x%02x (expect 0x8b) sts2 0x%02x (0x9a) isr3 0x%02x (0x19) adsr 0x%02x (0x40) isr0 0x%02x",
               p.ok ? "MATCH" : "mismatch", p.csr, p.sts1, p.sts2, p.isr3, p.adsr, p.isr0);
}

static void cmd_lines(void)
{
    gpib_lines_info li;
    DWORD got = 0;
    if (!DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_LINES, NULL, 0, &li, sizeof li, &got, NULL)) {
        log_printf(L"LINES failed: %u", GetLastError());
        return;
    }
    log_printf(L"bus: ATN %u DAV %u NDAC %u NRFD %u EOI %u SRQ %u IFC %u REN %u (bsr 0x%02x)",
               (li.bsr >> 7) & 1, (li.bsr >> 6) & 1, (li.bsr >> 5) & 1, (li.bsr >> 4) & 1,
               (li.bsr >> 3) & 1, (li.bsr >> 2) & 1, (li.bsr >> 1) & 1, li.bsr & 1, li.bsr);
    log_printf(L"adsr 0x%02x: CIC %u ATN %u TA %u LA %u; sts1 0x%02x sts2 0x%02x", li.adsr,
               (li.adsr >> 7) & 1, ((li.adsr >> 6) & 1) ^ 1, (li.adsr >> 1) & 1, (li.adsr >> 2) & 1,
               li.sts1, li.sts2);
}

static void cmd_reg(unsigned argc, LPWSTR *argv)
{
    gpib_register_op op;
    DWORD got = 0;
    int off = 0;
    int val = 0;
    if (argc < 3 || !parse_int(argv[2], 16, &off)) {
        log_printf(L"usage: reg r OFF | reg w OFF VAL (hex)");
        return;
    }
    op.op = argv[1][0] == 'w' ? GPIB_REG_WRITE : GPIB_REG_READ;
    op.offset = (UINT32)off;
    op.value = 0;
    if (op.op == GPIB_REG_WRITE) {
        if (argc < 4 || !parse_int(argv[3], 16, &val)) {
            log_printf(L"usage: reg w OFF VAL (hex)");
            return;
        }
        op.value = (UINT32)val;
    }
    if (!DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_REGISTER, &op, sizeof op, &op, sizeof op, &got, NULL)) {
        log_printf(L"REGISTER failed: %u", GetLastError());
        return;
    }
    log_printf(L"reg[0x%02x] %S 0x%02x", op.offset, op.op == GPIB_REG_WRITE ? "<-" : "=", op.value);
}

static void cmd_write(int pad, LPCWSTR text)
{
    char buf[256];
    int ud = open_device(pad);
    if (ud < 0) {
        return;
    }
    narrow(text, buf, sizeof buf);
    ibwrt(ud, buf, (long)rt_strlen(buf));
    report_status(L"ibwrt");
    ibonl(ud, 0);
}

static void cmd_read(int pad, long count)
{
    static char buf[READ_BUFFER];
    int ud = open_device(pad);
    if (ud < 0) {
        return;
    }
    if (count > READ_BUFFER) {
        count = READ_BUFFER;
    }
    ibrd(ud, buf, count);
    report_status(L"ibrd");
    log_bytes_as_text(L"data", buf, ibcnt);
    ibonl(ud, 0);
}

static void cmd_query(int pad, LPCWSTR text)
{
    static char buf[READ_BUFFER];
    char cmd[256];
    int ud = open_device(pad);
    if (ud < 0) {
        return;
    }
    narrow(text, cmd, sizeof cmd);
    ibwrt(ud, cmd, (long)rt_strlen(cmd));
    report_status(L"ibwrt");
    if (!(ibsta & IB_ERR)) {
        ibrd(ud, buf, READ_BUFFER);
        report_status(L"ibrd");
        log_bytes_as_text(L"answer", buf, ibcnt);
    }
    ibonl(ud, 0);
}

static void cmd_spoll(int pad)
{
    char stb = 0;
    int ud = open_device(pad);
    if (ud < 0) {
        return;
    }
    ibrsp(ud, &stb);
    report_status(L"ibrsp");
    log_printf(L"status byte 0x%02x", (unsigned char)stb);
    ibonl(ud, 0);
}

static void cmd_simple(int pad, int (*fn)(int), LPCWSTR name)
{
    int ud = open_device(pad);
    if (ud < 0) {
        return;
    }
    fn(ud);
    report_status(name);
    ibonl(ud, 0);
}

static void cmd_bench(int pad, long total)
{
    static char buf[READ_BUFFER];
    int ud = open_device(pad);
    long done = 0;
    DWORD t0;
    if (ud < 0) {
        return;
    }
    t0 = GetTickCount();
    while (done < total) {
        ibrd(ud, buf, READ_BUFFER);
        if (ibsta & IB_ERR) {
            break;
        }
        done += ibcnt;
        if (ibsta & IB_END) {
            break;
        }
    }
    report_status(L"bench");
    log_printf(L"%d bytes in %u ms", done, GetTickCount() - t0);
    ibonl(ud, 0);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    LPWSTR argv[MAX_ARGS];
    unsigned argc;
    int pad = 0;
    int n = 0;
    (void)hInstance;
    (void)hPrev;
    (void)nCmdShow;

    log_set_path(L"\\gpibtest.txt");
    argc = split(lpCmdLine, argv, MAX_ARGS);
    log_printf(L"gpibtest: %s", lpCmdLine);
    if (argc == 0) {
        log_printf(L"no command; see the source for the list");
        return 1;
    }
    if (ibfind_board() < 0) {
        log_printf(L"cannot open %s: error %d (is the card inserted and gpib.dll loaded?)",
                   GPIB_DEVICE_NAME, ibcnt);
        return 1;
    }
    if (argc >= 2) {
        parse_int(argv[1], 10, &pad);
    }
    if (argc >= 3) {
        parse_int(argv[2], 10, &n);
    }
    if (rt_wcscmp(argv[0], L"info") == 0) {
        cmd_info();
    } else if (rt_wcscmp(argv[0], L"probe") == 0) {
        cmd_probe();
    } else if (rt_wcscmp(argv[0], L"lines") == 0) {
        cmd_lines();
    } else if (rt_wcscmp(argv[0], L"ifc") == 0) {
        ibsic(0);
        report_status(L"ibsic");
    } else if (rt_wcscmp(argv[0], L"ren") == 0) {
        ibsre(0, pad);
        report_status(L"ibsre");
    } else if (rt_wcscmp(argv[0], L"idn") == 0) {
        cmd_query(pad, L"*IDN?");
    } else if (rt_wcscmp(argv[0], L"write") == 0 && argc >= 3) {
        cmd_write(pad, argv[2]);
    } else if (rt_wcscmp(argv[0], L"read") == 0) {
        cmd_read(pad, argc >= 3 && n > 0 ? n : 256);
    } else if (rt_wcscmp(argv[0], L"query") == 0 && argc >= 3) {
        cmd_query(pad, argv[2]);
    } else if (rt_wcscmp(argv[0], L"spoll") == 0) {
        cmd_spoll(pad);
    } else if (rt_wcscmp(argv[0], L"clear") == 0) {
        if (argc >= 2 && rt_wcscmp(argv[1], L"all") == 0) {
            gpib_address a;
            gpib_result r;
            DWORD got = 0;
            a.pad = -1;
            a.sad = -1;
            DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_CLEAR, &a, sizeof a, &r, sizeof r, &got, NULL);
            log_printf(L"DCL: status %d", r.status);
        } else {
            cmd_simple(pad, ibclr, L"ibclr");
        }
    } else if (rt_wcscmp(argv[0], L"trigger") == 0) {
        cmd_simple(pad, ibtrg, L"ibtrg");
    } else if (rt_wcscmp(argv[0], L"local") == 0) {
        cmd_simple(pad, ibloc, L"ibloc");
    } else if (rt_wcscmp(argv[0], L"reg") == 0) {
        cmd_reg(argc, argv);
    } else if (rt_wcscmp(argv[0], L"bench") == 0) {
        cmd_bench(pad, n > 0 ? n : 65536);
    } else {
        log_printf(L"unknown command %s", argv[0]);
    }
    CloseHandle(ib_driver_handle());
    return 0;
}
