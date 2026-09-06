/* gpibterm.exe: an instrument terminal for the Handheld PC screen (640 x 240).
 *
 * One window: address and command fields with Send / Query / Read, a row of bus operations
 * (IFC, Clear, Trigger, Poll, Local, Remote), a list of presets for the Tektronix TDS 300
 * family, a live bus-line indicator and a scrolling log. Enter in the command field sends.
 * Restrained by design: system colours, the default button is the only emphasis.
 */
#include "ce/ce_api.h"
#include "ce/ce_gui.h"
#include "gpib/gpib_ioctl.h"
#include "gpib/ib.h"
#include "rt/rt.h"

#define CLASS_NAME L"jornada-gpibterm"
#define TITLE L"GPIB terminal"

#define ID_ADDR 101
#define ID_CMD 102
#define ID_SEND 103
#define ID_QUERY 104
#define ID_READ 105
#define ID_IFC 106
#define ID_CLEAR 107
#define ID_TRIGGER 108
#define ID_POLL 109
#define ID_LOCAL 110
#define ID_REMOTE 111
#define ID_PRESET 112
#define ID_LOG 113
#define ID_STATUS 114
#define ID_CLEARLOG 115
#define ID_LBL_ADDR 120
#define ID_LBL_CMD 121
#define TIMER_LINES 1
#define TIMER_LINES_MS 700

#define MARGIN 4
#define ROW_H 22
#define GAP 4
#define LOG_LIMIT 24000
#define READ_MAX 4096
#define CMD_MAX 256
#define TIMEOUT_CODE IB_T3s

typedef struct preset {
    const WCHAR *label;
    const WCHAR *command;
} preset;

static const preset PRESETS[] = {
    { L"Identify (*IDN?)", L"*IDN?" },
    { L"Reset (*RST)", L"*RST" },
    { L"Clear status (*CLS)", L"*CLS" },
    { L"Event status (*ESR?)", L"*ESR?" },
    { L"All events (ALLEV?)", L"ALLEV?" },
    { L"Autoset", L"AUTOSET EXECUTE" },
    { L"Run", L"ACQUIRE:STATE RUN" },
    { L"Stop", L"ACQUIRE:STATE STOP" },
    { L"Single sequence", L"ACQUIRE:STOPAFTER SEQUENCE;STATE RUN" },
    { L"CH1 scale?", L"CH1:SCALE?" },
    { L"CH1 scale 1 V", L"CH1:SCALE 1.0" },
    { L"Timebase?", L"HORIZONTAL:MAIN:SCALE?" },
    { L"Trigger level?", L"TRIGGER:MAIN:LEVEL?" },
    { L"Measure frequency", L"MEASUREMENT:IMMED:TYPE FREQUENCY;:MEASUREMENT:IMMED:VALUE?" },
    { L"Measure Vpk-pk", L"MEASUREMENT:IMMED:TYPE PK2PK;:MEASUREMENT:IMMED:VALUE?" },
    { L"Measure mean", L"MEASUREMENT:IMMED:TYPE MEAN;:MEASUREMENT:IMMED:VALUE?" },
    { L"Waveform preamble", L"DATA:SOURCE CH1;:DATA:ENCDG ASCII;:WFMPRE?" },
    { L"Waveform data (CURVE?)", L"DATA:SOURCE CH1;:DATA:ENCDG ASCII;:CURVE?" },
};

typedef struct app {
    HINSTANCE inst;
    HWND win, addr, cmd, send, query, read, ifc, clear, trigger, poll, local, remote, preset, log, status, clearlog;
    HWND lbl_addr, lbl_cmd;
    HFONT logfont;
    int pad;
    BOOL driver_ok;
    UINT32 last_bsr, last_adsr;
} app;

static app g;

/* ---- log ------------------------------------------------------------------------- */

