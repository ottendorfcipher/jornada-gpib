/* Interface between gpib.dll (stream driver "GPB1:") and applications.
 *
 * Applications open L"GPB1:" with CreateFileW and use DeviceIoControl with the codes below.
 * All structures are flat (no embedded pointers) because Windows CE only maps the top-level
 * buffers into the driver's address space. Multi-byte fields are little-endian, 4-byte
 * aligned; data follows the header inside the same buffer.
 */
#ifndef GPIB_IOCTL_H
#define GPIB_IOCTL_H

#include "ce/ce_types.h"

#define GPIB_DEVICE_NAME  L"GPB1:"
#define GPIB_DRIVER_DLL   L"gpib.dll"
#define GPIB_DRIVER_PREFIX L"GPB"
#define GPIB_LOG_PATH     L"\\gpib.log"
#define GPIB_IOCTL_VERSION 1

#define GPIB_CTL(n) CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + (n), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GPIB_GET_INFO      GPIB_CTL(0)   /* out: gpib_info */
#define IOCTL_GPIB_SET_CONFIG    GPIB_CTL(1)   /* in: gpib_config */
#define IOCTL_GPIB_IFC           GPIB_CTL(2)   /* out: gpib_result */
#define IOCTL_GPIB_REN           GPIB_CTL(3)   /* in: UINT32 on; out: gpib_result */
#define IOCTL_GPIB_LINES         GPIB_CTL(4)   /* out: gpib_lines_info */
#define IOCTL_GPIB_WRITE         GPIB_CTL(5)   /* in: gpib_xfer + data; out: gpib_result */
#define IOCTL_GPIB_READ          GPIB_CTL(6)   /* in: gpib_xfer; out: gpib_result + data */
#define IOCTL_GPIB_COMMAND       GPIB_CTL(7)   /* in: command bytes; out: gpib_result */
#define IOCTL_GPIB_CLEAR         GPIB_CTL(8)   /* in: gpib_address (pad < 0 = DCL); out: gpib_result */
#define IOCTL_GPIB_TRIGGER       GPIB_CTL(9)   /* in: gpib_address; out: gpib_result */
#define IOCTL_GPIB_LOCAL         GPIB_CTL(10)  /* in: gpib_address; out: gpib_result */
#define IOCTL_GPIB_LOCAL_LOCKOUT GPIB_CTL(11)  /* out: gpib_result */
#define IOCTL_GPIB_SERIAL_POLL   GPIB_CTL(12)  /* in: gpib_address; out: gpib_result (count = status byte) */
#define IOCTL_GPIB_PASS_CONTROL  GPIB_CTL(13)  /* in: gpib_address; out: gpib_result */
#define IOCTL_GPIB_PROBE         GPIB_CTL(14)  /* out: gpib_probe_info (re-runs the reset self-test, re-initialises) */
#define IOCTL_GPIB_REGISTER      GPIB_CTL(15)  /* in: gpib_register_op; out: gpib_register_op (raw register access, debugging) */
#define IOCTL_GPIB_SET_DEVICE    GPIB_CTL(16)  /* in: gpib_address (address used by ReadFile/WriteFile) */

/* Driver status codes (gpib_result.status); mirror the chip driver's return codes. */
#define GPIB_ST_OK           0
#define GPIB_ST_TIMEOUT     -1
#define GPIB_ST_NOLISTENER  -2
#define GPIB_ST_NOTCIC      -3
#define GPIB_ST_IO          -4
#define GPIB_ST_INVAL       -5
#define GPIB_ST_NOTSC       -6
#define GPIB_ST_NOCARD      -7   /* card removed or never initialised */

/* gpib_xfer.flags */
#define GPIB_XF_EOI 0x0001       /* writes: assert EOI with the last byte */

typedef struct gpib_address {
    INT32 pad;                   /* 0..30, or -1 for "all" where meaningful */
    INT32 sad;                   /* 0..30 or -1 */
} gpib_address;

typedef struct gpib_result {
    INT32 status;                /* GPIB_ST_* */
    UINT32 count;                /* bytes transferred, or the serial poll status byte */
    UINT32 end;                  /* reads: 1 if EOI or EOS terminated the transfer */
} gpib_result;

typedef struct gpib_xfer {
    gpib_address addr;
    UINT32 flags;
    UINT32 length;               /* bytes to write (following this header) or read capacity */
} gpib_xfer;

typedef struct gpib_config {
    UINT32 timeout_ms;           /* 0 = no timeout */
    UINT32 t1_ns;                /* source handshake settling time, 350/500/1100/2000 */
    INT32 pad;                   /* this interface's primary address */
    INT32 sad;                   /* this interface's secondary address or -1 */
    UINT32 eos;                  /* EOS byte */
    UINT32 eos_flags;            /* GPIB_EOS_* */
} gpib_config;

#define GPIB_EOS_REOS 0x01
#define GPIB_EOS_BIN  0x02
#define GPIB_EOS_XEOS 0x04

typedef struct gpib_lines_info {
    UINT32 bsr;                  /* bus line status: ATN DAV NDAC NRFD EOI SRQ IFC REN (bit 7..0) */
    UINT32 adsr;                 /* address status: CIC nATN SPMS LPAS TPAS LA TA MJMN */
    UINT32 sts1;
    UINT32 sts2;
} gpib_lines_info;

typedef struct gpib_probe_info {
    UINT32 ok;
    UINT32 csr, sts1, sts2, isr3, adsr, isr0;
} gpib_probe_info;

typedef struct gpib_info {
    UINT32 version;              /* GPIB_IOCTL_VERSION */
    UINT32 card_present;
    UINT32 socket;               /* CARD_SOCKET_HANDLE as a word */
    UINT32 io_base;              /* card I/O base from the CIS */
    UINT32 io_length;
    UINT32 config_index;
    UINT32 window_granularity;
    UINT32 window_16bit;         /* 1 if the socket granted a 16-bit I/O window */
    gpib_probe_info probe;
    gpib_config config;
} gpib_info;

#define GPIB_REG_READ  0
#define GPIB_REG_WRITE 1

typedef struct gpib_register_op {
    UINT32 op;
    UINT32 offset;               /* 0..31 */
    UINT32 value;
} gpib_register_op;

#endif /* GPIB_IOCTL_H */
