/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "startmenumodel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStandardPaths>

#include <shlobj.h>
#include <shlwapi.h>

#include <QtQml/qqml.h>


StartMenuModel::StartMenuModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadCategories();
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
    if (m_directory == normalized && !m_categoryDirs.isEmpty() == normalized.startsWith(QLatin1String("cat:"))) {
        return;
    }
    m_directory = normalized;
    m_isRoot = path.isEmpty() || path == QLatin1String("all");
    if (m_isRoot) {
        m_stack.clear();
        m_categoryDirs.clear();
    } else if (normalized.startsWith(QLatin1String("cat:"))) {
        /* custom category: resolve its directories from the config */
        const QString catName = normalized.mid(4);
        m_categoryDirs.clear();
        for (const QJsonValue &cat : m_categories) {
            if (cat.toObject().value(QLatin1String("name")).toString() == catName) {
                const QJsonArray dirs = cat.toObject().value(QLatin1String("dirs")).toArray();
                for (const QJsonValue &d : dirs) {
                    const QString dir = QDir::fromNativeSeparators(d.toString());
                    if (!dir.isEmpty()) {
                        m_categoryDirs.append(dir);
                    }
                }
                break;
            }
        }
        m_stack.append(normalized);
    } else {
        m_categoryDirs.clear();
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
        m_categoryDirs.clear();
    } else {
        m_directory = m_stack.last();
        m_categoryDirs.clear();
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
    } else if (!m_categoryDirs.isEmpty()) {
        dirs = m_categoryDirs;
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

    /* UWP (Store) apps live in the virtual AppsFolder, not as .lnk
       files - append them once when showing the root view. */
    if (m_isRoot && !m_uwpLoaded) {
        scanUwpApps();
        m_uwpLoaded = true;
    }

    endResetModel();
}

void StartMenuModel::scanUwpApps()
{
    IShellFolder *desktop = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop))) {
        return;
    }
    PIDLIST_ABSOLUTE appsPidl = nullptr;
    if (FAILED(SHParseDisplayName(L"shell:AppsFolder", nullptr, &appsPidl, 0, nullptr))) {
        desktop->Release();
        return;
    }
    IShellFolder *appsFolder = nullptr;
    if (FAILED(desktop->BindToObject(appsPidl, nullptr, IID_IShellFolder, reinterpret_cast<void **>(&appsFolder)))) {
        CoTaskMemFree(appsPidl);
        desktop->Release();
        return;
    }
    IEnumIDList *en = nullptr;
    if (SUCCEEDED(appsFolder->EnumObjects(nullptr, SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en))) {
        PITEMID_CHILD child = nullptr;
        while (en->Next(1, &child, nullptr) == S_OK) {
            PIDLIST_ABSOLUTE full = ILCombine(appsPidl, child);
            IShellItem *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(full, IID_IShellItem, reinterpret_cast<void **>(&item)))) {
                PWSTR displayName = nullptr;
                PWSTR parsingName = nullptr;
                item->GetDisplayName(SIGDN_NORMALDISPLAY, &displayName);
                item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingName);
                const QString display = displayName ? QString::fromWCharArray(displayName) : QString();
                const QString parsing = parsingName ? QString::fromWCharArray(parsingName) : QString();
                CoTaskMemFree(displayName);
                CoTaskMemFree(parsingName);
                item->Release();
                if (!display.isEmpty() && !parsing.isEmpty()
                    && !parsing.contains(QLatin1String(".exe"), Qt::CaseInsensitive)
                    && !parsing.startsWith(QLatin1String("C:"), Qt::CaseInsensitive)
                    && !parsing.startsWith(QLatin1String("shell:"), Qt::CaseInsensitive)) {
                    /* AUMID -> shell:AppsFolder launch; the icon provider
                       handles the "apps:" linkPath prefix */
                    Entry e;
                    e.isDir = false;
                    e.name = display;
                    e.exec = QStringLiteral("explorer.exe \"shell:AppsFolder\\%1\"").arg(parsing);
                    e.path = e.linkPath = QStringLiteral("apps:%1").arg(parsing);
                    m_entries.append(e);
                }
            }
            CoTaskMemFree(child);
            CoTaskMemFree(full);
        }
        en->Release();
    }
    appsFolder->Release();
    CoTaskMemFree(appsPidl);
    desktop->Release();
}

void StartMenuModel::loadCategories()
{
    m_categories = QJsonArray();
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1String("/plasma/startmenu-categories.json");
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    m_categories = doc.object().value(QLatin1String("sidebar")).toArray();
}

QVariantList StartMenuModel::categories() const
{
    QVariantList out;
    for (const QJsonValue &cat : m_categories) {
        const QJsonObject obj = cat.toObject();
        QVariantMap m;
        m.insert(QLatin1String("name"), obj.value(QLatin1String("name")).toString());
        m.insert(QLatin1String("icon"), obj.value(QLatin1String("icon")).toString());
        out.append(m);
    }
    return out;
}

void StartMenuModel::reload()
{
    loadCategories();
    scan();
    Q_EMIT directoryChanged();
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
