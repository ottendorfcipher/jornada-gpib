/* Windows CE 2.11 PC Card "Card Services" interface (pcmcia.dll), as used by client drivers.
 *
 * These declarations are written from Microsoft's published Device Driver Kit documentation
 * for the Card Services API (functions CardRegisterClient .. CardAccessConfigurationRegister
 * and their parameter structures). pcmcia.dll exports the functions by name; there is no
 * import library, so a client resolves them with LoadLibraryW/GetProcAddressW and calls them
 * through the xt_callN thunks (see ce_cs.c).
 *
 * All structures are byte-packed, exactly as the DDK declares them.
 *
 * Socket handles: the DDK type CARD_SOCKET_HANDLE is a two-byte structure passed BY VALUE.
 * Microsoft's SH-3 convention passes such a struct as its 32-bit memory image, i.e. socket
 * number in bits 0..7 and function number in bits 8..15. We pass and receive it as a UINT32
 * (CS_SOCKET) to keep the ABI unambiguous; the packed struct is used inside parameter blocks.
 */
#ifndef CE_CARDSERV_H
#define CE_CARDSERV_H

#include "ce_types.h"

typedef UINT32 CARD_EVENT;
typedef UINT32 STATUS;
typedef PVOID  CARD_CLIENT_HANDLE;
typedef PVOID  CARD_WINDOW_HANDLE;
typedef UINT32 CS_SOCKET;                 /* CARD_SOCKET_HANDLE by value, see above */

#define CS_SOCKET_MAKE(sock, func) ((CS_SOCKET)(((UINT32)(sock) & 0xFF) | (((UINT32)(func) & 0xFF) << 8)))
#define CS_SOCKET_NUMBER(h)   ((UINT8)((h) & 0xFF))
#define CS_SOCKET_FUNCTION(h) ((UINT8)(((h) >> 8) & 0xFF))

#pragma pack(push, 1)

typedef struct _CARD_SOCKET_HANDLE {
    UINT8 uSocket;
    UINT8 uFunction;
} CARD_SOCKET_HANDLE, *PCARD_SOCKET_HANDLE;

typedef struct _CARD_EVENT_MASK_PARMS {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fAttributes;
    UINT16 fEventMask;
} CARD_EVENT_MASK_PARMS, *PCARD_EVENT_MASK_PARMS;

typedef struct _CARD_EVENT_PARMS {
    UINT32 uClientData;
    UINT32 Parm1;
    UINT32 Parm2;
} CARD_EVENT_PARMS, *PCARD_EVENT_PARMS;

typedef struct _CARD_REGISTER_PARMS {
    UINT16 fAttributes;
    UINT16 fEventMask;
    UINT32 uClientData;
} CARD_REGISTER_PARMS, *PCARD_REGISTER_PARMS;

typedef struct _CARD_WINDOW_PARMS {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fAttributes;
    UINT32 uWindowSize;
    UINT8  fAccessSpeed;
} CARD_WINDOW_PARMS, *PCARD_WINDOW_PARMS;

typedef struct _CARD_CONFIG_INFO {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fAttributes;
    UINT8  fInterfaceType;
    UINT8  uVcc;
    UINT8  uVpp1;
    UINT8  uVpp2;
    UINT8  fRegisters;
    UINT8  uConfigReg;
    UINT8  uStatusReg;
    UINT8  uPinReg;
    UINT8  uCopyReg;
    UINT8  uExtendedStatus;
} CARD_CONFIG_INFO, *PCARD_CONFIG_INFO;

typedef struct _CARD_STATUS {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fCardState;
    UINT16 fSocketState;
} CARD_STATUS, *PCARD_STATUS;

typedef struct _CARD_TUPLE_PARMS {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fAttributes;
    UINT8  uDesiredTuple;
    UINT8  uReserved;
    UINT16 fFlags;
    UINT32 uLinkOffset;
    UINT32 uCISOffset;
    UINT8  uTupleCode;
    UINT8  uTupleLink;
} CARD_TUPLE_PARMS, *PCARD_TUPLE_PARMS;

typedef struct _CARD_DATA_PARMS {
    CARD_SOCKET_HANDLE hSocket;
    UINT16 fAttributes;
    UINT8  uDesiredTuple;
    UINT8  uTupleOffset;
    UINT16 fFlags;
    UINT32 uLinkOffset;
    UINT32 uCISOffset;
    UINT16 uBufLen;
    UINT16 uDataLen;
} CARD_DATA_PARMS, *PCARD_DATA_PARMS;

