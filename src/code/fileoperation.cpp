#include "fileoperation.h"

#include <QDir>
#include <QRegularExpression>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef KIO_AVAILABLE
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/Global>
#include <KIO/Job>
#include <KJob>
#include <QDirIterator>
#include <QFileInfo>
#include <QTimer>
#endif

#include <QVariantMap>
#include <utility>

namespace
{
QString displayUrl(const QUrl &url)
{
    if (url.isLocalFile())
        return url.toLocalFile();

    QUrl safeUrl = url;
    safeUrl.setUserName(QString());
    safeUrl.setPassword(QString());
    return safeUrl.toDisplayString();
}

QString sanitizedErrorMessage(QString message)
{
    static const QRegularExpression userInfoPattern(QStringLiteral(R"(([A-Za-z][A-Za-z0-9+.-]*://)[^/@\s]+@)"));
    message.replace(userInfoPattern, QStringLiteral("\\1"));
    return message;
}

QString sourceNameForUrls(const QList<QUrl> &urls)
{
    if (urls.size() != 1)
        return {};

    const auto name = urls.constFirst().fileName();
    return name.isEmpty() ? displayUrl(urls.constFirst()) : name;
}

#ifdef KIO_AVAILABLE
bool openLocalFileWithoutFollowingSymlink(QFile &file, const QString &path, QIODevice::OpenMode mode, bool createNew)
{
#if defined(Q_OS_UNIX) && defined(O_NOFOLLOW)
    int flags = (mode & QIODevice::WriteOnly) ? O_RDWR : O_RDONLY;
    flags |= O_NOFOLLOW;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    if (createNew)
        flags |= O_CREAT | O_EXCL;

    const QByteArray encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(encodedPath.constData(), flags, 0600);
    if (descriptor < 0)
        return false;

    if (!file.open(descriptor, mode, QFileDevice::AutoCloseHandle))
    {
        ::close(descriptor);
        return false;
    }

    return true;
#else
    file.setFileName(path);
    return file.open(mode);
#endif
}

bool hasLocalPartialCopy(const QUrl &source, const QUrl &destination)
{
    if (!source.isLocalFile() || !destination.isLocalFile())
        return false;

    const QFileInfo sourceInfo(source.toLocalFile());
    if (sourceInfo.fileName().isEmpty())
        return false;

    const QDir destinationDir(destination.toLocalFile());
    return QFileInfo::exists(destinationDir.filePath(sourceInfo.fileName() + QStringLiteral(".part")));
}

void suspendKioCopyJob(KJob *job)
{
    if (job)
        job->suspend();
}

void disableKioUi(KJob *job)
{
    if (!job)
        return;

    job->setUiDelegate(nullptr);
    if (auto *kioJob = qobject_cast<KIO::Job *>(job))
        kioJob->setUiDelegateExtension(nullptr);
}
#endif

}

struct FileOperation::Operation
{
    int id = 0;
    QString kind;
    QString operation;
    QList<QUrl> urls;
    QUrl destinationUrl;
    QString targetName;
    QString sourceName;
    QString destination;
    int itemCount = 0;

    bool started = false;
    bool active = false;
    bool finished = false;
    bool failed = false;
    bool cancelRequested = false;
    bool paused = false;
    bool pauseSupported = false;
    bool conflictPending = false;
    bool overwriteExisting = false;
    QUrl conflictDestination;
    int progress = 0;
    qulonglong processedBytes = 0;
    qulonglong totalBytes = 0;
    qulonglong speed = 0;
    QString errorMessage;

#ifdef KIO_AVAILABLE
    KJob *job = nullptr;
    QTimer *localTimer = nullptr;
    QTimer *pauseTimer = nullptr;
    QFile localSource;
    QFile localDestination;
    QElapsedTimer localElapsed;
    QUrl localSourceUrl;
    QList<QUrl> localCopySources;
    QList<QString> localCopyDestinations;
    int localSourceIndex = 0;
    qulonglong localCompletedBytes = 0;
    qulonglong localCurrentSourceSize = 0;
    QString localDestinationPath;
    QString localTemporaryPath;
    bool localCopyActive = false;
#endif
};

FileOperation *FileOperation::s_instance = nullptr;

