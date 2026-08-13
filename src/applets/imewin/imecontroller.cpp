/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "imecontroller.h"

#include <QDebug>
#include <windows.h>

ImeController::ImeController(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ImeController::refresh);
    m_timer.start(300);
    refresh();
}

QString ImeController::layoutName() const
{
    return m_name;
}

QString ImeController::layoutCode() const
{
    return m_code;
}

void ImeController::refresh()
{
    // The IME of the *foreground* window's thread, not ours.
    const HWND fg = GetForegroundWindow();
    // Remember the real user window: when the panel is clicked, the
    // foreground window is our own shell, but the layout switch must be
    // sent to the application the user actually uses.
    if (fg && fg != m_targetWnd) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid != GetCurrentProcessId()) {
            m_targetWnd = fg;
        }
    }
    const DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const HKL hkl = GetKeyboardLayout(tid);
    const LANGID lang = LOWORD(hkl);
    const WORD primary = PRIMARYLANGID(lang);

    QString code;
    QString name;
    switch (primary) {
    case LANG_CHINESE:
        code = QStringLiteral("中");
        name = QStringLiteral("Chinese");
        break;
    case LANG_JAPANESE:
        code = QStringLiteral("日");
        name = QStringLiteral("Japanese");
        break;
    case LANG_KOREAN:
        code = QStringLiteral("한");
        name = QStringLiteral("Korean");
        break;
    default:
        code = QStringLiteral("EN");
        name = QStringLiteral("English");
        break;
    }

    if (code != m_code) {
        m_code = code;
        m_name = name;
        Q_EMIT layoutNameChanged();
    }
}

void ImeController::toggle()
{
    HKL list[16];
    const int count = GetKeyboardLayoutList(16, list);
    if (count <= 0) {
        return;
    }
    // Prefer the last real user window; fall back to the current
    // foreground (which after clicking our panel is the shell itself).
    HWND target = m_targetWnd;
    if (!target || !IsWindow(target)) {
        target = GetForegroundWindow();
    }
    if (!target) {
        return;
    }
    const HKL cur = GetKeyboardLayout(GetWindowThreadProcessId(target, nullptr));
    HKL next = list[0];
    for (int i = 0; i < count; ++i) {
        if (list[i] == cur) {
            next = list[(i + 1) % count];
            break;
        }
    }
    PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(next));
    refresh();
}

#include "moc_imecontroller.cpp"
