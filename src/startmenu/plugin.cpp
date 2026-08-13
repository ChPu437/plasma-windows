/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include <QQmlEngine>
#include <QQmlExtensionPlugin>

#include "startmenuimageprovider.h"
#include "startmenumodel.h"

class StartMenuPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri) override
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("org.kde.plasma.windowsmenu"));
        qmlRegisterType<StartMenuModel>(uri, 1, 0, "StartMenuModel");
    }

    void initializeEngine(QQmlEngine *engine, const char *uri) override
    {
        Q_UNUSED(uri)
        engine->addImageProvider(QStringLiteral("startmenu"), new StartMenuImageProvider);
    }
};

#include "plugin.moc"