FileOperation::FileOperation(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

FileOperation::~FileOperation()
{
    for (auto *operation : std::as_const(m_operations))
    {
#ifdef KIO_AVAILABLE
        if (operation->localTimer)
            operation->localTimer->stop();
        if (operation->pauseTimer)
            operation->pauseTimer->stop();
        operation->localSource.close();
        operation->localDestination.close();
#endif
        delete operation;
    }

    if (s_instance == this)
        s_instance = nullptr;
}

FileOperation *FileOperation::instance()
{
    if (!s_instance)
        s_instance = new FileOperation;

    return s_instance;
}

bool FileOperation::startCopy(const QList<QUrl> &urls, const QUrl &destination, const QString &operation)
{
    return instance()->enqueueOperation(QStringLiteral("copy"), operation, urls, destination);
}

bool FileOperation::startMove(const QList<QUrl> &urls, const QUrl &destination, const QString &operation)
{
    return instance()->enqueueOperation(QStringLiteral("move"), operation, urls, destination);
}

bool FileOperation::startDelete(const QList<QUrl> &urls)
{
    return instance()->enqueueOperation(QStringLiteral("delete"), QStringLiteral("delete"), urls, {});
}

bool FileOperation::startTrash(const QList<QUrl> &urls)
{
    return instance()->enqueueOperation(QStringLiteral("trash"), QStringLiteral("trash"), urls, {});
}

bool FileOperation::enqueueOperation(const QString &kind, const QString &operationName, const QList<QUrl> &urls, const QUrl &destination)
{
    if (urls.isEmpty() || ((kind != QStringLiteral("delete") && kind != QStringLiteral("trash")) && destination.isEmpty()))
        return false;

#ifndef KIO_AVAILABLE
    Q_UNUSED(kind)
    Q_UNUSED(operationName)
    Q_UNUSED(urls)
    Q_UNUSED(destination)
    return false;
#else
    for (const auto *existing : std::as_const(m_operations))
    {
        if (existing->finished || existing->kind != kind || existing->destinationUrl != destination || existing->urls.size() != urls.size())
            continue;

        bool sameSources = true;
        for (const QUrl &url : urls)
        {
            if (!existing->urls.contains(url))
            {
                sameSources = false;
                break;
            }
        }

        if (sameSources)
            return false;
    }

    auto *operation = new Operation;
    operation->id = m_nextId++;
    operation->kind = kind;
    operation->operation = operationName;
    operation->urls = urls;
    operation->destinationUrl = destination;
    operation->destination = destination.isEmpty() ? QString() : displayUrl(destination);
    operation->itemCount = urls.count();
    operation->sourceName = sourceNameForUrls(urls);
    m_operations.append(operation);

    notifyOperationsChanged();
    Q_EMIT operationAdded(operation->id);
    startPendingOperations();
    return true;
#endif
}

void FileOperation::startPendingOperations()
{
#ifdef KIO_AVAILABLE
    while (true)
    {
        Operation *pending = nullptr;
        for (auto *operation : std::as_const(m_operations))
        {
            if (!operation->started && !operation->finished && !operation->conflictPending && !hasActiveConflict(operation))
            {
                pending = operation;
                break;
            }
        }

        if (!pending)
            return;

        QString errorMessage;
        if (startOperation(pending, &errorMessage))
            continue;

        if (pending->conflictPending)
        {
            notifyOperationsChanged();
            continue;
        }

        pending->started = true;
        pending->finished = true;
        pending->failed = true;
        pending->errorMessage = errorMessage.isEmpty() ? QStringLiteral("Could not start the operation") : errorMessage;
        Q_EMIT finished(false, pending->errorMessage);
        notifyOperationsChanged();
    }
#endif
}

bool FileOperation::startOperation(Operation *operation, QString *errorMessage)
{
#ifdef KIO_AVAILABLE
    if (operation->kind == QStringLiteral("copy"))
    {
        if (startLocalCopy(operation))
            return true;

        if (operation->conflictPending)
            return false;

        if (operation->urls.size() == 1 && hasLocalPartialCopy(operation->urls.constFirst(), operation->destinationUrl))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not resume the partial copy");
            return false;
        }

        bool allLocal = operation->destinationUrl.isLocalFile();
        for (const QUrl &url : operation->urls)
            allLocal = allLocal && url.isLocalFile();
        if (allLocal)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not safely copy the local files");
            return false;
        }
    }

    if (operation->kind == QStringLiteral("move") && checkLocalMoveConflict(operation))
        return false;

    KIO::JobFlags flags = KIO::HideProgressInfo;
    if (operation->overwriteExisting)
        flags |= KIO::Overwrite;

    KJob *job = nullptr;
    if (operation->kind == QStringLiteral("delete"))
        job = KIO::del(operation->urls, KIO::HideProgressInfo);
    else if (operation->kind == QStringLiteral("trash"))
        job = KIO::trash(operation->urls, KIO::HideProgressInfo);
    else if (operation->kind == QStringLiteral("move"))
        job = KIO::move(operation->urls, operation->destinationUrl, flags);
    else
    {
        QUrl destination = operation->destinationUrl;
        if (!operation->targetName.isEmpty() && operation->urls.size() == 1)
        {
            QString path = destination.path();
            if (!path.endsWith(QLatin1Char('/')))
                path += QLatin1Char('/');
            path += operation->targetName;
            destination.setPath(path);
        }
        job = KIO::copy(operation->urls, destination, flags);
    }

    if (!job)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not create the file operation");
        return false;
    }

    disableKioUi(job);

    operation->job = job;
    operation->started = true;
    operation->active = true;
    operation->paused = false;
    operation->pauseSupported = true;
    operation->progress = 0;
    operation->processedBytes = 0;
    operation->totalBytes = 0;
    operation->speed = 0;

    Q_EMIT started(operation->operation, operation->itemCount, operation->destination);
    notifyOperationsChanged();

    if (auto *copyJob = qobject_cast<KIO::CopyJob *>(job))
    {
        QObject::connect(copyJob, &KIO::CopyJob::copyingDone, this, [this, operation](auto *, const QUrl &from, const QUrl &to, const auto &, bool, bool) {
            if (operation->active)
                Q_EMIT itemFinished(operation->operation, displayUrl(from), displayUrl(to));
        });
    }

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), this, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), this, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), this, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), this, SLOT(onSpeed(KJob*, unsigned long)));
    QObject::connect(job, &KJob::result, this, [this, operation](KJob *finishedJob) {
        finishKioOperation(operation, finishedJob);
    });

    job->start();
    return true;
