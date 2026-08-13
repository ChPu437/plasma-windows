/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include <QQmlEngine>
#include <QObject>

#include <KPluginFactory>
#include <Plasma/Applet>
#include <Plasma/Containment>

#include "imecontroller.h"

// Register before any QML is loaded (runtime qmlRegisterType in the applet
// ctor runs after Plasma::Applet already loaded the QML, so the type would
// not be visible to it).
static struct ImeTypeRegistrar
{
    ImeTypeRegistrar()
    {
        qmlRegisterType<ImeController>("org.kde.plasma.private.imewin", 1, 0, "ImeController");
    }
} s_imeTypeRegistrar;

class ImeWinApplet : public Plasma::Applet
{
    Q_OBJECT
public:
    ImeWinApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
        : Plasma::Applet(parent, data, args)
    {
    }
};

K_PLUGIN_CLASS_WITH_JSON(ImeWinApplet, "plasmoid.json")

#include "imewin.moc"
