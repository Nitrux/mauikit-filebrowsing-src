// SPDX-FileCopyrightText: 2026 Nitrux Latinoamericana S.C.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "fscrypt.h"
#include "fscryptconfigpersistence.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <KLocalizedString>

#include <sys/stat.h>

namespace
{
constexpr qsizetype MaximumStatusCacheEntries = 512;
constexpr qsizetype MaximumPendingStatusRequests = 256;
constexpr int OperationTimeout = 120000;
constexpr int StatusTimeout = 10000;

#ifndef FSCRYPT_CONFIG_PERSISTENCE_HELPER_PATH
#define FSCRYPT_CONFIG_PERSISTENCE_HELPER_PATH "/usr/libexec/mauikit-filebrowsing-fscrypt-persist"
#endif

QString trustedSystemExecutable(const QString &executable)
{
    const QString path = QStandardPaths::findExecutable(
        executable,
        {QStringLiteral("/usr/bin"), QStringLiteral("/bin")});
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    const QFileDevice::Permissions unsafePermissions =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (!info.isFile() || !info.isExecutable() || info.ownerId() != 0
        || (info.permissions() & unsafePermissions))
    {
        return {};
    }

    return info.canonicalFilePath();
}

QString trustedExecutableAt(const QString &path)
{
    const QFileInfo info(path);
    const QFileDevice::Permissions unsafePermissions =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (!info.isFile() || !info.isExecutable() || info.ownerId() != 0
        || (info.permissions() & unsafePermissions))
    {
        return {};
    }

    return info.canonicalFilePath();
}

void wipe(QString &value)
{
    value.fill(QChar(u'\0'));
    value.clear();
    value.squeeze();
}

bool directoryIdentity(const QString &path, quint64 &device, quint64 &inode)
{
    struct stat information;
    const QByteArray encodedPath = QFile::encodeName(path);
    if (::lstat(encodedPath.constData(), &information) != 0 || !S_ISDIR(information.st_mode))
        return false;

    device = static_cast<quint64>(information.st_dev);
    inode = static_cast<quint64>(information.st_ino);
    return true;
}

bool directoryIsEmpty(const QString &path)
{
    return QDir(path).entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System).isEmpty();
}

QString formatErrorMessage(QString message)
{
    message = message.trimmed();
    const QString errorPrefix = QStringLiteral("[ERROR]");
    if (message.startsWith(errorPrefix))
        message = message.mid(errorPrefix.size()).trimmed();

    const QString lowerMessage = message.toLower();
    if (lowerMessage.contains(QStringLiteral("error getting authority"))
        || lowerMessage.contains(QStringLiteral("error initializing authority")))
    {
        return i18n("Administrator authentication is unavailable. Start the PolicyKit authority and try again.");
    }

    if (lowerMessage.contains(QStringLiteral("directory was incompletely locked"))
        || lowerMessage.contains(QStringLiteral("some files are still open")))
    {
        return i18n("Some files are still open. Close them and try again.");
    }
    if (lowerMessage.contains(QStringLiteral("already encrypted")))
        return i18n("The selected directory is already encrypted.");
    if (lowerMessage.contains(QStringLiteral("/etc/fscrypt.conf"))
        && lowerMessage.contains(QStringLiteral("doesn't exist")))
    {
        return i18n("The fscrypt configuration is missing. Allow administrator setup and try again.");
    }
    if (message.startsWith(QStringLiteral("fscrypt ")))
    {
        const int separator = message.indexOf(QStringLiteral(": "));
        if (separator > 0)
            message = message.mid(separator + 2).trimmed();
    }

    if (message.contains(QStringLiteral("encryption not enabled on filesystem")))
        return i18n("Encryption is not enabled on the filesystem containing this directory. Enable F2FS encryption support before trying again.");

    if (message.contains(QStringLiteral("doesn't support encryption on overlay filesystems")))
        return i18n("This directory is on OverlayFS, which does not support fscrypt. Choose a directory on a persistent, non-overlay filesystem.");

    return message.isEmpty() ? i18n("fscrypt failed.") : message;
}
}

