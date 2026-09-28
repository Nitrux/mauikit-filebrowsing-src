// SPDX-FileCopyrightText: 2026 Nitrux Latinoamericana S.C.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "fscrypt.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

#include <KLocalizedString>

namespace
{
QString formatErrorMessage(QString message)
{
    message = message.trimmed();
    const QString errorPrefix = QStringLiteral("[ERROR]");
    if (!message.startsWith(errorPrefix))
        return message;

    message = message.mid(errorPrefix.size()).trimmed();
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
{
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

    if (!directory.isLocalFile() || !QFileInfo(directory.toLocalFile()).isDir())
        return i18n("The selected directory is not available locally.");

    const QStorageInfo storage(directory.toLocalFile());
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
        return i18n("The filesystem containing the directory is not available.");

    const auto filesystemType = storage.fileSystemType().toLower();
    if (filesystemType == QByteArrayLiteral("overlay") || filesystemType == QByteArrayLiteral("overlayfs"))
        return i18n("This directory is on OverlayFS, which does not support fscrypt. Choose a directory on a persistent, non-overlay filesystem.");

    return {};
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
    m_passphrase.clear();
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
    process->deleteLater();

    if (success && m_stage == Stage::GlobalSetup)
    {
        startMountSetup();
        return;
    }

    if (success && m_stage == Stage::MountSetup)
    {
        startEncryption();
        return;
    }

    m_stage = Stage::None;
    clearPendingOperation();
    setRunning(false);
    Q_EMIT finished(success, message);
}

bool Fscrypt::startProcess(const QStringList &arguments, const QString &passphrase, bool privileged)
{
    if (m_process)
    {
        fail(i18n("Another encryption operation is already running."));
        return false;
    }

    const auto executable = QStandardPaths::findExecutable(QStringLiteral("fscrypt"));
    if (executable.isEmpty())
    {
        fail(i18n("fscrypt is not installed."));
        return false;
    }

    QString program = executable;
    QStringList processArguments = arguments;
    if (privileged)
    {
        const auto pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty())
        {
            fail(i18n("PolicyKit is not installed."));
            return false;
        }

        program = pkexec;
        processArguments.prepend(executable);
    }

    auto *process = new QProcess(this);
    m_process = process;

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(process, false, process->errorString());
    });

    connect(process, &QProcess::started, this, [process, passphrase]() {
        if (!passphrase.isEmpty())
        {
            process->write(passphrase.toUtf8());
            process->write("\n");
        }

        process->closeWriteChannel();
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
        const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        const QString processMessage = !error.isEmpty() ? error : (!output.isEmpty() ? output : i18n("fscrypt failed."));
        const QString message = success ? (output.isEmpty() ? i18n("Directory encrypted.") : output) : formatErrorMessage(processMessage);

        finish(process, success, message);
    });

    process->start(program, processArguments);
    return true;
}

void Fscrypt::startSetup()
{
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
    const QDir metadataDirectory(QDir(m_mountPoint).filePath(QStringLiteral(".fscrypt")));
    const bool privileged = !QFileInfo(directoryPath).isWritable()
                            || !QFileInfo(metadataDirectory.filePath(QStringLiteral("policies"))).isWritable()
                            || !QFileInfo(metadataDirectory.filePath(QStringLiteral("protectors"))).isWritable();

    m_stage = Stage::Encrypt;
    startProcess({QStringLiteral("encrypt"),
                  directoryPath,
                  QStringLiteral("--quiet"),
                  QStringLiteral("--source=custom_passphrase"),
                  QStringLiteral("--name=%1").arg(m_protectorName)},
                 m_passphrase,
                 privileged);
}

void Fscrypt::encryptDirectory(const QUrl &directory, const QString &protectorName, const QString &passphrase)
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

    const QDir target(directory.toLocalFile());
    if (!target.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System).isEmpty())
    {
        Q_EMIT finished(false, i18n("The directory must be empty before it can be encrypted."));
        return;
    }

    const QStorageInfo storage(directory.toLocalFile());
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
    {
        Q_EMIT finished(false, i18n("The filesystem containing the directory is not available."));
        return;
    }

    m_directory = directory;
    m_mountPoint = QDir::cleanPath(storage.rootPath());
    m_protectorName = name;
    m_passphrase = passphrase;
    m_stage = Stage::None;
    setRunning(true);
    startSetup();
}

void Fscrypt::createEncryptedDirectory(const QUrl &parentDirectory,
                                       const QString &directoryName,
                                       const QString &protectorName,
                                       const QString &passphrase)
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

    if (QStandardPaths::findExecutable(QStringLiteral("fscrypt")).isEmpty())
    {
        Q_EMIT finished(false, i18n("fscrypt is not installed."));
        return;
    }

    QDir parent(parentDirectory.toLocalFile());
    if (!parent.mkdir(name))
    {
        Q_EMIT finished(false, i18n("A directory with this name already exists or could not be created."));
        return;
    }

    encryptDirectory(QUrl::fromLocalFile(parent.filePath(name)), protectorName, passphrase);
}
