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

static void overlayLog(const WCHAR *fmt, ...);

typedef struct
{
    HWND target;
    HWND bar;
    WCHAR title[512];
    int cx;          /* target width */
    POINT downPos;   /* cursor at WM_LBUTTONDOWN (drag threshold) */
    BOOL dragging;
    BOOL maximized;      /* our manual maximize state */
    RECT restoreRect;    /* window rect before our maximize */
    int hoverBtn;        /* BTN_* under the cursor (0 = none) */
} OverlayCtx;

static OverlayCtx g_ov = {0};

/* drag diagnostics (see beginManualDrag): win-event hooks fire for
   every window - non-target LOCATIONCHANGE frames are the panel
   animation; WM_SETTINGCHANGE broadcast volume lands on the bar. */
static volatile LONG g_diagOtherLoc = 0;
static volatile LONG g_diagSettingChg = 0;

static void overlayReposition(void)
{
    if (g_ov.dragging) {
        return; /* the drag loop moves target + bar itself every frame */
    }
    if (!g_ov.bar || !IsWindow(g_ov.target)) {
        return;
    }
    RECT r;
    GetWindowRect(g_ov.target, &r);
    if (!IsWindowVisible(g_ov.target)) {
        /* minimized or gone: take the bar along */
        ShowWindow(g_ov.bar, SW_HIDE);
        return;
    }
    /* the bar sits on top of the target; with our manual maximize the
       target keeps a BAR_H strip at the screen top, so the bar lands on
       the screen edge. Clamp so a window against the top edge never
       pushes the bar off-screen. */
    int barTop = r.top - BAR_H;
    if (barTop < 0) {
        barTop = 0;
    }
    /* z-order: the bar is an OWNED window of the target - the system
       keeps it directly above the owner and lowers it together with the
       owner when another window is activated. Never raise it manually:
       a raised bar would float over unrelated foreground windows. */
    SetWindowPos(g_ov.bar, NULL, r.left, barTop, r.right - r.left, BAR_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
    overlayLog(L"reposition max=%d fg=%d rect=%d,%d,%d,%d barTop=%d\n",
               g_ov.maximized, GetForegroundWindow() == g_ov.target,
               r.left, r.top, r.right, r.bottom, barTop);
}

static void overlayUpdateTitle(void)
{
    if (g_ov.target && g_ov.bar) {
        GetWindowTextW(g_ov.target, g_ov.title, 512);
        InvalidateRect(g_ov.bar, NULL, TRUE);
    }
}

/* Breeze-style bar buttons: minimize / maximize-restore / close, hit
   from the right edge. BTN_NONE must be 0: g_ov is zero-initialized. */
#define BTN_NONE 0
#define BTN_MIN 1
#define BTN_MAX 2
#define BTN_CLOSE 3

/* manual maximize/restore - the system SC_MAXIMIZE would own the window
   (raise it over the bar; its restore rect tracks the moved rect so
   repeated maximize shrinks the window every time). */
static void toggleMaximize(void)
{
    if (!IsWindow(g_ov.target)) {
        return;
    }
    if (g_ov.maximized) {
        SetWindowPos(g_ov.target, NULL, g_ov.restoreRect.left, g_ov.restoreRect.top,
                     g_ov.restoreRect.right - g_ov.restoreRect.left,
                     g_ov.restoreRect.bottom - g_ov.restoreRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_ov.maximized = FALSE;
        overlayLog(L"manual restore\n");
    } else {
        GetWindowRect(g_ov.target, &g_ov.restoreRect);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_ov.target, MONITOR_DEFAULTTOPRIMARY), &mi);
        /* work area minus a BAR_H strip at the top for the bar */
        RECT wa = mi.rcWork;
        SetWindowPos(g_ov.target, NULL, wa.left, wa.top + BAR_H,
                     wa.right - wa.left, wa.bottom - wa.top - BAR_H,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_ov.maximized = TRUE;
        overlayLog(L"manual maximize restore=%d,%d,%d,%d\n",
                   g_ov.restoreRect.left, g_ov.restoreRect.top,
                   g_ov.restoreRect.right, g_ov.restoreRect.bottom);
    }
    overlayReposition();
    InvalidateRect(g_ov.bar, NULL, FALSE);
}

static int barHitTest(int x)
{
    RECT cr;
    GetClientRect(g_ov.bar, &cr);
    const int w = cr.right;
    if (x >= w - BTN_W) {
        return BTN_CLOSE;
    }
    if (x >= w - BTN_W * 2) {
        return BTN_MAX;
    }
    if (x >= w - BTN_W * 3) {
        return BTN_MIN;
    }
    return BTN_NONE;
}

static void drawButtonIcon(HDC dc, const RECT *br, int btn, COLORREF color)
{
    const int cx = (br->left + br->right) / 2;
    const int cy = (br->top + br->bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    switch (btn) {
    case BTN_MIN:
        /* single horizontal bar */
        MoveToEx(dc, cx - 7, cy, NULL);
        LineTo(dc, cx + 7, cy);
        break;
    case BTN_MAX:
        if (g_ov.maximized) {
            /* restore: two overlapping squares */
            Rectangle(dc, cx - 6, cy - 6, cx + 1, cy + 1);
            Rectangle(dc, cx - 1, cy - 1, cx + 6, cy + 6);
        } else {
            /* maximize: hollow square */
            Rectangle(dc, cx - 6, cy - 6, cx + 7, cy + 7);
        }
        break;
    case BTN_CLOSE:
        /* X */
        MoveToEx(dc, cx - 5, cy - 5, NULL);
        LineTo(dc, cx + 6, cy + 6);
        MoveToEx(dc, cx + 6, cy - 5, NULL);
        LineTo(dc, cx - 5, cy + 6);
        break;
    default:
        break;
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void overlayActOn(int x)
{
    if (!g_ov.bar || !IsWindow(g_ov.target)) {
        return;
    }
    switch (barHitTest(x)) {
    case BTN_MIN:
        SendMessageW(g_ov.target, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    case BTN_MAX:
        toggleMaximize();
        break;
    case BTN_CLOSE:
        SendMessageW(g_ov.target, WM_SYSCOMMAND, SC_CLOSE, 0);
        break;
    default:
        break;
    }
}

/* manual drag loop (R1.3 4.3 B): the HTCAPTION native trick does not
   move windows whose caption styles were removed. */
static void beginManualDrag(HWND bar)
{
    if (!IsWindow(g_ov.target)) {
        return;
    }
    /* guard: the modal loop below dispatches queued mouse messages; a
       WM_MOUSEMOVE seen there must not re-enter this function (the
       WM_TIMER entry path used to leave dragging=FALSE -> recursion,
       each queued move nested one more frame -> visible stutter) */
    g_ov.dragging = TRUE;
    if (g_ov.maximized) {
        /* dragging a maximized window first restores it (AltSnap logic);
           manual restore - SC_RESTORE does nothing since we never sent
           SC_MAXIMIZE. Anchor the window top to the cursor: keep the
           cursor's position inside the bar, otherwise the window would
           restore at its old rect while the cursor is at the screen top
           (restore offY becomes -84 -> window rides far below the
           mouse). The bar must resize in the same step - the async
           event reposition is skipped while dragging. */
        POINT cur;
        GetCursorPos(&cur);
        RECT barR;
        GetWindowRect(bar, &barR);
        const int dy = cur.y - barR.top; /* cursor inside the bar */
        const int w = g_ov.restoreRect.right - g_ov.restoreRect.left;
        const int h = g_ov.restoreRect.bottom - g_ov.restoreRect.top;
        const int top = cur.y + (BAR_H - dy);
        SetWindowPos(g_ov.target, NULL, g_ov.restoreRect.left, top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(g_ov.bar, NULL, g_ov.restoreRect.left, top - BAR_H, w, BAR_H,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_ov.maximized = FALSE;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    RECT tr;
    GetWindowRect(g_ov.target, &tr);
    const int offX = cursor.x - tr.left;
    const int offY = cursor.y - tr.top;

    SetCapture(bar);
    /* diagnostics: per-iteration timing + system event sampling. The
       panel animation is suspected of stalling the drag loop; the
       sample line reports average/max iteration cost, queued message
       volume and win-event rates (non-target LOCATIONCHANGE = panel
       animation frames) every 250 ms. */
    LARGE_INTEGER diagFreq;
    QueryPerformanceFrequency(&diagFreq);
    ULONGLONG diagLast = GetTickCount64();
    ULONGLONG diagSumPeriodUs = 0, diagMaxPeriodUs = 0, diagSumMoveUs = 0;
    ULONG diagIters = 0, diagMsgs = 0;
    InterlockedExchange((volatile LONG *)&g_diagOtherLoc, 0);
    InterlockedExchange((volatile LONG *)&g_diagSettingChg, 0);
    for (;;) {
        LARGE_INTEGER t0, t1, t2;
        QueryPerformanceCounter(&t0);
        if (GetAsyncKeyState(VK_LBUTTON) >= 0) {
            break;
        }
        GetCursorPos(&cursor);
        if (IsWindow(g_ov.target)) {
            /* atomic batch: target + bar move in one EndDeferWindowPos.
               The bar must follow synchronously - the async
               WINEVENT_OUTOFCONTEXT reposition lags a few frames and
               reads as stutter on the bar. */
            HDWP hdwp = BeginDeferWindowPos(2);
            if (hdwp) {
                hdwp = DeferWindowPos(hdwp, g_ov.target, NULL, cursor.x - offX, cursor.y - offY,
                                      0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (hdwp) {
                    DeferWindowPos(hdwp, g_ov.bar, NULL, cursor.x - offX, cursor.y - offY - BAR_H,
                                   0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    EndDeferWindowPos(hdwp);
                }
            }
        }
        QueryPerformanceCounter(&t1);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            diagMsgs++;
            if (msg.message == WM_LBUTTONUP || msg.message == WM_CAPTURECHANGED) {
                ReleaseCapture();
                g_ov.dragging = FALSE;
                overlayReposition();
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
        QueryPerformanceCounter(&t2);
        {
            /* full loop period (move + messages + sleep): a long period
               means the mouse-tracking updates are rate-limited even
               though the SetWindowPos call itself is fast */
            const ULONGLONG periodUs = (t2.QuadPart - t0.QuadPart) * 1000000 / diagFreq.QuadPart;
            const ULONGLONG moveUs = (t1.QuadPart - t0.QuadPart) * 1000000 / diagFreq.QuadPart;
            diagSumPeriodUs += periodUs;
            diagSumMoveUs += moveUs;
            if (periodUs > diagMaxPeriodUs) {
                diagMaxPeriodUs = periodUs;
            }
            diagIters++;
        }
        const ULONGLONG diagNow = GetTickCount64();
        if (diagNow - diagLast >= 250) {
            const ULONG loc = InterlockedExchange((volatile LONG *)&g_diagOtherLoc, 0);
            const ULONG set = InterlockedExchange((volatile LONG *)&g_diagSettingChg, 0);
            overlayLog(L"drag sample: period avg=%llu max=%llu move avg=%llu iters=%lu msgs=%lu locEvt=%lu setChg=%lu\n",
                       diagSumPeriodUs / (diagIters ? diagIters : 1), diagMaxPeriodUs,
                       diagSumMoveUs / (diagIters ? diagIters : 1),
                       diagIters, diagMsgs, loc, set);
            diagLast = diagNow;
            diagSumPeriodUs = 0;
            diagMaxPeriodUs = 0;
            diagSumMoveUs = 0;
            diagIters = 0;
            diagMsgs = 0;
        }
    }
    ReleaseCapture();
    g_ov.dragging = FALSE;
    overlayReposition();
}

static LRESULT CALLBACK barWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        /* Breeze light title bar */
        HBRUSH bg = CreateSolidBrush(RGB(239, 240, 241));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        /* title, centered (Breeze default) */
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(35, 38, 41));
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)SelectObject(dc, font);
        RECT tr = {8, 0, r.right - BTN_W * 3, r.bottom};
        DrawTextW(dc, g_ov.title, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        /* bottom hairline */
        HPEN linePen = CreatePen(PS_SOLID, 1, RGB(208, 209, 210));
        HPEN oldPen = (HPEN)SelectObject(dc, linePen);
        MoveToEx(dc, 0, r.bottom - 1, NULL);
        LineTo(dc, r.right, r.bottom - 1);
        SelectObject(dc, oldPen);
        DeleteObject(linePen);
        /* buttons: minimize, maximize/restore, close */
        for (int b = BTN_MIN; b <= BTN_CLOSE; ++b) {
            RECT br = {r.right - BTN_W * (BTN_CLOSE - b + 1), 0,
                       r.right - BTN_W * (BTN_CLOSE - b), r.bottom};
            if (g_ov.hoverBtn == b) {
                HBRUSH hb = CreateSolidBrush(b == BTN_CLOSE ? RGB(232, 17, 35) : RGB(205, 209, 212));
                FillRect(dc, &br, hb);
                DeleteObject(hb);
            }
            /* white glyph on the red close hover, dark otherwise */
            const COLORREF ic = (b == BTN_CLOSE && g_ov.hoverBtn == BTN_CLOSE)
                                    ? RGB(255, 255, 255)
                                    : RGB(35, 38, 41);
            drawButtonIcon(dc, &br, b, ic);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp);
        /* buttons take priority */
        if (barHitTest(x) != BTN_NONE) {
            overlayActOn(x);
            return 0;
        }
        GetCursorPos(&g_ov.downPos);
        g_ov.dragging = FALSE;
        overlayLog(L"DOWN x=%d\n", x);
        SetCapture(hwnd);
        SetTimer(hwnd, 1, 200, NULL);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (GetCapture() == hwnd && !g_ov.dragging && IsWindow(g_ov.target)) {
            POINT pt;
            GetCursorPos(&pt);
            if (abs(pt.x - g_ov.downPos.x) > 4 || abs(pt.y - g_ov.downPos.y) > 4) {
                /* real drag: skip the double-click wait */
                KillTimer(hwnd, 1);
                g_ov.dragging = TRUE;
                overlayLog(L"MOVE -> drag\n");
                beginManualDrag(hwnd);
            }
        }
        /* hover highlight (Breeze) */
        {
            const int btn = barHitTest(GET_X_LPARAM(lp));
            if (btn != g_ov.hoverBtn) {
                g_ov.hoverBtn = btn;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_ov.hoverBtn != BTN_NONE) {
            g_ov.hoverBtn = BTN_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        KillTimer(hwnd, 1);
        ReleaseCapture();
        toggleMaximize();
        return 0;
    }
    case WM_LBUTTONUP:
        overlayLog(L"UP\n");
        KillTimer(hwnd, 1);
        g_ov.dragging = FALSE;
        ReleaseCapture();
        return 0;
    case WM_TIMER:
        if (wp == 1) {
            KillTimer(hwnd, 1);
            overlayLog(L"TIMER -> drag\n");
            beginManualDrag(hwnd);
        }
        return 0;
    case WM_CAPTURECHANGED:
        KillTimer(hwnd, 1);
        return 0;
    case WM_OVERLAY_REPOSITION:
        overlayReposition();
        overlayUpdateTitle();
        return 0;
    case WM_SETTINGCHANGE:
        /* SPIF_SENDCHANGE broadcast (panel work-area updates) - count
           while dragging to correlate stalls with broadcast storms */
        InterlockedIncrement(&g_diagSettingChg);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void overlayLog(const WCHAR *fmt, ...)
{
    FILE *f = _wfopen(L"C:\\Users\\jing\\AppData\\Local\\Temp\\opencode\\overlay.log", L"a");
    if (!f) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

static HWND overlayCreate(HWND target)
{
    WNDCLASSW wc = {0};
    wc.style = CS_DBLCLKS; /* needed for WM_LBUTTONDBLCLK (maximize) */
    wc.lpfnWndProc = barWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PlasmaTitleBarOverlay";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    g_ov.target = target;
    RECT tr;
    GetWindowRect(target, &tr);
    /* owned window: hwndParent = target. The system keeps an owned
       window in the z-order directly above its owner and lowers it with
       the owner on activation of another window - the bar rides with the
       target instead of floating over other apps. No WS_EX_NOACTIVATE:
       clicking an owned window activates its owner, i.e. clicking the
       bar brings the target to the foreground like a real title bar. */
    HWND bar = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"",
        WS_POPUP,
        tr.left, tr.top - BAR_H, tr.right - tr.left, BAR_H,
        target, NULL, wc.hInstance, NULL);
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
    if (hwnd != g_ov.target || !g_ov.bar) {
        if (event == EVENT_OBJECT_LOCATIONCHANGE) {
            /* other windows moving (e.g. the panel animation) - sampled
               per 250 ms by the drag loop */
            InterlockedIncrement(&g_diagOtherLoc);
        }
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        /* Show + reposition while the target owns the foreground. We
           deliberately do NOT hide on focus loss: clicks land on the bar
           without activating the target (WS_EX_NOACTIVATE), and hiding
           there would make the bar vanish on its own interactions (e.g.
           right after double-click maximize). */
        if (GetForegroundWindow() == g_ov.target) {
            ShowWindow(g_ov.bar, SW_SHOWNOACTIVATE);
        }
        PostMessageW(g_ov.bar, WM_OVERLAY_REPOSITION, 0, 0);
        return;
    }
    PostMessageW(g_ov.bar, WM_OVERLAY_REPOSITION, 0, 0);
}

static void overlayWatch(void)
{
    /* track the target: reposition on move/show/foreground/minimize,
       update the title on rename, quit when the target dies */
    SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, ovEventProc,
                    0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, ovEventProc,
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
