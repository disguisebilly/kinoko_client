#include "pch.h"
#include "config.h"
#include "debug.h"
#include "hook.h"

#pragma warning(disable : 4996)


typedef decltype(&CreateMutexA) CreateMutexA_t;
static CreateMutexA_t CreateMutexA_orig = reinterpret_cast<CreateMutexA_t>(GetAddress("KERNEL32", "CreateMutexA"));

HANDLE WINAPI CreateMutexA_hook(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
    DEBUG_MESSAGE("CreateMutexA : %s", lpName);
    if (lpName && !strcmp(lpName, "WvsClientMtx")) {
        char sMutex[128];
        sprintf_s(sMutex, 128, "%s-%d", lpName, GetCurrentProcessId());
        lpName = sMutex;
        AttachClientHooks();
        return CreateMutexA_orig(lpMutexAttributes, bInitialOwner, sMutex);
    }
    return CreateMutexA_orig(lpMutexAttributes, bInitialOwner, lpName);
}


typedef decltype(&CreateWindowExA) CreateWindowExA_t;
static CreateWindowExA_t CreateWindowExA_orig = reinterpret_cast<CreateWindowExA_t>(GetAddress("USER32", "CreateWindowExA"));
static WNDPROC g_WndProc;

LRESULT WndProc_hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    static POINT ptOffset;
    static bool bMoving;
    switch (Msg) {
        case WM_NCMOUSEMOVE:
        case WM_MOUSEMOVE:
            if (bMoving) {
                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                    POINT ptCursor;
                    GetCursorPos(&ptCursor);
                    SetWindowPos(hWnd, NULL, ptCursor.x - ptOffset.x, ptCursor.y - ptOffset.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                } else {
                    bMoving = false;
                    ReleaseCapture();
                }
            }
            break;
        case WM_NCLBUTTONDOWN:
            if (wParam == HTMENU || wParam == HTLEFT) {
                break;
            } else if (wParam == HTCAPTION) {
                RECT rcWnd;
                POINT ptCursor;
                GetWindowRect(hWnd, &rcWnd);
                GetCursorPos(&ptCursor);
                ptOffset.x = ptCursor.x - rcWnd.left;
                ptOffset.y = ptCursor.y - rcWnd.top;
                SetCapture(hWnd);
                bMoving = true;
            }
            return 0;
        case WM_NCLBUTTONUP:
        case WM_LBUTTONUP:
            if (wParam == HTCLOSE) {
                PostQuitMessage(0);
            } else if (wParam == HTMINBUTTON && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                ShowWindow(hWnd, SW_MINIMIZE);
            }
            bMoving = false;
            ReleaseCapture();
            break;
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONUP:
            return 0;
        case WM_RBUTTONUP:
            if (!bMoving) {
                break;
            }
            return 0;
    }
    return CallWindowProcA(g_WndProc, hWnd, Msg, wParam, lParam);
}

HWND WINAPI CreateWindowExA_hook(DWORD dwExStyle, LPCTSTR lpClassName, LPCTSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hWnd = CreateWindowExA_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (lpClassName && !strcmp(lpClassName, "MapleStoryClass")) {
        SetWindowText(hWnd, CONFIG_WINDOW_NAME);
        g_WndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc_hook)));
    }
    return hWnd;
}


typedef decltype(&RegCreateKeyExA) RegCreateKeyExA_t;
static RegCreateKeyExA_t RegCreateKeyExA_orig = reinterpret_cast<RegCreateKeyExA_t>(GetAddress("ADVAPI32", "RegCreateKeyExA"));

LSTATUS WINAPI RegCreateKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, const LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition) {
    return RegCreateKeyExA_orig(HKEY_CURRENT_USER, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
}


typedef decltype(&WSPStartup) WSPStartup_t;
static WSPStartup_t WSPStartup_orig = reinterpret_cast<WSPStartup_t>(GetAddress("MSWSOCK", "WSPStartup"));
static WSPPROC_TABLE g_ProcTable;

int WINAPI WSPConnect_hook(SOCKET s, const struct sockaddr FAR* name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS, LPINT lpErrno) {
    const char* sName = inet_ntoa(((sockaddr_in*) name)->sin_addr);
    if (strstr(sName, CONFIG_NEXON_SEARCH)) {
        InetPton(AF_INET, g_sServerAddress ? g_sServerAddress : CONFIG_SERVER_ADDRESS, &((sockaddr_in*) name)->sin_addr.S_un.S_addr);
        if (g_nServerPort) {
            ((sockaddr_in*) name)->sin_port = htons(g_nServerPort);
        }
    }
    return g_ProcTable.lpWSPConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS, lpErrno);
}

