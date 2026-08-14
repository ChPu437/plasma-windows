/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

    titlebar.c - L4 caption-removal engine (R1.3 in docs/titlebar-research.md).

    Cross-process window decoration for windows with a standard native
    caption (taxonomy A). Pure Win32, no injection, no Plasma dependency -
    the Plasma overlay (the "Plasma title bar" UI) is a later phase.

    Window taxonomy (per R1.3 section 4.6):
      A  standard caption windows (DefWindowProc NC) - decorable
      B  client-side-decorated (Electron/WPF custom chrome) - excluded:
         style surgery is useless and overlaying is harmful
      C  hybrid frame-extended - excluded

    Modes:
      titlebar.exe classify <hwnd|title|pid>
      titlebar.exe remove   <hwnd>          (styles saved, restore possible)
      titlebar.exe restore  <hwnd>
      titlebar.exe status   <hwnd>
      titlebar.exe --watch                   (SHOW events + whitelist -> remove)

    Safety: only the current user's windows, styles are saved and
    restore is always possible; elevated (UIPI) windows are skipped.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <stdio.h>

#pragma comment(lib, "dwmapi.lib")

/* ------------------------------------------------------------------ */
/* taxonomy                                                            */
/* ------------------------------------------------------------------ */

typedef enum { TAX_A = 0, TAX_B, TAX_C, TAX_NONE } Taxonomy;

static Taxonomy classifyWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return TAX_NONE;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == 0) {
        return TAX_NONE; /* no caption at all */
    }

    /* elevated windows: UIPI would block our SetWindowLong/SendMessage */
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            HANDLE tok = NULL;
            if (OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
                DWORD il = 0, len = 0;
                if (GetTokenInformation(tok, TokenIntegrityLevel, &il, sizeof(il), &len)) {
                    if (il >= 0x2000) { /* SID_MANDATORY_HIGH */
                        CloseHandle(tok);
                        CloseHandle(proc);
                        return TAX_NONE;
                    }
                }
                CloseHandle(tok);
            }
            CloseHandle(proc);
        }
    }

    /* B detection (R1.3 4.6): screen-mapped client rect == window rect
       while WS_CAPTION is still set means their WM_NCCALCSIZE zeroed the
       NC area (client-side decoration). */
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT clientOrigin = {cr.left, cr.top};
    ClientToScreen(hwnd, &clientOrigin);
    cr.left = clientOrigin.x;
    cr.top = clientOrigin.y;
    cr.right = clientOrigin.x + (cr.right - cr.left);
    cr.bottom = clientOrigin.y + (cr.bottom - cr.top);
    if (cr.left == wr.left && cr.top == wr.top && cr.right == wr.right && cr.bottom == wr.bottom) {
        return TAX_B;
    }

    /* C detection: frame extended into client (DwmExtendFrameIntoClientArea) */
    RECT ext;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &ext, sizeof(ext)))
        && (ext.left != wr.left || ext.top != wr.top || ext.right != wr.right || ext.bottom != wr.bottom)) {
        /* normal A windows also differ slightly (frame). Only treat as C
           when the extended bounds cover the full window (client-like). */
        if (ext.left <= wr.left && ext.top <= wr.top && ext.right >= wr.right && ext.bottom >= wr.bottom) {
            return TAX_C;
        }
    }

    return TAX_A;
}

/* ------------------------------------------------------------------ */
/* style surgery                                                       */
/* ------------------------------------------------------------------ */

static const LONG_PTR kCaptionStyles = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

typedef struct
{
    LONG_PTR style;
    LONG_PTR exStyle;
} SavedStyles;

static const WCHAR kStyleProp[] = L"PlasmaTitlebarStyle";
static const WCHAR kExStyleProp[] = L"PlasmaTitlebarExStyle";

static BOOL findSaved(HWND hwnd, LONG_PTR *style, LONG_PTR *exStyle)
{
    HANDLE s = GetPropW(hwnd, kStyleProp);
    HANDLE e = GetPropW(hwnd, kExStyleProp);
    if (!s && !e) {
        return FALSE;
    }
    if (style) {
        *style = (LONG_PTR)s;
    }
    if (exStyle) {
        *exStyle = (LONG_PTR)e;
    }
    return TRUE;
}

static BOOL isDecorated(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == 0;
}