#else
    Q_UNUSED(operation)
    Q_UNUSED(errorMessage)
    return false;
#endif
}

#ifdef KIO_AVAILABLE
bool FileOperation::checkLocalMoveConflict(Operation *operation)
{
    if (!operation || operation->kind != QStringLiteral("move") || operation->urls.isEmpty()
            || !operation->destinationUrl.isLocalFile())
        return false;

    if (operation->operation == QStringLiteral("rename"))
    {
        if (operation->urls.size() != 1 || !operation->urls.constFirst().isLocalFile())
            return false;

        const QFileInfo sourceInfo(operation->urls.constFirst().toLocalFile());
        const QFileInfo destinationInfo(operation->destinationUrl.toLocalFile());
        if (!sourceInfo.exists() || !destinationInfo.exists()
                || QDir::cleanPath(sourceInfo.absoluteFilePath()) == QDir::cleanPath(destinationInfo.absoluteFilePath())
                || operation->overwriteExisting)
            return false;

        operation->conflictPending = true;
        operation->conflictDestination = operation->destinationUrl;
        return true;
    }

    const QDir destinationDir(operation->destinationUrl.toLocalFile());
    if (!destinationDir.exists())
        return false;

    for (const QUrl &url : operation->urls)
    {
        if (!url.isLocalFile())
            return false;

        const QFileInfo sourceInfo(url.toLocalFile());
        if (!sourceInfo.exists())
            return false;

        const QFileInfo destinationInfo(destinationDir.filePath(sourceInfo.fileName()));
        if (destinationInfo.exists() && !operation->overwriteExisting)
        {
            operation->conflictPending = true;
            operation->conflictDestination = QUrl::fromLocalFile(destinationInfo.filePath());
            return true;
        }
    }

    return false;
}