Fscrypt::Fscrypt(QObject *parent)
    : QObject(parent)
    , m_operationTimer(new QTimer(this))
{
    m_operationTimer->setSingleShot(true);
    m_operationTimer->setInterval(OperationTimeout);
    connect(m_operationTimer, &QTimer::timeout, this, [this]() {
        if (!m_process)
            return;

        auto *process = m_process;
        process->kill();
        finish(process, false, i18n("The encryption operation timed out."));
    });
}

bool Fscrypt::running() const
{
    return m_running;
}

bool Fscrypt::isLiveSession()
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly))
        return false;

    const auto arguments = cmdline.readAll().split(' ');
    return std::any_of(arguments.cbegin(), arguments.cend(), [](const QByteArray &argument) {
        return argument == QByteArrayLiteral("boot=casper");
    });
}

QString Fscrypt::availabilityMessage(const QUrl &directory) const
{
    if (isLiveSession())
        return i18n("Live environments are not supported.");

    if (!directory.isLocalFile())
        return i18n("The selected directory is not available locally.");

    const QFileInfo directoryInfo(directory.toLocalFile());
    if (!directoryInfo.isDir() || directoryInfo.isSymLink() || directoryInfo.canonicalFilePath().isEmpty())
        return i18n("The selected directory is not available locally.");

    if (!directoryInfo.isWritable())
        return i18n("The selected directory is not writable.");

    const QStorageInfo storage(directoryInfo.canonicalFilePath());
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
        return i18n("The filesystem containing the directory is not available.");

    const auto filesystemType = storage.fileSystemType().toLower();
    if (filesystemType == QByteArrayLiteral("overlay") || filesystemType == QByteArrayLiteral("overlayfs"))
        return i18n("This directory is on OverlayFS, which does not support fscrypt. Choose a directory on a persistent, non-overlay filesystem.");

    return {};
}

bool Fscrypt::requiresSetup(const QUrl &directory) const
{
    if (!directory.isLocalFile())
        return false;

    const QFileInfo directoryInfo(directory.toLocalFile());
    const QString path = directoryInfo.canonicalFilePath();
    if (!directoryInfo.isDir() || directoryInfo.isSymLink() || path.isEmpty())
        return false;

    if (FscryptConfigPersistence::isOverlayrootActive())
        return true;

    if (!QFileInfo::exists(QStringLiteral("/etc/fscrypt.conf")))
        return true;

    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
        return false;

    const QDir metadataDirectory(QDir(storage.rootPath()).filePath(QStringLiteral(".fscrypt")));
    return !metadataDirectory.exists(QStringLiteral("policies"))
           || !metadataDirectory.exists(QStringLiteral("protectors"));
}

QString Fscrypt::cachedStatus(const QUrl &directory) const
{
    if (!directory.isLocalFile())
        return QStringLiteral("unknown");

    const QString path = QDir::cleanPath(directory.toLocalFile());
    return m_statusCache.value(path, QStringLiteral("unknown"));
}

void Fscrypt::requestStatus(const QUrl &directory)
{
    if (!directory.isLocalFile())
        return;

    const QString path = QDir::cleanPath(directory.toLocalFile());
    if (path.isEmpty())
        return;

    if (m_statusCache.contains(path))
    {
        Q_EMIT statusChanged(directory, m_statusCache.value(path));
        return;
    }

    if (!m_statusPending.contains(path))
    {
        if (m_statusPending.size() >= MaximumPendingStatusRequests && !m_statusQueue.isEmpty())
        {
            const QUrl droppedDirectory = m_statusQueue.dequeue();
            const QString droppedPath = QDir::cleanPath(droppedDirectory.toLocalFile());
            m_statusPending.remove(droppedPath);
            Q_EMIT statusChanged(droppedDirectory, QStringLiteral("unknown"));
        }

        m_statusPending.insert(path);
        m_statusQueue.enqueue(QUrl::fromLocalFile(path));
    }

    startNextStatusRequest();
}

