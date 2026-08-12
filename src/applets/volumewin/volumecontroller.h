/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqml.h>

class VolumeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY volumeChanged)

public:
    explicit VolumeController(QObject *parent = nullptr);
    ~VolumeController() override;

    qreal volume() const;
    void setVolume(qreal volume);
    bool muted() const;
    void setMuted(bool muted);
    QString iconName() const;

    Q_INVOKABLE void toggleMuted();

Q_SIGNALS:
    void volumeChanged();
    void mutedChanged();

private:
    void refresh();
    void applyVolume();
    void applyMuted();
    bool ensureDevice();

    QTimer m_timer;
    void *m_endpoint = nullptr; // IAudioEndpointVolume*
    qreal m_volume = 1.0;
    bool m_muted = false;
};

