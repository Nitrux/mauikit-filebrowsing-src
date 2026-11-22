/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2018  camilo <email>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "placeslist.h"
#include "tagging.h"

#include <algorithm>

#include <QDir>
#include <QDebug>
#include <QDBusConnection>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#ifdef KIO_AVAILABLE
#include <KFilePlacesModel>
#include <KProtocolInfo>
#include <Solid/Device>
#include <Solid/SolidNamespace>
#else
#include <MauiKit4/Core/appsettings.h>
#endif

#include <KLocalizedString>

#ifdef KIO_AVAILABLE
int mapPathType(const FMStatic::PATHTYPE_KEY& value)
{
    switch(value)
    {
    case FMStatic::PLACES_PATH: return KFilePlacesModel::GroupType::PlacesType;
    case FMStatic::REMOTE_PATH: return KFilePlacesModel::GroupType::RemoteType;
    case FMStatic::DRIVES_PATH: return KFilePlacesModel::GroupType::DevicesType;
    case FMStatic::REMOVABLE_PATH: return KFilePlacesModel::GroupType::RemovableDevicesType;
    case FMStatic::TAGS_PATH: return KFilePlacesModel::GroupType::TagsType;
    case FMStatic::UNKNOWN_TYPE: return KFilePlacesModel::GroupType::UnknownType;
    default: return value;
    }
}
#endif


#ifdef KIO_AVAILABLE
PlacesList::PlacesList(QObject *parent)
    : MauiList(parent)
    , model(new KFilePlacesModel(this))
#else
PlacesList::PlacesList(QObject *parent)
    : MauiList(parent)
    , model(nullptr)
#endif
{    
#ifdef KIO_AVAILABLE
    connect(this->model, &KFilePlacesModel::reloaded, this, &PlacesList::scheduleSetList);
    
    connect(this->model, &KFilePlacesModel::setupDone, this, [this](const QModelIndex &index, bool success) {
        const QString udi = this->model->deviceForIndex(index).udi();
        const QString path = this->model->url(index).toString();
        this->scheduleSetList();

        if (success)
        {
            QTimer::singleShot(0, this, [this, udi, path]() {
                Q_EMIT this->deviceSetupDone(udi, path);
            });
        }
    });
    connect(this->model, &KFilePlacesModel::rowsRemoved, this, &PlacesList::scheduleSetList);

    connect(this->model, &KFilePlacesModel::teardownDone, this, [this](const QModelIndex &index, Solid::ErrorType error, const QVariant &errorData) {
        qWarning() << "Device unmount completed:" << index << "error:" << static_cast<int>(error) << "details:" << errorData;
        const QString udi = this->model->deviceForIndex(index).udi();
        const QString path = this->model->url(index).toString();
        const QPersistentModelIndex persistentIndex(index);
        this->scheduleSetList();

        if (error == Solid::NoError)
        {
            QTimer::singleShot(0, this, [this, persistentIndex, udi, path]() {
                if (persistentIndex.isValid() && this->model->setupNeeded(persistentIndex))
                    Q_EMIT this->deviceUnavailable(udi, path);
            });
        }
    });

    connect(this->model, &KFilePlacesModel::errorMessage, this, [this](const QString &message) {
        qWarning() << "Device operation error:" << message;
        Q_EMIT this->operationError(message);
    });
    
    connect(this->model, &KFilePlacesModel::rowsInserted, [this](const QModelIndex, int, int) 
            {
                this->scheduleSetList();
                Q_EMIT this->bookmarksChanged();

                /*Q_EMIT this->preListChanged();
                 *
                 *        for (int i = first; i <= last; i++)
                 *        {
                 *            const QModelIndex index = model->index(i, 0);
                 *
                 *            if(this->groups.contains(model->groupType(index)))
                 *            {
                 *                this->list << getGroup(*this->model, static_cast<FMH::PATHTYPE_KEY>(model->groupType(index)));
            }
            }
            Q_EMIT this->postListChanged();	*/
            }); // TODO improve the usage of the model
#else
    connect(&AppSettings::global(), &AppSettings::settingChanged, [this](const QUrl, const QString &key, const QVariant, const QString &group) {
        if (key == QStringLiteral("BOOKMARKS") && group == QStringLiteral("PREFERENCES")) {
            this->setList();
            Q_EMIT this->bookmarksChanged();
        }
    });
#endif
}

