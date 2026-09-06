/* nettest.exe: staged probe of the two facilities the gateway needs, secondary threads and
 * Winsock, written so that every step is logged (file closed after each line) before the next
 * step runs. If the device locks up, \nettest.txt shows the last step that completed.
 *
 *   nettest            run every stage
 *   nettest thread     only the thread stage
 *   nettest socket     only the socket stages (main thread, select with a timeout, no accept)
 *   nettest accept     socket stages plus a blocking accept for up to 20 seconds
 */
#include "ce/ce_api.h"
#include "ce/ce_winsock.h"
#include "gpib/drv_log.h"
#include "rt/rt.h"

#define PORT 1234

static volatile int thread_ran;

static DWORD trivial_thread(LPVOID arg)
{
    (void)arg;
    log_printf(L"  thread: running");
    thread_ran = 1;
    Sleep(50);
    log_printf(L"  thread: returning");
    return 7;
}

static void stage_thread(void)
{
    HANDLE t;
    DWORD rc;
    log_printf(L"stage thread: CreateThread");
    t = CreateThread(NULL, 0, trivial_thread, NULL, 0, NULL);
    log_printf(L"  CreateThread -> %p (error %u)", (void *)t, GetLastError());
    if (t == NULL) {
        return;
    }
    rc = WaitForSingleObject(t, 5000);
    log_printf(L"  WaitForSingleObject -> %u, thread_ran %d", rc, thread_ran);
    CloseHandle(t);
}

static SOCKET stage_socket(void)
{
    WSADATA wsa;
    SOCKET s;
    sockaddr_in addr;
    int one = 1;
    int rc;
    log_printf(L"stage socket: WSAStartup");
    rc = WSAStartup(MAKEWORD(1, 1), &wsa);
    log_printf(L"  WSAStartup -> %d, version 0x%04x, %S", rc, (UINT32)wsa.wVersion, wsa.szDescription);
    if (rc != 0) {
        return INVALID_SOCKET;
    }
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    log_printf(L"  socket -> 0x%x (error %u)", s, GetLastError());
    if (s == INVALID_SOCKET) {
        return s;
    }
    rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    log_printf(L"  setsockopt -> %d", rc);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    log_printf(L"  htons(%u) = 0x%04x", PORT, (UINT32)addr.sin_port);
    rc = bind(s, (const sockaddr *)&addr, sizeof addr);
    log_printf(L"  bind -> %d (error %u)", rc, GetLastError());
    rc = listen(s, 1);
    log_printf(L"  listen -> %d (error %u)", rc, GetLastError());
    {
        fd_set rd;
        timeval tv;
        rd.fd_count = 1;
        rd.fd_array[0] = s;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        rc = select(0, &rd, NULL, NULL, &tv);
        log_printf(L"  select(3 s) -> %d (error %u)", rc, GetLastError());
    }
    return s;
}

static void stage_accept(SOCKET s)
{
    fd_set rd;
    timeval tv;
    int rc;
    log_printf(L"stage accept: waiting up to 20 s for a connection on port %u", PORT);
    rd.fd_count = 1;
    rd.fd_array[0] = s;
    tv.tv_sec = 20;
    tv.tv_usec = 0;
    rc = select(0, &rd, NULL, NULL, &tv);
    log_printf(L"  select(20 s) -> %d", rc);
    if (rc > 0) {
        sockaddr_in peer;
        int plen = sizeof peer;
        SOCKET c = accept(s, (sockaddr *)&peer, &plen);
        log_printf(L"  accept -> 0x%x (error %u)", c, GetLastError());
        if (c != INVALID_SOCKET) {
            char buf[64];
            int n = recv(c, buf, sizeof buf - 1, 0);
            log_printf(L"  recv -> %d", n);
            if (n > 0) {
                buf[n] = 0;
                log_printf(L"  got: %S", buf);
                send(c, "hello from the jornada\n", 23, 0);
            }
            closesocket(c);
        }
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    int do_thread = 1;
    int do_socket = 1;
    int do_accept = 1;
    SOCKET s = INVALID_SOCKET;
    (void)hInstance;
    (void)hPrev;
    (void)nCmdShow;
    log_set_path(L"\\nettest.txt");
    log_printf(L"nettest start [%s]", lpCmdLine);
    if (rt_wcscmp(lpCmdLine, L"thread") == 0) {
        do_socket = 0;
        do_accept = 0;
    } else if (rt_wcscmp(lpCmdLine, L"socket") == 0) {
        do_thread = 0;
        do_accept = 0;
    } else if (rt_wcscmp(lpCmdLine, L"accept") == 0) {
        do_thread = 0;
    }
    if (do_thread) {
        stage_thread();
    }
    if (do_socket) {
        s = stage_socket();
        if (s != INVALID_SOCKET && do_accept) {
            stage_accept(s);
        }
        if (s != INVALID_SOCKET) {
            closesocket(s);
            log_printf(L"  closesocket done");
        }
        WSACleanup();
        log_printf(L"  WSACleanup done");
    }
    log_printf(L"nettest end");
    return 0;
}
