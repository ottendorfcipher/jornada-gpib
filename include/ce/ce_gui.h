/* Windows CE 2.11 windowing and GDI declarations used by the on-device applications.
 *
 * Constants and structure layouts are the documented Win32/Windows CE ones. Functions are
 * imported through the calling-convention thunks like everything else (tools/imports/coredll.txt);
 * the window procedure is called by Microsoft code with four arguments, so it needs no thunk.
 */
#ifndef CE_GUI_H
#define CE_GUI_H

#include "ce_api.h"

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef HANDLE HICON, HCURSOR, HBITMAP;

typedef struct tagWNDCLASSW {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
} WNDCLASSW;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

#define LF_FACESIZE 32
typedef struct tagLOGFONTW {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW;

typedef struct tagSIZE { LONG cx, cy; } SIZE, *LPSIZE;

/* Window styles */
#define WS_OVERLAPPED   0x00000000u
#define WS_POPUP        0x80000000u
#define WS_CHILD        0x40000000u
#define WS_VISIBLE      0x10000000u
#define WS_DISABLED     0x08000000u
#define WS_CLIPSIBLINGS 0x04000000u
#define WS_CLIPCHILDREN 0x02000000u
#define WS_CAPTION      0x00C00000u
#define WS_BORDER       0x00800000u
#define WS_VSCROLL      0x00200000u
#define WS_HSCROLL      0x00100000u
#define WS_SYSMENU      0x00080000u
#define WS_TABSTOP      0x00010000u
#define WS_GROUP        0x00020000u
#define WS_EX_CLIENTEDGE 0x00000200u
#define WS_EX_STATICEDGE 0x00020000u
#define CW_USEDEFAULT   ((int)0x80000000)

/* Control styles */
#define ES_LEFT        0x0000u
#define ES_MULTILINE   0x0004u
#define ES_AUTOVSCROLL 0x0040u
#define ES_AUTOHSCROLL 0x0080u
#define ES_READONLY    0x0800u
#define BS_PUSHBUTTON     0x0000u
#define BS_DEFPUSHBUTTON  0x0001u
#define BS_CHECKBOX       0x0002u
#define BS_AUTOCHECKBOX   0x0003u
#define SS_LEFT   0x0000u
#define SS_CENTER 0x0001u
#define SS_RIGHT  0x0002u
#define LBS_NOTIFY 0x0001u
#define CBS_DROPDOWNLIST 0x0003u
#define CBS_DROPDOWN     0x0002u

/* Messages */
#define WM_CREATE        0x0001
#define WM_DESTROY       0x0002
#define WM_SIZE          0x0005
#define WM_ACTIVATE      0x0006
#define WM_SETFOCUS      0x0007
#define WM_PAINT         0x000F
#define WM_CLOSE         0x0010
#define WM_SETTEXT       0x000C
#define WM_GETTEXT       0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_SETFONT       0x0030
#define WM_KEYDOWN       0x0100
#define WM_CHAR          0x0102
#define WM_COMMAND       0x0111
#define WM_TIMER         0x0113
#define WM_CTLCOLORSTATIC 0x0138
#define WM_USER          0x0400
#define EM_SETSEL        0x00B1
#define EM_REPLACESEL    0x00C2
#define EM_LINESCROLL    0x00B6
#define EM_GETLINECOUNT  0x00BA
#define EM_SETLIMITTEXT  0x00C5
#define EM_SETREADONLY   0x00CF
#define BM_SETCHECK      0x00F1
#define BM_GETCHECK      0x00F0
#define CB_ADDSTRING     0x0143
#define CB_GETCURSEL     0x0147
#define CB_SETCURSEL     0x014E
#define CB_GETLBTEXT     0x0148
#define BN_CLICKED       0
#define EN_CHANGE        0x0300
#define CBN_SELCHANGE    1
#define VK_RETURN        0x0D
#define VK_ESCAPE        0x1B

/* GDI */
#define TRANSPARENT 1
#define OPAQUE      2
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH  2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH  5
#define SYSTEM_FONT 13
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define FW_NORMAL 400
#define FW_BOLD   700
#define DEFAULT_CHARSET 1
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define SW_SHOW 5
#define SW_HIDE 0
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define CS_HREDRAW 0x0002
#define CS_VREDRAW 0x0001
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xFFFF))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define SPI_GETWORKAREA 48

