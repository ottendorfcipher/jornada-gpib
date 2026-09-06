/* gpibsrv.exe: a GPIB-to-TCP gateway on the Jornada speaking the Prologix GPIB-ETHERNET
 * command language, so ordinary PyVISA (TCPIP::<jornada ip>::1234::SOCKET) and any software
 * that knows Prologix adapters can drive instruments through the handheld.
 *
 * Text lines are instrument commands sent to the current address; lines starting with "++"
 * are controller commands:
 *   ++addr [pad [sad]]     set/show the target address
 *   ++auto [0|1]           read back automatically after a command ending in '?'
 *   ++eoi [0|1]            assert EOI with the last byte (default 1)
 *   ++eos [0|1|2|3]        terminator appended to commands: CR+LF, CR, LF, none (default 2)
 *   ++read [eoi|<byte>]    read from the instrument until EOI / the byte / timeout
 *   ++read_tmo_ms [ms]     read timeout (default 3000)
 *   ++ifc  ++clr  ++trg  ++loc  ++llo  ++spoll [pad]  ++srq  ++ver  ++mode [1]  ++rst  ++help
 * ESC (0x1B) escapes the next byte in instrument data (Prologix convention).
 *
 * A small window with a Quit button keeps the process visible; the socket loop runs on a
 * worker thread. Events are logged to \gpibsrv.log.
 */
#include "ce/ce_api.h"
#include "ce/ce_gui.h"
#include "ce/ce_winsock.h"
#include "gpib/drv_log.h"
#include "gpib/gpib_ioctl.h"
#include "gpib/ib.h"
#include "rt/rt.h"

#define PORT 1234
#define VERSION_TEXT "jornada-gpib gateway 0.1 (Prologix GPIB-ETHERNET compatible)"
#define LINE_MAX 1024
#define READ_MAX 8192
#define ID_QUIT 1
#define CLASS_NAME L"jornada-gpibsrv"

typedef struct gateway {
    int pad, sad;
    int auto_read;
    int eoi;
    int eos;
    unsigned read_tmo_ms;
    SOCKET listener, client;
    HANDLE thread;
    volatile BOOL stop;
    HWND win, label;
    UINT32 requests;
} gateway;

static gateway gw;

/* ---- socket helpers ---------------------------------------------------------------- */

static void send_all(SOCKET s, const char *data, int len)
{
    while (len > 0) {
        int n = send(s, data, len, 0);
        if (n <= 0) {
            return;
        }
        data += n;
        len -= n;
    }
}

static void send_text(SOCKET s, const char *text)
{
    send_all(s, text, (int)rt_strlen(text));
}

static void send_number(SOCKET s, int v)
{
    char buf[16];
    rt_fmt8(buf, sizeof buf, L"%d\n", v);
    send_text(s, buf);
}

/* ---- GPIB operations through the driver ---------------------------------------------- */

static int timeout_code(unsigned ms)
{
    int code;
    for (code = IB_T10us; code < IB_T1000s; code++) {
        if (ib_timeout_ms(code) >= ms) {
            return code;
        }
    }
    return IB_T1000s;
}

static int open_target(void)
{
    return ibdev(0, gw.pad, gw.sad, timeout_code(gw.read_tmo_ms), gw.eoi, 0);
}

static void do_read(SOCKET s, int eos_byte)
{
    static char buf[READ_MAX];
    int ud = open_target();
    if (ud < 0) {
        return;
    }
    if (eos_byte >= 0) {
        ibeos(ud, IB_REOS | IB_BIN | (eos_byte & 0xFF));
    }
    for (;;) {
        ibrd(ud, buf, READ_MAX);
        if (ibcnt > 0) {
            send_all(s, buf, (int)ibcnt);
        }
        if ((ibsta & IB_ERR) || (ibsta & IB_END) || ibcnt == 0) {
            break;
        }
    }
    ibonl(ud, 0);
}

static void do_write(SOCKET s, const char *data, unsigned len)
{
    static char buf[LINE_MAX + 2];
    int ud;
    unsigned n = len;
    int query;
    if (len > LINE_MAX) {
        len = LINE_MAX;
        n = len;
    }
    memcpy(buf, data, len);
    query = len > 0 && data[len - 1] == '?';
    switch (gw.eos) {
    case 0: buf[n++] = '\r'; buf[n++] = '\n'; break;
    case 1: buf[n++] = '\r'; break;
    case 2: buf[n++] = '\n'; break;
    default: break;
    }
    ud = open_target();
    if (ud < 0) {
        return;
    }
    ibwrt(ud, buf, (long)n);
    ibonl(ud, 0);
    if (!(ibsta & IB_ERR) && gw.auto_read && query) {
        do_read(s, -1);
    }
}

