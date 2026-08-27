#include "fileoperation.h"

#ifdef KIO_AVAILABLE
#include <KIO/DeleteJob>
#include <KIO/CopyJob>
#include <KJob>
#endif

FileOperation *FileOperation::s_instance = nullptr;

FileOperation::FileOperation(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
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

bool FileOperation::startCopy(const QList<QUrl> &urls, const QUrl &destination, const QString &operationName)
{
    if (urls.isEmpty() || destination.isEmpty())
        return false;

    auto operation = FileOperation::instance();

    if (operation->running())
        return false;

#ifdef KIO_AVAILABLE
    auto job = KIO::copy(urls, destination, KIO::HideProgressInfo);
    if (!job)
        return false;

    job->setAutoRename(true);

    operation->m_job = job;
    operation->m_cancelled = false;
    operation->m_operation = operationName;
    operation->m_destination = destination.isLocalFile() ? destination.toLocalFile() : destination.toDisplayString();
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), operation->m_destination);

    QObject::connect(job, &KIO::CopyJob::copyingDone, operation, [operation](auto *, const QUrl &from, const QUrl &to, const auto &, bool, bool) {
        Q_EMIT operation->itemFinished(operation->m_operation, from.toString(), to.toString());
    });

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        const bool success = !operation->m_cancelled && finishedJob->error() == 0;
        const QString errorMessage = success ? QString() : finishedJob->errorString();

        operation->m_job = nullptr;
        operation->setProgress(success ? 100 : operation->m_progress);
        operation->setRunning(false);
        Q_EMIT operation->finished(success, errorMessage);
        operation->reset();
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
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), operation->m_destination);

    QObject::connect(job, &KIO::CopyJob::copyingDone, operation, [operation](auto *, const QUrl &from, const QUrl &to, const auto &, bool, bool) {
        Q_EMIT operation->itemFinished(operation->m_operation, from.toString(), to.toString());
    });

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        const bool success = !operation->m_cancelled && finishedJob->error() == 0;
        const QString errorMessage = success ? QString() : finishedJob->errorString();

        operation->m_job = nullptr;
        operation->setProgress(success ? 100 : operation->m_progress);
        operation->setRunning(false);
        Q_EMIT operation->finished(success, errorMessage);
        operation->reset();
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
    operation->setProgress(0);
    operation->setProcessedBytes(0);
    operation->setTotalBytes(0);
    operation->setSpeed(0);
    operation->setRunning(true);

    Q_EMIT operation->operationChanged();
    Q_EMIT operation->destinationChanged();
    Q_EMIT operation->started(operation->m_operation, urls.count(), QString());

    QObject::connect(job, SIGNAL(percentChanged(KJob*, unsigned long)), operation, SLOT(onPercent(KJob*, unsigned long)));
    QObject::connect(job, SIGNAL(processedSize(KJob*, qulonglong)), operation, SLOT(onProcessedSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(totalSize(KJob*, qulonglong)), operation, SLOT(onTotalSize(KJob*, qulonglong)));
    QObject::connect(job, SIGNAL(speed(KJob*, unsigned long)), operation, SLOT(onSpeed(KJob*, unsigned long)));

    QObject::connect(job, &KJob::result, operation, [operation](KJob *finishedJob) {
        const bool success = !operation->m_cancelled && finishedJob->error() == 0;
        const QString errorMessage = success ? QString() : finishedJob->errorString();

        operation->m_job = nullptr;
        operation->setProgress(success ? 100 : operation->m_progress);
        operation->setRunning(false);
        Q_EMIT operation->finished(success, errorMessage);
        operation->reset();
    });

    job->start();
    return true;
#else
    Q_UNUSED(operation)
    Q_UNUSED(urls)
    return false;
#endif
}

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

void FileOperation::cancel()
{
#ifdef KIO_AVAILABLE
    if (m_job) {
        m_cancelled = true;
        m_job->kill(KJob::EmitResult);
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

void FileOperation::reset()
{
    m_operation.clear();
    m_destination.clear();
    Q_EMIT operationChanged();
    Q_EMIT destinationChanged();
}