bool FileOperation::startLocalCopy(Operation *operation)
{
    if (operation->kind != QStringLiteral("copy") || operation->urls.isEmpty())
        return false;

    if (!operation->destinationUrl.isLocalFile())
        return false;

    const QDir destinationDir(operation->destinationUrl.toLocalFile());
    if (!destinationDir.exists())
        return false;

    QList<QUrl> localSources;
    QList<QString> localDestinations;
    QList<QString> localDirectories;
    qulonglong totalBytes = 0;

    const auto addDirectory = [&localDirectories](const QString &path) {
        if (!localDirectories.contains(path))
            localDirectories.append(path);
    };

    const auto requestConflict = [operation](const QString &destination) {
        operation->conflictPending = true;
        operation->conflictDestination = QUrl::fromLocalFile(destination);
    };

    for (const QUrl &url : operation->urls)
    {
        if (!url.isLocalFile())
            return false;

        const QFileInfo sourceInfo(url.toLocalFile());
        if (!sourceInfo.exists() || sourceInfo.isSymLink() || (!sourceInfo.isFile() && !sourceInfo.isDir()))
            return false;

        const QString destinationRoot = destinationDir.filePath(operation->targetName.isEmpty() ? sourceInfo.fileName() : operation->targetName);
        const QFileInfo destinationRootInfo(destinationRoot);
        const QString sourcePath = QDir::cleanPath(sourceInfo.absoluteFilePath());
        const QString destinationPath = QDir::cleanPath(destinationRootInfo.absoluteFilePath());
        if (sourcePath == destinationPath
                || (sourceInfo.isDir() && destinationPath.startsWith(sourcePath + QDir::separator())))
            return false;

        if (sourceInfo.isDir())
        {
            if (destinationRootInfo.exists() && !destinationRootInfo.isDir())
                return false;

            addDirectory(destinationRoot);
            const QDir sourceDir(sourceInfo.filePath());
            QDirIterator iterator(sourceInfo.filePath(), QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (iterator.hasNext())
            {
                iterator.next();
                const QFileInfo info = iterator.fileInfo();
                if (info.isSymLink() || (!info.isFile() && !info.isDir()))
                    return false;

                const QString destinationPath = QDir(destinationRoot).filePath(sourceDir.relativeFilePath(info.filePath()));
                if (info.isDir())
                {
                    addDirectory(destinationPath);
                    continue;
                }

                if (localDestinations.contains(destinationPath))
                    return false;

                const QFileInfo destinationInfo(destinationPath);
                const bool temporaryExists = QFileInfo::exists(destinationPath + QStringLiteral(".part"));
                if (destinationInfo.exists() && !destinationInfo.isFile())
                    return false;
                if (destinationInfo.exists() && !temporaryExists && !operation->overwriteExisting)
                {
                    requestConflict(destinationPath);
                    return false;
                }

                localSources.append(QUrl::fromLocalFile(info.filePath()));
                localDestinations.append(destinationPath);
                totalBytes += static_cast<qulonglong>(info.size());
            }
            continue;
        }

        if (localDestinations.contains(destinationRoot))
            return false;

        const bool temporaryExists = QFileInfo::exists(destinationRoot + QStringLiteral(".part"));
        if (destinationRootInfo.exists() && !destinationRootInfo.isFile())
            return false;
        if (destinationRootInfo.exists() && !temporaryExists && !operation->overwriteExisting)
        {
            requestConflict(destinationRoot);
            return false;
        }

        localSources.append(url);
        localDestinations.append(destinationRoot);
        totalBytes += static_cast<qulonglong>(sourceInfo.size());
    }

    for (const QString &directory : std::as_const(localDirectories))
    {
        if (!QDir().mkpath(directory))
            return false;
    }

    operation->localCopySources = localSources;
    operation->localCopyDestinations = localDestinations;
    operation->localSourceIndex = 0;
    operation->localCompletedBytes = 0;
    operation->localCurrentSourceSize = 0;
    operation->processedBytes = 0;
    operation->totalBytes = totalBytes;
    operation->progress = 0;
    operation->speed = 0;

    if (!localSources.isEmpty() && !prepareLocalCopySource(operation))
        return false;

    operation->localCopyActive = true;
    operation->started = true;
    operation->active = true;
    operation->paused = false;
    operation->pauseSupported = true;

    operation->localTimer = new QTimer(this);
    operation->localTimer->setSingleShot(true);
    QObject::connect(operation->localTimer, &QTimer::timeout, this, [this, operation]() {
        processLocalCopy(operation);
    });

    Q_EMIT started(operation->operation, operation->itemCount, operation->destination);
    notifyOperationsChanged();
    operation->localElapsed.start();
    operation->localTimer->start(0);
    return true;
}

bool FileOperation::prepareLocalCopySource(Operation *operation)
{
    if (operation->localSourceIndex < 0 || operation->localSourceIndex >= operation->localCopySources.size())
        return false;

    const QUrl source = operation->localCopySources.at(operation->localSourceIndex);
    const QFileInfo sourceInfo(source.toLocalFile());
    const QString destinationPath = operation->localCopyDestinations.at(operation->localSourceIndex);
    const QString temporaryPath = destinationPath + QStringLiteral(".part");
    const QFileInfo destinationInfo(destinationPath);
    const QFileInfo temporaryInfo(temporaryPath);
    if (temporaryInfo.isSymLink() || destinationInfo.isSymLink())
        return false;
    const bool temporaryExists = temporaryInfo.exists();

    if (destinationInfo.exists() && !temporaryExists && !operation->overwriteExisting)
        return false;

    if (destinationInfo.exists() && (temporaryExists || operation->overwriteExisting) && !QFile::remove(destinationPath))
        return false;

    operation->localSource.close();
    operation->localDestination.close();
    operation->localSource.setFileName(sourceInfo.filePath());
    if (!openLocalFileWithoutFollowingSymlink(operation->localSource, sourceInfo.filePath(), QIODevice::ReadOnly, false))
        return false;

    operation->localDestinationPath = destinationPath;
    operation->localTemporaryPath = temporaryPath;
    operation->localDestination.setFileName(temporaryPath);

    QIODevice::OpenMode destinationMode = QIODevice::ReadWrite;
    if (!temporaryExists)
        destinationMode |= QIODevice::Truncate;

    if (!openLocalFileWithoutFollowingSymlink(operation->localDestination, temporaryPath, destinationMode, !temporaryExists))
    {
        operation->localSource.close();
        return false;
    }

    qint64 partialSize = temporaryExists ? operation->localDestination.size() : 0;
    if (partialSize > sourceInfo.size())
    {
        if (!operation->localDestination.resize(0))
        {
            operation->localSource.close();
            operation->localDestination.close();
            return false;
        }
        partialSize = 0;
    }

    if (!operation->localSource.seek(partialSize) || !operation->localDestination.seek(partialSize))
    {
        operation->localSource.close();
        operation->localDestination.close();
        return false;
    }

    operation->localSourceUrl = source;
    operation->localCurrentSourceSize = static_cast<qulonglong>(sourceInfo.size());
    operation->processedBytes = operation->localCompletedBytes + static_cast<qulonglong>(partialSize);
    operation->progress = operation->totalBytes > 0 ? static_cast<int>((operation->processedBytes * 100) / operation->totalBytes) : 0;
    return true;
}

void FileOperation::processLocalCopy(Operation *operation)
{
    if (!operation->localCopyActive || operation->paused || operation->cancelRequested)
        return;

    if (operation->localSourceIndex >= operation->localCopySources.size())
    {
        finishLocalCopy(operation, true, QString());
        return;
    }

    constexpr qint64 copyChunkSize = 1024 * 1024;
    const QByteArray data = operation->localSource.read(copyChunkSize);
    if (data.isEmpty())
    {
        if (operation->localSource.error() != QFile::NoError)
        {
            finishLocalCopy(operation, false, operation->localSource.errorString());
            return;
        }

        if (!operation->localDestination.flush())
        {
            finishLocalCopy(operation, false, operation->localDestination.errorString());
            return;
        }

        operation->localSource.close();
        operation->localDestination.close();
        if (!QFile::rename(operation->localTemporaryPath, operation->localDestinationPath))
        {
            finishLocalCopy(operation, false, QStringLiteral("Could not finalize the copied file"));
            return;
        }

        Q_EMIT itemFinished(operation->operation, displayUrl(operation->localSourceUrl), displayUrl(QUrl::fromLocalFile(operation->localDestinationPath)));
        operation->localCompletedBytes += operation->localCurrentSourceSize;
        ++operation->localSourceIndex;

        if (operation->localSourceIndex >= operation->localCopySources.size())
        {
            finishLocalCopy(operation, true, QString());
            return;
        }

        if (!prepareLocalCopySource(operation))
        {
            finishLocalCopy(operation, false, QStringLiteral("Could not prepare the next file"));
            return;
        }

        notifyOperationsChanged();
        operation->localTimer->start(0);
        return;
    }

    if (operation->localDestination.write(data) != data.size())
    {
        finishLocalCopy(operation, false, operation->localDestination.errorString());
        return;
    }

    operation->processedBytes += static_cast<qulonglong>(data.size());
    operation->progress = operation->totalBytes > 0 ? static_cast<int>((operation->processedBytes * 100) / operation->totalBytes) : 0;
    const qint64 elapsed = operation->localElapsed.elapsed();
    if (elapsed > 0)
        operation->speed = (operation->processedBytes * 1000) / static_cast<qulonglong>(elapsed);

    notifyOperationsChanged();
    operation->localTimer->start(0);
}

void FileOperation::finishLocalCopy(Operation *operation, bool success, const QString &errorMessage)
{
    if (operation->localTimer)
    {
        operation->localTimer->stop();
        operation->localTimer->deleteLater();
        operation->localTimer = nullptr;
    }

    operation->localSource.close();
    operation->localDestination.close();
    if (!success && !operation->localTemporaryPath.isEmpty())
        QFile::remove(operation->localTemporaryPath);

    operation->localCopyActive = false;
    if (success)
    {
        operation->processedBytes = operation->totalBytes;
        operation->progress = 100;
    }
    finishOperation(operation, success, errorMessage);
}

void FileOperation::finishKioOperation(Operation *operation, KJob *job)
{
    if (!operation || !operation->active)
        return;

    operation->job = nullptr;
    const bool conflict = operation->kind != QStringLiteral("delete")
            && operation->kind != QStringLiteral("trash")
            && !operation->overwriteExisting
            && job
            && (job->error() == KIO::ERR_FILE_ALREADY_EXIST || job->error() == KIO::ERR_DIR_ALREADY_EXIST);
    if (conflict)
    {
        operation->active = false;
        operation->started = false;
        operation->paused = false;
        operation->pauseSupported = false;
        operation->conflictPending = true;
        operation->errorMessage.clear();

        if (operation->operation == QStringLiteral("rename"))
        {
            operation->conflictDestination = operation->destinationUrl;
        }
        else if (!operation->urls.isEmpty())
        {
            QUrl destination = operation->destinationUrl;
            QString path = destination.path();
            if (!path.endsWith(QLatin1Char(47)))
                path += QLatin1Char(47);
            path += operation->targetName.isEmpty() ? operation->urls.constFirst().fileName() : operation->targetName;
            destination.setPath(path);
            operation->conflictDestination = destination;
        }

        notifyOperationsChanged();
        startPendingOperations();
        return;
    }

    const bool success = !operation->cancelRequested && job && job->error() == 0;
    finishOperation(operation, success, success ? QString() : (job ? sanitizedErrorMessage(job->errorString()) : QString()));
}
#endif

void FileOperation::finishOperation(Operation *operation, bool success, const QString &errorMessage)
{
    if (!operation)
        return;

#ifdef KIO_AVAILABLE
    if (operation->pauseTimer)
    {
        operation->pauseTimer->stop();
        operation->pauseTimer->deleteLater();
        operation->pauseTimer = nullptr;
    }
#endif

    operation->active = false;
    operation->finished = true;
    operation->paused = false;
    operation->pauseSupported = false;
    operation->failed = !success && !operation->cancelRequested;
    operation->errorMessage = operation->failed ? errorMessage : QString();

    const bool remove = success || operation->cancelRequested;
    if (remove)
        removeOperation(operation);

    notifyOperationsChanged();
    Q_EMIT finished(success, errorMessage);
    startPendingOperations();
}

void FileOperation::refreshPrimaryOperation()
{
    Operation *primary = primaryOperation();
    setRunning(activeCount() > 0);
    setProgress(primary ? primary->progress : 0);
    setProcessedBytes(primary ? primary->processedBytes : 0);
    setTotalBytes(primary ? primary->totalBytes : 0);
    setSpeed(primary ? primary->speed : 0);
    const QString operationName = primary ? primary->operation : QString();
    if (m_operation != operationName)
    {
        m_operation = operationName;
        Q_EMIT operationChanged();
    }
    const QString destinationName = primary ? primary->destination : QString();
    if (m_destination != destinationName)
    {
        m_destination = destinationName;
        Q_EMIT destinationChanged();
    }
    const QString sourceName = primary ? primary->sourceName : QString();
    if (m_sourceName != sourceName)
    {
        m_sourceName = sourceName;
        Q_EMIT sourceNameChanged();
    }
    setPaused(primary ? primary->paused : false);
    setPauseSupported(primary ? primary->pauseSupported : false);
}

void FileOperation::notifyOperationsChanged()
{
    refreshPrimaryOperation();
    Q_EMIT operationsChanged();
    Q_EMIT queueChanged();
}

void FileOperation::removeOperation(Operation *operation)
{
    if (!operation)
        return;

#ifdef KIO_AVAILABLE
    if (operation->localTimer)
    {
        operation->localTimer->stop();
        operation->localTimer->deleteLater();
        operation->localTimer = nullptr;
    }
    if (operation->pauseTimer)
    {
        operation->pauseTimer->stop();
        operation->pauseTimer->deleteLater();
        operation->pauseTimer = nullptr;
    }
    operation->localSource.close();
    operation->localDestination.close();
#endif
    m_operations.removeOne(operation);
    delete operation;
}

FileOperation::Operation *FileOperation::findOperation(int id) const
{
    for (auto *operation : m_operations)
    {
        if (operation->id == id)
            return operation;
    }
    return nullptr;
}

FileOperation::Operation *FileOperation::primaryOperation() const
{
    for (auto *operation : m_operations)
    {
        if (operation->active)
            return operation;
    }

    for (auto *operation : m_operations)
    {
        if (!operation->finished)
            return operation;
    }

    return m_operations.isEmpty() ? nullptr : m_operations.constFirst();
}

QList<QUrl> FileOperation::operationTargets(const Operation *operation) const
{
    if (!operation)
        return {};

    if (operation->kind == QStringLiteral("delete") || operation->kind == QStringLiteral("trash"))
        return operation->urls;

    if (operation->operation == QStringLiteral("rename"))
        return {operation->destinationUrl};

#ifdef KIO_AVAILABLE
    if (operation->active && !operation->localCopyDestinations.isEmpty())
    {
        QList<QUrl> targets;
        for (const QString &path : operation->localCopyDestinations)
            targets.append(QUrl::fromLocalFile(path));
        return targets;
    }
#endif

    QList<QUrl> targets;
    for (const QUrl &url : operation->urls)
    {
        QUrl target = operation->destinationUrl;
        QString path = target.path();
        if (!path.endsWith(QLatin1Char('/')))
            path += QLatin1Char('/');
        path += operation->targetName.isEmpty() ? url.fileName() : operation->targetName;
        target.setPath(path);
        targets.append(target);
    }
    return targets;
}

bool FileOperation::hasActiveConflict(const Operation *operation) const
{
    if (!operation)
        return false;

    const auto targets = operationTargets(operation);
    for (const auto *other : std::as_const(m_operations))
    {
        if (other == operation || !other->active)
            continue;

        const auto otherTargets = operationTargets(other);
        for (const QUrl &target : targets)
        {
            const QString targetKey = target.isLocalFile()
                    ? QDir::cleanPath(target.toLocalFile())
                    : target.toString(QUrl::FullyEncoded);
            for (const QUrl &otherTarget : otherTargets)
            {
                const QString otherKey = otherTarget.isLocalFile()
                        ? QDir::cleanPath(otherTarget.toLocalFile())
                        : otherTarget.toString(QUrl::FullyEncoded);
                if (targetKey == otherKey
                        || targetKey.startsWith(otherKey + QLatin1Char('/'))
                        || otherKey.startsWith(targetKey + QLatin1Char('/')))
                    return true;
            }
        }
    }
    return false;
}

QVariantMap FileOperation::operationMap(const Operation *operation) const
{
    QVariantMap result;
    if (!operation)
        return result;

    QString status = QStringLiteral("finished");
    if (operation->active)
        status = operation->paused ? QStringLiteral("paused") : QStringLiteral("running");
    else if (!operation->started)
        status = QStringLiteral("queued");
    else if (operation->failed)
        status = QStringLiteral("failed");

    result.insert(QStringLiteral("id"), operation->id);
    result.insert(QStringLiteral("kind"), operation->kind);
    result.insert(QStringLiteral("operation"), operation->operation);
    result.insert(QStringLiteral("sourceName"), operation->sourceName);
    result.insert(QStringLiteral("destination"), operation->destination);
    result.insert(QStringLiteral("itemCount"), operation->itemCount);
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("active"), operation->active);
    result.insert(QStringLiteral("queued"), !operation->started && !operation->finished);
    result.insert(QStringLiteral("failed"), operation->failed);
    result.insert(QStringLiteral("progress"), operation->progress);
    result.insert(QStringLiteral("processedBytes"), operation->processedBytes);
    result.insert(QStringLiteral("totalBytes"), operation->totalBytes);
    result.insert(QStringLiteral("speed"), operation->speed);
    result.insert(QStringLiteral("paused"), operation->paused);
    result.insert(QStringLiteral("pauseSupported"), operation->pauseSupported);
    result.insert(QStringLiteral("conflictPending"), operation->conflictPending);
    result.insert(QStringLiteral("conflictDestination"), operation->conflictDestination.isEmpty() ? QString() : displayUrl(operation->conflictDestination));
    result.insert(QStringLiteral("error"), operation->errorMessage);
    const bool canMove = !operation->started && !operation->finished;
    result.insert(QStringLiteral("canCancel"), operation->active || canMove);
    result.insert(QStringLiteral("canClear"), !operation->active);
    result.insert(QStringLiteral("canMove"), canMove);
    return result;
}