/* CardGetParsedTuple results */
typedef struct _PARSED_CONFIG {
    UINT32 ConfigBase;
    UINT8  RegMask;
    UINT8  LastConfigIndex;
} PARSED_CONFIG, *PPARSED_CONFIG;

typedef struct _POWER_DESCR {
    UINT16 ValidMask, NominalV, MinV, MaxV, StaticI, AvgI, PeakI, PowerDownI;
} POWER_DESCR, *PPOWER_DESCR;

#define MAX_IO_RANGES 4

typedef struct _PARSED_CFTABLE {
    POWER_DESCR VccDescr;
    POWER_DESCR Vpp1Descr;
    POWER_DESCR Vpp2Descr;
    UINT32 IOLength[MAX_IO_RANGES];
    UINT32 IOBase[MAX_IO_RANGES];
    UINT8  NumIOEntries;
    UINT8  ConfigIndex;
    UINT8  ContainsDefaults;
    UINT8  IFacePresent;
    UINT8  IFaceType;
    UINT8  BVDActive;
    UINT8  WPActive;
    UINT8  ReadyActive;
    UINT8  WaitRequired;
    UINT8  IOAccess;
    UINT8  NumIOAddrLines;
    UINT8  Pad0;
} PARSED_CFTABLE, *PPARSED_CFTABLE;

#pragma pack(pop)

/* Callback and ISR signatures (Microsoft calls these; at most four argument words, so no
 * thunk is needed, only the CS_SOCKET-by-value convention). */
typedef STATUS (*CLIENT_CALLBACK)(CARD_EVENT, CS_SOCKET, PCARD_EVENT_PARMS);
typedef VOID   (*CARD_ISR)(UINT32);

/* Events */
#define CE_BATTERY_DEAD          0x01
#define CE_BATTERY_LOW           0x02
#define CE_CARD_LOCK             0x03
#define CE_CARD_READY            0x04
#define CE_CARD_REMOVAL          0x05
#define CE_CARD_UNLOCK           0x06
#define CE_EJECTION_COMPLETE     0x07
#define CE_EJECTION_REQUEST      0x08
#define CE_INSERTION_COMPLETE    0x09
#define CE_INSERTION_REQUEST     0x0A
#define CE_PM_RESUME             0x0B
#define CE_PM_SUSPEND            0x0C
#define CE_EXCLUSIVE_COMPLETE    0x0D
#define CE_EXCLUSIVE_REQUEST     0x0E
#define CE_RESET_PHYSICAL        0x0F
#define CE_RESET_REQUEST         0x10
#define CE_CARD_RESET            0x11
#define CE_MTD_REQUEST           0x12
#define CE_CLIENT_INFO           0x14
#define CE_TIMER_EXPIRED         0x15
#define CE_SS_UPDATED            0x16
#define CE_WRITE_PROTECT         0x17
#define CE_CARD_INSERTION        0x40
#define CE_RESET_COMPLETE        0x80
#define CE_ERASE_COMPLETE        0x81
#define CE_REGISTRATION_COMPLETE 0x82
#define CE_STATUS_CHANGE_INTERRUPT 0xFE

/* Return codes */
#define CERR_SUCCESS              0x00
#define CERR_BAD_ADAPTER          0x01
#define CERR_BAD_ATTRIBUTE        0x02
#define CERR_BAD_BASE             0x03
#define CERR_BAD_EDC              0x04
#define CERR_BAD_IRQ              0x06
#define CERR_BAD_OFFSET           0x07
#define CERR_BAD_PAGE             0x08
#define CERR_READ_FAILURE         0x09
#define CERR_BAD_SIZE             0x0A
#define CERR_BAD_SOCKET           0x0B
#define CERR_BAD_TYPE             0x0D
#define CERR_BAD_VCC              0x0E
#define CERR_BAD_VPP              0x0F
#define CERR_BAD_WINDOW           0x11
#define CERR_WRITE_FAILURE        0x12
#define CERR_NO_CARD              0x14
#define CERR_UNSUPPORTED_SERVICE  0x15
#define CERR_UNSUPPORTED_MODE     0x16
#define CERR_BAD_SPEED            0x17
#define CERR_BUSY                 0x18
#define CERR_GENERAL_FAILURE      0x19
#define CERR_WRITE_PROTECTED      0x1A
#define CERR_BAD_ARG_LENGTH       0x1B
#define CERR_BAD_ARGS             0x1C
#define CERR_CONFIGURATION_LOCKED 0x1D
#define CERR_IN_USE               0x1E
#define CERR_NO_MORE_ITEMS        0x1F
#define CERR_OUT_OF_RESOURCE      0x20
#define CERR_BAD_HANDLE           0x21
#define CERR_BAD_VERSION          0x22

