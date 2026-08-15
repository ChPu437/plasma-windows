/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QObject>

class SessionManagement;

/* Exposes the KDE session actions (shutdown/reboot/suspend/hibernate/
   lock) to the windowsmenu QML. Backed by KWorkspace's SessionManagement
   which, on Windows, is handled by the WindowsSessionBackend
   (plasma-workspace 0007: Win32 shutdown/reboot/suspend calls). */
class PowerActions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canShutdown READ canShutdown NOTIFY changed)
    Q_PROPERTY(bool canReboot READ canReboot NOTIFY changed)
    Q_PROPERTY(bool canSuspend READ canSuspend NOTIFY changed)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY changed)
    Q_PROPERTY(bool canLock READ canLock NOTIFY changed)

public:
    explicit PowerActions(QObject *parent = nullptr);

    bool canShutdown() const;
    bool canReboot() const;
    bool canSuspend() const;
    bool canHibernate() const;
    bool canLock() const;

    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void hibernate();
    Q_INVOKABLE void lock();

Q_SIGNALS:
    void changed();

private:
    SessionManagement *m_session;
};
