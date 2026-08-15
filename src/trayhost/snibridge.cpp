/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "snibridge.h"

#include <QDBusArgument>
#include <QDebug>
#include <QMetaType>
#include <QtDBus/qdbusmetatype.h>
#include <QVariant>

void logLine(const QString &line);

QDBusArgument &operator<<(QDBusArgument &arg, const DBusImageStruct &img);
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusImageStruct &img);
QDBusArgument &operator<<(QDBusArgument &arg, const DBusToolTipStruct &tip);
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusToolTipStruct &tip);

// Register the marshall operators for DBusImageStruct/DBusToolTipStruct
// (same work as qDBusRegisterMetaType, written out to avoid an MSVC quirk
// with the parameterless template call).
namespace
{
struct DbMetaReg
{
    DbMetaReg()
    {
        const QMetaType mt = QMetaType::fromType<DBusImageStruct>();
        QDBusMetaType::registerMarshallOperators(
            mt,
            [](QDBusArgument &arg, const void *t) { arg << *static_cast<const DBusImageStruct *>(t); },
            [](const QDBusArgument &arg, void *t) { arg >> *static_cast<DBusImageStruct *>(t); });
        const QMetaType mtTip = QMetaType::fromType<DBusToolTipStruct>();
        QDBusMetaType::registerMarshallOperators(
            mtTip,
            [](QDBusArgument &arg, const void *t) { arg << *static_cast<const DBusToolTipStruct *>(t); },
            [](const QDBusArgument &arg, void *t) { arg >> *static_cast<DBusToolTipStruct *>(t); });
        // QList<DBusImageStruct> inside a QVariant needs its own marshall
        // entry (Qt does not synthesize it), otherwise the GetAll reply
        // aborts mid-serialization.
        const QMetaType mtList = QMetaType::fromType<QList<DBusImageStruct>>();
        QDBusMetaType::registerMarshallOperators(
            mtList,
            [](QDBusArgument &arg, const void *t) { arg << *static_cast<const QList<DBusImageStruct> *>(t); },
            [](const QDBusArgument &arg, void *t) { arg >> *static_cast<QList<DBusImageStruct> *>(t); });
    }
};
DbMetaReg s_dbMetaReg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const DBusImageStruct &img)
{
    arg.beginStructure();
    arg << img.width;
    arg << img.height;
    arg << img.data;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusImageStruct &img)
{
    arg.beginStructure();
    arg >> img.width;
    arg >> img.height;
    arg >> img.data;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const DBusToolTipStruct &tip)
{
    arg.beginStructure();
    arg << tip.icon;
    arg << tip.image;
    arg << tip.title;
    arg << tip.subTitle;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusToolTipStruct &tip)
{
    arg.beginStructure();
    arg >> tip.icon;
    arg >> tip.image;
    arg >> tip.title;
    arg >> tip.subTitle;
    arg.endStructure();
    return arg;
}

Snibridge::Snibridge(QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , m_id(QStringLiteral("windows-tray-%1").arg(reinterpret_cast<quintptr>(this), 0, 16))
    , m_connection(QDBusConnection::sessionBus())
{
}

void Snibridge::setIcon(HICON hIcon)
{
    m_icon.clear();
    if (!hIcon) {
        logLine(QStringLiteral("  setIcon: null hIcon"));
        Q_EMIT iconChanged();
        return;
    }

    HICON copy = CopyIcon(hIcon);
    if (!copy) {
        logLine(QStringLiteral("  setIcon: CopyIcon failed err=%1").arg(GetLastError()));
        return;
    }

    ICONINFO info{};
    if (GetIconInfo(copy, &info)) {
        BITMAP bmp{};
        GetObjectW(info.hbmColor, sizeof(bmp), &bmp);
        const int w = bmp.bmWidth;
        const int h = bmp.bmHeight;
        logLine(QStringLiteral("  setIcon: hIcon=%1 size=%2x%3").arg(reinterpret_cast<quintptr>(hIcon), 0, 16).arg(w).arg(h));
        if (w > 0 && h > 0) {
            // Offer 16 and 24 px variants. The plasma panel on this setup
            // is 30 px thick (vertical); a 32 px icon makes the panel's
            // layered-window dirty rect exceed the window and
            // UpdateLayeredWindowIndirect fails - the tray icons never
            // get painted. Sized variants let the panel pick a fitting
            // icon (<=24) instead of forcing 32.
            for (int dw : {24, 16}) {
                if (dw > w) {
                    continue;
                }
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = dw;
                bmi.bmiHeader.biHeight = -dw; // top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                void *bits = nullptr;
                HDC screen = GetDC(nullptr);
                HDC dc = CreateCompatibleDC(screen);
                HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (dib) {
                    HGDIOBJ old = SelectObject(dc, dib);
                    memset(bits, 0, size_t(dw) * dw * 4);
                    DrawIconEx(dc, 0, 0, copy, dw, dw, 0, nullptr, DI_NORMAL);

                    DBusImageStruct img;
                    img.width = dw;
                    img.height = dw;
                    img.data.resize(dw * dw * 4);
                    // DIB is BGRA in memory; SNI wants ARGB (A,R,G,B byte order).
                    const auto *src = static_cast<const uchar *>(bits);
                    uchar *dst = reinterpret_cast<uchar *>(img.data.data());
                    for (int i = 0; i < dw * dw; ++i) {
                        dst[i * 4 + 0] = src[i * 4 + 3]; // A
                        dst[i * 4 + 1] = src[i * 4 + 2]; // R
                        dst[i * 4 + 2] = src[i * 4 + 1]; // G
                        dst[i * 4 + 3] = src[i * 4 + 0]; // B
                    }
                    m_icon.append(img);

                    SelectObject(dc, old);
                    DeleteObject(dib);
                }
                DeleteDC(dc);
                ReleaseDC(nullptr, screen);
            }
        }
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
    }
    DestroyIcon(copy);
    Q_EMIT iconChanged();
}

void Snibridge::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    Q_EMIT titleChanged();
}

