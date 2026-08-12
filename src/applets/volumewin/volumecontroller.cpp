/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "volumecontroller.h"

#include <QDebug>

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

// {BCDE0395-E52F-467C-8E3D-C4579291692E}
static const CLSID CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
// {5CDF2C82-841E-4546-9722-0CF74078229A}
static const IID IID_IAudioEndpointVolume = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

VolumeController::VolumeController(QObject *parent)
    : QObject(parent)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    connect(&m_timer, &QTimer::timeout, this, &VolumeController::refresh);
    m_timer.start(500);
    refresh();
}

VolumeController::~VolumeController()
{
    if (m_endpoint) {
        static_cast<IAudioEndpointVolume *>(m_endpoint)->Release();
    }
    CoUninitialize();
}

bool VolumeController::ensureDevice()
{
    if (m_endpoint) {
        return true;
    }
    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return false;
    }
    IMMDevice *device = nullptr;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr)) {
        return false;
    }
    hr = device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr, &m_endpoint);
    device->Release();
    if (FAILED(hr)) {
        m_endpoint = nullptr;
        return false;
    }
    return true;
}

qreal VolumeController::volume() const
{
    return m_volume;
}

void VolumeController::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume, m_volume)) {
        return;
    }
    m_volume = volume;
    applyVolume();
    Q_EMIT volumeChanged();
}

bool VolumeController::muted() const
{
    return m_muted;
}

void VolumeController::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    m_muted = muted;
    applyMuted();
    Q_EMIT mutedChanged();
}

void VolumeController::toggleMuted()
{
    setMuted(!m_muted);
}

QString VolumeController::iconName() const
{
    if (m_muted) {
        return QStringLiteral("audio-volume-muted");
    }
    if (m_volume <= 0.05) {
        return QStringLiteral("audio-volume-muted");
    }
    if (m_volume < 0.4) {
        return QStringLiteral("audio-volume-low");
    }
    if (m_volume < 0.7) {
        return QStringLiteral("audio-volume-medium");
    }
    return QStringLiteral("audio-volume-high");
}

void VolumeController::refresh()
{
    if (!ensureDevice()) {
        return;
    }
    auto *vol = static_cast<IAudioEndpointVolume *>(m_endpoint);
    float v = 0;
    BOOL m = FALSE;
    bool vChanged = false;
    bool mChanged = false;
    if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&v))) {
        const qreal nv = qBound(0.0, static_cast<qreal>(v), 1.0);
        if (!qFuzzyCompare(nv, m_volume)) {
            m_volume = nv;
            vChanged = true;
        }
    }
    if (SUCCEEDED(vol->GetMute(&m))) {
        const bool nm = m != FALSE;
        if (nm != m_muted) {
            m_muted = nm;
            mChanged = true;
        }
    }
    if (vChanged) {
        Q_EMIT volumeChanged();
    }
    if (mChanged) {
        Q_EMIT mutedChanged();
    }
}

void VolumeController::applyVolume()
{
    if (!ensureDevice()) {
        return;
    }
    static_cast<IAudioEndpointVolume *>(m_endpoint)->SetMasterVolumeLevelScalar(static_cast<float>(m_volume), nullptr);
}

void VolumeController::applyMuted()
{
    if (!ensureDevice()) {
        return;
    }
    static_cast<IAudioEndpointVolume *>(m_endpoint)->SetMute(m_muted ? TRUE : FALSE, nullptr);
}

#include "moc_volumecontroller.cpp"