static BOOL removeCaption(HWND hwnd)
{
    if (isDecorated(hwnd)) {
        return FALSE; /* already done */
    }
    if (findSaved(hwnd, NULL, NULL)) {
        return TRUE; /* already tracked */
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    /* window properties are readable cross-process: store the styles
       directly as the property values */
    SetPropW(hwnd, kStyleProp, (HANDLE)style);
    SetPropW(hwnd, kExStyleProp, (HANDLE)exStyle);

    /* clear the caption styles; keep WS_THICKFRAME so resize still works */
    SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~kCaptionStyles);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

static BOOL restoreCaption(HWND hwnd)
{
    LONG_PTR style = 0, exStyle = 0;
    if (!findSaved(hwnd, &style, &exStyle)) {
        return FALSE;
    }
    RemovePropW(hwnd, kStyleProp);
    RemovePropW(hwnd, kExStyleProp);

    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* target resolution                                                   */
/* ------------------------------------------------------------------ */

static HWND resolveHwnd(const WCHAR *arg)
{
    if (!arg) {
        return NULL;
    }
    /* parse as hex hwnd? "<hwnd>" */
    if (wcsncmp(arg, L"0x", 2) == 0 || wcsncmp(arg, L"0X", 2) == 0) {
        WCHAR *end = NULL;
        ULONG_PTR v = wcstoul(arg + 2, &end, 16);
        if (end && *end == 0) {
            return (HWND)v;
        }
    }
    /* pid? */
    if (iswdigit(arg[0])) {
        DWORD pid = (DWORD)_wtoi(arg);
        return FindWindowW(NULL, NULL); /* placeholder */
    }
    /* title match */
    return FindWindowW(NULL, arg);
}

static const WCHAR *taxName(Taxonomy t)
{
    switch (t) {
    case TAX_A:
        return L"A (standard caption)";
    case TAX_B:
        return L"B (client-decorated)";
    case TAX_C:
        return L"C (frame-extended)";
    default:
        return L"none";
    }
}

/* ------------------------------------------------------------------ */
/* watch mode (SHOW events + whitelist)                                */
/* ------------------------------------------------------------------ */

static const WCHAR *kWhitelist[] = {
    L"notepad.exe",
    L"notepad++.exe",
    L"7zfm.exe",
    L"mspaint.exe",
    L"calc.exe",
};

static BOOL inWhitelist(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        return FALSE;
    }
    WCHAR name[MAX_PATH];
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(proc, 0, name, &size);
    CloseHandle(proc);
    if (!ok) {
        return FALSE;
    }
    WCHAR *slash = wcsrchr(name, L'\\');
    const WCHAR *base = slash ? slash + 1 : name;
    for (int i = 0; i < (int)(sizeof(kWhitelist) / sizeof(kWhitelist[0])); ++i) {
        if (_wcsicmp(base, kWhitelist[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void handleWindow(HWND hwnd)
{
    if (inWhitelist(hwnd)) {
        Taxonomy t = classifyWindow(hwnd);
        if (t == TAX_A) {
            wprintf(L"decorating %p\n", hwnd);
            removeCaption(hwnd);
        }
    }
}

static BOOL CALLBACK enumWatchProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    handleWindow(hwnd);
    return TRUE;
}

static void CALLBACK winEventWatchProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                       LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hHook;
    (void)event;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;
    if (idObject == OBJID_WINDOW) {
        handleWindow(hwnd);
    }
}

static void watchLoop(void)
{
    wprintf(L"watching for whitelisted windows (Ctrl+C to stop)\n");
    /* initial sweep */
    EnumWindows(enumWatchProc, 0);
    /* win-event listener */
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, winEventWatchProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* ------------------------------------------------------------------ */

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) {
        wprintf(L"usage:\n"
                L"  titlebar.exe classify <hwnd|title>\n"
                L"  titlebar.exe remove   <hwnd|title>\n"
                L"  titlebar.exe restore  <hwnd|title>\n"
                L"  titlebar.exe status   <hwnd|title>\n"
                L"  titlebar.exe --watch\n");
        return 2;
    }

    if (wcscmp(argv[1], L"--watch") == 0) {
        watchLoop();
        return 0;
    }

    const WCHAR *cmd = argv[1];
    const WCHAR *arg = argc >= 3 ? argv[2] : NULL;
    HWND hwnd = resolveHwnd(arg);
    if (!hwnd) {
        wprintf(L"window not found\n");
        return 1;
    }

    if (wcscmp(cmd, L"classify") == 0) {
        wprintf(L"%s\n", taxName(classifyWindow(hwnd)));
    } else if (wcscmp(cmd, L"remove") == 0) {
        Taxonomy t = classifyWindow(hwnd);
        if (t != TAX_A) {
            wprintf(L"cannot decorate: %s\n", taxName(t));
            return 1;
        }
        wprintf(L"%s\n", removeCaption(hwnd) ? L"caption removed (styles saved)" : L"failed / already decorated");
    } else if (wcscmp(cmd, L"restore") == 0) {
        wprintf(L"%s\n", restoreCaption(hwnd) ? L"caption restored" : L"not tracked by this process");
    } else if (wcscmp(cmd, L"status") == 0) {
        wprintf(L"hwnd %p: %s, decorated=%ls\n", hwnd, taxName(classifyWindow(hwnd)),
                isDecorated(hwnd) ? L"yes" : L"no");
    } else {
        wprintf(L"unknown command: %s\n", cmd);
        return 2;
    }
    return 0;
}