void Snibridge::setCallback(HWND hwnd, UINT uID, UINT message, UINT version)
{
    m_hwnd = hwnd;
    m_uID = uID;
    m_message = message;
    m_version = version;
}

void Snibridge::sendMessage(UINT msg)
{
    if (!m_hwnd || !m_message) {
        return;
    }
    // Standard tray callback contract: wParam = icon id (the app matches
    // its own NID.uID against it), lParam = message code. For v4,
    // NIN_SELECT carries the message time in lParam like the real tray.
    const LPARAM lp = (m_version >= 4 && msg == NIN_SELECT) ? GetMessageTime() : static_cast<LPARAM>(msg);
    const LRESULT res = SendMessageW(m_hwnd, m_message, m_uID, lp);
    logLine(QStringLiteral("    click->hwnd=0x%1 msg=0x%2 wParam=0x%3 lParam=0x%4 v=%5 res=%6 err=%7")
                .arg(reinterpret_cast<quintptr>(m_hwnd), 0, 16)
                .arg(m_message, 0, 16)
                .arg(m_uID, 0, 16)
                .arg(lp, 0, 16)
                .arg(m_version)
                .arg(res)
                .arg(GetLastError()));
}

void Snibridge::sendClick(UINT downMsg, UINT upMsg)
{
    if (!m_hwnd || !m_message) {
        return;
    }
    // Let the app steal focus to show its context menu.
    AllowSetForegroundWindow(ASFW_ANY);
    sendMessage(downMsg);
    sendMessage(upMsg);
}

// The app's TrackPopupMenu only becomes active (and dismisses on outside
// clicks) when the app owns the foreground. Activate the callback window
// first; if it is a hidden host window that cannot take the foreground
// (Electron_NotifyIconHostWindow etc.), fall back to the process's
// topmost visible window.
// Activate the app owning the tray icon. A plain SetForegroundWindow from
// a background process is refused unless it holds foreground rights; the
// ASFW_ANY grant does not reliably work from a DBus callback, so attach to
// the target thread's input queue first (the classic cross-process focus
// trick), which makes SetForegroundWindow succeed.
static bool forceForeground(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    // A background process (DBus callback) does not hold foreground
    // rights, so SetForegroundWindow is normally refused. The standard
    // (input-free) trick: attach our thread to the CURRENT foreground
    // thread's input queue - the system then treats our SetForegroundWindow
    // as coming from the foreground context.
    AllowSetForegroundWindow(ASFW_ANY);
    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    const DWORD ourThread = GetCurrentThreadId();
    if (fgThread && fgThread != ourThread) {
        AttachThreadInput(ourThread, fgThread, TRUE);
    }
    if (targetThread && targetThread != ourThread && targetThread != fgThread) {
        AttachThreadInput(ourThread, targetThread, TRUE);
    }
    SetForegroundWindow(hwnd);
    if (targetThread && targetThread != ourThread && targetThread != fgThread) {
        AttachThreadInput(ourThread, targetThread, FALSE);
    }
    if (fgThread && fgThread != ourThread) {
        AttachThreadInput(ourThread, fgThread, FALSE);
    }
    const DWORD err = GetLastError();
    const bool ok = GetForegroundWindow() == hwnd;
    logLine(QStringLiteral("    fg: SetForegroundWindow(0x%1) ok=%2 err=%3")
                .arg(reinterpret_cast<quintptr>(hwnd), 0, 16).arg(ok).arg(err));
    return ok;
}

