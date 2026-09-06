/* CIS walk from inside the driver: raw tuple capture, decoded log lines, PnP identifier. */
#ifndef GPIB_CIS_H
#define GPIB_CIS_H

#include "ce/ce_cardserv.h"
#include "gpib/pnpid.h"

#define CIS_CAPTURE_MAX 1024      /* raw tuples kept for IOCTL_GPIB_CIS */
#define CIS_TUPLE_BUF 300
#define CIS_MAX_TUPLES 64

typedef struct cis_capture {
    UINT8 raw[CIS_CAPTURE_MAX];   /* sequence of: code, length, data bytes */
    UINT32 raw_len;
    UINT32 tuples;
    char pnpid[PNPID_MAX];
    UINT32 manufacturer_id, card_id;
    UINT8 function_type;
} cis_capture;

/* Walks the CIS of function 0 of the socket, logging each tuple, filling cap. */
BOOL cis_capture_socket(const cs_table *cs, CS_SOCKET sock, cis_capture *cap);

#endif
