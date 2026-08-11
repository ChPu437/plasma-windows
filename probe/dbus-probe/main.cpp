// M1 probe: verify a QDBus session connection and look for well-known
// service names (org.freedesktop.DBus, org.kde.kded).

#include <QCoreApplication>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QStringList>

static void report(const QString& line)
{
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/dbus-probe-result.txt"));
    f.open(QIODevice::Append | QIODevice::Text);
    f.write((line + QLatin1Char('\n')).toUtf8());
    f.close();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QDBusConnection bus = QDBusConnection::sessionBus();
    report(QStringLiteral("DBUS_SESSION_BUS_ADDRESS=[%1]")
               .arg(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS")));
    if (!bus.isConnected()) {
        report(QStringLiteral("FAIL: session bus not connected, error: %1")
                   .arg(bus.lastError().message()));
        return 1;
    }
    report(QStringLiteral("session bus connected: %1 baseService: %2")
               .arg(bus.name(), bus.baseService()));

    QDBusReply<QStringList> reply =
        bus.interface()->registeredServiceNames();
    if (!reply.isValid()) {
        report(QStringLiteral("FAIL: ListNames error: %1")
                   .arg(reply.error().message()));
        return 1;
    }
    const QStringList names = reply.value();
    report(QStringLiteral("registered names: %1").arg(names.join(QLatin1Char(' '))));
    const bool hasKded = names.contains(QStringLiteral("org.kde.kded"))
        || names.contains(QStringLiteral("org.kde.kded6"));
    const bool hasActivities =
        names.contains(QStringLiteral("org.kde.ActivityManager"));
    report(hasKded ? QStringLiteral("OK: kded present")
                   : QStringLiteral("kded absent"));
    report(hasActivities ? QStringLiteral("OK: kactivitymanagerd present")
                         : QStringLiteral("kactivitymanagerd absent"));
    return 0;
}
