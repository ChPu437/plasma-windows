/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QObject>
#include <QTimer>

class ImeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString layoutName READ layoutName NOTIFY layoutNameChanged)
    Q_PROPERTY(QString layoutCode READ layoutCode NOTIFY layoutNameChanged)

public:
    explicit ImeController(QObject *parent = nullptr);

    QString layoutName() const;
    QString layoutCode() const;

    Q_INVOKABLE void toggle();

Q_SIGNALS:
    void layoutNameChanged();

private:
    void refresh();

    QTimer m_timer;
    QString m_name;
    QString m_code;
};
