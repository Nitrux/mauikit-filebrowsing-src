#include "thumbnailer.h"

#ifdef KIO_AVAILABLE
#include <KIO/PreviewJob>
#endif

#include <QImage>
#include <QGuiApplication>

QQuickImageResponse *Thumbnailer::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    AsyncImageResponse *response = new AsyncImageResponse(id, requestedSize);
    return response;
}

AsyncImageResponse::AsyncImageResponse(const QString &id, const QSize &requestedSize)
    : m_id(id)
    , m_requestedSize(requestedSize)
{
#ifdef KIO_AVAILABLE
    const QSize effectiveSize = (requestedSize.width() > 0 && requestedSize.height() > 0)
        ? QSize(qMin(requestedSize.width(), 192), qMin(requestedSize.height(), 192))
        : QSize(64, 64);

    KIO::PreviewJob::setDefaultDevicePixelRatio(qApp->devicePixelRatio());
    static const QStringList plugins = KIO::PreviewJob::availablePlugins();
    const QUrl sourceUrl = QUrl::fromUserInput(id, QStringLiteral("/"), QUrl::AssumeLocalFile);
    m_job = new KIO::PreviewJob(KFileItemList() << KFileItem(sourceUrl), effectiveSize, &plugins);

    connect(m_job, &KIO::PreviewJob::gotPreview, this, [this](KFileItem, QPixmap pixmap) {
        if (m_done)
            return;
        m_image = pixmap.toImage();
        finish();
    });

    connect(m_job, &KIO::PreviewJob::failed, this, [this](KFileItem) {
        if (m_done)
            return;
        m_error.clear();
        cancel();
    });

    m_job->start();
#endif
}

QQuickTextureFactory *AsyncImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString AsyncImageResponse::errorString() const
{
    return m_error;
}

void AsyncImageResponse::cancel()
{
    if (m_done)
        return;

#ifdef KIO_AVAILABLE
    if (m_job)
    {
        m_job->kill();
        m_job = nullptr;
    }
#endif

    finish();
}

void AsyncImageResponse::finish()
{
    if (m_done)
        return;

    m_done = true;
    Q_EMIT finished();
}
