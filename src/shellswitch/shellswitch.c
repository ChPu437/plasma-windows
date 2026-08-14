/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

    shellswitch.c - tray-resident shell switcher (explorer <-> plasma).

    Pure Win32, zero dependencies: it must keep working no matter which
    shell is (or is not) running. It registers a real Shell_NotifyIcon
    tray icon, so it shows up under explorer's system tray, and under
    plasma it is bridged into the Plasma tray by trayhost like any other
    Windows tray app.

    Switching changes only the current user's
    HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
    value (never system binaries, never Winlogon), kills the shell that
    is being disabled and starts the target one.

    Safety: set SWITCH_DRYRUN=1 in the environment to log what would
    happen without killing/starting anything or writing the registry.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <tlhelp32.h>

#define WM_TRAYICON (WM_APP + 1)
#define IDM_SWITCH_PLASMA 1001
#define IDM_SWITCH_EXPLORER 1002
#define IDM_CURRENT 1003
#define IDM_EXIT 1004

static const WCHAR kWinlogonKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static const WCHAR kShellValue[] = L"Shell";

static HWND g_hwnd = NULL;
static HICON g_icon = NULL;
static BOOL g_dryRun = FALSE;
static WCHAR g_sessionCmd[MAX_PATH * 2] = {0}; /* full path to session-shell.cmd */
static WCHAR g_currentShell[1024] = {0};        /* registry value */
static UINT g_taskbarCreatedMsg = 0;
static NOTIFYICONDATAW g_nid = {0};

static void logMsg(const WCHAR *fmt, ...);
static void updateTrayTip(void);
static void addTrayIcon(void);
static BOOL isPlasmaValue(const WCHAR *value);

static void logMsg(const WCHAR *fmt, ...)
{
    WCHAR buf[1024];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(buf, 1024, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)wcslen(buf), NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static BOOL readShellValue(WCHAR *out, DWORD outChars)
{
    HKEY key = NULL;
    BOOL ok = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kWinlogonKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = outChars * sizeof(WCHAR);
        if (RegQueryValueExW(key, kShellValue, NULL, &type, (LPBYTE)out, &size) == ERROR_SUCCESS && type == REG_SZ) {
            ok = TRUE;
        }
        RegCloseKey(key);
    }
    if (!ok) {
        out[0] = 0;
    }
    return ok;
}

static BOOL writeShellValue(const WCHAR *value)
{
    if (g_dryRun) {
        logMsg(L"[dry-run] would set Shell = %s\n", value);
        return TRUE;
    }
    HKEY key = NULL;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kWinlogonKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) {
        logMsg(L"RegCreateKeyEx failed: %lu\n", r);
        return FALSE;
    }
    r = RegSetValueExW(key, kShellValue, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) {
        logMsg(L"RegSetValueEx failed: %lu\n", r);
        return FALSE;
    }
    logMsg(L"Shell set to: %s\n", value);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* process control                                                     */
/* ------------------------------------------------------------------ */

static void runTaskkill(const WCHAR *imageName)
{
    if (g_dryRun) {
        logMsg(L"[dry-run] would kill %s\n", imageName);
        return;
    }
    WCHAR cmd[MAX_PATH * 2];
    StringCchPrintfW(cmd, MAX_PATH * 2, L"taskkill.exe /f /im %s", imageName);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static BOOL isProcessRunning(const WCHAR *imageName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);
    BOOL found = FALSE;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, imageName) == 0) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* trayhost watchdog: while a plasma session is up, make sure trayhost
   (the SNI bridge that puts this icon into the plasma tray) is alive -
   it crashes under heavy DELETE/ADD churn, taking the tray down. */