void PlacesList::componentComplete()
{
    connect(this, &PlacesList::groupsChanged, this, &PlacesList::scheduleSetList);
    this->scheduleSetList();
}

const FMH::MODEL_LIST &PlacesList::items() const
{
    return this->list;
}

FMH::MODEL_LIST PlacesList::getGroup(const KFilePlacesModel &model, const FMStatic::PATHTYPE_KEY &type)
{
    FMH::MODEL_LIST res;
    
    if (type == FMStatic::PATHTYPE_KEY::QUICK_PATH) 
    {
        res << FMH::MODEL {{FMH::MODEL_KEY::PATH, FMStatic::PATHTYPE_URI[FMStatic::PATHTYPE_KEY::TAGS_PATH] + QStringLiteral("fav")}, {FMH::MODEL_KEY::ICON, QStringLiteral("love")}, {FMH::MODEL_KEY::LABEL, i18n("Favorite")}, {FMH::MODEL_KEY::TYPE, QStringLiteral("Quick")}};
        
        // #ifdef KIO_AVAILABLE
        //     res << FMH::MODEL {{FMH::MODEL_KEY::PATH, "recentdocuments:///"}, {FMH::MODEL_KEY::ICON, "view-media-recent"}, {FMH::MODEL_KEY::LABEL, "Recent"}, {FMH::MODEL_KEY::TYPE, "Quick"}};
        // #endif
        
        res << FMH::MODEL {{FMH::MODEL_KEY::PATH, QStringLiteral("tags:///")}, {FMH::MODEL_KEY::ICON, QStringLiteral("tag")}, {FMH::MODEL_KEY::LABEL, i18n("Tags")}, {FMH::MODEL_KEY::TYPE, QStringLiteral("Quick")}};
        
        return res;
    }
    
    if (type == FMStatic::PATHTYPE_KEY::PLACES_PATH) 
    {
        res << FMStatic::getDefaultPaths();
        return res;
    }

#ifdef KIO_AVAILABLE
    if ((type == FMStatic::PATHTYPE_KEY::DRIVES_PATH || type == FMStatic::PATHTYPE_KEY::REMOVABLE_PATH)
        && !QDBusConnection::systemBus().isConnected())
    {
        return res;
    }

    auto mappedType = mapPathType(type);
    
    const auto group = model.groupIndexes(static_cast<KFilePlacesModel::GroupType>(type == FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH ? mapPathType(FMStatic::PATHTYPE_KEY::PLACES_PATH) : mappedType));
    
    res << std::accumulate(group.constBegin(), group.constEnd(), FMH::MODEL_LIST(), [&model, &type, this](FMH::MODEL_LIST &list, const QModelIndex &index) -> FMH::MODEL_LIST 
                           {
                               const QUrl url = model.url(index);
                               if (type == FMStatic::PATHTYPE_KEY::REMOTE_PATH && !KProtocolInfo::isKnownProtocol(url))
                                   return list;

                               if (type == FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH && FMStatic::defaultPaths.contains(url.toString()))
                                   return list;

                               auto data = FMH::MODEL {{FMH::MODEL_KEY::PATH, url.toString()},
                                                      {FMH::MODEL_KEY::URL, url.toString()},
                                                      {FMH::MODEL_KEY::ICON, model.icon(index).name()},
                                                      {FMH::MODEL_KEY::LABEL, model.text(index)},
                                                      {FMH::MODEL_KEY::NAME, model.text(index)},
                                                      {FMH::MODEL_KEY::TYPE, type == FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH ? FMStatic::PathTypeLabel(FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH) : FMStatic::PathTypeLabel(type)}};

                               if(model.isDevice(index))
                               {
                                   const auto udi =  model.deviceForIndex(index).udi();

                                   data.insert(FMH::MODEL_KEY::UDI, udi);
                                   m_devices.insert(udi, index);
                               }

                               list << data;
                               return list;
                           });

#else
    Q_UNUSED(model)
    switch (type) {
    case (FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH):
    {
        auto bookmarks = AppSettings::global().load(QStringLiteral("BOOKMARKS"), QStringLiteral("PREFERENCES"), {}).toStringList();

        res << FMStatic::packItems(bookmarks, FMStatic::PathTypeLabel(FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH));

        break;
    }
    case (FMStatic::PATHTYPE_KEY::DRIVES_PATH):
        res = FMStatic::getDevices();
        break;
    default:
        break;
    }

#endif
    
    return res;
}

