/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "startmenuimageprovider.h"

#include <QFileInfo>
#include <QUrl>

#include <shlobj.h>

namespace
{
// Render an HICON into an ARGB32 QImage using the system's own icon
// compositing (DrawIconEx handles both 32-bit alpha and legacy mask
// icons), avoiding all-black results from naive mask interpretation.
QImage iconToImage(HICON hIcon)
{
    ICONINFO info;
    if (!hIcon || !GetIconInfo(hIcon, &info)) {
        return {};
    }

    BITMAP bmp{};
    GetObjectW(info.hbmColor, sizeof(bmp), &bmp);
    const int w = bmp.bmWidth;
    const int h = bmp.bmHeight;

    QImage out;
    if (w > 0 && h > 0) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *bits = nullptr;
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib) {
            HGDIOBJ old = SelectObject(dc, dib);
            memset(bits, 0, size_t(w) * h * 4); // fully transparent background
            DrawIconEx(dc, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);

            // Copy the pixels BEFORE releasing the DIB - the bits pointer
            // is only valid while the DIB section is alive.
            out = QImage(w, h, QImage::Format_ARGB32);
            // Windows DIB is BGRA in memory, which is the same byte order
            // as QImage::Format_ARGB32 on little-endian.
            memcpy(out.bits(), bits, size_t(w) * h * 4);

            SelectObject(dc, old);
            DeleteObject(dib);
        }
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
    }

    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return out;
}
} // namespace

StartMenuImageProvider::StartMenuImageProvider()
    : QQuickImageProvider(QQmlImageProviderBase::Image)
{
}

QImage StartMenuImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)

    const QString lnkPath = QUrl::fromPercentEncoding(id.toUtf8());

    auto it = m_cache.constFind(lnkPath);
    if (it == m_cache.constEnd()) {
        it = m_cache.insert(lnkPath, iconForLink(lnkPath));
    }
    if (size) {
        *size = it->size();
    }
    return it.value();
}

QImage StartMenuImageProvider::iconForLink(const QString &lnkPath) const
{
    // 1. explicit icon location from the .lnk (usually the exe + index)
    QString iconSource;
    int iconIndex = 0;
    QString exec;
    IShellLinkW *shellLink = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&shellLink)))) {
        IPersistFile *persist = nullptr;
        if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist)))) {
            if (SUCCEEDED(persist->Load(reinterpret_cast<LPCWSTR>(lnkPath.utf16()), STGM_READ))) {
                wchar_t path[MAX_PATH] = {};
                if (SUCCEEDED(shellLink->GetPath(path, MAX_PATH, nullptr, 0))) {
                    exec = QString::fromWCharArray(path);
                }
                wchar_t iconBuf[MAX_PATH] = {};
                int idx = 0;
                if (SUCCEEDED(shellLink->GetIconLocation(iconBuf, MAX_PATH, &idx))) {
                    iconSource = QString::fromWCharArray(iconBuf);
                    iconIndex = idx;
                }
            }
            persist->Release();
        }
        shellLink->Release();
    }

    HICON hIcon = nullptr;
    if (!iconSource.isEmpty() && QFileInfo::exists(iconSource)) {
        const UINT n = ExtractIconExW(reinterpret_cast<LPCWSTR>(iconSource.utf16()), iconIndex, &hIcon, nullptr, 1);
        if (n == 0) {
            hIcon = nullptr;
        }
    }
    if (!hIcon) {
        hIcon = static_cast<HICON>(ExtractIconW(nullptr, reinterpret_cast<LPCWSTR>(lnkPath.utf16()), 0));
    }
    if (!hIcon && !exec.isEmpty()) {
        const UINT n = ExtractIconExW(reinterpret_cast<LPCWSTR>(exec.utf16()), 0, &hIcon, nullptr, 1);
        if (n == 0) {
            hIcon = nullptr;
        }
    }

    QImage img;
    if (hIcon) {
        img = iconToImage(hIcon);
        DestroyIcon(hIcon);
    }
    return img;
}
