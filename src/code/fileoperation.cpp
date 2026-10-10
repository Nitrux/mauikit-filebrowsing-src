#include "fileoperation.h"

#ifdef KIO_AVAILABLE
#include <KIO/DeleteJob>
#include <KIO/CopyJob>
#include <KJob>
#include <QVariantMap>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#endif

namespace
{
QString displayUrl(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toDisplayString();
}

QString sourceNameForUrls(const QList<QUrl> &urls)
{
    if (urls.size() != 1)
        return {};

    const auto name = urls.constFirst().fileName();
    return name.isEmpty() ? displayUrl(urls.constFirst()) : name;
}
}

#ifdef KIO_AVAILABLE
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
#endif

FileOperation *FileOperation::s_instance = nullptr;

FileOperation::FileOperation(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

#ifdef KIO_AVAILABLE
    m_localCopyTimer = new QTimer(this);
    m_localCopyTimer->setSingleShot(true);
    QObject::connect(m_localCopyTimer, &QTimer::timeout, this, &FileOperation::processLocalCopy);
#endif
}

FileOperation::~FileOperation()
{
    if (s_instance == this)
        s_instance = nullptr;
}

FileOperation *FileOperation::instance()
{
    if (!s_instance)
        s_instance = new FileOperation;

    return s_instance;
}

#ifdef KIO_AVAILABLE
bool FileOperation::startLocalCopy(const QUrl &source, const QUrl &destination, const QString &operation)
{
    if (operation != QStringLiteral("copy") && operation != QStringLiteral("paste"))
        return false;

    if (!source.isLocalFile() || !destination.isLocalFile())
        return false;

    const QFileInfo sourceInfo(source.toLocalFile());
    const QDir destinationDir(destination.toLocalFile());
    if (!sourceInfo.exists() || !sourceInfo.isFile() || sourceInfo.isSymLink() || !destinationDir.exists())
        return false;

    const QString destinationPath = destinationDir.filePath(sourceInfo.fileName());
    const QString temporaryPath = destinationPath + QStringLiteral(".part");
    const QFileInfo destinationInfo(destinationPath);
    const bool temporaryExists = QFileInfo::exists(temporaryPath);
    if (destinationInfo.exists())
    {
        if (!temporaryExists)
            return false;

        // A final file alongside its matching .part is an interrupted copy artifact.
        if (!QFile::remove(destinationPath))
            return false;
    }

    m_localSource.close();
    m_localDestination.close();
    m_localSource.setFileName(sourceInfo.filePath());
    if (!m_localSource.open(QIODevice::ReadOnly))
        return false;

    m_localDestinationPath = destinationPath;
    m_localTemporaryPath = temporaryPath;
    m_localDestination.setFileName(m_localTemporaryPath);

    QIODevice::OpenMode destinationMode = QIODevice::ReadWrite;
    if (!temporaryExists)
        destinationMode |= QIODevice::Truncate;

    if (!m_localDestination.open(destinationMode))
    {
        m_localSource.close();
        m_localDestinationPath.clear();
        m_localTemporaryPath.clear();
        return false;
    }

    qint64 partialSize = temporaryExists ? m_localDestination.size() : 0;
    if (partialSize > sourceInfo.size())
    {
        if (!m_localDestination.resize(0))
        {
            m_localSource.close();
            m_localDestination.close();
            m_localDestinationPath.clear();
            m_localTemporaryPath.clear();
            return false;
        }
        partialSize = 0;
    }

    if (!m_localSource.seek(partialSize) || !m_localDestination.seek(partialSize))
    {
        m_localSource.close();
        m_localDestination.close();
        m_localDestinationPath.clear();
        m_localTemporaryPath.clear();
        return false;
    }

    m_localSourceUrl = source;
    m_localCopyActive = true;
    m_cancelled = false;
    m_operation = operation;
    m_destination = displayUrl(destination);
    m_sourceName = sourceNameForUrls(QList<QUrl>{source});
    setTotalBytes(static_cast<qulonglong>(sourceInfo.size()));
    setProcessedBytes(static_cast<qulonglong>(partialSize));
    setProgress(sourceInfo.size() > 0 ? static_cast<int>((partialSize * 100) / sourceInfo.size()) : 0);
    setSpeed(0);
    setPaused(false);
    setPauseSupported(true);
    setRunning(true);

    Q_EMIT operationChanged();
    Q_EMIT destinationChanged();
    Q_EMIT sourceNameChanged();
    Q_EMIT started(m_operation, 1, m_destination);

    m_localCopyElapsed.start();
    m_localCopyTimer->start(0);
    return true;
}
void FileOperation::processLocalCopy()
{
    if (!m_localCopyActive || m_paused)
        return;

    constexpr qint64 copyChunkSize = 1024 * 1024;
    const QByteArray data = m_localSource.read(copyChunkSize);
    if (data.isEmpty())
    {
        if (m_localSource.error() != QFile::NoError)
        {
            finishLocalCopy(false, m_localSource.errorString());
            return;
        }

        if (!m_localDestination.flush())
        {
            finishLocalCopy(false, m_localDestination.errorString());
            return;
        }

        m_localSource.close();
        m_localDestination.close();
        if (!QFile::rename(m_localTemporaryPath, m_localDestinationPath))
        {
            finishLocalCopy(false, QStringLiteral("Could not finalize the copied file"));
            return;
        }

        finishLocalCopy(true, QString());
        return;
    }

    if (m_localDestination.write(data) != data.size())
    {
        finishLocalCopy(false, m_localDestination.errorString());
        return;
    }

    setProcessedBytes(m_processedBytes + static_cast<qulonglong>(data.size()));
    if (m_totalBytes > 0)
        setProgress(static_cast<int>((m_processedBytes * 100) / m_totalBytes));

    const qint64 elapsed = m_localCopyElapsed.elapsed();
    if (elapsed > 0)
        setSpeed((m_processedBytes * 1000) / static_cast<qulonglong>(elapsed));

    m_localCopyTimer->start(0);
}

