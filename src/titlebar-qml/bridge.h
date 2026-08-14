/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

    TitleBarBridge - Win32 window management for the QML title bar.
    The QML bar is an OWNED window of the target (SetWindowLongPtr
    GWLP_HWNDPARENT after creation), so DWM keeps it directly above the
    owner and lowers it together with it - no z-order races.

    Interaction mirrors src/titlebar/titlebar.c (which was proven
    against the same bugs): manual maximize/restore (never SC_MAXIMIZE),
    drag loop with DeferWindowPos atomic moves, restore-anchored-to-
    cursor when dragging a maximized window.
*/

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <windows.h>

class TitleBarBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged)

public:
    explicit TitleBarBridge(QObject *parent = nullptr);

    QString title() const { return m_title; }
    bool maximized() const { return m_maximized; }

    Q_INVOKABLE void setTarget(quintptr hwnd);
    Q_INVOKABLE void attachBar(quintptr barHwnd);
    Q_INVOKABLE void beginDrag();
    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void close();

Q_SIGNALS:
    void titleChanged();
    void stateChanged();

private:
    void updateTarget();

    HWND m_target = nullptr;
    HWND m_bar = nullptr;
    QString m_title;
    bool m_maximized = false;
    RECT m_restore{};
    QTimer m_pollTimer;
};
