// SPDX-FileCopyrightText: 2020 Camilo Higuita <milo.h@aol.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QDir>
#include <QLibraryInfo>
#include <QQmlExtensionPlugin>

/**
 * @private
 */
class FileBrowsingPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
    
public:
    void registerTypes(const char *uri) override;
    
private:
    QUrl componentUrl(const QString &fileName) const;
    
    void initializeEngine(QQmlEngine *engine, const char *uri) override;
    
    QString resolveFileUrl(const QString &filePath) const
    {
#if defined(Q_OS_ANDROID)
        return QStringLiteral("qrc:/qt/qml/org/mauikit/filebrowsing/") + filePath;
#else
        const QString qmlModulePath = QDir(QLibraryInfo::path(QLibraryInfo::QmlImportsPath)).filePath(QStringLiteral("org/mauikit/filebrowsing"));
        return QUrl::fromLocalFile(QDir(qmlModulePath).filePath(filePath)).toString();
#endif
    }
};
