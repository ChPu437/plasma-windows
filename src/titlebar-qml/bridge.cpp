/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

    TitleBarBridge - Win32 window management for the QML title bar
    (logic mirrors src/titlebar/titlebar.c, proven against: system
    SC_MAXIMIZE shrink bug, drag recursion, event-lag stutter).
*/

#include "bridge.h"

#include <QCoreApplication>

namespace
{
constexpr int kBarH = 32;
}

TitleBarBridge::TitleBarBridge(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(200);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() { updateTarget(); });
}

void TitleBarBridge::setTarget(quintptr hwnd)
{
    m_target = reinterpret_cast<HWND>(hwnd);
    if (!m_target || !IsWindow(m_target)) {
        qWarning() << "invalid target" << hwnd;
        return;
    }
    updateTarget();
    m_pollTimer.start();
}

void TitleBarBridge::attachBar(quintptr barHwnd)
{
    m_bar = reinterpret_cast<HWND>(barHwnd);
    if (!m_bar || !m_target) {
        return;
    }
    /* owned window: DWM keeps the bar directly above the owner and
       lowers it together with it - no z-order races, click activates
       the owner (standard title bar behavior). */
    SetWindowLongPtrW(m_bar, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_target));
    updateTarget();
}

void TitleBarBridge::updateTarget()
{
    if (!m_target || !IsWindow(m_target)) {
        /* target gone: take the bar (and the app) down */
        if (m_pollTimer.isActive()) {
            m_pollTimer.stop();
        }
        qApp->quit();
        return;
    }
    WCHAR buf[512];
    if (GetWindowTextW(m_target, buf, 512) > 0) {
        const QString t = QString::fromWCharArray(buf);
        if (t != m_title) {
            m_title = t;
            Q_EMIT titleChanged();
        }
    }
    if (m_bar && IsWindowVisible(m_target)) {
        RECT r;
        GetWindowRect(m_target, &r);
        int barTop = r.top - kBarH;
        if (barTop < 0) {
            barTop = 0;
        }
        SetWindowPos(m_bar, NULL, r.left, barTop, r.right - r.left, kBarH,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    } else if (m_bar) {
        ShowWindow(m_bar, SW_HIDE);
    }
}

void TitleBarBridge::beginDrag()
{
    if (!m_target || !m_bar || !IsWindow(m_target)) {
        return;
    }
    if (m_maximized) {
        /* dragging a maximized window first restores it, anchored to
           the cursor (AltSnap behavior) */
        POINT cur;
        GetCursorPos(&cur);
        RECT barR;
        GetWindowRect(m_bar, &barR);
        const int dy = cur.y - barR.top;
        const int w = m_restore.right - m_restore.left;
        const int h = m_restore.bottom - m_restore.top;
        const int top = cur.y + (kBarH - dy);
        SetWindowPos(m_target, NULL, m_restore.left, top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(m_bar, NULL, m_restore.left, top - kBarH, w, kBarH, SWP_NOZORDER | SWP_NOACTIVATE);
        m_maximized = false;
        Q_EMIT stateChanged();
    }

    POINT cursor;
    GetCursorPos(&cursor);
    RECT tr;
    GetWindowRect(m_target, &tr);
    const int offX = cursor.x - tr.left;
    const int offY = cursor.y - tr.top;

    SetCapture(m_bar);
    for (;;) {
        if (GetAsyncKeyState(VK_LBUTTON) >= 0) {
            break;
        }
        GetCursorPos(&cursor);
        if (IsWindow(m_target)) {
            HDWP hdwp = BeginDeferWindowPos(2);
            if (hdwp) {
                hdwp = DeferWindowPos(hdwp, m_target, NULL, cursor.x - offX, cursor.y - offY, 0, 0,
                                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (hdwp) {
                    DeferWindowPos(hdwp, m_bar, NULL, cursor.x - offX, cursor.y - offY - kBarH, 0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    EndDeferWindowPos(hdwp);
                }
            }
        }
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_LBUTTONUP || msg.message == WM_CAPTURECHANGED) {
                ReleaseCapture();
                updateTarget();
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
    ReleaseCapture();
    updateTarget();
}

void TitleBarBridge::minimize()
{
    if (m_target && IsWindow(m_target)) {
        SendMessageW(m_target, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
}

void TitleBarBridge::toggleMaximize()
{
    if (!m_target || !IsWindow(m_target)) {
        return;
    }
    if (m_maximized) {
        SetWindowPos(m_target, NULL, m_restore.left, m_restore.top,
                     m_restore.right - m_restore.left, m_restore.bottom - m_restore.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        m_maximized = false;
    } else {
        GetWindowRect(m_target, &m_restore);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(m_target, MONITOR_DEFAULTTOPRIMARY), &mi);
        /* work area minus a bar strip at the top - never SC_MAXIMIZE
           (system-owned maximize raises the target over the bar and its
           restore rect tracks the moved rect, shrinking the window on
           every cycle) */
        const RECT wa = mi.rcWork;
        SetWindowPos(m_target, NULL, wa.left, wa.top + kBarH,
                     wa.right - wa.left, wa.bottom - wa.top - kBarH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        m_maximized = true;
    }
    updateTarget();
    Q_EMIT stateChanged();
}

void TitleBarBridge::close()
{
    if (m_target && IsWindow(m_target)) {
        SendMessageW(m_target, WM_SYSCOMMAND, SC_CLOSE, 0);
    }
}