bool FileOperation::running() const
{
    return activeCount() > 0;
}

int FileOperation::progress() const
{
    return m_progress;
}

qulonglong FileOperation::processedBytes() const
{
    return m_processedBytes;
}

qulonglong FileOperation::totalBytes() const
{
    return m_totalBytes;
}

qulonglong FileOperation::speed() const
{
    return m_speed;
}

QString FileOperation::operation() const
{
    return m_operation;
}

QString FileOperation::destination() const
{
    return m_destination;
}

QString FileOperation::sourceName() const
{
    return m_sourceName;
}

bool FileOperation::paused() const
{
    return m_paused;
}

bool FileOperation::pauseSupported() const
{
    return m_pauseSupported;
}

int FileOperation::activeCount() const
{
    int count = 0;
    for (auto *operation : m_operations)
        count += operation->active ? 1 : 0;
    return count;
}

int FileOperation::queuedCount() const
{
    int count = 0;
    for (auto *operation : m_operations)
        count += !operation->started && !operation->finished ? 1 : 0;
    return count;
}

int FileOperation::operationCount() const
{
    return m_operations.size();
}

QVariantList FileOperation::operations() const
{
    QVariantList result;
    for (const auto *operation : m_operations)
        result.append(operationMap(operation));
    return result;
}