/* Event masks (CARD_REGISTER_PARMS.fEventMask, CARD_STATUS.fCardState) */
#define EVENT_MASK_WRITE_PROTECT    0x0001
#define EVENT_MASK_CARD_LOCK        0x0002
#define EVENT_MASK_EJECT_REQ        0x0004
#define EVENT_MASK_INSERT_REQ       0x0008
#define EVENT_MASK_BATTERY_DEAD     0x0010
#define EVENT_MASK_BATTERY_LOW      0x0020
#define EVENT_MASK_CARD_READY       0x0040
#define EVENT_MASK_CARD_DETECT      0x0080
#define EVENT_MASK_POWER_MGMT       0x0100
#define EVENT_MASK_RESET            0x0200
#define EVENT_MASK_STATUS_CHANGE    0x0400
#define EVENT_ATTR_SOCKET_ONLY      0x0001

/* Client attributes */
#define CLIENT_ATTR_MEM_DRIVER       0x0001
#define CLIENT_ATTR_MTD_DRIVER       0x0002
#define CLIENT_ATTR_IO_DRIVER        0x0004
#define CLIENT_ATTR_NOTIFY_SHARED    0x0008
#define CLIENT_ATTR_NOTIFY_EXCLUSIVE 0x0010

/* Window attributes */
#define WIN_ATTR_IO_SPACE      0x0001
#define WIN_ATTR_ATTRIBUTE     0x0002
#define WIN_ATTR_ENABLED       0x0004
#define WIN_ATTR_16BIT         0x0008
#define WIN_ATTR_PAGED         0x0010
#define WIN_ATTR_SHARED        0x0020
#define WIN_ATTR_FIRST_SHARED  0x0040
#define WIN_ATTR_OFFSETS_SIZED 0x0100
#define WIN_ATTR_ACCESS_SPEED_VALID 0x0200
#define WIN_SPEED_USE_WAIT     0x80

/* Configuration attributes, interface types, register presence */
#define CFG_ATTR_EXCLUSIVE    0x0001
#define CFG_ATTR_IRQ_STEERING 0x0002
#define CFG_ATTR_IRQ_WAKEUP   0x0004
#define CFG_ATTR_KEEP_POWERED 0x0008
#define CFG_ATTR_ENABLE_DMA   0x0040
#define CFG_ATTR_VALID_CLIENT 0x0100
#define CFG_ATTR_VS_OVERRIDE  0x0200
#define CFG_IFACE_MEMORY      0x0001
#define CFG_IFACE_MEMORY_IO   0x0002
#define CFG_REGISTER_CONFIG   0x01
#define CFG_REGISTER_STATUS   0x02
#define CFG_REGISTER_PIN      0x04
#define CFG_REGISTER_COPY     0x08
#define CFG_REGISTER_EXSTATUS 0x10

/* Function configuration registers (PC Card standard) */
#define FCR_OFFSET_COR   0
#define FCR_OFFSET_FCSR  1
#define FCR_COR_LEVEL_IREQ  0x40
#define FCR_COR_SRESET      0x80
#define FCR_FCSR_INTR_ACK   0x01
#define FCR_FCSR_INTR       0x02
#define FCR_FCSR_PWR_DOWN   0x04
#define FCR_FCSR_AUDIO      0x08
#define FCR_FCSR_IO_IS_8    0x20
#define FCR_FCSR_STSCHG     0x40
#define FCR_FCSR_CHANGED    0x80
#define FCR_FCSR_REQUIRED_BITS (FCR_FCSR_INTR_ACK | FCR_FCSR_IO_IS_8)
#define CARD_FCR_READ  0
#define CARD_FCR_WRITE 1

/* Tuple enumeration */
#define TUPLE_RETURN_LINKS  0x0001
#define CISTPL_NULL          0x00
#define CISTPL_DEVICE        0x01
#define CISTPL_LONGLINK_MFC  0x06
#define CISTPL_CHECKSUM      0x10
#define CISTPL_LONGLINK_A    0x11
#define CISTPL_LONGLINK_C    0x12
#define CISTPL_LINKTARGET    0x13
#define CISTPL_NO_LINK       0x14
#define CISTPL_VERS_1        0x15
#define CISTPL_ALTSTR        0x16
#define CISTPL_DEVICE_A      0x17
#define CISTPL_JEDEC_C       0x18
#define CISTPL_JEDEC_A       0x19
#define CISTPL_CONFIG        0x1A
#define CISTPL_CFTABLE_ENTRY 0x1B
#define CISTPL_DEVICE_OC     0x1C
#define CISTPL_DEVICE_OA     0x1D
#define CISTPL_MANFID        0x20
#define CISTPL_FUNCID        0x21
#define CISTPL_FUNCE         0x22
#define CISTPL_END           0xFF

