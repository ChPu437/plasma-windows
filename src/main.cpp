// Plasma Windows - Phase 0
//
// Minimal native Win32 shell executable (shell.exe).
//
// Phase 0 scope (see AGENTS.md):
//   * start on Windows 10 LTSC 2021
//   * create a top-level window covering the desktop work area
//   * process the normal Windows message loop
//   * accept keyboard and mouse input
//   * exit cleanly
//   * produce useful diagnostics
//
// No Qt, no KDE, no Plasma, no external dependencies.

#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <winternl.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kClassName[] = L"PlasmaWindowsPhase0Shell";
constexpr wchar_t kWindowTitle[] = L"Plasma Windows (Phase 0)";

// Exit codes (documented in README.md).
enum ExitCode : int {
    kExitOk = 0,             // clean shutdown requested by the user
    kExitGeneric = 1,        // unexpected startup failure
    kExitRegisterClass = 2,  // RegisterClassExW failed
    kExitCreateWindow = 3,   // CreateWindowExW failed
    kExitMessageLoop = 4,    // GetMessage returned -1
};

// ---------------------------------------------------------------------------
// Diagnostics (AGENTS.md section 7): startup logging, graceful error
// reporting, exit code reporting, optional debug logging via --debug.

bool g_debug = false;
FILE* g_logFile = nullptr;

void LogOpen(const wchar_t* path)
{
    _wfopen_s(&g_logFile, path, L"ab");
}

void LogClose()
{
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

enum class Level { kInfo, kWarn, kError, kDebug };

const char* LevelTag(Level level)
{
    switch (level) {
    case Level::kWarn:  return "WARN";
    case Level::kError: return "ERROR";
    case Level::kDebug: return "DEBUG";
    default:            return "INFO";
    }
}

void LogWrite(Level level, const char* fmt, ...)
{
    if (level == Level::kDebug && !g_debug) {
        return;
    }

    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(text, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1400];
    snprintf(line, sizeof(line),
             "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds, LevelTag(level), text);

    if (g_logFile) {
        fputs(line, g_logFile);
        fflush(g_logFile);
    }
    OutputDebugStringA(line);
    fputs(line, stdout);
    fflush(stdout);
}

#define LOG_INFO(...)  LogWrite(Level::kInfo, __VA_ARGS__)
#define LOG_WARN(...)  LogWrite(Level::kWarn, __VA_ARGS__)
#define LOG_ERROR(...) LogWrite(Level::kError, __VA_ARGS__)
#define LOG_DEBUG(...) LogWrite(Level::kDebug, __VA_ARGS__)

// Log the failure, show a message box (useful when testing in the VM) and
// exit with a distinct exit code.
[[noreturn]] void FatalFailure(const char* what, DWORD error, int exitCode)
{
    wchar_t detail[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, detail,
                   static_cast<DWORD>(sizeof(detail) / sizeof(detail[0])),
                   nullptr);

    LOG_ERROR("FATAL: %s failed (GetLastError=%lu): %ls", what, error, detail);

    wchar_t msg[1024];
    swprintf(msg, sizeof(msg) / sizeof(msg[0]),
             L"shell.exe failed to start.\r\n\r\n"
             L"%hs failed with GetLastError = %lu\r\n"
             L"%ls\r\n\r\n"
             L"See shell.log for details.",
             what, error, detail);

    MessageBoxW(nullptr, msg, L"Plasma Windows (Phase 0) - startup failure",
                MB_OK | MB_ICONERROR);
    LogClose();
    ExitProcess(static_cast<UINT>(exitCode));
}

// Report the real OS version. GetVersionEx is deprecated and lies without
// a matching manifest; RtlGetVersion does not.
void GetOSVersion(char (&buf)[64])
{
    RTL_OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);

    typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));

    if (fn && fn(&os) == 0) {
        snprintf(buf, sizeof(buf), "%lu.%lu.%lu", os.dwMajorVersion,
                 os.dwMinorVersion, os.dwBuildNumber);
    } else {
        snprintf(buf, sizeof(buf), "unknown");
    }
}

std::wstring GetExePath()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n ? n : 0);
}

// ---------------------------------------------------------------------------
// Window

RECT GetPrimaryWorkArea()
{
    RECT rc{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0)) {
        return rc;
    }
    GetWindowRect(GetDesktopWindow(), &rc);
    return rc;
}

void ResizeToWorkArea(HWND hwnd)
{
    const RECT rc = GetPrimaryWorkArea();
    LOG_DEBUG("resize to work area (%ld, %ld, %ld, %ld)", rc.left, rc.top,
              rc.right, rc.bottom);
    MoveWindow(hwnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
               TRUE);
}