ATOM    RegisterClassW(const WNDCLASSW *) CE_IMPORT(RegisterClassW);
HWND    CreateWindowExW(DWORD exstyle, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) CE_IMPORT(CreateWindowExW);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) CE_IMPORT(DefWindowProcW);
BOOL    GetMessageW(LPMSG, HWND, UINT, UINT) CE_IMPORT(GetMessageW);
BOOL    TranslateMessage(const MSG *) CE_IMPORT(TranslateMessage);
LRESULT DispatchMessageW(const MSG *) CE_IMPORT(DispatchMessageW);
BOOL    IsDialogMessageW(HWND, LPMSG) CE_IMPORT(IsDialogMessageW);
VOID    PostQuitMessage(int) CE_IMPORT(PostQuitMessage);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) CE_IMPORT(SendMessageW);
BOOL    PostMessageW(HWND, UINT, WPARAM, LPARAM) CE_IMPORT(PostMessageW);
BOOL    SetWindowTextW(HWND, LPCWSTR) CE_IMPORT(SetWindowTextW);
int     GetWindowTextW(HWND, LPWSTR, int) CE_IMPORT(GetWindowTextW);
int     GetWindowTextLengthW(HWND) CE_IMPORT(GetWindowTextLengthW);
BOOL    ShowWindow(HWND, int) CE_IMPORT(ShowWindow);
BOOL    UpdateWindow(HWND) CE_IMPORT(UpdateWindow);
BOOL    MoveWindow(HWND, int x, int y, int w, int h, BOOL repaint) CE_IMPORT(MoveWindow);
BOOL    GetClientRect(HWND, LPRECT) CE_IMPORT(GetClientRect);
BOOL    GetWindowRect(HWND, LPRECT) CE_IMPORT(GetWindowRect);
HWND    SetFocus(HWND) CE_IMPORT(SetFocus);
BOOL    EnableWindow(HWND, BOOL) CE_IMPORT(EnableWindow);
BOOL    DestroyWindow(HWND) CE_IMPORT(DestroyWindow);
HWND    GetDlgItem(HWND, int) CE_IMPORT(GetDlgItem);
BOOL    InvalidateRect(HWND, const RECT *, BOOL) CE_IMPORT(InvalidateRect);
HDC     BeginPaint(HWND, LPPAINTSTRUCT) CE_IMPORT(BeginPaint);
BOOL    EndPaint(HWND, const PAINTSTRUCT *) CE_IMPORT(EndPaint);
BOOL    ExtTextOutW(HDC, int x, int y, UINT opts, const RECT *, LPCWSTR, UINT count, const int *dx) CE_IMPORT(ExtTextOutW);
HDC     GetDC(HWND) CE_IMPORT(GetDC);
int     ReleaseDC(HWND, HDC) CE_IMPORT(ReleaseDC);
int     SetBkMode(HDC, int) CE_IMPORT(SetBkMode);
COLORREF SetBkColor(HDC, COLORREF) CE_IMPORT(SetBkColor);
COLORREF SetTextColor(HDC, COLORREF) CE_IMPORT(SetTextColor);
HGDIOBJ GetStockObject(int) CE_IMPORT(GetStockObject);
HBRUSH  CreateSolidBrush(COLORREF) CE_IMPORT(CreateSolidBrush);
BOOL    DeleteObject(HGDIOBJ) CE_IMPORT(DeleteObject);
int     FillRect(HDC, const RECT *, HBRUSH) CE_IMPORT(FillRect);
HFONT   CreateFontIndirectW(const LOGFONTW *) CE_IMPORT(CreateFontIndirectW);
HGDIOBJ SelectObject(HDC, HGDIOBJ) CE_IMPORT(SelectObject);
int     GetSystemMetrics(int) CE_IMPORT(GetSystemMetrics);
UINT_PTR SetTimer(HWND, UINT_PTR id, UINT ms, PVOID proc) CE_IMPORT(SetTimer);
BOOL    KillTimer(HWND, UINT_PTR) CE_IMPORT(KillTimer);
BOOL    GetTextExtentExPointW(HDC, LPCWSTR, int, int maxext, int *fit, int *dx, LPSIZE) CE_IMPORT(GetTextExtentExPointW);
BOOL    SystemParametersInfoW(UINT, UINT, PVOID, UINT) CE_IMPORT(SystemParametersInfoW);
BOOL    SetForegroundWindow(HWND) CE_IMPORT(SetForegroundWindow);

#endif /* CE_GUI_H */