static void ensureTrayHost(void)
{
    if (!isProcessRunning(L"plasmashell.exe") || isProcessRunning(L"trayhost.exe")) {
        return;
    }
    if (g_dryRun) {
        logMsg(L"[dry-run] would restart trayhost.exe\n");
        return;
    }
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return;
    }
    WCHAR *slash = wcsrchr(path, L'\\');
    if (slash) {
        *slash = 0;
    }
    WCHAR trayhostPath[MAX_PATH * 2];
    StringCchPrintfW(trayhostPath, MAX_PATH * 2, L"%s\\trayhost.exe", path);
    if (GetFileAttributesW(trayhostPath) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(trayhostPath, NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        logMsg(L"trayhost restarted\n");
    }
}

static void killPlasmaSession(void)
{
    /* Order matters: plasmashell first (session host script exits with
       it), then the services, then the bus. */
    runTaskkill(L"plasmashell.exe");
    runTaskkill(L"trayhost.exe");
    runTaskkill(L"kactivitymanagerd.exe");
    runTaskkill(L"kded6.exe");
    runTaskkill(L"dbus-daemon.exe");
}

static void startExplorer(void)
{
    if (g_dryRun) {
        logMsg(L"[dry-run] would start explorer.exe\n");
        return;
    }
    ShellExecuteW(NULL, NULL, L"explorer.exe", NULL, NULL, SW_SHOWNORMAL);
}