void PlacesList::scheduleSetList()
{
    if (this->listUpdatePending)
        return;

    this->listUpdatePending = true;
    QTimer::singleShot(0, this, [this]()
    {
        this->listUpdatePending = false;
        this->setList();
    });
}

void PlacesList::setList()
{
    const auto previousList = this->list;
    const auto previousDevices = this->m_devices;
    FMH::MODEL_LIST updatedList;
    this->m_devices.clear();
    
    if (!this->groups.isEmpty())
    {
        for (const auto &group : std::as_const(this->groups))
        {
            switch (group.toInt()) {
            case FMStatic::PATHTYPE_KEY::PLACES_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::PLACES_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::BOOKMARKS_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::QUICK_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::QUICK_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::DRIVES_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::DRIVES_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::REMOTE_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::REMOTE_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::REMOVABLE_PATH:
                updatedList << getGroup(*this->model, FMStatic::PATHTYPE_KEY::REMOVABLE_PATH);
                break;

            case FMStatic::PATHTYPE_KEY::TAGS_PATH:
                updatedList << Tagging::getInstance()->getTags();
                break;
            }
        }
    }
    
    if (this->list == updatedList)
        return;

    QList<QPair<QString, QString>> removedDevices;
    for (const auto &previousPlace : previousList)
    {
        if (previousPlace[FMH::MODEL_KEY::TYPE] != FMStatic::PathTypeLabel(FMStatic::PATHTYPE_KEY::REMOVABLE_PATH))
            continue;

        const auto udi = previousPlace[FMH::MODEL_KEY::UDI];
        if (udi.isEmpty() || !previousDevices.contains(udi) || previousDevices.value(udi).isValid())
            continue;

        const bool stillPresent = std::any_of(updatedList.cbegin(), updatedList.cend(), [&udi](const FMH::MODEL &place) {
            return place[FMH::MODEL_KEY::UDI] == udi;
        });
        if (!stillPresent)
            removedDevices.append({udi, previousPlace[FMH::MODEL_KEY::PATH]});
    }

    Q_EMIT this->preListChanged();
    this->list = std::move(updatedList);
    Q_EMIT this->postListChanged();
    Q_EMIT this->countChanged();

    for (const auto &device : std::as_const(removedDevices))
        Q_EMIT this->deviceUnavailable(device.first, device.second);
}

QVariantList PlacesList::getGroups() const
{
    return this->groups;
}

void PlacesList::setGroups(const QVariantList &value)
{
    if (this->groups == value)
        return;
    
    this->groups = value;
    Q_EMIT this->groupsChanged();
}

void PlacesList::removePlace(const int &index)
{
    if (index >= this->list.size() || index < 0)
        return;

#ifdef KIO_AVAILABLE
    Q_EMIT this->preItemRemoved(index);
    this->model->removePlace(this->model->closestItem(QUrl(this->list.at(index)[FMH::MODEL_KEY::PATH])));
    this->list.removeAt(index);
    Q_EMIT this->postItemRemoved();
#else
    auto bookmarks = AppSettings::global().load(QStringLiteral("BOOKMARKS"), QStringLiteral("PREFERENCES"), {}).toStringList();
    bookmarks.removeOne(this->list.at(index)[FMH::MODEL_KEY::PATH]);
    AppSettings::global().save(QStringLiteral("BOOKMARKS"), bookmarks, QStringLiteral("PREFERENCES"));
#endif
    Q_EMIT bookmarksChanged();
}

