#include "pch.h"
#include "config.h"
#include "debug.h"
#include "hook.h"

#pragma warning(disable : 4996)


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


typedef decltype(&GetStartupInfoA) GetStartupInfoA_t;
static GetStartupInfoA_t GetStartupInfoA_orig = reinterpret_cast<GetStartupInfoA_t>(GetAddress("KERNEL32", "GetStartupInfoA"));
static bool bAttached = false;

void WINAPI GetStartupInfoA_hook(LPSTARTUPINFOA lpStartupInfo) {
    if (!bAttached && lpStartupInfo && reinterpret_cast<uintptr_t>(_ReturnAddress()) == 0x00B58455) {
        AttachClientHooks();
        bAttached = true;
    }
    GetStartupInfoA_orig(lpStartupInfo);
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
        InetPton(AF_INET, g_sServerAddress, &((sockaddr_in*) name)->sin_addr.S_un.S_addr);
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


void AttachSystemHooks() {
    ATTACH_HOOK(CreateWindowExA_orig, CreateWindowExA_hook);
    ATTACH_HOOK(GetStartupInfoA_orig, GetStartupInfoA_hook);
    ATTACH_HOOK(RegCreateKeyExA_orig, RegCreateKeyExA_hook);
    ATTACH_HOOK(WSPStartup_orig, WSPStartup_hook);
}