#pragma once

#include <QObject>
#include <QUrl>
#include <QList>
#include <QString>

#include "filebrowsing_export.h"

#ifdef KIO_AVAILABLE
class KJob;
#endif

/**
 *  Tracks one asynchronous copy, move, or delete operation.
 *
 * FileOperation is a process-wide singleton. Starting an operation fails when
 * another operation is running, when its arguments are empty, or when KIO is
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

    /** Cancels the active operation; does nothing when no job is running. */
    Q_INVOKABLE void cancel();

#ifdef KIO_AVAILABLE
private Q_SLOTS:
    void onPercent(KJob *job, unsigned long percent);
    void onProcessedSize(KJob *job, qulonglong size);
    void onTotalSize(KJob *job, qulonglong size);
    void onSpeed(KJob *job, unsigned long speed);
#endif

Q_SIGNALS:
    void started(const QString &operation, int itemCount, const QString &destination);
    void finished(bool success, const QString &errorMessage);
    void runningChanged();
    void progressChanged();
    void processedBytesChanged();
    void totalBytesChanged();
    void speedChanged();
    void operationChanged();
    void destinationChanged();

private:
    void setRunning(bool running);
    void setProgress(int progress);
    void setProcessedBytes(qulonglong bytes);
    void setTotalBytes(qulonglong bytes);
    void setSpeed(qulonglong bytesPerSecond);
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

#ifdef KIO_AVAILABLE
    KJob *m_job = nullptr;
#endif
};
