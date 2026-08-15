/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QByteArray>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QMap>
#include <QObject>
#include <QSize>

#include <windows.h>

struct DBusImageStruct
{
    int width;
    int height;
    QByteArray data; // ARGB32 bytes
};

Q_DECLARE_METATYPE(DBusImageStruct)

// org.kde.StatusNotifierItem ToolTip property: (s a(iiay) s s) - the same
// layout plasma-workspace's KDbusToolTipStruct expects.
struct DBusToolTipStruct
{
    QString icon;
    QList<DBusImageStruct> image;
    QString title;
    QString subTitle;
};

Q_DECLARE_METATYPE(DBusToolTipStruct)

// One org.kde.StatusNotifierItem service per tray icon. Renders the
// HICON into ARGB pixels (DrawIconEx, no Qt6Gui dependency) and routes
// plasma's Activate/ContextMenu back to the app's callback window with
// the v3/v4 message semantics.
class Snibridge : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QList<DBusImageStruct> IconPixmap READ iconPixmap NOTIFY iconChanged)
    Q_PROPERTY(QDBusVariant ToolTip READ toolTip)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
    Q_PROPERTY(int WindowId READ windowId)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)

public:
    explicit Snibridge(QObject *parent = nullptr);

    QString category() const { return QStringLiteral("ApplicationStatus"); }
    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString status() const { return QStringLiteral("Active"); }
    QString iconName() const { return QString(); }
    QList<DBusImageStruct> iconPixmap() const { return m_icon; }
    QDBusVariant toolTip() const
    {
        DBusToolTipStruct tip;
        tip.icon = QString();
        tip.image = QList<DBusImageStruct>();
        tip.title = m_title;
        tip.subTitle = QString();
        return QDBusVariant(QVariant::fromValue(tip));
    }
    /* No menu: return an empty path per the SNI spec. A fake path like
       /NO_DBUSMENU makes clients try to connect to a DBus object that
       does not exist (silent errors in the plasma tray). */
    QDBusObjectPath menu() const { return QDBusObjectPath(); }
    int windowId() const { return 0; }
    bool itemIsMenu() const { return false; }

    void setIcon(HICON hIcon);
    void setTitle(const QString &title);
    void setCallback(HWND hwnd, UINT uID, UINT message, UINT version);
    void setServiceName(const QString &name) { m_serviceName = name; }
    const QString &serviceName() const { return m_serviceName; }
    void setConnection(const QDBusConnection &conn) { m_connection = conn; }
    QDBusConnection connection() const { return m_connection; }

Q_SIGNALS:
    void titleChanged();
    void iconChanged();

public:
    // org.kde.StatusNotifierItem methods (must stay public: QtDBus only
    // exports Q_INVOKABLEs with public access from adaptors)
    Q_INVOKABLE void Activate(int x, int y);
    Q_INVOKABLE void SecondaryActivate(int x, int y);
    Q_INVOKABLE void ContextMenu(int x, int y);
    Q_INVOKABLE void Scroll(int delta, const QString &orientation);

private:
    friend class SnibridgeProperties;

    void sendClick(UINT downMsg, UINT upMsg);
    void sendMessage(UINT msg);
    void foregroundApp();

    QString m_id;
    QString m_serviceName;
    QDBusConnection m_connection;
    QString m_title;
    HWND m_hwnd = nullptr;
    UINT m_uID = 0;
    UINT m_message = 0;
    UINT m_version = 0;
    QList<DBusImageStruct> m_icon;
};

// Second adaptor exported on the same object path: plasma's
// StatusNotifierItemSource fetches everything through a single
// org.freedesktop.DBus.Properties.GetAll call, which Qt's dynamic
// registerObject does not provide for adaptor Q_PROPERTYs (it replies
// UnknownInterface). Implement it explicitly.
class SnibridgeProperties : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.Properties")

public:
    explicit SnibridgeProperties(Snibridge *item)
        : QDBusAbstractAdaptor(item)
        , m_item(item)
    {
    }

    Q_INVOKABLE QMap<QString, QVariant> GetAll(const QString &iface) const;
    Q_INVOKABLE QDBusVariant Get(const QString &iface, const QString &property) const;
    Q_INVOKABLE void Set(const QString &iface, const QString &property, const QDBusVariant &value);

private:
    Snibridge *const m_item;
};
