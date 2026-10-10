#pragma once

#include <QObject>
#include <QList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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
 * Tracks all file operations created by the application.
 *
 * Operations are kept in one ordered list. Pending operations can be reordered
 * while they wait to start.
 */
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
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceNameChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool pauseSupported READ pauseSupported NOTIFY pauseSupportedChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY operationsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY operationsChanged)
    Q_PROPERTY(int operationCount READ operationCount NOTIFY operationsChanged)
    Q_PROPERTY(QVariantList operations READ operations NOTIFY operationsChanged)
    Q_PROPERTY(QVariantList queuedOperations READ queuedOperations NOTIFY operationsChanged)

public:
    explicit FileOperation(QObject *parent = nullptr);
    ~FileOperation() override;

    static FileOperation *instance();
    static bool startCopy(const QList<QUrl> &urls, const QUrl &destination, const QString &operation = QStringLiteral("copy"));
    static bool startMove(const QList<QUrl> &urls, const QUrl &destination, const QString &operation = QStringLiteral("move"));
    static bool startDelete(const QList<QUrl> &urls);
    static bool startTrash(const QList<QUrl> &urls);

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
    int activeCount() const;
    int queuedCount() const;
    int operationCount() const;
    QVariantList operations() const;
    QVariantList queuedOperations() const;

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void cancelOperation(int id);
    Q_INVOKABLE void clearOperation(int id);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool pauseOperation(int id);
    Q_INVOKABLE bool resume();
    Q_INVOKABLE bool resumeOperation(int id);
    Q_INVOKABLE bool moveOperationTo(int id, int targetId);
    Q_INVOKABLE void resolveConflict(int id, bool replace);

#ifdef KIO_AVAILABLE
private Q_SLOTS:
    void onPercent(KJob *job, unsigned long percent);
    void onProcessedSize(KJob *job, qulonglong size);
    void onTotalSize(KJob *job, qulonglong size);
    void onSpeed(KJob *job, unsigned long speed);
#endif

Q_SIGNALS:
    void started(const QString &operation, int itemCount, const QString &destination);
    void operationAdded(int id);
    void itemFinished(const QString &operation, const QString &source, const QString &destination);
    void finished(bool success, const QString &errorMessage);
    void operationsChanged();
    void queueChanged();
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

private:
    struct Operation;

    bool enqueueOperation(const QString &kind, const QString &operation, const QList<QUrl> &urls, const QUrl &destination);
    void startPendingOperations();
    bool startOperation(Operation *operation, QString *errorMessage);
    void finishOperation(Operation *operation, bool success, const QString &errorMessage);
    void refreshPrimaryOperation();
    void notifyOperationsChanged();
    void removeOperation(Operation *operation);
    Operation *findOperation(int id) const;
    Operation *primaryOperation() const;
    QVariantMap operationMap(const Operation *operation) const;
    QList<QUrl> operationTargets(const Operation *operation) const;
    bool hasActiveConflict(const Operation *operation) const;

#ifdef KIO_AVAILABLE
    bool startLocalCopy(Operation *operation);
    bool prepareLocalCopySource(Operation *operation);
    void processLocalCopy(Operation *operation);
    void finishLocalCopy(Operation *operation, bool success, const QString &errorMessage);
    void finishKioOperation(Operation *operation, KJob *job);
#endif

    void setRunning(bool running);
    void setProgress(int progress);
    void setProcessedBytes(qulonglong bytes);
    void setTotalBytes(qulonglong bytes);
    void setSpeed(qulonglong bytesPerSecond);
    void setPaused(bool paused);
    void setPauseSupported(bool supported);

    static FileOperation *s_instance;

    QList<Operation *> m_operations;
    int m_nextId = 1;

    bool m_running = false;
    int m_progress = 0;
    qulonglong m_processedBytes = 0;
    qulonglong m_totalBytes = 0;
    qulonglong m_speed = 0;
    QString m_operation;
    QString m_destination;
    QString m_sourceName;
    bool m_paused = false;
    bool m_pauseSupported = false;
};
