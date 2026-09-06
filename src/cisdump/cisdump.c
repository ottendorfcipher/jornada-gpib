/* cisdump.exe: dump the CIS of the card in each PC Card socket and probe the socket driver.
 *
 * Writes \cisdump.txt with: system info, per socket the card status, every tuple as a hex
 * dump with the interesting ones decoded (MANFID, VERS_1, FUNCID, CONFIG, CFTABLE_ENTRY),
 * the Windows CE PnP identifier the device manager derives, and the outcome of requesting
 * 8-bit and 16-bit I/O windows. Read it back with:  jornada get '\cisdump.txt'
 */
#include "ce/ce_api.h"
#include "ce/ce_cardserv.h"
#include "gpib/drv_log.h"
#include "gpib/pnpid.h"
#include "rt/rt.h"

#define TUPLE_BUF 300
#define MAX_SOCKETS 2
#define MAX_TUPLES 64

static cs_table cs;
static CARD_CLIENT_HANDLE client;

typedef struct tuple_buffer {
    CARD_DATA_PARMS parms;
    UINT8 data[TUPLE_BUF];
} tuple_buffer;

static void decode_tuple(UINT8 code, const UINT8 *d, unsigned len, pnpid_builder *pnp)
{
    pnpid_add_tuple(pnp, code, d, len);
    switch (code) {
    case CISTPL_MANFID:
        if (len >= 4) {
            log_printf(L"  MANFID: manufacturer 0x%04x card 0x%04x", (UINT32)(d[0] | (d[1] << 8)),
                       (UINT32)(d[2] | (d[3] << 8)));
        }
        break;
    case CISTPL_VERS_1: {
        unsigned i = 2;
        unsigned field = 0;
        char s[80];
        log_printf(L"  VERS_1: major %u minor %u", (UINT32)d[0], (UINT32)d[1]);
        while (i < len && d[i] != 0xFF && field < 4) {
            unsigned n = 0;
            while (i < len && d[i] != 0 && n + 1 < sizeof s) {
                s[n++] = (char)d[i++];
            }
            s[n] = 0;
            i++;
            log_printf(L"    string %u: \"%S\"", field++, s);
        }
        break;
    }
    case CISTPL_FUNCID:
        if (len >= 1) {
            log_printf(L"  FUNCID: function type %u", (UINT32)d[0]);
        }
        break;
    case CISTPL_CONFIG:
        if (len >= 2) {
            unsigned rasz = (d[0] & 3) + 1;
            unsigned rmsz = ((d[0] >> 2) & 15) + 1;
            unsigned i;
            UINT32 base = 0;
            for (i = 0; i < rasz && 2 + i < len; i++) {
                base |= (UINT32)d[2 + i] << (8 * i);
            }
            log_printf(L"  CONFIG: last index 0x%02x, config register base 0x%x, mask size %u",
                       (UINT32)d[1], base, rmsz);
        }
        break;
    case CISTPL_CFTABLE_ENTRY:
        if (len >= 1) {
            log_printf(L"  CFTABLE_ENTRY: index 0x%02x%S%S", (UINT32)(d[0] & 0x3F),
                       (d[0] & 0x40) ? " default" : "", (d[0] & 0x80) ? " +interface" : "");
        }
        break;
    default:
        break;
    }
}

static void dump_parsed_cftable(CS_SOCKET sock)
{
    PARSED_CFTABLE cft[8];
    UINT32 n = 8;
    UINT32 i;
    STATUS st;
    memset(cft, 0, sizeof cft);
    st = cs_GetParsedTuple(&cs, sock, CISTPL_CFTABLE_ENTRY, cft, &n);
    if (st != CERR_SUCCESS) {
        log_printf(L"CardGetParsedTuple(CFTABLE_ENTRY) failed: 0x%x", st);
        return;
    }
    log_printf(L"parsed configuration table: %u entries", n);
    for (i = 0; i < n; i++) {
        log_printf(L"  [%u] index 0x%02x defaults %u iface %u type %u io entries %u base 0x%x len %u access %u addr lines %u vcc %u mV static %u mA",
                   i, (UINT32)cft[i].ConfigIndex, (UINT32)cft[i].ContainsDefaults,
                   (UINT32)cft[i].IFacePresent, (UINT32)cft[i].IFaceType,
                   (UINT32)cft[i].NumIOEntries, cft[i].IOBase[0], cft[i].IOLength[0],
                   (UINT32)cft[i].IOAccess, (UINT32)cft[i].NumIOAddrLines,
                   (UINT32)cft[i].VccDescr.NominalV, (UINT32)cft[i].VccDescr.StaticI);
    }
}

static void try_window(CS_SOCKET sock, UINT16 attrs, UINT32 size, LPCWSTR label)
{
    CARD_WINDOW_PARMS wp;
    CARD_WINDOW_HANDLE w;
    memset(&wp, 0, sizeof wp);
    wp.hSocket.uSocket = CS_SOCKET_NUMBER(sock);
    wp.hSocket.uFunction = CS_SOCKET_FUNCTION(sock);
    wp.fAttributes = attrs;
    wp.uWindowSize = size;
    wp.fAccessSpeed = WIN_SPEED_USE_WAIT;
    w = cs_RequestWindow(&cs, client, &wp);
    if (w == NULL) {
        log_printf(L"window %s (%u bytes): refused, error %u", label, size, GetLastError());
        return;
    }
    log_printf(L"window %s (%u bytes): granted", label, size);
    cs_ReleaseWindow(&cs, w);
}

