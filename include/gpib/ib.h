/* A small NI-488.2 "traditional" style API for applications on the Jornada.
 *
 * Talks to the gpib.dll stream driver through DeviceIoControl. Naming and semantics follow
 * the classic ib* calls so instrument code written for NI-488.2 reads naturally, but this is
 * an independent implementation with a deliberately small surface. Descriptor 0 is the
 * board ("gpib0"); ibdev() creates device descriptors above it.
 *
 * Status is reported NI style through ibsta/iberr/ibcnt after every call.
 */
#ifndef GPIB_IB_H
#define GPIB_IB_H

#include "ce/ce_types.h"

/* ibsta bits */
#define IB_ERR  0x8000
#define IB_TIMO 0x4000
#define IB_END  0x2000
#define IB_SRQI 0x1000
#define IB_CMPL 0x0100
#define IB_CIC  0x0020
#define IB_ATN  0x0010
#define IB_TACS 0x0008
#define IB_LACS 0x0004

/* iberr codes */
#define IB_EDVR 0    /* system error (GetLastError in ibcnt) */
#define IB_ECIC 1    /* not controller in charge */
#define IB_ENOL 2    /* no listeners */
#define IB_EADR 3    /* bad address */
#define IB_EARG 4    /* invalid argument */
#define IB_ESAC 5    /* not system controller */
#define IB_EABO 6    /* operation aborted (timeout) */
#define IB_ENEB 7    /* no board (driver not open) */
#define IB_EOIP 10   /* operation in progress */
#define IB_EBUS 14   /* bus error */
#define IB_ETAB 20   /* table problem */

/* ibtmo codes (NI values) */
#define IB_TNONE 0
#define IB_T10us 1
#define IB_T30us 2
#define IB_T100us 3
#define IB_T300us 4
#define IB_T1ms 5
#define IB_T3ms 6
#define IB_T10ms 7
#define IB_T30ms 8
#define IB_T100ms 9
#define IB_T300ms 10
#define IB_T1s 11
#define IB_T3s 12
#define IB_T10s 13
#define IB_T30s 14
#define IB_T100s 15
#define IB_T300s 16
#define IB_T1000s 17

/* ibeos: low byte is the EOS character, high bits the mode */
#define IB_REOS 0x0400
#define IB_XEOS 0x0800
#define IB_BIN  0x1000

#define IB_MAX_DEVICES 16
#define IB_NO_SAD (-1)

extern int ibsta;
extern int iberr;
extern long ibcnt;
extern long ibcntl;

/* Board level */
int ibfind_board(void);                   /* opens the driver; returns 0 (the board) or -1 */
int ibsic(int ud);                        /* interface clear, become controller in charge */
int ibsre(int ud, int v);                 /* remote enable on/off */
int iblines(int ud, short *lines);        /* bus line status (low byte = BSR) */
int ibconfig_board(int pad, int sad, unsigned timeout_ms, unsigned t1_ns);

/* Device level */
int ibdev(int board, int pad, int sad, int tmo, int eot, int eos);
int ibonl(int ud, int v);
int ibwrt(int ud, const void *buf, long cnt);
int ibrd(int ud, void *buf, long cnt);
int ibclr(int ud);
int ibtrg(int ud);
int ibloc(int ud);
int ibrsp(int ud, char *spr);
int ibtmo(int ud, int v);
int ibtmo_ms(int ud, unsigned ms);       /* exact timeout in milliseconds (0 = none) */
int ibeot(int ud, int v);
int ibeos(int ud, int v);
int ibpct(int ud);
int ibllo(int ud);

/* Helpers */
unsigned ib_timeout_ms(int tmo);          /* NI code to milliseconds (0 = none) */
const char *ib_error_name(int err);
HANDLE ib_driver_handle(void);            /* the underlying GPB1: handle (for raw IOCTLs) */

#endif /* GPIB_IB_H */