void DrawCenteredText(HDC dc, const RECT& rc, const wchar_t* text,
                      int fontSize, COLORREF color, const wchar_t* face)
{
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, face);
    const HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT r = rc;
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                                   DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void PaintShell(HWND hwnd)
{
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);

    // Breeze Dark-inspired background.
    const HBRUSH bg = CreateSolidBrush(RGB(0x23, 0x26, 0x29));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    RECT titleRect = rc;
    titleRect.top = rc.top + rc.bottom / 4;
    titleRect.bottom = rc.bottom / 4 + 160;
    DrawCenteredText(dc, titleRect, L"Plasma Windows", 96, RGB(0xF0, 0xF0, 0xF0),
                     L"Segoe UI");

    RECT subtitleRect = rc;
    subtitleRect.top = titleRect.bottom;
    subtitleRect.bottom = titleRect.bottom + 80;
    DrawCenteredText(dc, subtitleRect, L"Phase 0 - native Win32 shell", 40,
                     RGB(0x9A, 0xA0, 0xA6), L"Segoe UI");

    RECT hintRect = rc;
    hintRect.top = rc.bottom - 96;
    hintRect.bottom = rc.bottom - 32;
    DrawCenteredText(dc, hintRect, L"Press ESC or Alt+F4 to exit", 28,
                     RGB(0x6E, 0x74, 0x7A), L"Segoe UI");

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ShellWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        LOG_INFO("window created (hwnd=%p)", hwnd);
        return 0;

    case WM_PAINT:
        PaintShell(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1; // background is painted in WM_PAINT

    case WM_KEYDOWN:
        LOG_DEBUG("WM_KEYDOWN vk=0x%X", static_cast<unsigned>(wp));
        if (wp == VK_ESCAPE) {
            LOG_INFO("ESC pressed, shutting down");
            PostQuitMessage(kExitOk);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        LOG_DEBUG("mouse button %s at (%d, %d)",
                  msg == WM_LBUTTONDOWN ? "left" : "right",
                  GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_DISPLAYCHANGE:
        LOG_INFO("display changed: %lux%lu, bpp=%lu", LOWORD(lp), HIWORD(lp),
                 static_cast<DWORD>(wp));
        ResizeToWorkArea(hwnd);
        return 0;

    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) {
            LOG_INFO("work area changed, resizing");
            ResizeToWorkArea(hwnd);
        }
        return 0;

    case WM_QUERYENDSESSION:
        LOG_INFO("WM_QUERYENDSESSION, allowing end session");
        return TRUE;

    case WM_ENDSESSION:
        if (wp) {
            LOG_INFO("WM_ENDSESSION, shutting down");
            PostQuitMessage(kExitOk);
        }
        return 0;

    case WM_CLOSE:
        LOG_INFO("WM_CLOSE received, shutting down");
        PostQuitMessage(kExitOk);
        return 0;

    case WM_DESTROY:
        LOG_INFO("window destroyed");
        PostQuitMessage(kExitOk);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool RegisterShellClass(HINSTANCE inst)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ShellWndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    // When launched from a console (cmd.exe), attach to it so log output is
    // visible. Harmless when there is no parent console.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        setvbuf(stdout, nullptr, _IONBF, 0);
    }

    const std::wstring exePath = GetExePath();
    std::wstring exeDir = exePath;
    const size_t slash = exeDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        exeDir.resize(slash);
    }
    LogOpen((exeDir + L"\\shell.log").c_str());

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--debug") == 0) {
            g_debug = true;
        } else if (wcscmp(argv[i], L"--help") == 0) {
            fputs("Plasma Windows Phase 0 shell\r\n\r\n"
                  "Usage: shell.exe [--debug]\r\n"
                  "\r\n"
                  "  --debug  enable verbose debug logging\r\n"
                  "\r\n"
                  "Press ESC or Alt+F4 to exit.\r\n",
                  stdout);
            LogClose();
            return kExitOk;
        }
    }
    if (argv) {
        LocalFree(argv);
    }

    // Per-monitor DPI awareness so the work area and text scale correctly.
    const HRESULT dpiResult = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    if (dpiResult != S_OK) {
        LOG_WARN("SetProcessDpiAwareness failed, HRESULT 0x%08lX",
                 static_cast<unsigned long>(dpiResult));
    }

    char osVer[64];
    GetOSVersion(osVer);
    const RECT wa = GetPrimaryWorkArea();

    LOG_INFO("Plasma Windows Phase 0 shell starting");
    LOG_INFO("executable: %ls", exePath.c_str());
    LOG_INFO("OS version: %s", osVer);
    LOG_INFO("primary work area: (%ld, %ld) - (%ld, %ld), %ld x %ld",
             wa.left, wa.top, wa.right, wa.bottom,
             wa.right - wa.left, wa.bottom - wa.top);
    LOG_INFO("command line: %ls", GetCommandLineW());
    LOG_INFO("debug logging: %s", g_debug ? "on" : "off");

    if (!RegisterShellClass(inst)) {
        FatalFailure("RegisterClassExW", GetLastError(), kExitRegisterClass);
    }

    const RECT rc = GetPrimaryWorkArea();
    const HWND hwnd = CreateWindowExW(0, kClassName, kWindowTitle, WS_POPUP,
                                      rc.left, rc.top, rc.right - rc.left,
                                      rc.bottom - rc.top, nullptr, nullptr,
                                      inst, nullptr);
    if (!hwnd) {
        FatalFailure("CreateWindowExW", GetLastError(), kExitCreateWindow);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    LOG_INFO("window shown (%ld x %ld)", rc.right - rc.left, rc.bottom - rc.top);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (msg.message == WM_QUIT) {
        const int code = static_cast<int>(msg.wParam);
        LOG_INFO("message loop ended, exiting with code %d", code);
        LogClose();
        return code;
    }

    // GetMessage returned -1: fatal message loop error.
    FatalFailure("GetMessage", GetLastError(), kExitMessageLoop);
}