static void dump_socket(UINT8 socket)
{
    CS_SOCKET sock = CS_SOCKET_MAKE(socket, 0);
    CARD_STATUS st;
    CARD_TUPLE_PARMS tp;
    tuple_buffer tb;
    pnpid_builder pnp;
    char id[PNPID_MAX];
    unsigned count = 0;
    STATUS rc;

    memset(&st, 0, sizeof st);
    st.hSocket.uSocket = socket;
    rc = cs_GetStatus(&cs, &st);
    log_printf(L"socket %u: CardGetStatus 0x%x card state 0x%04x socket state 0x%04x", (UINT32)socket,
               rc, (UINT32)st.fCardState, (UINT32)st.fSocketState);
    if (rc != CERR_SUCCESS || !(st.fCardState & EVENT_MASK_CARD_DETECT)) {
        log_printf(L"socket %u: no card", (UINT32)socket);
        return;
    }
    pnpid_begin(&pnp);
    memset(&tp, 0, sizeof tp);
    tp.hSocket.uSocket = socket;
    tp.fAttributes = 0;
    tp.uDesiredTuple = 0xFF;
    rc = cs_GetFirstTuple(&cs, &tp);
    while (rc == CERR_SUCCESS && count < MAX_TUPLES) {
        memset(&tb, 0, sizeof tb);
        memcpy(&tb.parms, &tp, sizeof tp);          /* same layout up to the private fields */
        tb.parms.uTupleOffset = 0;
        tb.parms.uBufLen = TUPLE_BUF;
        tb.parms.uDataLen = 0;
        rc = cs_GetTupleData(&cs, &tb.parms);
        if (rc != CERR_SUCCESS) {
            log_printf(L"tuple 0x%02x link %u: CardGetTupleData failed 0x%x", (UINT32)tp.uTupleCode,
                       (UINT32)tp.uTupleLink, rc);
            break;
        }
        {
            WCHAR label[40];
            rt_fmt(label, 40, L"tuple 0x%02x link %u", (UINT32)tp.uTupleCode, (UINT32)tp.uTupleLink);
            log_hex(label, tb.data, tb.parms.uDataLen);
        }
        decode_tuple(tp.uTupleCode, tb.data, tb.parms.uDataLen, &pnp);
        count++;
        if (tp.uTupleCode == CISTPL_END) {
            break;
        }
        rc = cs_GetNextTuple(&cs, &tp);
    }
    if (rc != CERR_SUCCESS && rc != CERR_NO_MORE_ITEMS) {
        log_printf(L"tuple walk ended with 0x%x after %u tuples", rc, count);
    }
    pnpid_finish(&pnp, id, sizeof id);
    log_printf(L"Windows CE PnP identifier: %S", id);
    log_printf(L"registry key: HKLM\\Drivers\\PCMCIA\\%S", id);
    dump_parsed_cftable(sock);
    try_window(sock, WIN_ATTR_IO_SPACE, 32, L"8-bit I/O");
    try_window(sock, WIN_ATTR_IO_SPACE | WIN_ATTR_16BIT, 32, L"16-bit I/O");
    try_window(sock, WIN_ATTR_ATTRIBUTE, 4096, L"attribute memory");
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    SYSTEM_INFO si;
    CARD_REGISTER_PARMS rp;
    UINT8 socket;
    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    log_set_path(L"\\cisdump.txt");
    DeleteFileW(L"\\cisdump.txt");
    log_printf(L"jornada-gpib cisdump");
    memset(&si, 0, sizeof si);
    GetSystemInfo(&si);
    log_printf(L"page size %u, processor type %u", si.dwPageSize, si.dwProcessorType);
    if (!cs_bind(&cs)) {
        log_printf(L"cannot bind pcmcia.dll (error %u)", GetLastError());
        MessageBoxW(NULL, L"pcmcia.dll Card Services not available", L"cisdump", MB_OK | MB_ICONERROR);
        return 1;
    }
    rp.fAttributes = CLIENT_ATTR_IO_DRIVER;
    rp.fEventMask = 0;
    rp.uClientData = 0;
    client = cs_RegisterClient(&cs, NULL, &rp);
    if (client == NULL) {
        log_printf(L"CardRegisterClient failed: %u", GetLastError());
        cs_unbind(&cs);
        MessageBoxW(NULL, L"CardRegisterClient failed", L"cisdump", MB_OK | MB_ICONERROR);
        return 1;
    }
    for (socket = 0; socket < MAX_SOCKETS; socket++) {
        dump_socket(socket);
    }
    cs_DeregisterClient(&cs, client);
    cs_unbind(&cs);
    log_printf(L"done");
    MessageBoxW(NULL, L"CIS dump written to \\cisdump.txt", L"cisdump", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    return 0;
}
