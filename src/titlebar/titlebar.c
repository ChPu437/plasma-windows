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
#include <windowsx.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

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
/* overlay (Plasma-style title bar for decorated windows)              */
/* ------------------------------------------------------------------ */
/* A minimal spike of R1.3 section 4.4: a frameless WS_EX_NOACTIVATE
   top-level bar above the target. It paints the title, draws close /
   minimize buttons and forwards drag to the target via WM_NCLBUTTONDOWN
   HTCAPTION (the target's own modal move loop - gives native move
   animation and Win10 edge snap where available). */

#define BAR_H 32
#define BTN_W 32
#define WM_OVERLAY_REPOSITION (WM_APP + 2)

typedef struct
{
    HWND target;
    HWND bar;
    WCHAR title[512];
    int cx; /* target width */
} OverlayCtx;

static OverlayCtx g_ov = {0};

static void overlayReposition(void)
{
    if (!g_ov.bar || !IsWindow(g_ov.target)) {
        return;
    }
    RECT r;
    GetWindowRect(g_ov.target, &r);
    /* place the bar right above the target, full target width */
    SetWindowPos(g_ov.bar, g_ov.target, r.left, r.top - BAR_H, r.right - r.left, BAR_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void overlayUpdateTitle(void)
{
    if (g_ov.target && g_ov.bar) {
        GetWindowTextW(g_ov.target, g_ov.title, 512);
        InvalidateRect(g_ov.bar, NULL, TRUE);
    }
}

static void overlayActOn(int x)
{
    /* x within the bar: last BTN_W = close, previous = minimize */
    if (g_ov.bar && IsWindow(g_ov.target)) {
        int w;
        RECT r;
        GetWindowRect(g_ov.bar, &r);
        w = r.right - r.left;
        if (x >= w - BTN_W) {
            SendMessageW(g_ov.target, WM_SYSCOMMAND, SC_CLOSE, 0);
        } else if (x >= w - BTN_W * 2) {
            SendMessageW(g_ov.target, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
    }
}

static LRESULT CALLBACK barWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        /* plasma-ish dark bar */
        HBRUSH bg = CreateSolidBrush(RGB(45, 45, 48));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        /* title */
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(230, 230, 230));
        RECT tr = {8, 0, r.right - BTN_W * 2, r.bottom};
        DrawTextW(dc, g_ov.title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        /* buttons */
        RECT close = {r.right - BTN_W, 0, r.right, r.bottom};
        RECT min = {r.right - BTN_W * 2, 0, r.right - BTN_W, r.bottom};
        FrameRect(dc, &min, (HBRUSH)GetStockObject(GRAY_BRUSH));
        FrameRect(dc, &close, (HBRUSH)GetStockObject(GRAY_BRUSH));
        /* minimize: small horizontal line */
        POINT ml = {min.left + 10, min.top + 16};
        POINT mr = {min.right - 10, min.top + 16};
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(230, 230, 230));
        HPEN old = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, ml.x, ml.y, NULL);
        LineTo(dc, mr.x, mr.y);
        /* close: X */
        MoveToEx(dc, close.left + 9, close.top + 9, NULL);
        LineTo(dc, close.right - 9, close.bottom - 9);
        MoveToEx(dc, close.right - 9, close.top + 9, NULL);
        LineTo(dc, close.left + 9, close.bottom - 9);
        SelectObject(dc, old);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp);
        RECT cr;
        GetClientRect(hwnd, &cr);
        /* buttons take priority */
        if (x >= cr.right - BTN_W) {
            overlayActOn(x);
            return 0;
        }
        if (x >= cr.right - BTN_W * 2) {
            overlayActOn(x);
            return 0;
        }
        /* drag: forward to the target's modal move loop */
        if (IsWindow(g_ov.target)) {
            SendMessageW(g_ov.target, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        if (IsWindow(g_ov.target)) {
            SendMessageW(g_ov.target, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_OVERLAY_REPOSITION:
        overlayReposition();
        overlayUpdateTitle();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND overlayCreate(HWND target)
{
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = barWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PlasmaTitleBarOverlay";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    g_ov.target = target;
    RECT tr;
    GetWindowRect(target, &tr);
    HWND bar = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"",
        WS_POPUP,
        tr.left, tr.top - BAR_H, tr.right - tr.left, BAR_H,
        NULL, NULL, wc.hInstance, NULL);
    g_ov.bar = bar;
    overlayUpdateTitle();
    overlayReposition();
    ShowWindow(bar, SW_SHOWNOACTIVATE);
    return bar;
}

static void CALLBACK ovEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                 LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hHook;
    (void)event;
    (void)idObject;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;
    if (hwnd == g_ov.target && g_ov.bar) {
        PostMessageW(g_ov.bar, WM_OVERLAY_REPOSITION, 0, 0);
    }
}

static void overlayWatch(void)
{
    /* track the target: reposition on move/show/hide/minimize, update
       the title on rename, quit when the target dies */
    SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, NULL, ovEventProc,
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

    if (wcscmp(argv[1], L"--overlay") == 0) {
        const WCHAR *arg = argc >= 3 ? argv[2] : NULL;
        HWND target = resolveHwnd(arg);
        if (!target) {
            wprintf(L"window not found\n");
            return 1;
        }
        overlayCreate(target);
        overlayWatch();
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