static int parse_int(const char *s, int *out)
{
    int v = 0;
    int any = 0;
    while (*s == ' ') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        any = 1;
        s++;
    }
    *out = v;
    return any;
}

static const char *skip_word(const char *s)
{
    while (*s != 0 && *s != ' ') {
        s++;
    }
    while (*s == ' ') {
        s++;
    }
    return s;
}

static int starts_with(const char *s, const char *word)
{
    unsigned i = 0;
    while (word[i] != 0) {
        if (s[i] != word[i]) {
            return 0;
        }
        i++;
    }
    return s[i] == 0 || s[i] == ' ';
}

static void controller_command(SOCKET s, const char *cmd)
{
    const char *arg = skip_word(cmd);
    int v;
    if (starts_with(cmd, "addr")) {
        if (parse_int(arg, &v) && v >= 0 && v <= 30) {
            int sad;
            gw.pad = v;
            gw.sad = parse_int(skip_word(arg), &sad) && sad >= 96 && sad <= 126 ? sad - 96 : -1;
        } else {
            send_number(s, gw.pad);
        }
    } else if (starts_with(cmd, "auto")) {
        if (parse_int(arg, &v)) {
            gw.auto_read = v != 0;
        } else {
            send_number(s, gw.auto_read);
        }
    } else if (starts_with(cmd, "eoi")) {
        if (parse_int(arg, &v)) {
            gw.eoi = v != 0;
        } else {
            send_number(s, gw.eoi);
        }
    } else if (starts_with(cmd, "eos")) {
        if (parse_int(arg, &v) && v >= 0 && v <= 3) {
            gw.eos = v;
        } else {
            send_number(s, gw.eos);
        }
    } else if (starts_with(cmd, "read_tmo_ms")) {
        if (parse_int(arg, &v) && v > 0) {
            gw.read_tmo_ms = (unsigned)v;
        } else {
            send_number(s, (int)gw.read_tmo_ms);
        }
    } else if (starts_with(cmd, "read")) {
        if (*arg == 0 || starts_with(arg, "eoi")) {
            do_read(s, -1);
        } else if (parse_int(arg, &v)) {
            do_read(s, v & 0xFF);
        }
    } else if (starts_with(cmd, "ifc")) {
        ibsic(0);
    } else if (starts_with(cmd, "clr")) {
        int ud = open_target();
        if (ud >= 0) {
            ibclr(ud);
            ibonl(ud, 0);
        }
    } else if (starts_with(cmd, "trg")) {
        int ud = open_target();
        if (ud >= 0) {
            ibtrg(ud);
            ibonl(ud, 0);
        }
    } else if (starts_with(cmd, "loc")) {
        int ud = open_target();
        if (ud >= 0) {
            ibloc(ud);
            ibonl(ud, 0);
        }
    } else if (starts_with(cmd, "llo")) {
        ibllo(0);
    } else if (starts_with(cmd, "spoll")) {
        int pad = gw.pad;
        int ud;
        char stb = 0;
        if (parse_int(arg, &v) && v >= 0 && v <= 30) {
            pad = v;
        }
        ud = ibdev(0, pad, IB_NO_SAD, timeout_code(gw.read_tmo_ms), 1, 0);
        if (ud >= 0) {
            ibrsp(ud, &stb);
            ibonl(ud, 0);
            if (!(ibsta & IB_ERR)) {
                send_number(s, (unsigned char)stb);
            }
        }
    } else if (starts_with(cmd, "srq")) {
        short lines = 0;
        iblines(0, &lines);
        send_number(s, (lines & 0x04) ? 1 : 0);
    } else if (starts_with(cmd, "ver")) {
        send_text(s, VERSION_TEXT "\n");
    } else if (starts_with(cmd, "mode")) {
        if (!parse_int(arg, &v)) {
            send_number(s, 1);
        }
    } else if (starts_with(cmd, "rst")) {
        gpib_probe_info p;
        DWORD got = 0;
        DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_PROBE, NULL, 0, &p, sizeof p, &got, NULL);
        ibsic(0);
    } else if (starts_with(cmd, "savecfg")) {
        /* accepted for compatibility, nothing to save */
    } else if (starts_with(cmd, "help")) {
        send_text(s, "++addr ++auto ++eoi ++eos ++read ++read_tmo_ms ++ifc ++clr ++trg ++loc ++llo ++spoll ++srq ++ver ++mode ++rst\n");
    } else if (starts_with(cmd, "lines")) {
        short lines = 0;
        iblines(0, &lines);
        send_number(s, lines & 0xFF);
    }
}

