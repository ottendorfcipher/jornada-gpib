/* Minimal Windows CE 2.11 type definitions for code compiled with sh-elf GCC.
 *
 * These mirror the public Win32/Windows CE API types (sizes and names are part of the
 * documented ABI). Compile with -fshort-wchar so that wchar_t and L"..." literals are 16-bit,
 * as they are on Windows CE.
 */
#ifndef CE_TYPES_H
#define CE_TYPES_H

typedef unsigned char       UINT8, UCHAR, BYTE, *PUINT8, *PUCHAR, *PBYTE, *LPBYTE;
typedef unsigned short      UINT16, USHORT, WORD, *PUINT16, *PUSHORT, *PWORD;
typedef unsigned int        UINT32, UINT, ULONG, DWORD, *PUINT32, *PULONG, *PDWORD, *LPDWORD;
typedef int                 INT32, INT, LONG, BOOL, *PLONG, *LPLONG, *PBOOL;
typedef short               SHORT;
typedef char                CHAR, *PSTR, *LPSTR;
typedef const char          *PCSTR, *LPCSTR;
typedef unsigned short      WCHAR, *PWSTR, *LPWSTR;
typedef const WCHAR         *PCWSTR, *LPCWSTR;
typedef void                VOID, *PVOID, *LPVOID, *HANDLE, *HLOCAL, *HGLOBAL;
typedef const void          *LPCVOID;
typedef HANDLE              HKEY, HMODULE, HINSTANCE, HWND, HDC, HGDIOBJ, HMENU, HFONT, HBRUSH, HPEN;
typedef HKEY                *PHKEY;
typedef long long           INT64, LONGLONG;
typedef unsigned long long  UINT64, ULONGLONG;
typedef unsigned int        UINT_PTR, ULONG_PTR, DWORD_PTR, SIZE_T;
typedef int                 INT_PTR, LONG_PTR;
typedef UINT_PTR            WPARAM;
typedef LONG_PTR            LPARAM;
typedef LONG_PTR            LRESULT;
typedef WORD                ATOM;
typedef DWORD               COLORREF;

#ifndef NULL
#define NULL ((void *)0)
#endif
#define TRUE  1
#define FALSE 0

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu

#define CALLBACK
#define WINAPI
#define APIENTRY
#define TEXT(s) L##s

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
typedef struct _POINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct _RECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { DWORD Internal, InternalHigh, Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;

typedef struct _SYSTEM_INFO {
    union { DWORD dwOemId; struct { WORD wProcessorArchitecture; WORD wReserved; } s; } u;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct _OSVERSIONINFOW {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    WCHAR szCSDVersion[128];
} OSVERSIONINFOW, *LPOSVERSIONINFOW;

typedef struct _CRITICAL_SECTION {
    /* Opaque on Windows CE 2.11: the kernel owns the layout. 6 words is what coredll expects. */
    DWORD opaque[6];
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

/* File API constants */
#define GENERIC_READ            0x80000000u
#define GENERIC_WRITE           0x40000000u
#define FILE_SHARE_READ         0x00000001u
#define FILE_SHARE_WRITE        0x00000002u
#define CREATE_NEW              1
#define CREATE_ALWAYS           2
#define OPEN_EXISTING           3
#define OPEN_ALWAYS             4
#define TRUNCATE_EXISTING       5
#define FILE_ATTRIBUTE_NORMAL   0x00000080u
#define FILE_BEGIN              0
#define FILE_CURRENT            1
#define FILE_END                2
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu

/* Registry */
#define HKEY_CLASSES_ROOT   ((HKEY)0x80000000u)
#define HKEY_CURRENT_USER   ((HKEY)0x80000001u)
#define HKEY_LOCAL_MACHINE  ((HKEY)0x80000002u)
#define HKEY_USERS          ((HKEY)0x80000003u)
#define REG_NONE      0
#define REG_SZ        1
#define REG_BINARY    3
#define REG_DWORD     4
#define REG_MULTI_SZ  7
#define REG_OPTION_NON_VOLATILE 0
#define KEY_ALL_ACCESS 0xF003F
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259

/* Memory */
#define MEM_COMMIT      0x1000
#define MEM_RESERVE     0x2000
#define MEM_RELEASE     0x8000
#define PAGE_NOACCESS   0x01
#define PAGE_READONLY   0x02
#define PAGE_READWRITE  0x04
#define PAGE_NOCACHE    0x200
#define PAGE_PHYSICAL   0x400
#define LMEM_FIXED      0x0000
#define LMEM_ZEROINIT   0x0040
#define LPTR            (LMEM_FIXED | LMEM_ZEROINIT)

/* Threads and synchronisation */
#define WAIT_OBJECT_0   0
#define WAIT_TIMEOUT    258
#define WAIT_FAILED     0xFFFFFFFFu
#define THREAD_PRIORITY_TIME_CRITICAL  (-15)
#define THREAD_PRIORITY_HIGHEST        (-2)
#define THREAD_PRIORITY_ABOVE_NORMAL   (-1)
#define THREAD_PRIORITY_NORMAL         0

/* MessageBox */
#define MB_OK           0x00000000u
#define MB_ICONERROR    0x00000010u
#define MB_ICONINFORMATION 0x00000040u
#define MB_SETFOREGROUND 0x00010000u
#define MB_TOPMOST      0x00040000u

/* DLL entry reasons */
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH  2
#define DLL_THREAD_DETACH  3
#define DLL_PROCESS_DETACH 0

/* Device I/O control code construction (same macro as winioctl.h) */
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DWORD)(DeviceType) << 16) | ((DWORD)(Access) << 14) | ((DWORD)(Function) << 2) | (DWORD)(Method))
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define FILE_DEVICE_UNKNOWN 0x22

#endif /* CE_TYPES_H */
