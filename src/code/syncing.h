#pragma once

#include <QObject>
#include <QNetworkReply>

#include <QFile>

#include <MauiKit4/Core/fmh.h>

#include "filebrowsing_export.h"

class WebDAVClient;
class WebDAVReply;
/**
 *  Performs WebDAV listing, transfer, and directory operations.
 */
class FILEBROWSING_EXPORT Syncing : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Syncing)
    
public:
    enum SIGNAL_TYPE : uint_fast8_t { OPEN, DOWNLOAD, COPY, SAVE, CUT, DELETE, RENAME, MOVE, UPLOAD };

    /**
     *   Local files waiting to be uploaded.
     */
    QStringList uploadQueue;

    /**
     *  Creates a WebDAV synchronization helper.
     * @param parent
     */
    explicit Syncing(QObject *parent = nullptr);

    /**
     *  Lists remote entries below a WebDAV path.
     * @param path
     * @param filters
     * @param depth
     */
    void listContent(const QUrl &path, const QStringList &filters, const int &depth = 1);

    /**
     *  Sets the WebDAV endpoint and login credentials.
     * @param server
     * @param user
     * @param password
     */
    void setCredentials(const QString &server, const QString &user, const QString &password);

    /**
     *  Downloads a remote file into the configured destination.
     * @param path
     */
    void download(const QUrl &path);

    /**
     *  Uploads a local file to a remote path.
     * @param path
     * @param filePath
     */
    void upload(const QUrl &path, const QUrl &filePath);

    /**
     *  Creates a directory below a remote path.
     * @param path
     * @param name
     */
    void createDir(const QUrl &path, const QString &name);

    /**
     *  Resolves a remote item for the requested follow-up operation.
     * @param item
     * @param signalType
     */
    void resolveFile(const FMH::MODEL &item, const Syncing::SIGNAL_TYPE &signalType);

    /**
     *  Sets the local destination used by download operations.
     * @param path
     */
    void setCopyTo(const QUrl &path);

    /**
     *  Returns the configured local download destination.
     * @return
     */
    QUrl getCopyTo() const;

    /**
     *  Returns the configured WebDAV user name.
     * @return
     */
    QString getUser() const;

    /**
     *  Replaces the queue of local files waiting for upload.
     * @param list
     */
    void setUploadQueue(const QStringList &list);

    /**
     *  Converts a cached local URL back to its cloud-relative path.
     * @param url
     * @return
     */
    QString localToAbstractCloudPath(const QString &url);

private:
    WebDAVClient *client;
    QString host = QStringLiteral("https://cloud.opendesktop.cc/remote.php/webdav/");
    QString user = QStringLiteral("mauitest");
    QString password = QStringLiteral("mauitest");
    void listDirOutputHandler(WebDAVReply *reply, const QStringList &filters = QStringList());

    void saveTo(const QByteArray &array, const QUrl &path);
    QString saveToCache(const QString &file, const QUrl &where);
    QUrl getCacheFile(const QUrl &path);

    QUrl currentPath;
    QUrl copyTo;

    void emitError(const QNetworkReply::NetworkError &err);

    SIGNAL_TYPE signalType;

    QFile mFile;

Q_SIGNALS:
    /**
     *  Emitted when a remote directory listing is available.
     * @param data
     * @param url
     */
    void listReady(FMH::MODEL_LIST data, QUrl url);

    /**
     *  Emitted when a requested remote item has been resolved.
     * @param item
     * @param url
     * @param signalType
     */
    void itemReady(FMH::MODEL item, QUrl url, Syncing::SIGNAL_TYPE &signalType);

    /**
     *  Emitted after a remote directory is created.
     * @param item
     * @param url
     */
    void dirCreated(FMH::MODEL item, QUrl url);

    /**
     *  Emitted after an upload completes.
     * @param item
     * @param url
     */
    void uploadReady(FMH::MODEL item, QUrl url);

    /**
     *  Reports a WebDAV or local I/O failure.
     * @param message
     */
    void error(QString message);

    /**
     *  Reports transfer progress as a percentage.
     * @param percent
     */
    void progress(int percent);
};