// Bring the app owning the tray icon to the foreground (see
// forceForeground above); fall back to the process's topmost visible
// window when the callback window is a hidden host
// (Electron_NotifyIconHostWindow etc.).
void Snibridge::foregroundApp()
{
    if (!m_hwnd) {
        return;
    }
    if (forceForeground(m_hwnd)) {
        return;
    }
    DWORD pid = 0;
    if (GetWindowThreadProcessId(m_hwnd, &pid) && pid != 0) {
        struct EnumCtx
        {
            DWORD pid;
            HWND best;
        } ctx = {pid, nullptr};
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto *c = reinterpret_cast<EnumCtx *>(lp);
            DWORD p = 0;
            if (GetWindowThreadProcessId(h, &p) && p == c->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
                c->best = h;
                return FALSE; // topmost enumerated window of this process
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.best) {
            forceForeground(ctx.best);
        } else {
            logLine(QStringLiteral("    fg: no visible main window found"));
        }
    }
}

void Snibridge::Activate(int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)
    foregroundApp();
    sendClick(WM_LBUTTONDOWN, WM_LBUTTONUP);
    // v4 apps expect NIN_SELECT after a left click; older versions only
    // know the mouse messages and may misbehave on unknown codes.
    if (m_version >= 4) {
        sendMessage(NIN_SELECT);
    }
}

void Snibridge::SecondaryActivate(int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)
    foregroundApp();
    sendClick(WM_MBUTTONDOWN, WM_MBUTTONUP);
}

void Snibridge::ContextMenu(int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)
    const HWND fgBefore = GetForegroundWindow();
    foregroundApp();
    const HWND fgAfter = GetForegroundWindow();
    logLine(QStringLiteral("    context: fg 0x%1 -> 0x%2 (ours 0x%3)")
                .arg(reinterpret_cast<quintptr>(fgBefore), 0, 16)
                .arg(reinterpret_cast<quintptr>(fgAfter), 0, 16)
                .arg(reinterpret_cast<quintptr>(m_hwnd), 0, 16));
    sendClick(WM_RBUTTONDOWN, WM_RBUTTONUP);
    // v4 apps expect WM_CONTEXTMENU after the right-button up; older
    // versions show their menu on the button-up itself.
    if (m_version >= 4) {
        sendMessage(WM_CONTEXTMENU);
    }
}

void Snibridge::Scroll(int delta, const QString &orientation)
{
    Q_UNUSED(delta)
    Q_UNUSED(orientation)
}

QMap<QString, QVariant> SnibridgeProperties::GetAll(const QString &iface) const
{
    QMap<QString, QVariant> result;
    if (iface != QLatin1String("org.kde.StatusNotifierItem")) {
        return result;
    }
    result.insert(QStringLiteral("Category"), m_item->category());
    result.insert(QStringLiteral("Id"), m_item->id());
    result.insert(QStringLiteral("Title"), m_item->title());
    result.insert(QStringLiteral("Status"), m_item->status());
    result.insert(QStringLiteral("IconName"), m_item->iconName());
    result.insert(QStringLiteral("IconPixmap"), QVariant::fromValue(m_item->iconPixmap()));
    result.insert(QStringLiteral("ToolTip"), QVariant::fromValue(m_item->toolTip()));
    result.insert(QStringLiteral("Menu"), QVariant::fromValue(m_item->menu()));
    result.insert(QStringLiteral("WindowId"), m_item->windowId());
    result.insert(QStringLiteral("ItemIsMenu"), m_item->itemIsMenu());
    return result;
}

QDBusVariant SnibridgeProperties::Get(const QString &iface, const QString &property) const
{
    if (iface != QLatin1String("org.kde.StatusNotifierItem")) {
        return QDBusVariant();
    }
    const QMap<QString, QVariant> all = GetAll(iface);
    const auto it = all.constFind(property);
    return it == all.constEnd() ? QDBusVariant() : QDBusVariant(*it);
}

void SnibridgeProperties::Set(const QString &iface, const QString &property, const QDBusVariant &value)
{
    Q_UNUSED(iface)
    Q_UNUSED(property)
    Q_UNUSED(value)
}