#define PCCARD_TYPE_VENDOR_SPECIFIC 0
#define PCCARD_TYPE_MEMORY          1
#define PCCARD_TYPE_SERIAL          2
#define PCCARD_TYPE_PARALLEL        3
#define PCCARD_TYPE_FIXED_DISK      4
#define PCCARD_TYPE_VIDEO           5
#define PCCARD_TYPE_NETWORK         6
#define PCCARD_TYPE_AIMS            7
#define PCCARD_TYPE_UNKNOWN         0xff

/* Registry value names written by device.exe under HKLM\Drivers\Active\nn */
#define DEVLOAD_ACTIVE_KEY      L"Drivers\\Active"
#define DEVLOAD_PCMCIA_KEY      L"Drivers\\PCMCIA"
#define DEVLOAD_SOCKET_VALNAME  L"Sckt"
#define DEVLOAD_PNPID_VALNAME   L"PnpId"
#define DEVLOAD_DEVKEY_VALNAME  L"Key"
#define DEVLOAD_DEVNAME_VALNAME L"Name"
#define DEVLOAD_DLLNAME_VALNAME L"Dll"
#define DEVLOAD_PREFIX_VALNAME  L"Prefix"

/* Card Services function table, bound at run time by cs_bind() (src/ce/ce_cs.c). Each entry
 * is the raw pcmcia.dll entry point; call them only through the cs_* wrappers, which route
 * through the xt_callN thunks. */
typedef struct cs_table {
    PVOID RegisterClient, DeregisterClient, GetFirstTuple, GetNextTuple, GetTupleData,
          GetParsedTuple, RequestConfiguration, ReleaseConfiguration, GetStatus,
          ResetFunction, RequestExclusive, ReleaseExclusive, RequestWindow, ReleaseWindow,
          ModifyWindow, MapWindow, RequestIRQ, ReleaseIRQ, AccessConfigurationRegister;
    HMODULE module;
} cs_table;

BOOL   cs_bind(cs_table *t);
VOID   cs_unbind(cs_table *t);
CARD_CLIENT_HANDLE cs_RegisterClient(const cs_table *, CLIENT_CALLBACK, PCARD_REGISTER_PARMS);
STATUS cs_DeregisterClient(const cs_table *, CARD_CLIENT_HANDLE);
STATUS cs_GetFirstTuple(const cs_table *, PCARD_TUPLE_PARMS);
STATUS cs_GetNextTuple(const cs_table *, PCARD_TUPLE_PARMS);
STATUS cs_GetTupleData(const cs_table *, PCARD_DATA_PARMS);
STATUS cs_GetParsedTuple(const cs_table *, CS_SOCKET, UINT8 tuple, PVOID buf, PUINT32 items);
STATUS cs_RequestConfiguration(const cs_table *, CARD_CLIENT_HANDLE, PCARD_CONFIG_INFO);
STATUS cs_ReleaseConfiguration(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET);
STATUS cs_GetStatus(const cs_table *, PCARD_STATUS);
STATUS cs_ResetFunction(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET);
STATUS cs_RequestExclusive(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET);
STATUS cs_ReleaseExclusive(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET);
CARD_WINDOW_HANDLE cs_RequestWindow(const cs_table *, CARD_CLIENT_HANDLE, PCARD_WINDOW_PARMS);
STATUS cs_ReleaseWindow(const cs_table *, CARD_WINDOW_HANDLE);
PVOID  cs_MapWindow(const cs_table *, CARD_WINDOW_HANDLE, UINT32 cardAddr, UINT32 size, PUINT32 granularity);
STATUS cs_RequestIRQ(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET, CARD_ISR, UINT32 context);
STATUS cs_ReleaseIRQ(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET);
STATUS cs_AccessConfigurationRegister(const cs_table *, CARD_CLIENT_HANDLE, CS_SOCKET, UINT8 rw, UINT8 offset, PUINT8 value);

#endif /* CE_CARDSERV_H */
