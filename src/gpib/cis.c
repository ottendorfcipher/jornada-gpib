/* CIS walk from inside the driver. See include/gpib/cis.h.
 *
 * Card Services can only be used from inside device.exe (pcmcia.dll refuses to initialise a
 * second instance in another process), so this runs in GPB_Init and the result is exposed
 * through IOCTL_GPIB_CIS and the log.
 */
#include "gpib/cis.h"
#include "gpib/drv_log.h"
#include "rt/rt.h"

typedef struct tuple_buffer {
    CARD_DATA_PARMS parms;
    UINT8 data[CIS_TUPLE_BUF];
} tuple_buffer;

static void decode(UINT8 code, const UINT8 *d, unsigned len, cis_capture *cap)
{
    switch (code) {
    case CISTPL_MANFID:
        if (len >= 4) {
            cap->manufacturer_id = (UINT32)(d[0] | (d[1] << 8));
            cap->card_id = (UINT32)(d[2] | (d[3] << 8));
            log_printf(L"  MANFID: manufacturer 0x%04x card 0x%04x", cap->manufacturer_id, cap->card_id);
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
            cap->function_type = d[0];
            log_printf(L"  FUNCID: function type %u", (UINT32)d[0]);
        }
        break;
    case CISTPL_CONFIG:
        if (len >= 2) {
            unsigned rasz = (d[0] & 3) + 1;
            unsigned i;
            UINT32 base = 0;
            for (i = 0; i < rasz && 2 + i < len; i++) {
                base |= (UINT32)d[2 + i] << (8 * i);
            }
            log_printf(L"  CONFIG: last index 0x%02x, config register base 0x%x", (UINT32)d[1], base);
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

static void capture_raw(cis_capture *cap, UINT8 code, const UINT8 *d, unsigned len)
{
    if (cap->raw_len + 2 + len > CIS_CAPTURE_MAX) {
        return;
    }
    cap->raw[cap->raw_len++] = code;
    cap->raw[cap->raw_len++] = (UINT8)len;
    memcpy(cap->raw + cap->raw_len, d, len);
    cap->raw_len += len;
}

BOOL cis_capture_socket(const cs_table *cs, CS_SOCKET sock, cis_capture *cap)
{
    CARD_TUPLE_PARMS tp;
    tuple_buffer tb;
    pnpid_builder pnp;
    STATUS rc;

    memset(cap, 0, sizeof *cap);
    pnpid_begin(&pnp);
    memset(&tp, 0, sizeof tp);
    tp.hSocket.uSocket = CS_SOCKET_NUMBER(sock);
    tp.hSocket.uFunction = 0;
    tp.fAttributes = 0;
    tp.uDesiredTuple = 0xFF;
    rc = cs_GetFirstTuple(cs, &tp);
    while (rc == CERR_SUCCESS && cap->tuples < CIS_MAX_TUPLES) {
        WCHAR label[40];
        memset(&tb, 0, sizeof tb);
        memcpy(&tb.parms, &tp, sizeof tp);       /* same layout up to the private fields */
        tb.parms.uTupleOffset = 0;
        tb.parms.uBufLen = CIS_TUPLE_BUF;
        tb.parms.uDataLen = 0;
        rc = cs_GetTupleData(cs, &tb.parms);
        if (rc != CERR_SUCCESS) {
            log_printf(L"tuple 0x%02x link %u: CardGetTupleData failed 0x%x", (UINT32)tp.uTupleCode,
                       (UINT32)tp.uTupleLink, rc);
            break;
        }
        rt_fmt(label, 40, L"tuple 0x%02x link %u", (UINT32)tp.uTupleCode, (UINT32)tp.uTupleLink);
        log_hex(label, tb.data, tb.parms.uDataLen);
        capture_raw(cap, tp.uTupleCode, tb.data, tb.parms.uDataLen);
        pnpid_add_tuple(&pnp, tp.uTupleCode, tb.data, tb.parms.uDataLen);
        decode(tp.uTupleCode, tb.data, tb.parms.uDataLen, cap);
        cap->tuples++;
        if (tp.uTupleCode == CISTPL_END) {
            break;
        }
        rc = cs_GetNextTuple(cs, &tp);
    }
    if (rc != CERR_SUCCESS && rc != CERR_NO_MORE_ITEMS) {
        log_printf(L"tuple walk ended with 0x%x after %u tuples", rc, cap->tuples);
    }
    pnpid_finish(&pnp, cap->pnpid, sizeof cap->pnpid);
    log_printf(L"PnP identifier: %S (%u tuples, %u raw bytes)", cap->pnpid, cap->tuples, cap->raw_len);
    return cap->tuples > 0;
}