static void log_append(LPCWSTR text)
{
    int len = GetWindowTextLengthW(g.log);
    if (len > LOG_LIMIT) {
        SetWindowTextW(g.log, L"");
        len = 0;
    }
    SendMessageW(g.log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(g.log, EM_LINESCROLL, 0, (LPARAM)SendMessageW(g.log, EM_GETLINECOUNT, 0, 0));
}

static void log_line(LPCWSTR fmt, ...)
{
    WCHAR line[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = rt_vfmt(line, 510, fmt, ap);
    va_end(ap);
    if (n > 509) {
        n = 509;
    }
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}

/* Show instrument bytes as text: printable ASCII as is, control bytes as \\n \\r or . */
static void log_data(LPCWSTR prefix, const char *buf, long n)
{
    WCHAR line[520];
    unsigned k = 0;
    long i;
    while (prefix[k] != 0 && k < 8) {
        line[k] = prefix[k];
        k++;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (k >= 500) {
            line[k++] = '.';
            line[k++] = '.';
            line[k++] = '.';
            break;
        }
        if (c == '\n') {
            line[k++] = '\\';
            line[k++] = 'n';
        } else if (c == '\r') {
            line[k++] = '\\';
            line[k++] = 'r';
        } else if (c < 0x20 || c >= 0x7F) {
            line[k++] = '.';
        } else {
            line[k++] = c;
        }
    }
    line[k++] = '\r';
    line[k++] = '\n';
    line[k] = 0;
    log_append(line);
}

static void log_status(LPCWSTR what)
{
    if (ibsta & IB_ERR) {
        log_line(L"%s failed: %S", what, ib_error_name(iberr));
    }
}

/* ---- GPIB operations ---------------------------------------------------------------- */

static int read_address(void)
{
    WCHAR text[8];
    int v = 0;
    unsigned i;
    GetWindowTextW(g.addr, text, 8);
    for (i = 0; text[i] >= '0' && text[i] <= '9' && i < 2; i++) {
        v = v * 10 + (text[i] - '0');
    }
    if (i == 0 || v > 30) {
        return -1;
    }
    return v;
}

static int open_current(void)
{
    int pad = read_address();
    int ud;
    if (pad < 0) {
        log_line(L"address must be 0..30");
        return -1;
    }
    ud = ibdev(0, pad, IB_NO_SAD, TIMEOUT_CODE, 1, 0);
    if (ud < 0) {
        log_status(L"open");
    }
    return ud;
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

static void do_read(int ud)
{
    static char buf[READ_MAX];
    ibrd(ud, buf, READ_MAX);
    if (ibsta & IB_ERR) {
        log_status(L"read");
        if (ibcnt > 0) {
            log_data(L"< ", buf, ibcnt);
        }
        return;
    }
    log_data(L"< ", buf, ibcnt);
    if (!(ibsta & IB_END)) {
        log_line(L"(buffer full, more data pending: use Read)");
    }
}

static void do_send(BOOL then_read)
{
    WCHAR text[CMD_MAX];
    char cmd[CMD_MAX];
    int ud;
    GetWindowTextW(g.cmd, text, CMD_MAX);
    if (text[0] == 0) {
        return;
    }
    ud = open_current();
    if (ud < 0) {
        return;
    }
    narrow(text, cmd, sizeof cmd);
    log_line(L"> %s", text);
    ibwrt(ud, cmd, (long)rt_strlen(cmd));
    if (ibsta & IB_ERR) {
        log_status(L"send");
    } else if (then_read) {
        do_read(ud);
    }
    ibonl(ud, 0);
    SendMessageW(g.cmd, EM_SETSEL, 0, -1);
    SetFocus(g.cmd);
}

static void do_read_only(void)
{
    int ud = open_current();
    if (ud < 0) {
        return;
    }
    do_read(ud);
    ibonl(ud, 0);
}

static void do_board(int (*fn)(int), LPCWSTR name)
{
    fn(0);
    log_line(L"%s: %S", name, (ibsta & IB_ERR) ? ib_error_name(iberr) : "ok");
}

static void do_device(int (*fn)(int), LPCWSTR name)
{
    int ud = open_current();
    if (ud < 0) {
        return;
    }
    fn(ud);
    log_line(L"%s %d: %S", name, read_address(), (ibsta & IB_ERR) ? ib_error_name(iberr) : "ok");
    ibonl(ud, 0);
}

static void do_poll(void)
{
    char stb = 0;
    int ud = open_current();
    if (ud < 0) {
        return;
    }
    ibrsp(ud, &stb);
    if (ibsta & IB_ERR) {
        log_status(L"serial poll");
    } else {
        log_line(L"serial poll %d: status byte 0x%02x%S", read_address(), (unsigned char)stb,
                 (stb & 0x40) ? " (requesting service)" : "");
    }
    ibonl(ud, 0);
}

static void do_remote(void)
{
    int on = (int)SendMessageW(g.remote, BM_GETCHECK, 0, 0);
    ibsre(0, on);
    log_line(L"remote enable %S: %S", on ? "on" : "off", (ibsta & IB_ERR) ? ib_error_name(iberr) : "ok");
}

static void update_status(void)
{
    gpib_lines_info li;
    DWORD got = 0;
    WCHAR text[96];
    if (!g.driver_ok) {
        if (ibfind_board() == 0) {
            g.driver_ok = TRUE;
            log_line(L"driver GPB1: opened");
        } else {
            SetWindowTextW(g.status, L"driver not loaded: insert the GPIB card");
            return;
        }
    }
    if (!DeviceIoControl(ib_driver_handle(), IOCTL_GPIB_LINES, NULL, 0, &li, sizeof li, &got, NULL)) {
        SetWindowTextW(g.status, L"card not responding");
        return;
    }
    if (li.bsr == g.last_bsr && li.adsr == g.last_adsr) {
        return;
    }
    g.last_bsr = li.bsr;
    g.last_adsr = li.adsr;
    rt_fmt(text, 96, L"%S %S %S %S %S %S %S %S   %S",
           (li.adsr & 0x80) ? "CIC" : "cic",
           (li.bsr & 0x80) ? "ATN" : "atn",
           (li.bsr & 0x40) ? "DAV" : "dav",
           (li.bsr & 0x10) ? "NRFD" : "nrfd",
           (li.bsr & 0x20) ? "NDAC" : "ndac",
           (li.bsr & 0x08) ? "EOI" : "eoi",
           (li.bsr & 0x04) ? "SRQ" : "srq",
           (li.bsr & 0x01) ? "REN" : "ren",
           (li.adsr & 0x02) ? "talker" : (li.adsr & 0x04) ? "listener" : "idle");
    SetWindowTextW(g.status, text);
}

/* ---- window -------------------------------------------------------------------- */

static HWND make(LPCWSTR cls, LPCWSTR text, DWORD style, int id)
{
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.win,
                           (HMENU)(UINT_PTR)id, g.inst, NULL);
}

static void create_children(void)
{
    unsigned i;
    LOGFONTW lf;
    g.lbl_addr = make(L"STATIC", L"Address", SS_LEFT, ID_LBL_ADDR);
    g.addr = make(L"EDIT", L"1", WS_BORDER | WS_TABSTOP | ES_LEFT, ID_ADDR);
    g.lbl_cmd = make(L"STATIC", L"Command", SS_LEFT, ID_LBL_CMD);
    g.cmd = make(L"EDIT", L"*IDN?", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_CMD);
    g.send = make(L"BUTTON", L"Send", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_SEND);
    g.query = make(L"BUTTON", L"Query", WS_TABSTOP | BS_PUSHBUTTON, ID_QUERY);
    g.read = make(L"BUTTON", L"Read", WS_TABSTOP | BS_PUSHBUTTON, ID_READ);
    g.preset = make(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_PRESET);
    g.ifc = make(L"BUTTON", L"IFC", WS_TABSTOP | BS_PUSHBUTTON, ID_IFC);
    g.clear = make(L"BUTTON", L"Clear", WS_TABSTOP | BS_PUSHBUTTON, ID_CLEAR);
    g.trigger = make(L"BUTTON", L"Trigger", WS_TABSTOP | BS_PUSHBUTTON, ID_TRIGGER);
    g.poll = make(L"BUTTON", L"Poll", WS_TABSTOP | BS_PUSHBUTTON, ID_POLL);
    g.local = make(L"BUTTON", L"Local", WS_TABSTOP | BS_PUSHBUTTON, ID_LOCAL);
    g.remote = make(L"BUTTON", L"Remote", WS_TABSTOP | BS_AUTOCHECKBOX, ID_REMOTE);
    g.clearlog = make(L"BUTTON", L"Clear log", WS_TABSTOP | BS_PUSHBUTTON, ID_CLEARLOG);
    g.status = make(L"STATIC", L"", SS_LEFT, ID_STATUS);
    g.log = make(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, ID_LOG);
    SendMessageW(g.log, EM_SETLIMITTEXT, 0, 0);
    for (i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) {
        SendMessageW(g.preset, CB_ADDSTRING, 0, (LPARAM)PRESETS[i].label);
    }
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -11;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    rt_wcscpy_n(lf.lfFaceName, LF_FACESIZE, L"Courier New");
    g.logfont = CreateFontIndirectW(&lf);
    if (g.logfont != NULL) {
        SendMessageW(g.log, WM_SETFONT, (WPARAM)g.logfont, TRUE);
    }
}

static void place(HWND h, int x, int y, int w, int hgt)
{
    MoveWindow(h, x, y, w, hgt, TRUE);
}

static void layout(void)
{
    RECT rc;
    int x, y, w;
    GetClientRect(g.win, &rc);
    w = rc.right - rc.left;
    y = MARGIN;
    x = MARGIN;
    /* row 1: Address [__] Command [________________] [Send] [Query] [Read] */
    place(g.lbl_addr, x, y + 4, 50, 16);
    place(g.addr, x + 52, y, 30, ROW_H);
    place(g.lbl_cmd, x + 52 + 30 + GAP, y + 4, 58, 16);
    place(g.cmd, x + 52 + 30 + GAP + 60, y, w - (x + 52 + 30 + GAP + 60) - 3 * (58 + GAP) - MARGIN, ROW_H);
    place(g.send, w - 3 * (58 + GAP) - MARGIN + GAP, y, 58, ROW_H);
    place(g.query, w - 2 * (58 + GAP) - MARGIN + GAP, y, 58, ROW_H);
    place(g.read, w - (58 + GAP) - MARGIN + GAP, y, 58, ROW_H);
    y += ROW_H + GAP;
    /* row 2: presets combo, bus buttons, remote checkbox, clear log */
    place(g.preset, x, y, 200, 120);
    place(g.ifc, x + 204, y, 40, ROW_H);
    place(g.clear, x + 248, y, 48, ROW_H);
    place(g.trigger, x + 300, y, 56, ROW_H);
    place(g.poll, x + 360, y, 40, ROW_H);
    place(g.local, x + 404, y, 48, ROW_H);
    place(g.remote, x + 456, y, 70, ROW_H);
    place(g.clearlog, w - 72 - MARGIN, y, 72, ROW_H);
    y += ROW_H + GAP;
    /* row 3: status line */
    place(g.status, x, y, w - 2 * MARGIN, 16);
    y += 16 + 2;
    /* log fills the rest */
    place(g.log, x, y, w - 2 * MARGIN, rc.bottom - y - MARGIN);
}

static void on_command(int id, int code)
{
    switch (id) {
    case ID_SEND:
        do_send(FALSE);
        break;
    case ID_QUERY:
        do_send(TRUE);
        break;
    case ID_READ:
        do_read_only();
        break;
    case ID_IFC:
        do_board(ibsic, L"interface clear");
        break;
    case ID_CLEAR:
        do_device(ibclr, L"device clear");
        break;
    case ID_TRIGGER:
        do_device(ibtrg, L"trigger");
        break;
    case ID_POLL:
        do_poll();
        break;
    case ID_LOCAL:
        do_device(ibloc, L"go to local");
        break;
    case ID_REMOTE:
        do_remote();
        break;
    case ID_CLEARLOG:
        SetWindowTextW(g.log, L"");
        break;
    case ID_PRESET:
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g.preset, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)(sizeof PRESETS / sizeof PRESETS[0])) {
                SetWindowTextW(g.cmd, PRESETS[sel].command);
                SetFocus(g.cmd);
                SendMessageW(g.cmd, EM_SETSEL, 0, -1);
            }
        }
        break;
    default:
        break;
    }
}