void Fscrypt::invalidateStatus(const QUrl &directory)
{
    if (!directory.isLocalFile())
        return;

    const QString path = QDir::cleanPath(directory.toLocalFile());
    if (path.isEmpty())
        return;

    m_statusCache.remove(path);
    m_statusCacheOrder.removeAll(path);
    requestStatus(directory);
}

void Fscrypt::cacheStatus(const QString &path, const QString &status)
{
    m_statusCacheOrder.removeAll(path);
    while (m_statusCache.size() >= MaximumStatusCacheEntries && !m_statusCacheOrder.isEmpty())
        m_statusCache.remove(m_statusCacheOrder.dequeue());

    m_statusCache.insert(path, status);
    m_statusCacheOrder.enqueue(path);
}

void Fscrypt::updateStatus(const QUrl &directory, const QString &status)
{
    if (!directory.isLocalFile())
        return;

    const QString path = QDir::cleanPath(directory.toLocalFile());
    if (path.isEmpty())
        return;

    const QUrl normalizedDirectory = QUrl::fromLocalFile(path);
    cacheStatus(path, status);
    Q_EMIT statusChanged(normalizedDirectory, status);
}

QString Fscrypt::parseStatus(const QString &output, bool success)
{
    const QString lowerOutput = output.toLower();
    if (lowerOutput.contains(QStringLiteral("is encrypted with fscrypt")) || lowerOutput.contains(QStringLiteral("policy:")))
    {
        if (lowerOutput.contains(QStringLiteral("unlocked: yes")))
            return QStringLiteral("encrypted_unlocked");

        if (lowerOutput.contains(QStringLiteral("unlocked: no")))
            return QStringLiteral("encrypted_locked");

        return QStringLiteral("encrypted");
    }

    if (lowerOutput.contains(QStringLiteral("not encrypted")))
        return QStringLiteral("unencrypted");

    return success ? QStringLiteral("unencrypted") : QStringLiteral("unknown");
}

void Fscrypt::startNextStatusRequest()
{
    if (m_statusProcess || m_statusQueue.isEmpty())
        return;

    const QUrl directory = m_statusQueue.dequeue();
    const QString path = QDir::cleanPath(directory.toLocalFile());
    const auto executable = trustedSystemExecutable(QStringLiteral("fscrypt"));
    if (executable.isEmpty())
    {
        m_statusPending.remove(path);
        cacheStatus(path, QStringLiteral("unknown"));
        Q_EMIT statusChanged(directory, QStringLiteral("unknown"));
        startNextStatusRequest();
        return;
    }

    auto *process = new QProcess(this);
    m_statusProcess = process;

    connect(process, &QProcess::errorOccurred, this, [this, process, directory, path](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_statusProcess != process)
            return;

        finishStatusRequest(process, directory, path, QStringLiteral("unknown"));
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, directory, path](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_statusProcess != process)
            return;

        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
                                   + QStringLiteral("\n")
                                   + QString::fromLocal8Bit(process->readAllStandardError());
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        const QString status = parseStatus(output, success);
        finishStatusRequest(process, directory, path, status);
    });

    process->start(executable, {QStringLiteral("status"), path});
    QTimer::singleShot(StatusTimeout, process, [this, process, directory, path]() {
        if (m_statusProcess != process)
            return;

        process->kill();
        finishStatusRequest(process, directory, path, QStringLiteral("unknown"));
    });
}

void Fscrypt::finishStatusRequest(QProcess *process, const QUrl &directory, const QString &path, const QString &status)
{
    if (m_statusProcess != process)
        return;

    m_statusPending.remove(path);
    cacheStatus(path, status);
    Q_EMIT statusChanged(directory, status);
    m_statusProcess = nullptr;
    process->deleteLater();
    startNextStatusRequest();
}

void Fscrypt::setRunning(bool running)
{
    if (m_running == running)
        return;

    m_running = running;
    Q_EMIT runningChanged();
}

