#pragma once

#include <QObject>
#include <QUrl>
#include <QList>
#include <QString>
#include <QVariantList>
#ifdef KIO_AVAILABLE
#include <QElapsedTimer>
#include <QFile>
#endif

#include "filebrowsing_export.h"

#ifdef KIO_AVAILABLE
class KJob;
class QTimer;
#endif

/**
 *  Tracks one asynchronous copy, move, or delete operation.
 *
 * FileOperation is a process-wide singleton. Copy requests received while another
 * operation is running are queued and started in order. Other operations fail when
 * another operation is running, when their arguments are empty, or when KIO is
 * unavailable. Progress is reported as a percentage, byte counts, and bytes per
 * second; cancel() requests termination of the active job.
 */
class FILEBROWSING_EXPORT FileOperation : public QObject
{
    Q_OBJECT

    /** Whether a file job is currently active. */
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    /** Completion percentage from 0 through 100. */
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    /** Number of bytes processed by the active job. */
    Q_PROPERTY(qulonglong processedBytes READ processedBytes NOTIFY processedBytesChanged)
    /** Total byte count reported by the active job. */
    Q_PROPERTY(qulonglong totalBytes READ totalBytes NOTIFY totalBytesChanged)
    /** Current transfer speed in bytes per second. */
    Q_PROPERTY(qulonglong speed READ speed NOTIFY speedChanged)
    /** User-facing operation identifier, such as copy, move, or delete. */
    Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
    /** Display form of the destination, empty for deletion. */
    Q_PROPERTY(QString destination READ destination NOTIFY destinationChanged)
    /** Display name of the active source when one item is being transferred. */
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceNameChanged)
    /** Whether the active job is suspended. */
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    /** Whether the active operation can be paused by this component. */
    Q_PROPERTY(bool pauseSupported READ pauseSupported NOTIFY pauseSupportedChanged)
    /** Number of copy requests waiting behind the active job. */
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY queueChanged)
    /** Copy requests waiting behind the active job. */
    Q_PROPERTY(QVariantList queuedOperations READ queuedOperations NOTIFY queueChanged)

public:
    explicit FileOperation(QObject *parent = nullptr);
    ~FileOperation() override;

    static FileOperation *instance();
    static bool startCopy(const QList<QUrl> &urls, const QUrl &destination, const QString &operation = QStringLiteral("copy"));
    static bool startMove(const QList<QUrl> &urls, const QUrl &destination, const QString &operation = QStringLiteral("move"));
    static bool startDelete(const QList<QUrl> &urls);

    bool running() const;
    int progress() const;
    qulonglong processedBytes() const;
    qulonglong totalBytes() const;
    qulonglong speed() const;
    QString operation() const;
    QString destination() const;
    QString sourceName() const;
    bool paused() const;
    bool pauseSupported() const;
    int queuedCount() const;
    QVariantList queuedOperations() const;

    /** Cancels the active operation; does nothing when no job is running. */
    Q_INVOKABLE void cancel();
    /** Cancels one queued copy request by its current queue index. */
    Q_INVOKABLE void cancelQueued(int index);
    /** Cancels the active job and discards all queued copy requests. */
    Q_INVOKABLE void cancelAll();
    /** Suspends the active job when the backend supports it. */
    Q_INVOKABLE bool pause();
    /** Resumes the active job when it is suspended. */
    Q_INVOKABLE bool resume();

#ifdef KIO_AVAILABLE
private Q_SLOTS:
    void onPercent(KJob *job, unsigned long percent);
    void onProcessedSize(KJob *job, qulonglong size);
    void onTotalSize(KJob *job, qulonglong size);
    void onSpeed(KJob *job, unsigned long speed);
#endif

Q_SIGNALS:
    void started(const QString &operation, int itemCount, const QString &destination);
    void itemFinished(const QString &operation, const QString &source, const QString &destination);
    void finished(bool success, const QString &errorMessage);
    void runningChanged();
    void progressChanged();
    void processedBytesChanged();
    void totalBytesChanged();
    void speedChanged();
    void operationChanged();
    void destinationChanged();
    void sourceNameChanged();
    void pausedChanged();
    void pauseSupportedChanged();
    void queueChanged();

private:
#ifdef KIO_AVAILABLE
    void startNextCopy();
    void finishJob(KJob *job);
    bool startLocalCopy(const QUrl &source, const QUrl &destination, const QString &operation);
    void processLocalCopy();
    void finishLocalCopy(bool success, const QString &errorMessage);
#endif

    void setRunning(bool running);
    void setProgress(int progress);
    void setProcessedBytes(qulonglong bytes);
    void setTotalBytes(qulonglong bytes);
    void setSpeed(qulonglong bytesPerSecond);
    void setPaused(bool paused);
    void setPauseSupported(bool supported);
    void reset();

    static FileOperation *s_instance;

    bool m_running = false;
    bool m_cancelled = false;
    int m_progress = 0;
    qulonglong m_processedBytes = 0;
    qulonglong m_totalBytes = 0;
    qulonglong m_speed = 0;
    QString m_operation;
    QString m_destination;
    QString m_sourceName;
    bool m_paused = false;
    bool m_pauseSupported = false;

#ifdef KIO_AVAILABLE
    struct CopyRequest
    {
        QList<QUrl> urls;
        QUrl destination;
        QString operation;
    };

    QList<CopyRequest> m_copyQueue;
    KJob *m_job = nullptr;

    QTimer *m_localCopyTimer = nullptr;
    QFile m_localSource;
    QFile m_localDestination;
    QElapsedTimer m_localCopyElapsed;
    QUrl m_localSourceUrl;
    QString m_localDestinationPath;
    QString m_localTemporaryPath;
    bool m_localCopyActive = false;
#endif
};
