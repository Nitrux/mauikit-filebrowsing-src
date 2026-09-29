// SPDX-FileCopyrightText: 2026 Nitrux Latinoamericana S.C.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QTemporaryFile>

#include <cstdio>
#include <unistd.h>

namespace
{
constexpr auto ConfigPath = "/etc/fscrypt.conf";
constexpr auto OverlayrootChrootPath = "/usr/sbin/overlayroot-chroot";
constexpr auto InstallPath = "/usr/bin/install";
constexpr qint64 MaximumConfigSize = 1024 * 1024;
constexpr int ProcessStartTimeout = 5000;
constexpr int ProcessFinishTimeout = 30000;

int fail(const QString &message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
    return 1;
}

QString trustedExecutable(const QString &path)
{
    const QFileInfo info(path);
    const QFileDevice::Permissions unsafePermissions =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (!info.isFile()
        || !info.isExecutable()
        || info.ownerId() != 0
        || (info.permissions() & unsafePermissions))
    {
        return {};
    }

    return info.canonicalFilePath();
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 1)
        return fail(QStringLiteral("This helper does not accept arguments."));

    if (::geteuid() != 0)
        return fail(QStringLiteral("This helper must run with administrator privileges."));

    const QFileInfo sourceInfo(QString::fromLatin1(ConfigPath));
    const QFileDevice::Permissions unsafePermissions =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (!sourceInfo.isFile()
        || sourceInfo.isSymLink()
        || !sourceInfo.isReadable()
        || sourceInfo.ownerId() != 0
        || (sourceInfo.permissions() & unsafePermissions)
        || sourceInfo.size() <= 0
        || sourceInfo.size() > MaximumConfigSize)
    {
        return fail(QStringLiteral("The generated fscrypt configuration is not a trusted regular file."));
    }

    QFile source(QString::fromLatin1(ConfigPath));
    if (!source.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("The generated fscrypt configuration could not be read."));

    QTemporaryFile stagingFile(QStringLiteral("/run/mauikit-fscrypt-XXXXXX"));
    if (!stagingFile.open())
        return fail(QStringLiteral("A temporary fscrypt configuration file could not be created."));

    while (!source.atEnd())
    {
        const QByteArray data = source.read(64 * 1024);
        if (data.isEmpty() && source.error() != QFileDevice::NoError)
            return fail(QStringLiteral("The generated fscrypt configuration could not be read."));

        if (stagingFile.write(data) != data.size())
            return fail(QStringLiteral("The fscrypt configuration could not be staged for persistence."));
    }

    if (!stagingFile.flush())
        return fail(QStringLiteral("The staged fscrypt configuration could not be flushed."));

    const QString overlayrootChroot = trustedExecutable(QString::fromLatin1(OverlayrootChrootPath));
    const QString install = trustedExecutable(QString::fromLatin1(InstallPath));
    if (overlayrootChroot.isEmpty() || install.isEmpty())
        return fail(QStringLiteral("NX Overlayroot tools are not installed correctly."));

    QProcess process;
    process.setProgram(overlayrootChroot);
    process.setArguments({install,
                          QStringLiteral("-D"),
                          QStringLiteral("-o"), QStringLiteral("0"),
                          QStringLiteral("-g"), QStringLiteral("0"),
                          QStringLiteral("-m"), QStringLiteral("0644"),
                          QStringLiteral("--"),
                          stagingFile.fileName(),
                          QString::fromLatin1(ConfigPath)});
    process.start();

    if (!process.waitForStarted(ProcessStartTimeout))
        return fail(QStringLiteral("NX Overlayroot could not be started: %1").arg(process.errorString()));

    if (!process.waitForFinished(ProcessFinishTimeout))
    {
        process.terminate();
        if (!process.waitForFinished(ProcessStartTimeout))
        {
            process.kill();
            process.waitForFinished(ProcessStartTimeout);
        }
        return fail(QStringLiteral("Persisting the fscrypt configuration timed out."));
    }

    const QString standardOutput = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString standardError = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        return fail(standardError.isEmpty()
                        ? QStringLiteral("NX Overlayroot could not persist the fscrypt configuration.")
                        : standardError);
    }

    if (!standardOutput.isEmpty())
        std::fprintf(stdout, "%s\n", qPrintable(standardOutput));

    return 0;
}