void Fscrypt::clearPendingOperation()
{
    m_directory = QUrl();
    m_mountPoint.clear();
    m_protectorName.clear();
    wipe(m_passphrase);
    wipe(m_processPassphrase);
    m_directoryDevice = 0;
    m_directoryInode = 0;
    m_setupAuthorized = false;
    m_cancelRequested = false;
}

void Fscrypt::cancel()
{
    if (!m_process)
        return;

    if (m_stage == Stage::PersistConfig)
    {
        m_cancelRequested = true;
        return;
    }

    auto *process = m_process;
    process->kill();
    finish(process, false, i18n("The encryption operation was canceled."));
}

void Fscrypt::fail(const QString &message)
{
    m_stage = Stage::None;
    clearPendingOperation();
    setRunning(false);
    Q_EMIT finished(false, message);
}

void Fscrypt::finish(QProcess *process, bool success, const QString &message)
{
    if (m_process != process)
        return;

    m_process = nullptr;
    m_operationTimer->stop();
    process->deleteLater();

    if (m_cancelRequested && m_stage == Stage::PersistConfig)
    {
        fail(i18n("The encryption operation was canceled."));
        return;
    }

    if (success && m_stage == Stage::GlobalSetup)
    {
        if (FscryptConfigPersistence::isOverlayrootActive())
        {
            startConfigPersistence();
            return;
        }

        startMountSetup();
        return;
    }

    if (success && m_stage == Stage::PersistConfig)
    {
        startMountSetup();
        return;
    }

    if (success && m_stage == Stage::MountSetup)
    {
        startEncryption();
        return;
    }

    if (success && m_stage == Stage::CheckStatus)
    {
        const QString status = parseStatus(message, true);
        if (status.startsWith(QStringLiteral("encrypted")))
        {
            updateStatus(m_directory, status);
            m_stage = Stage::None;
            clearPendingOperation();
            setRunning(false);
            Q_EMIT finished(false, i18n("The selected directory is already encrypted."));
            return;
        }

        startEncryptionCommand();
        return;
    }

    if (success)
    {
        if (m_stage == Stage::Encrypt || m_stage == Stage::Unlock)
            updateStatus(m_directory, QStringLiteral("encrypted_unlocked"));
        else if (m_stage == Stage::Lock)
            updateStatus(m_directory, QStringLiteral("encrypted_locked"));
    }

    m_stage = Stage::None;
    clearPendingOperation();
    setRunning(false);
    Q_EMIT finished(success, message);
}

bool Fscrypt::startProcess(const QStringList &arguments, QString passphrase, bool privileged)
{
    const auto executable = trustedSystemExecutable(QStringLiteral("fscrypt"));
    if (executable.isEmpty())
    {
        wipe(passphrase);
        fail(i18n("A trusted fscrypt executable is not installed."));
        return false;
    }

    return startTrustedProcess(executable, arguments, std::move(passphrase), privileged);
}

