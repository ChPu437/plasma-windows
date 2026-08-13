// trayclient-spike - Phase 1 spike client: exercises Shell_NotifyIcon
// against whatever Shell_TrayWnd window is the receiver (ours, if the
// spike host is running and won the z-order battle).
//
// Pure Win32, no Qt. Sequence: NIM_ADD -> wait -> NIM_MODIFY -> wait ->
// NIM_DELETE. Also prints the callback messages it receives (our spike
// host simulates a left click after the first NIM_ADD).

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

static HWND g_cbWnd = nullptr;
static UINT g_cbMsg = 0;

static LRESULT CALLBACK CallbackWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_cbMsg) {
        printf("[client] callback: wParam=0x%X lParam=0x%X (%s)\n", (unsigned)wParam, (unsigned)lParam,
               lParam == WM_LBUTTONDOWN ? "WM_LBUTTONDOWN"
                   : lParam == WM_LBUTTONUP ? "WM_LBUTTONUP"
                                            : lParam == WM_RBUTTONDOWN ? "WM_RBUTTONDOWN"
                                                                       : "other");
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL DoNim(UINT action, const wchar_t *what, bool useGuid)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_cbWnd;
    nid.uID = 42;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | (useGuid ? NIF_GUID : 0);
    nid.uCallbackMessage = g_cbMsg;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, _TRUNCATE, useGuid ? L"trayclient-spike-guid" : L"trayclient-spike-noguid", 64);
    nid.guidItem = {0x11111111, 0x2222, 0x3333, {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB}};

    const BOOL ok = Shell_NotifyIconW(action, &nid);
    printf("[client] %S -> %s (err=%lu)\n", what, ok ? "TRUE" : "FALSE", GetLastError());
    return ok;
}

int main(int argc, char **argv)
{
    const bool useGuid = (argc < 2 || _stricmp(argv[1], "-noguid") != 0);
    printf("trayclient-spike starting (guid=%s)\n", useGuid ? "yes" : "no");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CallbackWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TrayClientSpikeWnd";
    RegisterClassExW(&wc);
    g_cbWnd = CreateWindowExW(0, L"TrayClientSpikeWnd", nullptr, WS_OVERLAPPED,
                              100, 100, 200, 100, nullptr, nullptr, wc.hInstance, nullptr);
    g_cbMsg = WM_APP + 0x42;
    printf("[client] callback window %p, message 0x%X\n", (void *)g_cbWnd, (unsigned)g_cbMsg);

    if (!DoNim(NIM_ADD, L"NIM_ADD", useGuid)) {
        printf("[client] NIM_ADD failed - no notification area receiver?\n");
    }
    Sleep(3000);
    DoNim(NIM_MODIFY, L"NIM_MODIFY", useGuid);
    Sleep(3000);

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DoNim(NIM_DELETE, L"NIM_DELETE", useGuid);
    printf("[client] done\n");
    return 0;
}
