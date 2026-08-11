// M2 probe: KWindowSystem Windows backend acceptance test.
// Verifies platform detection, window list, active window, work area,
// KWindowInfo data and activateWindow against Win32 ground truth.

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QRect>
#include <QString>
#include <QThread>
#include <QWindow>

#include <KWindowInfo>
#include <KWindowSystem>
#include <KWindowSystemWindows>

#include <windows.h>

static QFile g_result;

static void report(const QString& line)
{
    g_result.write((line + QLatin1Char('\n')).toUtf8());
    g_result.flush();
}

static void check(bool ok, const QString& what)
{
    report(QStringLiteral("%1: %2").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), what));
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    g_result.setFileName(QCoreApplication::applicationDirPath() + QStringLiteral("/kws-probe-result.txt"));
    g_result.open(QIODevice::WriteOnly | QIODevice::Text);

    report(QStringLiteral("=== M2 probe: KWindowSystem Windows backend ==="));

    // 1. platform detection
    const bool platformOk = KWindowSystem::isPlatformWindows()
        && KWindowSystem::platform() == KWindowSystem::Platform::Windows;
    check(platformOk, QStringLiteral("platform() == Windows (got %1)")
              .arg(static_cast<int>(KWindowSystem::platform())));

    // 2. window list
    const QList<WId> windows = KWindowSystemWindows::windows();
    report(QStringLiteral("windows() count: %1").arg(windows.size()));
    bool allValid = true;
    for (WId id : windows) {
        if (!IsWindow(reinterpret_cast<HWND>(id))) {
            allValid = false;
            break;
        }
    }
    check(allValid && !windows.isEmpty(), QStringLiteral("windows() non-empty and all valid"));

    // 3. active window matches GetForegroundWindow
    const WId active = KWindowSystemWindows::activeWindow();
    const HWND fg = GetForegroundWindow();
    check(active == reinterpret_cast<WId>(fg) && IsWindow(fg),
          QStringLiteral("activeWindow() matches GetForegroundWindow (0x%1)")
              .arg(active, 0, 16));

    // 4. work area matches SPI_GETWORKAREA
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const QRect expectedWork(wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top);
    const QRect work = KWindowSystemWindows::workArea();
    check(work == expectedWork, QStringLiteral("workArea() matches (got %1,%2 %3x%4)")
              .arg(work.x()).arg(work.y()).arg(work.width()).arg(work.height()));

    // 5. show a real window and verify KWindowInfo + list membership
    QWindow window;
    window.setTitle(QStringLiteral("M2 Probe Window"));
    window.resize(640, 480);
    window.show();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const WId wid = window.winId();
    const HWND hwnd = reinterpret_cast<HWND>(wid);

    check(IsWindowVisible(hwnd), QStringLiteral("probe window visible"));
    check(KWindowSystemWindows::windows().contains(wid),
          QStringLiteral("probe window in windows()"));

    KWindowInfo info(wid, NET::WMName | NET::WMState | NET::WMGeometry | NET::WMPid | NET::WMWindowType,
                     NET::WM2TransientFor);
    check(info.valid(), QStringLiteral("KWindowInfo valid"));
    check(info.name() == QStringLiteral("M2 Probe Window"),
          QStringLiteral("KWindowInfo name() == title (got %1)").arg(info.name()));
    check(info.pid() == static_cast<int>(GetCurrentProcessId()),
          QStringLiteral("KWindowInfo pid() matches (got %1)").arg(info.pid()));

    RECT rc{};
    GetWindowRect(hwnd, &rc);
    const QRect expectedGeo(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    const QRect geo = info.geometry();
    check(geo == expectedGeo, QStringLiteral("KWindowInfo geometry() matches (got %1,%2 %3x%4)")
              .arg(geo.x()).arg(geo.y()).arg(geo.width()).arg(geo.height()));
    check(info.windowType(NET::WindowTypes(NET::NormalMask)) == NET::Normal,
          QStringLiteral("KWindowInfo windowType() == Normal"));
    check(info.isOnCurrentDesktop() && info.desktop() == 0,
          QStringLiteral("KWindowInfo single-desktop semantics"));

    // 6. signals: windowAdded/activeWindowChanged via the event hooks
    // Note: SetWinEventHook requires an interactive window station; in
    // non-interactive automation sessions the hook registration fails and
    // this section is reported as SKIP.
    bool sawWindowAdded = false;
    bool sawActiveChanged = false;
    QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::windowAdded,
                     [&sawWindowAdded](WId) { sawWindowAdded = true; });
    QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::activeWindowChanged,
                     [&sawActiveChanged](WId) { sawActiveChanged = true; });

    const bool hooksAvailable = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
                                                [](HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {}, 0, 0,
                                                WINEVENT_OUTOFCONTEXT)
        != nullptr;
    if (!hooksAvailable) {
        report(QStringLiteral("SKIP: SetWinEventHook unavailable in this session (non-interactive)"));
    } else {
        // verify the hook callback actually fires in this session
        static int hookFired = 0;
        HWINEVENTHOOK testHook = SetWinEventHook(
            EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
            [](HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) { ++hookFired; }, 0, 0,
            WINEVENT_OUTOFCONTEXT);
        QWindow second;
        second.setTitle(QStringLiteral("M2 Probe Window 2"));
        second.resize(320, 200);
        second.show();
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(50);
        }
        UnhookWinEvent(testHook);
        report(QStringLiteral("direct hook callback count: %1").arg(hookFired));

        // let the win event hooks fire and the queued invocations run
        for (int i = 0; i < 20 && (!sawWindowAdded || !sawActiveChanged); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(50);
        }
        check(sawWindowAdded, QStringLiteral("windowAdded signal received"));
        check(sawActiveChanged, QStringLiteral("activeWindowChanged signal received"));

        // 7. activateWindow: minimize the second window, then activate it
        ShowWindow(reinterpret_cast<HWND>(second.winId()), SW_MINIMIZE);
        QCoreApplication::processEvents();
        KWindowSystem::activateWindow(&second);
        QCoreApplication::processEvents();
        QThread::msleep(200);
        QCoreApplication::processEvents();
        const HWND fgAfter = GetForegroundWindow();
        check(fgAfter == reinterpret_cast<HWND>(second.winId()),
              QStringLiteral("activateWindow() made the window foreground"));
    }

    report(QStringLiteral("=== M2 probe done ==="));
    return 0;
}