QVariantList FileOperation::queuedOperations() const
{
    QVariantList result;
    for (const auto *operation : m_operations)
    {
        if (!operation->started && !operation->finished)
            result.append(operationMap(operation));
    }
    return result;
}

void FileOperation::cancel()
{
    Operation *primary = primaryOperation();
    if (primary && primary->active)
        cancelOperation(primary->id);
}

void FileOperation::cancelOperation(int id)
{
    Operation *operation = findOperation(id);
    if (!operation)
        return;

    if (!operation->active)
    {
        removeOperation(operation);
        notifyOperationsChanged();
        startPendingOperations();
        return;
    }

    operation->cancelRequested = true;
#ifdef KIO_AVAILABLE
    if (operation->localCopyActive)
    {
        finishLocalCopy(operation, false, QString());
        return;
    }

    if (operation->job)
    {
        if (operation->pauseTimer)
            operation->pauseTimer->stop();
        if (operation->job->isSuspended())
            operation->job->resume();
        operation->job->kill(KJob::EmitResult);
    }
#endif
}

void FileOperation::clearOperation(int id)
{
    Operation *operation = findOperation(id);
    if (!operation || operation->active)
        return;

    removeOperation(operation);
    notifyOperationsChanged();
    startPendingOperations();
}

void FileOperation::cancelAll()
{
    const auto snapshot = m_operations;
    for (auto *operation : snapshot)
    {
        if (!operation->active)
            removeOperation(operation);
    }
    notifyOperationsChanged();

    for (auto *operation : snapshot)
    {
        if (m_operations.contains(operation) && operation->active)
            cancelOperation(operation->id);
    }
}

