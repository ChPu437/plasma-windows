/*
    SPDX-FileCopyrightText: 2026 Plasma Windows contributors
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVector>

#include <QtQml/qqml.h>

class StartMenuModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(bool isRoot READ isRoot NOTIFY directoryChanged)
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconRole,
        ExecRole,
        IsDirRole,
        PathRole,
        LinkPathRole,
    };

    explicit StartMenuModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString directory() const;
    void setDirectory(const QString &path);
    bool isRoot() const;
    QString rootPath() const;

    Q_INVOKABLE void goParent();
    Q_INVOKABLE void launch(int row) const;

Q_SIGNALS:
    void directoryChanged();

private:
    void scan();
    QString resolveLnk(const QString &lnkPath, QString *targetOut) const;

    struct Entry
    {
        QString name;
        QString exec;
        bool isDir = false;
        QString path;
        QString linkPath;
    };

    QString m_directory;
    bool m_isRoot = true;
    QVector<Entry> m_entries;
    QStringList m_stack;
};
