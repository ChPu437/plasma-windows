// trayhost-spike - Phase 1 spike: register the Shell_TrayWnd receiver
// window and log every WM_COPYDATA (the Shell_NotifyIcon protocol).
//
// Mirrors ManagedShell's TrayService (MIT) with the minimal surface we
// need to prove the approach on LTSC:
//   * Shell_TrayWnd (hidden, TOPMOST, TOOLWINDOW) + TrayNotifyWnd child
//   * WM_COPYDATA parsing: dwData=1 SHELLTRAYDATA (NIM_* + NOTIFYICONDATA),
//     dwData=3 WINNOTIFYICONIDENTIFIER (Shell_NotifyIconGetRect)
//   * TaskbarCreated broadcast after creation
//   * 100 ms z-order guard (explorer coexistence; harmless in the VM)
//   * after the first NIM_ADD is captured, a simulated left-click is sent
//     back to the app's callback window to prove round-tripping
//
// Pure Win32, no Qt. Run: trayhost-spike.exe [logfile]

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

// ---- protocol structures (ManagedShell.NativeMethods.Shell32) ---------------
struct SHELLTRAYDATA
{
    int dwUnknown;
    UINT dwMessage;
    NOTIFYICONDATAW nid;
};

struct WINNOTIFYICONIDENTIFIER
{
    int dwMagic;
    int dwMessage;
    int cbSize;
    int dwPadding;
    UINT hWnd;
    UINT uID;
    GUID guidItem;
};

// ---------------------------------------------------------------------------
static HWND g_trayWnd = nullptr;
static HWND g_notifyWnd = nullptr;
static FILE *g_log = nullptr;

static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_log) {
        vfprintf(g_log, fmt, ap);
        fflush(g_log);
    }
    vprintf(fmt, ap);
    va_end(ap);
}