void FileOperation::finishLocalCopy(bool success, const QString &errorMessage)
{
    m_localCopyTimer->stop();
    m_localSource.close();
    m_localDestination.close();

    const QUrl source = m_localSourceUrl;
    const QUrl destination = QUrl::fromLocalFile(m_localDestinationPath);
    if (!success && !m_localTemporaryPath.isEmpty())
        QFile::remove(m_localTemporaryPath);

    m_localCopyActive = false;
    setPaused(false);
    setPauseSupported(false);
    if (success)
    {
        setProcessedBytes(m_totalBytes);
        setProgress(100);
        Q_EMIT itemFinished(m_operation, source.toString(), destination.toString());
    }

    setRunning(false);
    Q_EMIT finished(success, errorMessage);
    reset();
    startNextCopy();
}
#endif

bool FileOperation::startCopy(const QList<QUrl> &urls, const QUrl &destination, const QString &operationName)
{
    if (urls.isEmpty() || destination.isEmpty())
        return false;

    auto operation = FileOperation::instance();

    if (operation->running())
    {
#ifdef KIO_AVAILABLE
        operation->m_copyQueue.append(CopyRequest{urls, destination, operationName});
        Q_EMIT operation->queueChanged();
        return true;
#else
        return false;
#endif
    }

#ifdef KIO_AVAILABLE
    if (urls.size() == 1)
    {
        if (operation->startLocalCopy(urls.constFirst(), destination, operationName))
            return true;

        if (hasLocalPartialCopy(urls.constFirst(), destination))
            return false;
    }

    auto job = KIO::copy(urls, destination, KIO::HideProgressInfo);
    if (!job)
        return false;

    job->setAutoRename(true);

    operation->m_job = job;
    operation->m_cancelled = false;
    operation->m_operation = operationName;
    operation->m_destination = destination.isLocalFile() ? destination.toLocalFile() : destination.toDisplayString();
    operation->m_sourceName = sourceNameForUrls(urls);
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setPaused(false);
    operation->setPauseSupported(false);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->sourceNameChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), operation->m_destination);

    QObject::connect(job, &KIO::CopyJob::copyingDone, operation, [operation](auto *, const QUrl &from, const QUrl &to, const auto &, bool, bool) {
        Q_EMIT operation->itemFinished(operation->m_operation, from.toString(), to.toString());
    });

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        operation->finishJob(finishedJob);
    });

    job->start();
    return true;
#else
    Q_UNUSED(operation)
    Q_UNUSED(urls)
    Q_UNUSED(destination)
    Q_UNUSED(operationName)
    return false;
#endif
}

bool FileOperation::startMove(const QList<QUrl> &urls, const QUrl &destination, const QString &operationName)
{
    if (urls.isEmpty() || destination.isEmpty())
        return false;

    auto operation = FileOperation::instance();

    if (operation->running())
        return false;

#ifdef KIO_AVAILABLE
    auto job = KIO::move(urls, destination, KIO::HideProgressInfo);
    if (!job)
        return false;

    job->setAutoRename(true);

    operation->m_job = job;
    operation->m_cancelled = false;
    operation->m_operation = operationName;
    operation->m_destination = destination.isLocalFile() ? destination.toLocalFile() : destination.toDisplayString();
    operation->m_sourceName = sourceNameForUrls(urls);
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setPaused(false);
    operation->setPauseSupported(false);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->sourceNameChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), operation->m_destination);

    QObject::connect(job, &KIO::CopyJob::copyingDone, operation, [operation](auto *, const QUrl &from, const QUrl &to, const auto &, bool, bool) {
        Q_EMIT operation->itemFinished(operation->m_operation, from.toString(), to.toString());
    });

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        operation->finishJob(finishedJob);
    });

    job->start();
    return true;
#else
    Q_UNUSED(operation)
    Q_UNUSED(urls)
    Q_UNUSED(destination)
    Q_UNUSED(operationName)
    return false;
#endif
}