static void startPlasmaSession(void)
{
    if (g_dryRun) {
        logMsg(L"[dry-run] would start: cmd.exe /c \"%s\"\n", g_sessionCmd);
        return;
    }
    WCHAR cmd[MAX_PATH * 2 + 16];
    StringCchPrintfW(cmd, MAX_PATH * 2 + 16, L"cmd.exe /c \"%s\"", g_sessionCmd);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

/* ------------------------------------------------------------------ */
/* switching                                                           */
/* ------------------------------------------------------------------ */

static BOOL isPlasmaValue(const WCHAR *value)
{
    return value && (wcsstr(value, L"session-shell.cmd") != NULL || wcsstr(value, L"plasmashell.exe") != NULL);
}

static void doSwitchToExplorer(void)
{
    logMsg(L"Switching to explorer...\n");
    if (writeShellValue(L"explorer.exe")) {
        killPlasmaSession();
        startExplorer();
        readShellValue(g_currentShell, 1024);
        updateTrayTip();
    }
}

static void doSwitchToPlasma(void)
{
    if (!g_sessionCmd[0]) {
        logMsg(L"ERROR: session-shell.cmd not found\n");
        MessageBoxW(g_hwnd, L"session-shell.cmd not found.\n\nSearched:\n  D:\\Projects\\CraftRoot\\session-shell.cmd\n  D:\\documents\\shared\\plasma-vm\\session-shell.cmd",
                    L"Shell Switcher", MB_ICONERROR);
        return;
    }
    WCHAR regValue[MAX_PATH * 2 + 16];
    StringCchPrintfW(regValue, MAX_PATH * 2 + 16, L"cmd.exe /c \"%s\"", g_sessionCmd);
    logMsg(L"Switching to plasma...\n");
    if (writeShellValue(regValue)) {
        runTaskkill(L"explorer.exe");
        startPlasmaSession();
        readShellValue(g_currentShell, 1024);
        updateTrayTip();
    }
}

/* ------------------------------------------------------------------ */
/* session script discovery                                            */
/* ------------------------------------------------------------------ */

static void discoverSessionCmd(void)
{
    const WCHAR *candidates[] = {
        L"D:\\Projects\\CraftRoot\\session-shell.cmd",
        L"D:\\documents\\shared\\plasma-vm\\session-shell.cmd",
    };
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (GetFileAttributesW(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            StringCchCopyW(g_sessionCmd, MAX_PATH * 2, candidates[i]);
            return;
        }
    }
    g_sessionCmd[0] = 0;
}

/* ------------------------------------------------------------------ */
/* tray                                                                */
/* ------------------------------------------------------------------ */

static void addTrayIcon(void)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_icon;
    StringCchPrintfW(g_nid.szTip, 128, L"Shell Switcher - current: %s",
                     isPlasmaValue(g_currentShell) ? L"Plasma" : (g_currentShell[0] ? L"Explorer" : L"unknown"));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void updateTrayTip(void)
{
    if (!g_hwnd) {
        return;
    }
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_TIP;
    StringCchPrintfW(nid.szTip, 128, L"Shell Switcher - current: %s",
                     isPlasmaValue(g_currentShell) ? L"Plasma" : (g_currentShell[0] ? L"Explorer" : L"unknown"));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void showMenu(void)
{
    HMENU menu = CreatePopupMenu();
    const BOOL plasma = isPlasmaValue(g_currentShell);
    const BOOL configured = g_currentShell[0] != 0;
    WCHAR current[256];
    StringCchPrintfW(current, 256, L"Current: %s",
                     plasma ? L"Plasma" : (configured ? L"Explorer" : L"unknown (not configured)"));
    AppendMenuW(menu, MF_STRING | MF_GRAYED, IDM_CURRENT, current);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    /* both entries stay enabled while the shell is unknown, so a broken
       switch can always be undone */
    AppendMenuW(menu, MF_STRING | (plasma ? MF_GRAYED : 0), IDM_SWITCH_PLASMA, L"Switch to Plasma");
    AppendMenuW(menu, MF_STRING | ((plasma || !configured) ? 0 : MF_GRAYED), IDM_SWITCH_EXPLORER, L"Switch to Explorer");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_taskbarCreatedMsg) {
        /* tray host restarted (trayhost crash, explorer restart, shell
           switch): re-register the icon */
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
            showMenu();
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SWITCH_PLASMA:
            doSwitchToPlasma();
            updateTrayTip();
            break;
        case IDM_SWITCH_EXPLORER:
            doSwitchToExplorer();
            updateTrayTip();
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;
    case WM_DESTROY:
        NOTIFYICONDATAW nid = {0};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        break;
    case WM_TIMER:
        if (wp == 1) {
            ensureTrayHost();
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)nCmdShow;
    g_dryRun = GetEnvironmentVariableW(L"SWITCH_DRYRUN", NULL, 0) > 0;

    discoverSessionCmd();
    readShellValue(g_currentShell, 1024);

    /* CLI mode: shellswitch.exe --to explorer|plasma (no tray, exits
       after switching). Emergency path that works even when the tray
       host is down. */
    if (lpCmdLine && (wcsstr(lpCmdLine, L"--to") || wcsstr(lpCmdLine, L"-to"))) {
        if (wcsstr(lpCmdLine, L"explorer")) {
            doSwitchToExplorer();
        } else if (wcsstr(lpCmdLine, L"plasma")) {
            doSwitchToPlasma();
        } else {
            logMsg(L"usage: shellswitch.exe --to explorer|plasma\n");
            return 2;
        }
        return 0;
    }

    /* keep the console window hidden unless running from a console */
    if (GetConsoleWindow()) {
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    }

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ShellSwitcherTrayWnd";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(wc.lpszClassName, L"Shell Switcher", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) {
        return 1;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    /* A simple two-tone square so the icon is distinguishable and does
       not rely on a shared system icon that trayhost might choke on. */
    {
        static const BYTE andMask[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        static const BYTE xorMask[] = {
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
        };
        g_icon = CreateIcon(hInstance, 32, 32, 1, 1, andMask, xorMask);
    }
    if (!g_icon) {
        g_icon = LoadIconW(NULL, IDI_APPLICATION);
    }

    addTrayIcon();

    /* watchdog timer: keep trayhost alive while plasma is running */
    SetTimer(g_hwnd, 1, 5000, NULL);

    logMsg(L"Shell Switcher running%s. Current: %s\n",
           g_dryRun ? L" (DRY RUN)" : L"",
           isPlasmaValue(g_currentShell) ? L"Plasma" : (g_currentShell[0] ? L"Explorer" : L"unknown"));

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