bool PlacesList::contains(const QUrl &path)
{
    return this->exists(FMH::MODEL_KEY::PATH, path.toString());
}

bool PlacesList::isDevice(const int &index)
{
    if (index >= this->list.size() || index < 0)
        return false;

#ifdef KIO_AVAILABLE
    const auto item = this->list.at(index);
    return m_devices.contains(item[FMH::MODEL_KEY::UDI]);
#endif
    
    return false;
}

bool PlacesList::setupNeeded(const int &index)
{
    if (index >= this->list.size() || index < 0)
        return false;

#ifdef KIO_AVAILABLE
    const auto item = this->list.at(index);
    if(m_devices.contains(item[FMH::MODEL_KEY::UDI]))
    {
        return this->model->setupNeeded(m_devices.value(item[FMH::MODEL_KEY::UDI]));
    }
#endif
    
    return false;
}

void PlacesList::requestEject(const int &index)
{
    if (index >= this->list.size() || index < 0)
        return;

#ifdef KIO_AVAILABLE
    const auto item = this->list.at(index);
    if(m_devices.contains(item[FMH::MODEL_KEY::UDI]))
    {
        this->model->requestEject(m_devices.value(item[FMH::MODEL_KEY::UDI]));
    }
#endif
}

void PlacesList::requestSetup(const int &index)
{
    if (index >= this->list.size() || index < 0)
        return;

#ifdef KIO_AVAILABLE
    const auto item = this->list.at(index);
    if(m_devices.contains(item[FMH::MODEL_KEY::UDI]))
    {
        this->model->requestSetup(m_devices.value(item[FMH::MODEL_KEY::UDI]));
    }
#endif
}

void PlacesList::requestTeardown(const int &index)
{
    if (index >= this->list.size() || index < 0)
    {
        qWarning() << "Device unmount requested with invalid list index:" << index;
        return;
    }

#ifdef KIO_AVAILABLE
    const auto item = this->list.at(index);
    if(m_devices.contains(item[FMH::MODEL_KEY::UDI]))
    {
        const auto deviceIndex = m_devices.value(item[FMH::MODEL_KEY::UDI]);
        qWarning() << "Requesting device unmount:" << item[FMH::MODEL_KEY::UDI] << "model index:" << deviceIndex;
        this->model->requestTeardown(deviceIndex);
    } else
    {
        qWarning() << "Device unmount requested without a matching device:" << index << item[FMH::MODEL_KEY::UDI];
    }
#endif
}

void PlacesList::addBookmark(const QUrl& url)
{
#ifdef KIO_AVAILABLE
    KFilePlacesModel model;
    model.addPlace(QDir(url.toLocalFile()).dirName(), url, FMStatic::getIconName(url));
#else
  // do android stuff until cmake works with android
    if (FMStatic::isDefaultPath(url.toString()))
        return;

    auto bookmarks = AppSettings::global().load(QStringLiteral("BOOKMARKS"), QStringLiteral("PREFERENCES"), {}).toStringList();
    if(bookmarks.contains(url.toString()))
        return;

    bookmarks << url.toString();
    AppSettings::global().save(QStringLiteral("BOOKMARKS"), bookmarks, QStringLiteral("PREFERENCES"));
#endif
}

int PlacesList::indexOfPath(const QUrl &url) const
{
    return this->indexOf(FMH::MODEL_KEY::PATH, url.toString());
}

void PlacesList::toggleSection(const int& section)
{
    if(this->groups.contains(section))
    {
        this->groups.removeAll(section);
    }else
    {
        this->groups.append(section);
    }
    
    Q_EMIT this->groupsChanged();
}

bool PlacesList::containsGroup(const int& group)
{
    return this->groups.contains(group);
}