static LRESULT wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g.win = h;
        create_children();
        layout();
        SetTimer(h, TIMER_LINES, TIMER_LINES_MS, NULL);
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_COMMAND:
        on_command((int)LOWORD(wp), (int)HIWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == TIMER_LINES) {
            update_status();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_LINES);
        if (g.logfont != NULL) {
            DeleteObject(g.logfont);
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, msg, wp, lp);
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSW wc;
    MSG msg;
    RECT work;
    int x = 0, y = 0, w, hgt;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    memset(&g, 0, sizeof g);
    g.inst = hInstance;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    if (RegisterClassW(&wc) == 0) {
        MessageBoxW(NULL, L"RegisterClass failed", TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    w = GetSystemMetrics(SM_CXSCREEN);
    hgt = GetSystemMetrics(SM_CYSCREEN);
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        x = work.left;
        y = work.top;
        w = work.right - work.left;
        hgt = work.bottom - work.top;
    }
    g.win = CreateWindowExW(0, CLASS_NAME, TITLE, WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            x, y, w, hgt, NULL, NULL, hInstance, NULL);
    if (g.win == NULL) {
        MessageBoxW(NULL, L"CreateWindow failed", TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(g.win, SW_SHOW);
    UpdateWindow(g.win);
    SetForegroundWindow(g.win);
    log_line(L"jornada-gpib terminal. Enter sends; Query sends and reads.");
    update_status();
    SetFocus(g.cmd);
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(g.win, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
