/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "poweractions.h"

#include <sessionmanagement.h>

PowerActions::PowerActions(QObject *parent)
    : QObject(parent)
    , m_session(new SessionManagement(this))
{
    auto relay = [this]() {
        Q_EMIT changed();
    };
    connect(m_session, &SessionManagement::canShutdownChanged, this, relay);
    connect(m_session, &SessionManagement::canRebootChanged, this, relay);
    connect(m_session, &SessionManagement::canSuspendChanged, this, relay);
    connect(m_session, &SessionManagement::canHibernateChanged, this, relay);
}

bool PowerActions::canShutdown() const
{
    return m_session->canShutdown();
}

bool PowerActions::canReboot() const
{
    return m_session->canReboot();
}

bool PowerActions::canSuspend() const
{
    return m_session->canSuspend();
}

bool PowerActions::canHibernate() const
{
    return m_session->canHibernate();
}

bool PowerActions::canLock() const
{
    return m_session->canLock();
}

void PowerActions::shutdown()
{
    m_session->requestShutdown();
}

void PowerActions::reboot()
{
    m_session->requestReboot();
}

void PowerActions::suspend()
{
    m_session->suspend();
}

void PowerActions::hibernate()
{
    m_session->hibernate();
}

void PowerActions::lock()
{
    m_session->lock();
}
