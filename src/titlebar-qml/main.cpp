/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

    titlebar-qml - Plasma-style QML title bar for decorated windows.

    Usage: titlebar-qml.exe --target <hwnd>     (target window handle)

    The bar is an owned window of the target; rendering is Qt Quick
    with the real Breeze icon theme (window-minimize/-maximize/-close
    SVGs), window management is Win32 (see TitleBarBridge).
*/

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>

#include <windows.h>

#include "bridge.h"

namespace
{
/* The bridge repositions/resizes the bar HWND (SetWindowPos) to follow
   the target; the QML content item must track the window size - the
   default contentItem did not follow external resizes, leaving the
   QML (buttons) at the initial 320px width. */
class BarWindow : public QQuickWindow
{
public:
    void setBarContent(QQuickItem *item)
    {
        m_content = item;
        if (m_content) {
            m_content->setSize(QSizeF(size()));
        }
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QQuickWindow::resizeEvent(e);
        if (m_content) {
            m_content->setSize(QSizeF(e->size()));
        }
    }

private:
    QQuickItem *m_content = nullptr;
};

/* QQuickImageProvider::Pixmap returns do not reach the scene graph
   under the software renderer; use the Image flavour with QImages
   pre-rendered on the GUI thread (QIcon is not safe off-thread). */
QHash<QString, QImage> g_iconCache;

QImage loadThemeIcon(const QString &id, bool white)
{
    QImage img = QIcon::fromTheme(id).pixmap(16, 16).toImage().convertToFormat(QImage::Format_ARGB32);
    if (white) {
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb p = img.pixel(x, y);
                img.setPixel(x, y, qRgba(255, 255, 255, qAlpha(p)));
            }
        }
    }
    return img;
}

class ThemeIconProvider : public QQuickImageProvider
{
public:
    ThemeIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        Q_UNUSED(requestedSize)
        const QImage img = g_iconCache.value(id);
        if (size) {
            *size = img.size();
        }
        return img;
    }
};
}

int main(int argc, char *argv[])
{
    /* Qt plugins live in CraftRoot\plugins (Craft layout); without this
       the platform plugin (qwindows) is not found and no window can be
       created - set before QGuiApplication exists. Software backend like
       the plasma shell (no GPU assumptions on the target machines). */
    qputenv("QT_PLUGIN_PATH", "D:/Projects/CraftRoot/plugins");
    qputenv("QT_QUICK_BACKEND", "software");

    QGuiApplication app(argc, argv);

    /* real Breeze icon theme: craft bundles it next to the executable */
    QIcon::setThemeSearchPaths({QCoreApplication::applicationDirPath() + QStringLiteral("/data/icons")});
    QIcon::setThemeName(QStringLiteral("breeze"));
    /* pre-render the icons on the GUI thread */
    g_iconCache.insert(QStringLiteral("window-minimize"), loadThemeIcon(QStringLiteral("window-minimize"), false));
    g_iconCache.insert(QStringLiteral("window-maximize"), loadThemeIcon(QStringLiteral("window-maximize"), false));
    g_iconCache.insert(QStringLiteral("window-restore"), loadThemeIcon(QStringLiteral("window-restore"), false));
    g_iconCache.insert(QStringLiteral("window-close"), loadThemeIcon(QStringLiteral("window-close"), false));
    g_iconCache.insert(QStringLiteral("window-close-white"), loadThemeIcon(QStringLiteral("window-close"), true));

    quintptr targetHwnd = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--target") == 0) {
            targetHwnd = wcstoull(QString::fromLocal8Bit(argv[i + 1]).toStdWString().c_str(), nullptr, 16);
        }
    }
    if (!targetHwnd) {
        qWarning() << "usage: titlebar-qml.exe --target <hwnd>";
        return 2;
    }

    TitleBarBridge bridge;
    bridge.setTarget(targetHwnd);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);

    /* Create the window from C++ (the QtQuick Window wrapper did not
       show reliably), load the bar content as an Item into it. */
    BarWindow win;
    win.setFlags(Qt::FramelessWindowHint | Qt::Tool);
    win.setColor(QColor(QStringLiteral("#EFF0F1")));
    win.resize(320, 32);

    QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/main.qml")));
    QObject *obj = comp.create();
    if (!obj) {
        qWarning() << "QML content failed:" << comp.errorString();
        return 1;
    }
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        item->setParentItem(win.contentItem());
        win.setBarContent(item);
    }

    win.show();
    bridge.attachBar(quintptr(win.winId()));

    return app.exec();
}
