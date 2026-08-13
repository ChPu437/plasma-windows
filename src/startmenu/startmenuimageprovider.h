/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <windows.h>

// Provides icons for `image://startmenu/<url-encoded-link-path>`.
// Extracts the HICON via the .lnk's icon location (or the resolved
// target executable) and converts it to a QImage in memory - no PNG
// files on disk, no main-thread blocking after the first request.
class StartMenuImageProvider : public QQuickImageProvider
{
public:
    explicit StartMenuImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage iconForLink(const QString &lnkPath) const;

    mutable QHash<QString, QImage> m_cache;
};