bool Fscrypt::startTrustedProcess(const QString &executable,
                                  const QStringList &arguments,
                                  QString passphrase,
                                  bool privileged)
{
    if (m_process)
    {
        wipe(passphrase);
        fail(i18n("Another encryption operation is already running."));
        return false;
    }

    QString program = executable;
    QStringList processArguments = arguments;
    if (privileged)
    {
        const auto pkexec = trustedSystemExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty())
        {
            wipe(passphrase);
            fail(i18n("PolicyKit is not installed."));
            return false;
        }

        program = pkexec;
        processArguments.prepend(executable);
    }

    auto *process = new QProcess(this);
    m_process = process;
    m_processPassphrase = std::move(passphrase);

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(process, false, process->errorString());
    });

    connect(process, &QProcess::started, this, [this, process]() {
        if (!m_processPassphrase.isEmpty())
        {
            QByteArray encodedPassphrase = m_processPassphrase.toUtf8();
            process->write(encodedPassphrase);
            process->write("\n");
            encodedPassphrase.fill('\0');
            encodedPassphrase.clear();
        }

        wipe(m_processPassphrase);
        process->closeWriteChannel();
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
        const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        const QString fallbackMessage = m_stage == Stage::PersistConfig
                                            ? i18n("The fscrypt configuration could not be made persistent.")
                                            : i18n("fscrypt failed.");
        const QString processMessage = !error.isEmpty() ? error : (!output.isEmpty() ? output : fallbackMessage);
        QString successMessage = i18n("Directory encrypted.");
        if (m_stage == Stage::PersistConfig)
            successMessage = i18n("The fscrypt configuration was made persistent.");
        else if (m_stage == Stage::Lock)
            successMessage = i18n("Directory locked.");
        else if (m_stage == Stage::Unlock)
            successMessage = i18n("Directory unlocked.");
        else if (m_stage == Stage::CheckStatus)
            successMessage = i18n("The directory status could not be determined.");

        const QString message = success ? (output.isEmpty() ? successMessage : output) : formatErrorMessage(processMessage);

        finish(process, success, message);
    });

    process->start(program, processArguments);
    m_operationTimer->start();
    return true;
}

void Fscrypt::startConfigPersistence()
{
    const QString helper = trustedExecutableAt(QString::fromLatin1(FSCRYPT_CONFIG_PERSISTENCE_HELPER_PATH));
    if (helper.isEmpty())
    {
        fail(i18n("The fscrypt configuration persistence helper is not installed correctly."));
        return;
    }

    m_stage = Stage::PersistConfig;
    startTrustedProcess(helper, {}, {}, true);
}

void Fscrypt::startSetup()
{
    if (FscryptConfigPersistence::isOverlayrootActive())
    {
        if (!m_setupAuthorized)
        {
            fail(i18n("Administrator filesystem setup must be approved."));
            return;
        }

        if (!QFileInfo::exists(QStringLiteral("/etc/fscrypt.conf")))
        {
            m_stage = Stage::GlobalSetup;
            startProcess({QStringLiteral("setup"),
                          QStringLiteral("--quiet"),
                          QStringLiteral("--force"),
                          QStringLiteral("--all-users")},
                         {},
                         true);
            return;
        }

        startConfigPersistence();
        return;
    }

    if (!QFileInfo::exists(QStringLiteral("/etc/fscrypt.conf")))
    {
        if (!m_setupAuthorized)
        {
            fail(i18n("Administrator filesystem setup was not approved."));
            return;
        }

        m_stage = Stage::GlobalSetup;
        startProcess({QStringLiteral("setup"),
                      QStringLiteral("--quiet"),
                      QStringLiteral("--force"),
                      QStringLiteral("--all-users")},
                     {},
                     true);
        return;
    }

    startMountSetup();
}

void Fscrypt::startMountSetup()
{
    const QDir metadataDirectory(QDir(m_mountPoint).filePath(QStringLiteral(".fscrypt")));
    if (metadataDirectory.exists(QStringLiteral("policies")) && metadataDirectory.exists(QStringLiteral("protectors")))
    {
        startEncryption();
        return;
    }

    if (!m_setupAuthorized)
    {
        fail(i18n("Administrator filesystem setup was not approved."));
        return;
    }

    m_stage = Stage::MountSetup;
    startProcess({QStringLiteral("setup"),
                  m_mountPoint,
                  QStringLiteral("--quiet"),
                  QStringLiteral("--all-users")},
                 {},
                 !QFileInfo(m_mountPoint).isWritable());
}

void Fscrypt::startEncryption()
{
    const QString directoryPath = m_directory.toLocalFile();
    if (!validatePendingDirectory())
    {
        fail(i18n("The selected directory changed before encryption could start."));
        return;
    }

    m_stage = Stage::CheckStatus;
    startProcess({QStringLiteral("status"), directoryPath}, {}, false);
}

