/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include <QCoreApplication>
#include <QTimer>

#include "windowstrayhost.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    WindowsTrayHost host;
    if (!host.start()) {
        return 1;
    }

    QTimer dumpTimer;
    dumpTimer.setInterval(10000);
    QObject::connect(&dumpTimer, &QTimer::timeout, &host, &WindowsTrayHost::dumpIcons);
    dumpTimer.start();

    return app.exec();
}
