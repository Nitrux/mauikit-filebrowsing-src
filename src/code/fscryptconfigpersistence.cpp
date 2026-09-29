// SPDX-FileCopyrightText: 2026 Nitrux Latinoamericana S.C.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "fscryptconfigpersistence.h"

#include <QFile>
#include <QIODevice>

namespace
{
bool hasOverlayrootKernelParameter()
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QList<QByteArray> arguments = cmdline.readAll().simplified().split(' ');
    for (const auto &argument : arguments)
    {
        if (argument == QByteArrayLiteral("overlayroot")
            || (argument.startsWith(QByteArrayLiteral("overlayroot="))
                && argument != QByteArrayLiteral("overlayroot=disabled")))
        {
            return true;
        }
    }

    return false;
}
}

bool FscryptConfigPersistence::isOverlayrootActive()
{
    if (hasOverlayrootKernelParameter())
        return true;

    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    while (!mounts.atEnd())
    {
        const QList<QByteArray> fields = mounts.readLine().simplified().split(' ');
        if (fields.size() >= 3
            && fields.at(0) == QByteArrayLiteral("overlayroot")
            && fields.at(1) == QByteArrayLiteral("/")
            && fields.at(2) == QByteArrayLiteral("overlay"))
        {
            return true;
        }
    }

    return false;
}