/* One complete line (terminator removed, escapes resolved). */
static void handle_line(SOCKET s, const char *line, unsigned len)
{
    gw.requests++;
    if (len >= 2 && line[0] == '+' && line[1] == '+') {
        char cmd[LINE_MAX];
        memcpy(cmd, line + 2, len - 2);
        cmd[len - 2] = 0;
        controller_command(s, cmd);
    } else if (len > 0) {
        do_write(s, line, len);
    }
}

static void serve_client(SOCKET s)
{
    static char line[LINE_MAX];
    unsigned n = 0;
    int escaped = 0;
    char buf[256];
    for (;;) {
        int got = recv(s, buf, sizeof buf, 0);
        int i;
        if (got <= 0) {
            return;
        }
        for (i = 0; i < got; i++) {
            char c = buf[i];
            if (escaped) {
                if (n < LINE_MAX) {
                    line[n++] = c;
                }
                escaped = 0;
            } else if (c == 0x1B) {
                escaped = 1;
            } else if (c == '\n' || c == '\r') {
                if (n > 0) {
                    handle_line(s, line, n);
                }
                n = 0;
            } else if (n < LINE_MAX) {
                line[n++] = c;
            }
        }
        if (gw.stop) {
            return;
        }
    }
}

static DWORD server_thread(LPVOID arg)
{
    sockaddr_in addr;
    int one = 1;
    (void)arg;
    gw.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gw.listener == INVALID_SOCKET) {
        log_printf(L"socket failed: %u", WSAGetLastError());
        return 1;
    }
    setsockopt(gw.listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(gw.listener, (const sockaddr *)&addr, sizeof addr) == SOCKET_ERROR ||
        listen(gw.listener, 1) == SOCKET_ERROR) {
        log_printf(L"bind/listen failed: %u", WSAGetLastError());
        closesocket(gw.listener);
        return 1;
    }
    log_printf(L"listening on port %u", PORT);
    while (!gw.stop) {
        sockaddr_in peer;
        int plen = sizeof peer;
        SOCKET c = accept(gw.listener, (sockaddr *)&peer, &plen);
        if (c == INVALID_SOCKET) {
            break;
        }
        log_printf(L"client connected");
        gw.client = c;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        serve_client(c);
        closesocket(c);
        gw.client = INVALID_SOCKET;
        log_printf(L"client disconnected after %u requests", gw.requests);
    }
    closesocket(gw.listener);
    return 0;
}

/* ---- tiny window with a Quit button ---------------------------------------------- */

static LRESULT wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_QUIT) {
            DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, msg, wp, lp);
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSW wc;
    WSADATA wsa;
    MSG msg;
    WCHAR text[96];
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    memset(&gw, 0, sizeof gw);
    gw.pad = 1;
    gw.sad = -1;
    gw.eoi = 1;
    gw.eos = 2;
    gw.read_tmo_ms = 3000;
    gw.listener = INVALID_SOCKET;
    gw.client = INVALID_SOCKET;
    log_set_path(L"\\gpibsrv.log");
    log_printf(L"gpibsrv starting");

    if (ibfind_board() < 0) {
        MessageBoxW(NULL, L"GPIB driver GPB1: not available. Insert the card first.", L"GPIB gateway", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        MessageBoxW(NULL, L"Winsock not available", L"GPIB gateway", MB_OK | MB_ICONERROR);
        return 1;
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);
    gw.win = CreateWindowExW(0, CLASS_NAME, L"GPIB gateway", WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
                             200, 60, 240, 80, NULL, NULL, hInstance, NULL);
    rt_fmt(text, 96, L"Prologix-compatible on TCP port %u", PORT);
    gw.label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, 8, 8, 224, 18, gw.win, NULL, hInstance, NULL);
    CreateWindowExW(0, L"BUTTON", L"Quit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 30, 64, 22, gw.win,
                    (HMENU)ID_QUIT, hInstance, NULL);
    gw.thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    gw.stop = TRUE;
    if (gw.client != INVALID_SOCKET) {
        closesocket(gw.client);
    }
    if (gw.listener != INVALID_SOCKET) {
        closesocket(gw.listener);
    }
    if (gw.thread != NULL) {
        if (WaitForSingleObject(gw.thread, 1000) != WAIT_OBJECT_0) {
            TerminateThread(gw.thread, 0);
        }
        CloseHandle(gw.thread);
    }
    WSACleanup();
    log_printf(L"gpibsrv stopped");
    return 0;
}
