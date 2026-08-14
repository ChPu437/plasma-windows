/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

#include <QDBusConnection>

#include <windows.h>

// Windows notification-area host: registers the Shell_TrayWnd receiver
// window and translates the Shell_NotifyIcon protocol (WM_COPYDATA /
// SHELLTRAYDATA) into an icon table. Phase 3 turns the table into
// StatusNotifierItems.
//
// Layout note (from the Phase 1 spike): the SHELLTRAYDATA payload carries
// the NOTIFYICONDATA with the app's cbSize (956 for 64-bit W). The
// hWnd/uID fields of GUID-registered icons are rewritten by shell32 to
// internal identifiers - read them as-is and use the GUID for identity.
class WindowsTrayHost : public QObject
{
    Q_OBJECT

public:
    explicit WindowsTrayHost(QObject *parent = nullptr);
    ~WindowsTrayHost() override;

    bool start();
    void stop();

    int iconCount() const;
    void dumpIcons();

private:
    struct IconEntry
    {
        HWND hwnd = nullptr;
        UINT uID = 0;
        GUID guid{};
        bool hasGuid = false;
        quintptr hIcon = 0;
        QString tip;
        UINT callbackMessage = 0;
        UINT version = 0;
        bool hidden = false;
        class Snibridge *bridge = nullptr;
    };

    void handleCopyData(WPARAM fromWnd, const COPYDATASTRUCT *cds);
    void handleTrayMessage(UINT message, const void *nid);
    void handleNotifyRect(const void *data, DWORD cbData);
    void logIcons();

    class Snibridge *registerBridge(quint64 key, const IconEntry &e);
    void unregisterBridge(quint64 key);
    void updateBridgeFromEntry(class Snibridge *bridge, const IconEntry &e);
    void reRegisterAllBridges();

    static LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_trayWnd = nullptr;
    HWND m_notifyWnd = nullptr;
    UINT m_taskbarCreatedMsg = 0;
    QTimer m_zOrderTimer;
    class QDBusServiceWatcher *m_watcherWatcher = nullptr;
    QHash<quint64, IconEntry> m_icons;    // key: (quint64(hwnd) << 32) | uID
    QHash<QByteArray, quint64> m_guidMap; // GUID bytes -> primary key
    QHash<quint64, class Snibridge *> m_bridges;
    quint64 m_nextBridgeId = 1;
};
