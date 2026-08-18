/*
 *   Copyright 2018 Camilo Higuita <milo.h@aol.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "tagdb.h"

#include <QDebug>
#include <QDir>
#include <QList>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

TAGDB::TAGDB() : QObject(nullptr)
{
    QDir collectionDBPath_dir(TAG::TaggingPath);
    if (!collectionDBPath_dir.exists())
        collectionDBPath_dir.mkpath(QStringLiteral("."));

    this->name = QUuid::createUuid().toString();
    if (!FMH::fileExists(QUrl::fromLocalFile(TAG::TaggingPath + TAG::DBName))) {
        this->openDB(this->name);
        this->prepareCollectionDB();
    } else
        this->openDB(this->name);
}

TAGDB::~TAGDB()
{
    this->m_db.close();
}

void TAGDB::openDB(const QString &name)
{
    if (!QSqlDatabase::contains(name)) {
        this->m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        this->m_db.setDatabaseName(TAG::TaggingPath + TAG::DBName);
    }

    if (!this->m_db.isOpen() && !this->m_db.open())
        return;

    auto query = this->getQuery(QStringLiteral("PRAGMA synchronous=OFF"));
    query.exec();
}

void TAGDB::prepareCollectionDB() const
{
    QSqlQuery query(this->m_db);

    QFile file(QStringLiteral(":/script.sql"));

    if (!file.exists()) {
        QString log = QStringLiteral("Fatal error on build database. The file '");
        log.append(file.fileName() + QStringLiteral("' for database and tables creation query cannot be not found!"));
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    bool hasText;
    QString line;
    QByteArray readLine;
    QString cleanedLine;
    QStringList strings;

    while (!file.atEnd()) {
        hasText = false;
        line = QStringLiteral("");
        readLine = "";
        cleanedLine = QStringLiteral("");
        strings.clear();
        while (!hasText) {
            readLine = file.readLine();
            cleanedLine = QString::fromStdString(readLine.trimmed().toStdString());
            strings = cleanedLine.split(QStringLiteral("--"));
            cleanedLine = strings.at(0);
            if (!cleanedLine.startsWith(QStringLiteral("--")) && !cleanedLine.startsWith(QStringLiteral("DROP")) && !cleanedLine.isEmpty())
                line += cleanedLine;
            if (cleanedLine.endsWith(QStringLiteral(";")))
                break;
            if (cleanedLine.startsWith(QStringLiteral("COMMIT")))
                hasText = true;
        }
        if (!line.isEmpty()) {
            query.exec(line);

        }
    }
    file.close();
}

bool TAGDB::checkExistance(const QString &tableName, const QString &searchId, const QString &search) const
{
    const auto queryStr = QString(QStringLiteral("SELECT %1 FROM %2 WHERE %3 = \"%4\"")).arg(searchId, tableName, searchId, search);
    return this->checkExistance(queryStr);
}

bool TAGDB::checkExistance(const QString &queryStr) const
{
    auto query = this->getQuery(queryStr);

    if (query.exec() && query.next())
        return true;

    return false;
}

QSqlQuery TAGDB::getQuery(const QString &queryTxt) const
{
    QSqlQuery query(queryTxt, this->m_db);
    return query;
}

QSqlQuery TAGDB::getQuery() const
{
    QSqlQuery query(this->m_db);
    return query;
}

bool TAGDB::insert(const QString &tableName, const QVariantMap &insertData) const
{
    if (tableName.isEmpty()) {
        return false;

    } else if (insertData.isEmpty()) {
        return false;
    }

    QStringList strValues;
    QStringList fields = insertData.keys();
    QVariantList values = insertData.values();
    int totalFields = fields.size();
    for (int i = 0; i < totalFields; ++i)
        strValues.append(QStringLiteral("?"));

    QString sqlQueryString = QStringLiteral("INSERT INTO ") + tableName + QStringLiteral(" (") + QString(fields.join(QStringLiteral(","))) + QStringLiteral(") VALUES(") + QString(strValues.join(QStringLiteral(","))) + QStringLiteral(")");
    QSqlQuery query(this->m_db);
    query.prepare(sqlQueryString);

    int k = 0;

    for(const QVariant &value : values)
        query.bindValue(k++, value);

    return query.exec();
}

bool TAGDB::update(const QString &tableName, const FMH::MODEL &updateData, const QVariantMap &where) const
{
    if (tableName.isEmpty()) {
        return false;
    } else if (updateData.isEmpty()) {
        return false;
    }

    QStringList set;
    const auto updateKeys = updateData.keys();
    for (const auto &key : updateKeys)
    {
        set.append(FMH::MODEL_NAME[key] + QStringLiteral(" = '") + updateData[key] + QStringLiteral("'"));
    }

    QStringList condition;
    const auto whereKeys = where.keys();
    for (const auto &key : whereKeys)
    {
         condition.append(key + QStringLiteral(" = '") + where[key].toString() + QStringLiteral("'"));
    }

    QString sqlQueryString = QStringLiteral("UPDATE ") + tableName + QStringLiteral(" SET ") + QString(set.join(QStringLiteral(","))) + QStringLiteral(" WHERE ") + QString(condition.join(QStringLiteral(",")));
    auto query = this->getQuery(sqlQueryString);
    return query.exec();
}

bool TAGDB::update(const QString &table, const QString &column, const QVariant &newValue, const QVariant &op, const QString &id) const
{
    auto queryStr = QString(QStringLiteral("UPDATE %1 SET %2 = \"%3\" WHERE %4 = \"%5\"")).arg(table, column, newValue.toString().replace(QStringLiteral("\""), QStringLiteral("\"\"")), op.toString(), id);
    auto query = this->getQuery(queryStr);
    return query.exec();
}

bool TAGDB::remove(const QString &tableName, const FMH::MODEL &removeData) const
{
    if (tableName.isEmpty()) {
        return false;

    } else if (removeData.isEmpty()) {
        return false;
    }

    QString strValues;
    auto i = 0;
    const auto keys = removeData.keys();
    for (const auto &key : keys) {
        strValues.append(QString(QStringLiteral("%1 = \"%2\"")).arg(FMH::MODEL_NAME[key], removeData[key]));
        i++;

        if (removeData.size() > 1 && i < removeData.size())
        {
            strValues.append(QStringLiteral(" AND "));
        }
    }

    QString sqlQueryString = QStringLiteral("DELETE FROM ") + tableName + QStringLiteral(" WHERE ") + strValues;

    return this->getQuery(sqlQueryString).exec();
}

const QSqlDatabase & TAGDB::db() const
{
    return this->m_db;
}