bool FileOperation::pause()
{
    Operation *primary = primaryOperation();
    return primary ? pauseOperation(primary->id) : false;
}

bool FileOperation::pauseOperation(int id)
{
    Operation *operation = findOperation(id);
    if (!operation || !operation->active || operation->paused || !operation->pauseSupported)
        return false;

#ifdef KIO_AVAILABLE
    if (operation->localCopyActive)
    {
        operation->localTimer->stop();
        operation->paused = true;
        notifyOperationsChanged();
        return true;
    }

    if (operation->job && operation->job->suspend())
    {
        operation->paused = true;
        if (qobject_cast<KIO::CopyJob *>(operation->job))
        {
            suspendKioCopyJob(operation->job);
            if (!operation->pauseTimer)
            {
                operation->pauseTimer = new QTimer(this);
                operation->pauseTimer->setInterval(1);
                QObject::connect(operation->pauseTimer, &QTimer::timeout, this, [operation]() {
                    if (!operation->active || !operation->paused || !operation->job)
                    {
                        if (operation->pauseTimer)
                            operation->pauseTimer->stop();
                        return;
                    }
                    suspendKioCopyJob(operation->job);
                });
            }
            operation->pauseTimer->start();
        }
        notifyOperationsChanged();
        return true;
    }
#endif
    return false;
}

