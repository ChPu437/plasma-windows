/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include <QObject>

#include <KPluginFactory>
#include <plasma/applet.h>

class WindowsMenuApplet : public Plasma::Applet
{
    Q_OBJECT
public:
    WindowsMenuApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
        : Plasma::Applet(parent, data, args)
    {
    }
};

K_PLUGIN_CLASS_WITH_JSON(WindowsMenuApplet, "plasmoid/metadata.json")

#include "windowsmenu.moc"
