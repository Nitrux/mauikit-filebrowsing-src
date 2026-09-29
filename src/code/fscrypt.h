// SPDX-FileCopyrightText: 2026 Nitrux Latinoamericana S.C.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include "filebrowsing_export.h"

class QProcess;
class QTimer;

class FILEBROWSING_EXPORT Fscrypt : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit Fscrypt(QObject *parent = nullptr);

    bool running() const;

    Q_INVOKABLE QString availabilityMessage(const QUrl &directory) const;
    Q_INVOKABLE bool requiresSetup(const QUrl &directory) const;
    Q_INVOKABLE QString cachedStatus(const QUrl &directory) const;
    Q_INVOKABLE void requestStatus(const QUrl &directory);
    Q_INVOKABLE void invalidateStatus(const QUrl &directory);
    Q_INVOKABLE void cancel();

    Q_INVOKABLE void encryptDirectory(const QUrl &directory,
                                       const QString &protectorName,
                                       const QString &passphrase);
    Q_INVOKABLE void encryptDirectory(const QUrl &directory,
                                      const QString &protectorName,
                                      const QString &passphrase,
                                      bool authorizeSetup);
    Q_INVOKABLE void createEncryptedDirectory(const QUrl &parentDirectory,
                                              const QString &directoryName,
                                              const QString &protectorName,
                                              const QString &passphrase);
    Q_INVOKABLE void createEncryptedDirectory(const QUrl &parentDirectory,
                                              const QString &directoryName,
                                              const QString &protectorName,
                                              const QString &passphrase,
                                              bool authorizeSetup);
    Q_INVOKABLE void lockDirectory(const QUrl &directory);
    Q_INVOKABLE void unlockDirectory(const QUrl &directory, const QString &passphrase);

Q_SIGNALS:
    void runningChanged();
    void finished(bool success, const QString &message);
    void statusChanged(const QUrl &directory, const QString &status);

private:
    enum class Stage {
        None,
        GlobalSetup,
        PersistConfig,
        MountSetup,
        Encrypt,
        Lock,
        Unlock,
    };

    bool startProcess(const QStringList &arguments, QString passphrase, bool privileged);
    bool startTrustedProcess(const QString &executable,
                             const QStringList &arguments,
                             QString passphrase,
                             bool privileged);
    void cacheStatus(const QString &path, const QString &status);
    void finishStatusRequest(QProcess *process, const QUrl &directory, const QString &path, const QString &status);
    void startNextStatusRequest();
    static QString parseStatus(const QString &output, bool success);
    void startSetup();
    void startConfigPersistence();
    void startMountSetup();
    void startEncryption();
    void finish(QProcess *process, bool success, const QString &message);
    void updateStatus(const QUrl &directory, const QString &status);
    void fail(const QString &message);
    void clearPendingOperation();
    bool validatePendingDirectory() const;
    static bool isLiveSession();
    void setRunning(bool running);

    QProcess *m_process = nullptr;
    QTimer *m_operationTimer = nullptr;
    bool m_running = false;
    bool m_setupAuthorized = false;
    bool m_cancelRequested = false;
    Stage m_stage = Stage::None;
    QUrl m_directory;
    QString m_mountPoint;
    QString m_protectorName;
    QString m_passphrase;
    QString m_processPassphrase;
    quint64 m_directoryDevice = 0;
    quint64 m_directoryInode = 0;
    QProcess *m_statusProcess = nullptr;
    QQueue<QUrl> m_statusQueue;
    QSet<QString> m_statusPending;
    QHash<QString, QString> m_statusCache;
    QQueue<QString> m_statusCacheOrder;
};