static void logNid(const NOTIFYICONDATAW *nid)
{
    wchar_t tip[64] = {};
    wcsncpy_s(tip, 64, nid->szTip, _TRUNCATE);
    wchar_t info[64] = {};
    wcsncpy_s(info, 64, nid->szInfo, _TRUNCATE);
    logf("      hWnd=%p uID=%u cbSize=%u flags=0x%X\n", (void *)nid->hWnd, nid->uID,
         (unsigned)nid->cbSize, (unsigned)nid->uFlags);
    logf("      tip=[%ls] msg=0x%X version=%u icon=%p\n", tip, (unsigned)nid->uCallbackMessage,
         (unsigned)nid->uVersion, (void *)nid->hIcon);
    logf("      info=[%ls] title=[%ls]\n", info, nid->szInfoTitle);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COPYDATA: {
        const COPYDATASTRUCT *cds = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
        logf("[WM_COPYDATA] dwData=%d cbData=%u from=%p\n", (int)cds->dwData, (unsigned)cds->cbData,
             (void *)wParam);
        switch (cds->dwData) {
        case 1: { // SHELLTRAYDATA
            if (cds->cbData < sizeof(int) + sizeof(UINT)) {
                break;
            }
            const SHELLTRAYDATA *st = reinterpret_cast<const SHELLTRAYDATA *>(cds->lpData);
            const char *action = "?";
            switch (st->dwMessage) {
            case NIM_ADD: action = "NIM_ADD"; break;
            case NIM_MODIFY: action = "NIM_MODIFY"; break;
            case NIM_DELETE: action = "NIM_DELETE"; break;
            case NIM_SETFOCUS: action = "NIM_SETFOCUS"; break;
            case NIM_SETVERSION: action = "NIM_SETVERSION"; break;
            }
            logf("  SHELLTRAYDATA: msg=%u (%s)\n", (unsigned)st->dwMessage, action);
            if (st->dwMessage != NIM_SETVERSION) {
                logNid(&st->nid);
            }
            // Round-trip: simulate a left-button down/up on the first icon.
            if (st->dwMessage == NIM_ADD && st->nid.hWnd && st->nid.uCallbackMessage) {
                logf("  -> sending simulated click to %p (msg 0x%X)\n", (void *)st->nid.hWnd,
                     (unsigned)st->nid.uCallbackMessage);
                SendMessageW(st->nid.hWnd, st->nid.uCallbackMessage, st->nid.uID, WM_LBUTTONDOWN);
                SendMessageW(st->nid.hWnd, st->nid.uCallbackMessage, st->nid.uID, WM_LBUTTONUP);
            }
            return TRUE;
        }
        case 3: { // WINNOTIFYICONIDENTIFIER (Shell_NotifyIconGetRect)
            if (cds->cbData < sizeof(WINNOTIFYICONIDENTIFIER)) {
                break;
            }
            const WINNOTIFYICONIDENTIFIER *wi = reinterpret_cast<const WINNOTIFYICONIDENTIFIER *>(cds->lpData);
            logf("  WINNOTIFYICONIDENTIFIER: magic=%d msg=%d hWnd=%p uID=%u\n", wi->dwMagic, wi->dwMessage,
                 (void *)(ULONG_PTR)wi->hWnd, (unsigned)wi->uID);
            return 1; // pretend the icon rect is at (0,0); apps only use it for tooltip placement
        }
        default:
            break;
        }
        return FALSE;
    }
    case WM_WINDOWPOSCHANGED: {
        // keep the tray window hidden (mirrors ManagedShell)
        WINDOWPOS *wp = reinterpret_cast<WINDOWPOS *>(lParam);
        if ((wp->flags & SWP_SHOWWINDOW) || IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Keep our Shell_TrayWnd above explorer's so FindWindow resolves to us.
static VOID CALLBACK ZOrderGuard(HWND hwnd, UINT, UINT_PTR, DWORD)
{
    HWND top = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (top != g_trayWnd) {
        logf("[z-order] FindWindow -> other window, raising ours\n");
        SetWindowPos(g_trayWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

int main(int argc, char **argv)
{
    const char *logPath = (argc > 1) ? argv[1] : "trayhost-spike.log";
    g_log = fopen(logPath, "w");
    logf("trayhost-spike starting (log: %s)\n", logPath);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Shell_TrayWnd";
    RegisterClassExW(&wc);
    logf("Shell_TrayWnd class registered\n");

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int trayH = 23; // DPI-scaled in ManagedShell; enough for the spike
    g_trayWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                L"Shell_TrayWnd", nullptr,
                                WS_POPUP, 0, 0, screenW, trayH,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_trayWnd) {
        logf("ERROR: CreateWindowEx Shell_TrayWnd failed: %lu\n", GetLastError());
        return 1;
    }
    logf("Shell_TrayWnd created: %p\n", (void *)g_trayWnd);

    // TrayNotifyWnd child (mirror of the explorer hierarchy)
    wc.lpszClassName = L"TrayNotifyWnd";
    RegisterClassExW(&wc);
    g_notifyWnd = CreateWindowExW(0, L"TrayNotifyWnd", nullptr, WS_CHILD, 0, 0, 32, 32,
                                  g_trayWnd, nullptr, wc.hInstance, nullptr);
    logf("TrayNotifyWnd created: %p\n", (void *)g_notifyWnd);

    // TaskbarCreated broadcast - apps re-register their icons
    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    SendNotifyMessageW(HWND_BROADCAST, taskbarCreated, 0, 0);
    logf("TaskbarCreated broadcast (msg 0x%X)\n", (unsigned)taskbarCreated);

    // z-order guard while explorer coexists
    SetTimer(g_trayWnd, 1, 100, ZOrderGuard);

    logf("Ready. Waiting for Shell_NotifyIcon traffic...\n");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    logf("exiting\n");
    if (g_log) {
        fclose(g_log);
    }
    return 0;
}
