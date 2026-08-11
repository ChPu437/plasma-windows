// KF6 probe - Phase 2 acceptance test.
// Builds and runs inside the Craft environment (D:\Projects\CraftRoot).
// Proves that a Qt application can link against KDE Frameworks 6 and run.

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <KAboutData>
#include <KConfig>
#include <KConfigGroup>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    KAboutData about(QStringLiteral("kf6-probe"), QStringLiteral("KF6 Probe"),
                     QStringLiteral("0.1.0"));
    KAboutData::setApplicationData(about);
    qInfo() << "KF6 probe" << KAboutData::applicationData().version()
            << "(KCoreAddons)";

    const QString iniPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kf6probe.ini"));
    KConfig cfg(iniPath, KConfig::SimpleConfig);
    KConfigGroup grp = cfg.group(QStringLiteral("Phase2"));
    grp.writeEntry(QStringLiteral("version"), QStringLiteral("6.28.0"));
    cfg.sync();

    KConfig reread(iniPath, KConfig::SimpleConfig);
    const QString value =
        reread.group(QStringLiteral("Phase2")).readEntry(QStringLiteral("version"), QStringLiteral("missing"));
    if (value != QLatin1String("6.28.0")) {
        qCritical() << "FAIL: KConfig round-trip mismatch:" << value;
        return 1;
    }
    qInfo() << "KConfig round-trip OK:" << value;
    return 0;
}