int WINAPI WSPGetPeerName_hook(SOCKET s, struct sockaddr* name, LPINT namelen, LPINT lpErrNo) {
    int result = g_ProcTable.lpWSPGetPeerName(s, name, namelen, lpErrNo);
    InetPton(AF_INET, CONFIG_NEXON_ADDRESS, &((sockaddr_in*) name)->sin_addr.S_un.S_addr);
    return result;
}

int WINAPI WSPStartup_hook(WORD wVersionRequested, LPWSPDATA lpWSPData, LPWSAPROTOCOL_INFOW lpProtocolInfo, WSPUPCALLTABLE UpcallTable, LPWSPPROC_TABLE lpProcTable) {
    int result = WSPStartup_orig(wVersionRequested, lpWSPData, lpProtocolInfo, UpcallTable, lpProcTable);
    g_ProcTable = *lpProcTable;
    lpProcTable->lpWSPConnect = &WSPConnect_hook;
    lpProcTable->lpWSPGetPeerName = &WSPGetPeerName_hook;
    return result;
}


typedef decltype(&MultiByteToWideChar) MultiByteToWideChar_t;
static MultiByteToWideChar_t MultiByteToWideChar_orig = reinterpret_cast<MultiByteToWideChar_t>(GetAddress("KERNEL32", "MultiByteToWideChar"));

int WINAPI MultiByteToWideChar_hook(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar) {
    return MultiByteToWideChar_orig(CP_UTF8, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}

typedef decltype(&WideCharToMultiByte) WideCharToMultiByte_t;
static WideCharToMultiByte_t WideCharToMultiByte_orig = reinterpret_cast<WideCharToMultiByte_t>(GetAddress("KERNEL32", "WideCharToMultiByte"));

int WINAPI WideCharToMultiByte_hook(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar) {
    return WideCharToMultiByte_orig(CP_UTF8, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
}

typedef decltype(&GetTextExtentPoint32A) GetTextExtentPoint32A_t;
static GetTextExtentPoint32A_t GetTextExtentPoint32A_orig = reinterpret_cast<GetTextExtentPoint32A_t>(GetAddress("GDI32", "GetTextExtentPoint32A"));

BOOL WINAPI GetTextExtentPoint32A_hook(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl) {
    wchar_t* lpWideString = reinterpret_cast<wchar_t*>(alloca(c * 2 + 2));
    int wc = MultiByteToWideChar(CP_UTF8, 0, lpString, c, lpWideString, c);
    lpWideString[wc] = '\0';
    return GetTextExtentPoint32W(hdc, lpWideString, wc, psizl);
}

typedef decltype(&TextOutA) TextOutA_t;
static TextOutA_t TextOutA_orig = reinterpret_cast<TextOutA_t>(GetAddress("GDI32", "TextOutA"));

BOOL WINAPI TextOutA_hook(HDC hdc, int x, int y, LPCSTR lpString, int c) {
    wchar_t* lpWideString = reinterpret_cast<wchar_t*>(alloca(c * 2 + 2));
    int wc = MultiByteToWideChar(CP_UTF8, 0, lpString, c, lpWideString, c);
    lpWideString[wc] = '\0';
    return TextOutW(hdc, x, y, lpWideString, wc);
}


void AttachSystemHooks() {
    ATTACH_HOOK(CreateMutexA_orig, CreateMutexA_hook);
    ATTACH_HOOK(CreateWindowExA_orig, CreateWindowExA_hook);
    ATTACH_HOOK(RegCreateKeyExA_orig, RegCreateKeyExA_hook);
    ATTACH_HOOK(WSPStartup_orig, WSPStartup_hook);

    ATTACH_HOOK(MultiByteToWideChar_orig, MultiByteToWideChar_hook);
    ATTACH_HOOK(WideCharToMultiByte_orig, WideCharToMultiByte_hook);
    ATTACH_HOOK(GetTextExtentPoint32A_orig, GetTextExtentPoint32A_hook);
    ATTACH_HOOK(TextOutA_orig, TextOutA_hook);
}