bool FileOperation::startDelete(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return false;

    auto operation = FileOperation::instance();

    if (operation->running())
        return false;

#ifdef KIO_AVAILABLE
    auto job = KIO::del(urls, KIO::HideProgressInfo);
    if (!job)
        return false;

    operation->m_job = job;
    operation->m_cancelled = false;
    operation->m_operation = QStringLiteral("delete");
    operation->m_destination.clear();
    operation->m_sourceName = sourceNameForUrls(urls);
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setPaused(false);
    operation->setPauseSupported(false);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->sourceNameChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), QString());

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        operation->finishJob(finishedJob);
    });

    job->start();
    return true;
#else
    Q_UNUSED(operation)
    Q_UNUSED(urls)
    return false;
#endif
}

#ifdef KIO_AVAILABLE
void FileOperation::finishJob(KJob *finishedJob)
{
    const bool success = !m_cancelled && finishedJob->error() == 0;
    const QString errorMessage = success ? QString() : finishedJob->errorString();

    m_job = nullptr;
    setPaused(false);
    setProgress(success ? 100 : m_progress);
    setRunning(false);
    Q_EMIT finished(success, errorMessage);
    reset();

    startNextCopy();
}

void FileOperation::startNextCopy()
{
    if (running() || m_copyQueue.isEmpty())
        return;

    const CopyRequest request = m_copyQueue.takeFirst();
    Q_EMIT queueChanged();
    if (!FileOperation::startCopy(request.urls, request.destination, request.operation))
        startNextCopy();
}
#endif

bool FileOperation::running() const
{
    return m_running;
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

int FileOperation::queuedCount() const
{
#ifdef KIO_AVAILABLE
    return m_copyQueue.size();
#else
    return 0;
#endif
}

QVariantList FileOperation::queuedOperations() const
{
    QVariantList operations;
#ifdef KIO_AVAILABLE
    for (int index = 0; index < m_copyQueue.size(); ++index)
    {
        const auto &request = m_copyQueue.at(index);
        QVariantMap operation;
        operation.insert(QStringLiteral("index"), index);
        operation.insert(QStringLiteral("operation"), request.operation);
        operation.insert(QStringLiteral("itemCount"), request.urls.count());
        operation.insert(QStringLiteral("sourceName"), sourceNameForUrls(request.urls));
        operation.insert(QStringLiteral("destination"), displayUrl(request.destination));
        operations.append(operation);
    }
#endif
    return operations;
}

void FileOperation::cancelQueued(int index)
{
#ifdef KIO_AVAILABLE
    if (index < 0 || index >= m_copyQueue.size())
        return;

    m_copyQueue.removeAt(index);
    Q_EMIT queueChanged();
#else
    Q_UNUSED(index)
#endif
}

void FileOperation::cancelAll()
{
#ifdef KIO_AVAILABLE
    if (!m_copyQueue.isEmpty())
    {
        m_copyQueue.clear();
        Q_EMIT queueChanged();
    }
#endif
    cancel();
}

bool FileOperation::pause()
{
#ifdef KIO_AVAILABLE
    if (m_localCopyActive)
    {
        if (m_paused)
            return false;

        m_localCopyTimer->stop();
        setPaused(true);
        return true;
    }

    return false;
#else
    return false;
#endif
}

bool FileOperation::resume()
{
#ifdef KIO_AVAILABLE
    if (!m_localCopyActive || !m_paused)
        return false;

    setPaused(false);
    m_localCopyTimer->start(0);
    return true;
#else
    return false;
#endif
}

void FileOperation::cancel()
{
#ifdef KIO_AVAILABLE
    if (m_localCopyActive)
    {
        m_cancelled = true;
        finishLocalCopy(false, QString());
        return;
    }

    if (m_job)
    {
        m_cancelled = true;

        auto *job = m_job;
        if (m_paused || job->isSuspended())
            job->resume();

        setPaused(false);
        job->kill(KJob::EmitResult);
    }
#endif
}

#ifdef KIO_AVAILABLE
void FileOperation::onPercent(KJob *, unsigned long percent)
{
    setProgress(static_cast<int>(percent));
}
#endif

#ifdef KIO_AVAILABLE
void FileOperation::onProcessedSize(KJob *, qulonglong size)
{
    setProcessedBytes(size);
    if (m_totalBytes > 0)
        setProgress(static_cast<int>((static_cast<double>(size) / static_cast<double>(m_totalBytes)) * 100.0));
}

void FileOperation::onTotalSize(KJob *, qulonglong size)
{
    setTotalBytes(size);
    if (size > 0)
        setProgress(static_cast<int>((static_cast<double>(m_processedBytes) / static_cast<double>(size)) * 100.0));
}

void FileOperation::onSpeed(KJob *, unsigned long bytesPerSecond)
{
    setSpeed(bytesPerSecond);
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

void FileOperation::reset()
{
    m_operation.clear();
    m_destination.clear();
    m_sourceName.clear();
    setPauseSupported(false);
#ifdef KIO_AVAILABLE
    m_localSourceUrl.clear();
    m_localDestinationPath.clear();
    m_localTemporaryPath.clear();
#endif
    Q_EMIT operationChanged();
    Q_EMIT destinationChanged();
    Q_EMIT sourceNameChanged();
}