void Fscrypt::startEncryptionCommand()
{
    const QDir metadataDirectory(QDir(m_mountPoint).filePath(QStringLiteral(".fscrypt")));
    if (!QFileInfo(metadataDirectory.filePath(QStringLiteral("policies"))).isWritable()
        || !QFileInfo(metadataDirectory.filePath(QStringLiteral("protectors"))).isWritable())
    {
        fail(i18n("The fscrypt metadata on this filesystem is not writable by the current user."));
        return;
    }

    m_stage = Stage::Encrypt;
    QString passphrase = std::move(m_passphrase);
    startProcess({QStringLiteral("encrypt"),
                  m_directory.toLocalFile(),
                  QStringLiteral("--quiet"),
                  QStringLiteral("--source=custom_passphrase"),
                  QStringLiteral("--name=%1").arg(m_protectorName)},
                 std::move(passphrase),
                 false);
}

bool Fscrypt::validatePendingDirectory() const
{
    const QString path = m_directory.toLocalFile();
    const QFileInfo directoryInfo(path);
    quint64 device = 0;
    quint64 inode = 0;
    return directoryInfo.isDir()
           && !directoryInfo.isSymLink()
           && directoryInfo.isWritable()
           && directoryInfo.canonicalFilePath() == path
           && directoryIdentity(path, device, inode)
           && device == m_directoryDevice
           && inode == m_directoryInode
           && directoryIsEmpty(path);
}

void Fscrypt::encryptDirectory(const QUrl &directory, const QString &protectorName, const QString &passphrase)
{
    encryptDirectory(directory, protectorName, passphrase, false);
}

void Fscrypt::encryptDirectory(const QUrl &directory,
                               const QString &protectorName,
                               const QString &passphrase,
                               bool authorizeSetup)
{
    if (m_process || m_running)
    {
        Q_EMIT finished(false, i18n("Another encryption operation is already running."));
        return;
    }

    if (!directory.isLocalFile() || !QFileInfo(directory.toLocalFile()).isDir())
    {
        Q_EMIT finished(false, i18n("The selected directory is not available locally."));
        return;
    }

    const QString environmentError = availabilityMessage(directory);
    if (!environmentError.isEmpty())
    {
        Q_EMIT finished(false, environmentError);
        return;
    }

    if (requiresSetup(directory) && !authorizeSetup)
    {
        Q_EMIT finished(false, i18n("Administrator filesystem setup must be approved before encrypting this directory."));
        return;
    }

    const QString name = protectorName.trimmed();
    if (name.isEmpty())
    {
        Q_EMIT finished(false, i18n("Protector name can not be empty."));
        return;
    }

    if (passphrase.isEmpty())
    {
        Q_EMIT finished(false, i18n("Passphrase can not be empty."));
        return;
    }

    const QFileInfo directoryInfo(directory.toLocalFile());
    const QString directoryPath = directoryInfo.canonicalFilePath();
    quint64 device = 0;
    quint64 inode = 0;
    if (directoryPath.isEmpty() || !directoryIdentity(directoryPath, device, inode))
    {
        Q_EMIT finished(false, i18n("The selected directory is not available locally."));
        return;
    }

    if (!directoryIsEmpty(directoryPath))
    {
        Q_EMIT finished(false, i18n("The directory must be empty before it can be encrypted."));
        return;
    }

    const QStorageInfo storage(directoryPath);
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
    {
        Q_EMIT finished(false, i18n("The filesystem containing the directory is not available."));
        return;
    }

    m_directory = QUrl::fromLocalFile(directoryPath);
    m_mountPoint = QDir::cleanPath(storage.rootPath());
    m_protectorName = name;
    m_passphrase = passphrase;
    m_directoryDevice = device;
    m_directoryInode = inode;
    m_setupAuthorized = authorizeSetup;
    m_stage = Stage::None;
    setRunning(true);
    startSetup();
}

void Fscrypt::createEncryptedDirectory(const QUrl &parentDirectory,
                                       const QString &directoryName,
                                       const QString &protectorName,
                                       const QString &passphrase)
{
    createEncryptedDirectory(parentDirectory, directoryName, protectorName, passphrase, false);
}

