/* Winsock 1.1 as shipped in Windows CE 2.11 (winsock.dll), the subset used by gpibsrv. */
#ifndef CE_WINSOCK_H
#define CE_WINSOCK_H

#include "ce_api.h"

typedef UINT SOCKET;
#define INVALID_SOCKET ((SOCKET)~0u)
#define SOCKET_ERROR (-1)

#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define INADDR_ANY 0u
#define SOL_SOCKET 0xFFFF
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define TCP_NODELAY 0x0001
#define SD_BOTH 2
#define FD_SETSIZE 8
#define FIONBIO 0x8004667Eu
#define WSAEWOULDBLOCK 10035

typedef struct in_addr { DWORD s_addr; } in_addr;
typedef struct sockaddr_in {
    SHORT sin_family;
    WORD sin_port;
    in_addr sin_addr;
    CHAR sin_zero[8];
} sockaddr_in;
typedef struct sockaddr { WORD sa_family; CHAR sa_data[14]; } sockaddr;
typedef struct fd_set { UINT fd_count; SOCKET fd_array[FD_SETSIZE]; } fd_set;
typedef struct timeval { LONG tv_sec; LONG tv_usec; } timeval;

typedef struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
    CHAR szDescription[257];
    CHAR szSystemStatus[129];
    WORD iMaxSockets;
    WORD iMaxUdpDg;
    CHAR *lpVendorInfo;
} WSADATA;

#define MAKEWORD(lo, hi) ((WORD)(((BYTE)(lo)) | ((WORD)((BYTE)(hi))) << 8))
#define WSAGetLastError() GetLastError()

int    WSAStartup(WORD version, WSADATA *) CE_IMPORT(WSAStartup);
int    WSACleanup(VOID) CE_IMPORT(WSACleanup);
SOCKET socket(int af, int type, int protocol) CE_IMPORT(socket);
int    bind(SOCKET, const sockaddr *, int) CE_IMPORT(bind);
int    listen(SOCKET, int backlog) CE_IMPORT(listen);
SOCKET accept(SOCKET, sockaddr *, int *) CE_IMPORT(accept);
int    recv(SOCKET, char *, int, int flags) CE_IMPORT(recv);
int    send(SOCKET, const char *, int, int flags) CE_IMPORT(send);
int    closesocket(SOCKET) CE_IMPORT(closesocket);
int    setsockopt(SOCKET, int level, int opt, const char *, int) CE_IMPORT(setsockopt);
int    select(int nfds, fd_set *r, fd_set *w, fd_set *e, const timeval *) CE_IMPORT(select);
int    ioctlsocket(SOCKET, LONG cmd, DWORD *) CE_IMPORT(ioctlsocket);
int    shutdown(SOCKET, int) CE_IMPORT(shutdown);
WORD   htons(WORD) CE_IMPORT(htons);
WORD   ntohs(WORD) CE_IMPORT(ntohs);

#endif /* CE_WINSOCK_H */
