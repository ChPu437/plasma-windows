/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "windowstrayhost.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QTextStream>

#include <shellapi.h>

#include "snibridge.h"

QFile s_logFile;
QTextStream s_log;

void logLine(const QString &line)
{
    if (s_logFile.isOpen()) {
        s_log << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << QLatin1Char(' ')
              << line << Qt::endl;
        s_log.flush();
    }
}

namespace
{
WindowsTrayHost *s_self = nullptr;

// The SHELLTRAYDATA payload carries the NOTIFYICONDATA in a 32-bit-style
// layout (hWnd/hIcon/hBalloonIcon are 4-byte DWORDs) even on x64; the
// app's own cbSize value is preserved (956 for a 64-bit W app). Reading
// with the native HWND layout shifts every following field (the tip lost
// its first 8 chars in the Phase 2 spike). hWnd is also rewritten by
// shell32 into an internal id for the session - treat it as identity,
// not as the app's callback window.
struct NidLayout
{
    DWORD cbSize;
    DWORD hWnd;
    DWORD uID;
    DWORD uFlags;
    DWORD uCallbackMessage;
    DWORD hIcon;
    wchar_t szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    wchar_t szInfo[256];
    DWORD uVersion;
    wchar_t szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    DWORD hBalloonIcon;
};

QString guidToString(const GUID &g)
{
    return QStringLiteral("%1-%2-%3-%4%5-%6%7%8%9%10%11")
        .arg(quint32(g.Data1), 8, 16, QLatin1Char('0'))
        .arg(quint16(g.Data2), 4, 16, QLatin1Char('0'))
        .arg(quint16(g.Data3), 4, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[0]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[1]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[2]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[3]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[4]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[5]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[6]), 2, 16, QLatin1Char('0'))
        .arg(quint8(g.Data4[7]), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString nidFlagsToString(DWORD flags)
{
    QStringList f;
    if (flags & NIF_MESSAGE) f << QStringLiteral("MESSAGE");
    if (flags & NIF_ICON) f << QStringLiteral("ICON");
    if (flags & NIF_TIP) f << QStringLiteral("TIP");
    if (flags & NIF_STATE) f << QStringLiteral("STATE");
    if (flags & NIF_INFO) f << QStringLiteral("INFO");
    if (flags & NIF_GUID) f << QStringLiteral("GUID");
    if (flags & NIF_SHOWTIP) f << QStringLiteral("SHOWTIP");
    return f.join(QLatin1Char('|'));
}
} // namespace

WindowsTrayHost::WindowsTrayHost(QObject *parent)
    : QObject(parent)
{
}

WindowsTrayHost::~WindowsTrayHost()
{
    stop();
}

bool WindowsTrayHost::start()
{
    const QString logPath = QStringLiteral("trayhost.log");
    s_logFile.setFileName(logPath);
    if (s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        s_log.setDevice(&s_logFile);
    }
    logLine(QStringLiteral("=== WindowsTrayHost start ==="));
    s_self = this;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = trayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Shell_TrayWnd";
    if (!RegisterClassExW(&wc)) {
        const DWORD err = GetLastError();
        logLine(QStringLiteral("RegisterClass Shell_TrayWnd failed: %1").arg(err));
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    m_trayWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Shell_TrayWnd", nullptr,
                                WS_POPUP, 0, 0, screenW, 23 * 2, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!m_trayWnd) {
        logLine(QStringLiteral("CreateWindowEx Shell_TrayWnd failed: %1").arg(GetLastError()));
        return false;
    }
    logLine(QStringLiteral("Shell_TrayWnd: %1").arg(reinterpret_cast<quintptr>(m_trayWnd), 0, 16));

    wc.lpszClassName = L"TrayNotifyWnd";
    RegisterClassExW(&wc);
    m_notifyWnd = CreateWindowExW(0, L"TrayNotifyWnd", nullptr, WS_CHILD, 0, 0, 32, 32,
                                  m_trayWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    logLine(QStringLiteral("TrayNotifyWnd: %1").arg(reinterpret_cast<quintptr>(m_notifyWnd), 0, 16));

    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    SendNotifyMessageW(HWND_BROADCAST, m_taskbarCreatedMsg, 0, 0);
    logLine(QStringLiteral("TaskbarCreated broadcast (0x%1)").arg(m_taskbarCreatedMsg, 0, 16));

    connect(&m_zOrderTimer, &QTimer::timeout, this, [this]() {
        HWND top = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (top != m_trayWnd && m_trayWnd) {
            logLine(QStringLiteral("z-order: FindWindow -> other (0x%1), raising ours")
                        .arg(reinterpret_cast<quintptr>(top), 0, 16));
            SetWindowPos(m_trayWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    });
    m_zOrderTimer.start(100);

    logLine(QStringLiteral("Ready."));
    return true;
}

void WindowsTrayHost::stop()
{
    if (m_trayWnd) {
        DestroyWindow(m_trayWnd);
        m_trayWnd = nullptr;
    }
    m_zOrderTimer.stop();
    m_icons.clear();
    m_guidMap.clear();
    if (s_logFile.isOpen()) {
        logLine(QStringLiteral("=== WindowsTrayHost stop ==="));
        s_logFile.close();
    }
    s_self = nullptr;
}

int WindowsTrayHost::iconCount() const
{
    return m_icons.size();
}

void WindowsTrayHost::dumpIcons()
{
    logIcons();
}

void WindowsTrayHost::handleCopyData(WPARAM fromWnd, const COPYDATASTRUCT *cds)
{
    logLine(QStringLiteral("WM_COPYDATA dwData=%1 cbData=%2 from=0x%3")
                .arg(static_cast<int>(cds->dwData))
                .arg(cds->cbData)
                .arg(static_cast<quintptr>(fromWnd), 0, 16));

    switch (cds->dwData) {
    case 1: // SHELLTRAYDATA
        if (cds->cbData >= sizeof(int) + sizeof(UINT) + sizeof(NOTIFYICONDATAW)) {
            const char *data = static_cast<const char *>(cds->lpData);
            const UINT message = *reinterpret_cast<const UINT *>(data + 4);
            const NidLayout *nid = reinterpret_cast<const NidLayout *>(data + 8);
            handleTrayMessage(message, nid);
        }
        break;
    case 3: // WINNOTIFYICONIDENTIFIER (Shell_NotifyIconGetRect)
        handleNotifyRect(cds->lpData, cds->cbData);
        break;
    default:
        break;
    }
}

void WindowsTrayHost::handleTrayMessage(UINT message, const void *nidPtr)
{
    const NidLayout *nid = static_cast<const NidLayout *>(nidPtr);
    const QString action = message == NIM_ADD ? QStringLiteral("NIM_ADD")
        : message == NIM_MODIFY ? QStringLiteral("NIM_MODIFY")
                                 : message == NIM_DELETE ? QStringLiteral("NIM_DELETE")
                                                          : message == NIM_SETFOCUS ? QStringLiteral("NIM_SETFOCUS")
                                                                                    : message == NIM_SETVERSION ? QStringLiteral("NIM_SETVERSION")
                                                                                                                : QStringLiteral("NIM_%1").arg(message);

    const quint64 key = (quint64(nid->hWnd) << 32) | nid->uID;
    const bool hasGuid = (nid->uFlags & NIF_GUID) && nid->guidItem.Data1 != 0;
    const QByteArray guidKey(reinterpret_cast<const char *>(&nid->guidItem), sizeof(GUID));

    // Windows system tray icons (Volume, Network, Security/Defender, ...)
    // live in explorer and carry the fixed 7820AE7x-23E3-4229-82C1-
    // E41CB67D5B9C GUID family. They cannot be driven through SNI (the
    // system owns their clicks); skip them here - our own system-tray
    // widgets will surface them later.
    if (hasGuid && nid->guidItem.Data2 == 0x23E3 && nid->guidItem.Data3 == 0x4229
        && nid->guidItem.Data4[0] == 0x82 && nid->guidItem.Data4[1] == 0xC1
        && nid->guidItem.Data4[2] == 0xE4 && nid->guidItem.Data4[3] == 0x1C
        && nid->guidItem.Data4[4] == 0xB6 && nid->guidItem.Data4[5] == 0x7D
        && nid->guidItem.Data4[6] == 0x5B && nid->guidItem.Data4[7] == 0x9C
        && nid->guidItem.Data1 >= 0x7820AE73 && nid->guidItem.Data1 <= 0x7820AE7F) {
        logLine(QStringLiteral("  SKIP system icon guid=%1 key=0x%2")
                    .arg(guidToString(nid->guidItem))
                    .arg(key, 0, 16));
        return;
    }

    if (message == NIM_SETVERSION) {
        auto it = m_icons.find(key);
        if (it != m_icons.end()) {
            it->version = nid->uVersion;
            // Guard against stale e.bridge pointers left behind by DELETE
            // messages whose (drifted) key never matched the entry.
            if (it->bridge && m_bridges.value(key) == it->bridge) {
                it->bridge->setCallback(it->hwnd, it->uID, it->callbackMessage, it->version);
            }
            logLine(QStringLiteral("  SETVERSION key=0x%1 version=%2").arg(key, 0, 16).arg(nid->uVersion));
        } else {
            logLine(QStringLiteral("  SETVERSION for unknown key=0x%1 version=%2").arg(key, 0, 16).arg(nid->uVersion));
        }
        return;
    }
    if (message == NIM_DELETE) {
        // Guid-registered icons live under the mapped primary key, not the
        // raw (hwnd, uID) pair - use the same resolution as ADD/MODIFY.
        quint64 delKey = key;
        if (hasGuid) {
            const auto git = m_guidMap.constFind(guidKey);
            if (git != m_guidMap.constEnd()) {
                delKey = git.value();
            }
        }
        bool erased = m_icons.contains(delKey);
        if (!erased) {
            // shell32 rewrites hWnd in the SHELLTRAYDATA payload and the
            // value can drift between ADD and DELETE, so the raw key misses.
            // Fall back to (owner process, uID) when the delete window is a
            // real window; guid is already handled above.
            DWORD delPid = 0;
            const HWND delHwnd = reinterpret_cast<HWND>(static_cast<quintptr>(nid->hWnd));
            if (GetWindowThreadProcessId(delHwnd, &delPid) && delPid != 0) {
                for (auto it = m_icons.constBegin(); it != m_icons.constEnd(); ++it) {
                    DWORD pid = 0;
                    if (it.value().hwnd && !it.value().hasGuid && GetWindowThreadProcessId(it.value().hwnd, &pid)
                        && pid == delPid && it.value().uID == nid->uID) {
                        delKey = it.key();
                        erased = true;
                        break;
                    }
                }
            }
        }
        if (erased) {
            m_icons.remove(delKey);
        }
        if (hasGuid) {
            m_guidMap.remove(guidKey);
        }
        logLine(QStringLiteral("  DELETE key=0x%1 hwnd=0x%2 uID=%3 guid=%4 -> %5")
                    .arg(delKey, 0, 16)
                    .arg(nid->hWnd, 0, 16)
                    .arg(nid->uID)
                    .arg(hasGuid ? guidToString(nid->guidItem) : QStringLiteral("-"))
                    .arg(erased ? QStringLiteral("removed") : QStringLiteral("unknown")));
        unregisterBridge(delKey);
        return;
    }

    // ADD / MODIFY
    quint64 primaryKey = key;
    if (hasGuid) {
        const auto git = m_guidMap.constFind(guidKey);
        if (git != m_guidMap.constEnd()) {
            primaryKey = git.value();
        }
    }

    auto it = m_icons.find(primaryKey);
    if (it == m_icons.end()) {
        it = m_icons.insert(primaryKey, IconEntry{});
    }
    IconEntry &e = it.value();

    if (hasGuid) {
        e.guid = nid->guidItem;
        e.hasGuid = true;
        m_guidMap.insert(guidKey, primaryKey);
    }
    if (message == NIM_ADD || (nid->uFlags & NIF_MESSAGE)) {
        e.hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(nid->hWnd));
        e.uID = nid->uID;
        e.callbackMessage = nid->uCallbackMessage;
    }
    if (nid->uFlags & NIF_ICON && nid->hIcon) {
        e.hIcon = nid->hIcon;
    }
    if (nid->uFlags & NIF_TIP) {
        e.tip = QString::fromWCharArray(nid->szTip);
    }
    if (nid->uFlags & NIF_STATE) {
        e.hidden = (nid->dwState & NIS_HIDDEN) != 0;
    }
    if (nid->uFlags & NIF_INFO) {
        logLine(QStringLiteral("  BALLOON title=[%1] info=[%2] flags=0x%3")
                    .arg(QString::fromWCharArray(nid->szInfoTitle))
                    .arg(QString::fromWCharArray(nid->szInfo))
                    .arg(nid->dwInfoFlags, 0, 16));
    }

    // keep the SNI bridge in sync
    Snibridge *bridge = e.bridge;
    if (!bridge || m_bridges.value(primaryKey) != bridge) {
        e.bridge = registerBridge(primaryKey, e);
        bridge = e.bridge;
    } else {
        if (nid->uFlags & NIF_ICON && nid->hIcon) {
            bridge->setIcon(reinterpret_cast<HICON>(nid->hIcon));
        }
        if (nid->uFlags & NIF_TIP) {
            bridge->setTitle(e.tip);
        }
        if (nid->uFlags & NIF_MESSAGE) {
            bridge->setCallback(e.hwnd, e.uID, e.callbackMessage, e.version);
        }
    }

    logLine(QStringLiteral("  %1 key=0x%2 hwnd=0x%3 uID=%4 cbSize=%5 flags=%6 tip=[%7] msg=0x%8")
                .arg(action)
                .arg(primaryKey, 0, 16)
                .arg(reinterpret_cast<quintptr>(e.hwnd), 0, 16)
                .arg(e.uID)
                .arg(nid->cbSize)
                .arg(nidFlagsToString(nid->uFlags))
                .arg(e.tip)
                .arg(e.callbackMessage, 0, 16));
}

void WindowsTrayHost::handleNotifyRect(const void *data, DWORD cbData)
{
    if (!data || cbData < 32) {
        return;
    }
    const auto *wi = static_cast<const char *>(data);
    const int magic = *reinterpret_cast<const int *>(wi);
    const int msg = *reinterpret_cast<const int *>(wi + 4);
    const quintptr hWnd = *reinterpret_cast<const quintptr *>(wi + 16);
    const UINT uID = *reinterpret_cast<const UINT *>(wi + 24);
    logLine(QStringLiteral("  GETRECT magic=%1 msg=%2 hwnd=0x%3 uID=%4").arg(magic).arg(msg).arg(hWnd, 0, 16).arg(uID));
    // dwMessage 1 = top-left, 2 = bottom-right. We answer with the icon's
    // placement; for now return (0,0) - only used for tooltip positioning.
}

void WindowsTrayHost::unregisterBridge(quint64 key)
{
    Snibridge *bridge = m_bridges.take(key);
    if (!bridge) {
        return;
    }
    // Proper teardown order: unregister the object from its bus connection
    // FIRST (synchronously removes the adaptors from Qt's dispatch table,
    // so no in-flight DBus call can reach the bridge), then unregister the
    // service name (drops it from the StatusNotifierWatcher), then destroy
    // the bridge, then close the connection on a deferred timer. Destroying
    // the bridge while the connection still dispatches to it crashed with a
    // null deref in Qt6Core (0xf2ec2/0xf3097) under DELETE/ADD churn.
    const QString serviceName = bridge->serviceName();
    const QString bridgeId = bridge->id();
    QDBusConnection conn = bridge->connection();
    if (conn.isConnected()) {
        conn.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        conn.unregisterService(serviceName);
    }
    delete bridge;
    QTimer::singleShot(100, this, [serviceName]() {
        QDBusConnection::disconnectFromBus(serviceName);
    });
    logLine(QStringLiteral("  SNI unregistered: %1").arg(bridgeId));
}

Snibridge *WindowsTrayHost::registerBridge(quint64 key, const IconEntry &e)
{
    auto *bridge = new Snibridge(this);
    const QString service = QStringLiteral("org.kde.StatusNotifierItem-%1-%2")
                                .arg(QCoreApplication::applicationPid())
                                .arg(m_nextBridgeId++);
    bridge->setCallback(e.hwnd, e.uID, e.callbackMessage, e.version);
    bridge->setServiceName(service);
    bridge->setTitle(e.tip);
    bridge->setIcon(reinterpret_cast<HICON>(e.hIcon));
    // Second adaptor: org.freedesktop.DBus.Properties (GetAll), which the
    // dynamic registerObject path does not synthesize from Q_PROPERTYs.
    new SnibridgeProperties(bridge);
    QDBusConnection conn = QDBusConnection::connectToBus(QDBusConnection::SessionBus, service);
    bridge->setConnection(conn);
    if (!conn.isConnected() || !conn.registerService(service)) {
        logLine(QStringLiteral("  SNI registerService failed for %1").arg(service));
        delete bridge;
        return nullptr;
    }
    if (!conn.registerObject(QStringLiteral("/StatusNotifierItem"), bridge,
                               QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllSlots
                                   | QDBusConnection::ExportAllInvokables | QDBusConnection::ExportAllSignals)) {
        logLine(QStringLiteral("  SNI registerObject failed for %1").arg(service));
        conn.unregisterService(service);
        delete bridge;
        return nullptr;
    }

    // Tell the plasma StatusNotifierWatcher about the item (it does not
    // auto-discover services; the SNI spec has the app register itself).
    QDBusMessage reg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                      QStringLiteral("/StatusNotifierWatcher"),
                                                      QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                      QStringLiteral("RegisterStatusNotifierItem"));
    reg << service;
    conn.send(reg);

    m_bridges.insert(key, bridge);
    logLine(QStringLiteral("  SNI registered: %1 (key=0x%2)").arg(service).arg(key, 0, 16));
    return bridge;
}

void WindowsTrayHost::logIcons()
{
    // Opportunistic cleanup: apps that died without NIM_DELETE (or whose
    // DELETE key drifted away) leave stale bridges + SNI services behind.
    // Drop entries whose callback window no longer exists.
    for (auto it = m_icons.begin(); it != m_icons.end();) {
        if (it.value().hwnd && !IsWindow(it.value().hwnd)) {
            logLine(QStringLiteral("  cleanup dead icon key=0x%1 hwnd=0x%2").arg(it.key(), 0, 16).arg(reinterpret_cast<quintptr>(it.value().hwnd), 0, 16));
            unregisterBridge(it.key());
            it = m_icons.erase(it);
        } else {
            ++it;
        }
    }

    logLine(QStringLiteral("--- icon table (%1 entries) ---").arg(m_icons.size()));
    for (auto it = m_icons.constBegin(); it != m_icons.constEnd(); ++it) {
        const IconEntry &e = it.value();
        logLine(QStringLiteral("  key=0x%1 hwnd=0x%2 uID=%3 tip=[%4] callback=0x%5 v=%6 guid=%7")
                    .arg(it.key(), 0, 16)
                    .arg(reinterpret_cast<quintptr>(e.hwnd), 0, 16)
                    .arg(e.uID)
                    .arg(e.tip)
                    .arg(e.callbackMessage, 0, 16)
                    .arg(e.version)
                    .arg(e.hasGuid ? guidToString(e.guid) : QStringLiteral("-")));
    }
}

LRESULT CALLBACK WindowsTrayHost::trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COPYDATA) {
        if (s_self) {
            s_self->handleCopyData(wp, reinterpret_cast<const COPYDATASTRUCT *>(lp));
        }
        return TRUE;
    }
    if (msg == WM_WINDOWPOSCHANGED) {
        WINDOWPOS *p = reinterpret_cast<WINDOWPOS *>(lp);
        if ((p->flags & SWP_SHOWWINDOW) || IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
