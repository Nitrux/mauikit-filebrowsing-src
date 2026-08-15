#pragma once

#include <QObject>
#include <QUrl>
#include <QList>
#include <QString>

#include "filebrowsing_export.h"

#ifdef KIO_AVAILABLE
class KJob;
#endif

class FILEBROWSING_EXPORT FileOperation : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(qulonglong processedBytes READ processedBytes NOTIFY processedBytesChanged)
    Q_PROPERTY(qulonglong totalBytes READ totalBytes NOTIFY totalBytesChanged)
    Q_PROPERTY(qulonglong speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
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