void Fscrypt::createEncryptedDirectory(const QUrl &parentDirectory,
                                       const QString &directoryName,
                                       const QString &protectorName,
                                       const QString &passphrase,
                                       bool authorizeSetup)
{
    if (m_process || m_running)
    {
        Q_EMIT finished(false, i18n("Another encryption operation is already running."));
        return;
    }

    if (!parentDirectory.isLocalFile() || !QFileInfo(parentDirectory.toLocalFile()).isDir())
    {
        Q_EMIT finished(false, i18n("The destination directory is not available locally."));
        return;
    }

    const QString environmentError = availabilityMessage(parentDirectory);
    if (!environmentError.isEmpty())
    {
        Q_EMIT finished(false, environmentError);
        return;
    }

    if (requiresSetup(parentDirectory) && !authorizeSetup)
    {
        Q_EMIT finished(false, i18n("Administrator filesystem setup must be approved before creating an encrypted directory."));
        return;
    }

    const QString name = directoryName.trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char('/')))
    {
        Q_EMIT finished(false, i18n("Enter a valid directory name."));
        return;
    }

    if (protectorName.trimmed().isEmpty())
    {
        Q_EMIT finished(false, i18n("Protector name can not be empty."));
        return;
    }

    if (passphrase.isEmpty())
    {
        Q_EMIT finished(false, i18n("Passphrase can not be empty."));
        return;
    }

    if (trustedSystemExecutable(QStringLiteral("fscrypt")).isEmpty())
    {
        Q_EMIT finished(false, i18n("A trusted fscrypt executable is not installed."));
        return;
    }

    QDir parent(parentDirectory.toLocalFile());
    if (!parent.mkdir(name))
    {
        Q_EMIT finished(false, i18n("A directory with this name already exists or could not be created."));
        return;
    }

    encryptDirectory(QUrl::fromLocalFile(parent.filePath(name)), protectorName, passphrase, authorizeSetup);
}

void Fscrypt::lockDirectory(const QUrl &directory)
{
    if (m_process || m_running)
    {
        Q_EMIT finished(false, i18n("Another encryption operation is already running."));
        return;
    }

    if (!directory.isLocalFile() || !QFileInfo(directory.toLocalFile()).isDir())
    {
        Q_EMIT finished(false, i18n("The selected directory is not available locally."));
        return;
    }

    const QString environmentError = availabilityMessage(directory);
    if (!environmentError.isEmpty())
    {
        Q_EMIT finished(false, environmentError);
        return;
    }

    m_directory = QUrl::fromLocalFile(QFileInfo(directory.toLocalFile()).canonicalFilePath());
    m_stage = Stage::Lock;
    setRunning(true);
    startProcess({QStringLiteral("lock"), m_directory.toLocalFile(), QStringLiteral("--quiet")}, {}, false);
}

void Fscrypt::unlockDirectory(const QUrl &directory, const QString &passphrase)
{
    if (m_process || m_running)
    {
        Q_EMIT finished(false, i18n("Another encryption operation is already running."));
        return;
    }

    if (!directory.isLocalFile() || !QFileInfo(directory.toLocalFile()).isDir())
    {
        Q_EMIT finished(false, i18n("The selected directory is not available locally."));
        return;
    }

    const QString environmentError = availabilityMessage(directory);
    if (!environmentError.isEmpty())
    {
        Q_EMIT finished(false, environmentError);
        return;
    }

    if (passphrase.isEmpty())
    {
        Q_EMIT finished(false, i18n("Passphrase can not be empty."));
        return;
    }

    m_directory = QUrl::fromLocalFile(QFileInfo(directory.toLocalFile()).canonicalFilePath());
    m_stage = Stage::Unlock;
    setRunning(true);
    startProcess({QStringLiteral("unlock"), m_directory.toLocalFile(), QStringLiteral("--quiet")}, QString(passphrase), false);
}
