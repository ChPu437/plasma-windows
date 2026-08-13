/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include <QObject>

#include <KPluginFactory>
#include <Plasma/Applet>
#include <Plasma/Containment>

#include "volumecontroller.h"

// Register before any QML is loaded (runtime qmlRegisterType in the applet
// ctor runs after Plasma::Applet already loaded the QML, so the type would
// not be visible to it).
static struct VolumeTypeRegistrar
{
    VolumeTypeRegistrar()
    {
        qmlRegisterType<VolumeController>("org.kde.plasma.private.volumewin", 1, 0, "VolumeController");
    }
} s_volumeTypeRegistrar;

class VolumeWinApplet : public Plasma::Applet
{
    Q_OBJECT
public:
    VolumeWinApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
        : Plasma::Applet(parent, data, args)
    {
    }
};

K_PLUGIN_CLASS_WITH_JSON(VolumeWinApplet, "plasmoid.json")

#include "volumewin.moc"
