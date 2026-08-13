/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "startmenumodel.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <shlobj.h>
#include <shlwapi.h>

#include <QtQml/qqml.h>


StartMenuModel::StartMenuModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StartMenuModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant StartMenuModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return e.name;
    case IconRole:
        return QString(); // icons come via image://startmenu/ (see StartMenuImageProvider)
    case ExecRole:
        return e.exec;
    case IsDirRole:
        return e.isDir;
    case PathRole:
        return e.path;
    case LinkPathRole:
        return e.linkPath;
    default:
        return {};
    }
}

QHash<int, QByteArray> StartMenuModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconRole, "icon"},
        {ExecRole, "exec"},
        {IsDirRole, "isDir"},
        {PathRole, "path"},
        {LinkPathRole, "linkPath"},
    };
}

QString StartMenuModel::directory() const
{
    return m_directory;
}

bool StartMenuModel::isRoot() const
{
    return m_isRoot;
}

QString StartMenuModel::rootPath() const
{
    QStringList dirs;
    const QString user = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!user.isEmpty()) {
        dirs << user;
    }
    const QString machine = QString::fromLocal8Bit(qgetenv("ProgramData")) + QLatin1String("\\Microsoft\\Windows\\Start Menu\\Programs");
    if (!machine.isEmpty()) {
        dirs << machine;
    }
    return dirs.join(QLatin1Char(';'));
}

void StartMenuModel::setDirectory(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    if (m_directory == normalized) {
        return;
    }
    m_directory = normalized;
    m_isRoot = path.isEmpty() || path == QLatin1String("all");
    if (m_isRoot) {
        m_stack.clear();
    } else {
        m_stack.append(normalized);
    }
    scan();
    Q_EMIT directoryChanged();
}

void StartMenuModel::goParent()
{
    if (m_stack.isEmpty()) {
        return;
    }
    m_stack.removeLast();
    if (m_stack.isEmpty()) {
        m_directory.clear();
        m_isRoot = true;
    } else {
        m_directory = m_stack.last();
    }
    scan();
    Q_EMIT directoryChanged();
}

void StartMenuModel::scan()
{
    beginResetModel();
    m_entries.clear();

    // Ensure COM is usable on this thread for IShellLink (idempotent).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    QStringList dirs;
    if (m_isRoot) {
        dirs << rootPath().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    } else {
        dirs << m_directory;
    }

    QStringList subdirs;
    for (const QString &dir : dirs) {
        QDir d(dir);
        if (!d.exists()) {
            continue;
        }
        const QFileInfoList entries = d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                subdirs.append(fi.absoluteFilePath());
            } else if (fi.suffix().compare(QLatin1String("lnk"), Qt::CaseInsensitive) == 0) {
                Entry e;
                e.isDir = false;
                e.linkPath = fi.absoluteFilePath();
                QString target;
                e.name = resolveLnk(fi.absoluteFilePath(), &target);
                e.exec = target;
                e.path = fi.absoluteFilePath();
                m_entries.append(e);
            }
        }
    }
    std::sort(subdirs.begin(), subdirs.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString &sd : std::as_const(subdirs)) {
        Entry e;
        e.isDir = true;
        e.name = QFileInfo(sd).fileName();
        e.path = sd;
        m_entries.append(e);
    }

    endResetModel();
}

QString StartMenuModel::resolveLnk(const QString &lnkPath, QString *targetOut) const
{
    QString name = QFileInfo(lnkPath).completeBaseName();
    QString target;

    IShellLinkW *shellLink = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&shellLink)))) {
        IPersistFile *persist = nullptr;
        if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist)))) {
            if (SUCCEEDED(persist->Load(reinterpret_cast<LPCWSTR>(lnkPath.utf16()), STGM_READ))) {
                wchar_t path[MAX_PATH] = {};
                if (SUCCEEDED(shellLink->GetPath(path, MAX_PATH, nullptr, 0))) {
                    target = QString::fromWCharArray(path);
                }
                wchar_t desc[MAX_PATH] = {};
                if (SUCCEEDED(shellLink->GetDescription(desc, MAX_PATH))) {
                    const QString d = QString::fromWCharArray(desc);
                    if (!d.isEmpty()) {
                        name = d;
                    }
                }
            }
            persist->Release();
        }
        shellLink->Release();
    }

    if (targetOut) {
        *targetOut = target;
    }
    return name;
}

void StartMenuModel::launch(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    const Entry &e = m_entries.at(row);
    if (e.isDir || e.exec.isEmpty()) {
        return;
    }
    QProcess::startDetached(e.exec);
}