bool FileOperation::resume()
{
    Operation *primary = primaryOperation();
    return primary ? resumeOperation(primary->id) : false;
}

bool FileOperation::resumeOperation(int id)
{
    Operation *operation = findOperation(id);
    if (!operation || !operation->active || !operation->paused)
        return false;

#ifdef KIO_AVAILABLE
    if (operation->localCopyActive)
    {
        operation->paused = false;
        operation->localTimer->start(0);
        notifyOperationsChanged();
        return true;
    }

    if (operation->job && operation->job->resume())
    {
        operation->paused = false;
        if (operation->pauseTimer)
            operation->pauseTimer->stop();
        notifyOperationsChanged();
        return true;
    }
#endif
    return false;
}

bool FileOperation::moveOperationTo(int id, int targetId)
{
    if (id == targetId)
        return false;

    Operation *operation = findOperation(id);
    Operation *target = findOperation(targetId);
    if (!operation || !target || operation->started || operation->finished || target->started || target->finished)
        return false;

    const int index = m_operations.indexOf(operation);
    const int targetIndex = m_operations.indexOf(target);
    if (index < 0 || targetIndex < 0)
        return false;

    m_operations.move(index, targetIndex);
    notifyOperationsChanged();
    return true;
}

void FileOperation::resolveConflict(int id, bool replace)
{
    Operation *operation = findOperation(id);
    if (!operation || operation->active || !operation->conflictPending)
        return;

    if (!replace)
    {
        cancelOperation(id);
        return;
    }

    operation->overwriteExisting = true;
    operation->conflictPending = false;
    operation->conflictDestination.clear();
    notifyOperationsChanged();
    startPendingOperations();
}

void FileOperation::renameConflict(int id, const QString &name)
{
    Operation *operation = findOperation(id);
    if (!operation || operation->active || !operation->conflictPending
            || operation->kind != QStringLiteral("copy") || operation->urls.size() != 1)
        return;

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || trimmedName == QStringLiteral(".")
            || trimmedName == QStringLiteral("..") || trimmedName.contains(QLatin1Char('/'))
            || trimmedName.contains(QLatin1Char('\\'))
            || trimmedName.contains(QChar(0))
            || trimmedName.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))))
        return;

    operation->targetName = trimmedName;
    operation->conflictPending = false;
    operation->conflictDestination.clear();
    notifyOperationsChanged();
    startPendingOperations();
}

#ifdef KIO_AVAILABLE
void FileOperation::onPercent(KJob *job, unsigned long percent)
{
    for (auto *operation : std::as_const(m_operations))
    {
        if (operation->job == job)
        {
            operation->progress = static_cast<int>(percent);
            notifyOperationsChanged();
            return;
        }
    }
}

void FileOperation::onProcessedSize(KJob *job, qulonglong size)
{
    for (auto *operation : std::as_const(m_operations))
    {
        if (operation->job == job)
        {
            operation->processedBytes = size;
            if (operation->totalBytes > 0)
                operation->progress = static_cast<int>((static_cast<double>(size) / static_cast<double>(operation->totalBytes)) * 100.0);
            notifyOperationsChanged();
            return;
        }
    }
}

void FileOperation::onTotalSize(KJob *job, qulonglong size)
{
    for (auto *operation : std::as_const(m_operations))
    {
        if (operation->job == job)
        {
            operation->totalBytes = size;
            if (size > 0)
                operation->progress = static_cast<int>((static_cast<double>(operation->processedBytes) / static_cast<double>(size)) * 100.0);
            notifyOperationsChanged();
            return;
        }
    }
}

void FileOperation::onSpeed(KJob *job, unsigned long speed)
{
    for (auto *operation : std::as_const(m_operations))
    {
        if (operation->job == job)
        {
            operation->speed = speed;
            notifyOperationsChanged();
            return;
        }
    }
}
#endif

void FileOperation::setRunning(bool running)
{
    if (m_running == running)
        return;

    m_running = running;
    Q_EMIT runningChanged();
}

void FileOperation::setProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (m_progress == progress)
        return;

    m_progress = progress;
    Q_EMIT progressChanged();
}

void FileOperation::setProcessedBytes(qulonglong bytes)
{
    if (m_processedBytes == bytes)
        return;

    m_processedBytes = bytes;
    Q_EMIT processedBytesChanged();
}

void FileOperation::setTotalBytes(qulonglong bytes)
{
    if (m_totalBytes == bytes)
        return;

    m_totalBytes = bytes;
    Q_EMIT totalBytesChanged();
}

void FileOperation::setSpeed(qulonglong bytesPerSecond)
{
    if (m_speed == bytesPerSecond)
        return;

    m_speed = bytesPerSecond;
    Q_EMIT speedChanged();
}

void FileOperation::setPaused(bool paused)
{
    if (m_paused == paused)
        return;

    m_paused = paused;
    Q_EMIT pausedChanged();
}

void FileOperation::setPauseSupported(bool supported)
{
    if (m_pauseSupported == supported)
        return;

    m_pauseSupported = supported;
    Q_EMIT pauseSupportedChanged();
}
