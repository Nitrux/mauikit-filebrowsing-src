#pragma once
#include <QString>
#include <QQuickImageProvider>
#include <QPointer>
#include "filebrowsing_export.h"

#ifdef KIO_AVAILABLE
namespace KIO
{
class PreviewJob;
}
#endif

/**
 * @private
 */
class AsyncImageResponse : public QQuickImageResponse
{
public:
    AsyncImageResponse(const QString &id, const QSize &requestedSize);
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override;
    void cancel() override;

private:
    void finish();
    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
    QString m_error;
    bool m_done = false;
#ifdef KIO_AVAILABLE
    QPointer<KIO::PreviewJob> m_job;
#endif
};

class FILEBROWSING_EXPORT Thumbnailer : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
