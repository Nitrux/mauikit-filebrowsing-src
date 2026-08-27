#include "thumbnailer.h"

#ifdef KIO_AVAILABLE
#include <KIO/PreviewJob>
#endif

#include <QImage>
#include <QCache>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

#ifdef KIO_AVAILABLE
namespace
{
QCache<QString, QImage> s_previewCache(64 * 1024);
QMutex s_previewCacheMutex;

int previewCost(const QImage &image)
{
    return static_cast<int>(std::max(qint64(1), image.sizeInBytes() / 1024));
}

QString previewCacheKey(const QUrl &sourceUrl, const QSize &size)
{
    const QFileInfo fileInfo(sourceUrl.toLocalFile());
    return sourceUrl.toString(QUrl::FullyEncoded)
        + QLatin1Char('|') + QString::number(size.width())
        + QLatin1Char('x') + QString::number(size.height())
        + QLatin1Char('|') + QString::number(fileInfo.size())
        + QLatin1Char('|') + QString::number(fileInfo.lastModified().toMSecsSinceEpoch());
}

QImage cachedPreview(const QString &key)
{
    QMutexLocker locker(&s_previewCacheMutex);
    const auto image = s_previewCache.object(key);
    return image ? *image : QImage();
}

void cachePreview(const QString &key, const QImage &image)
{
    if (image.isNull())
        return;

    QMutexLocker locker(&s_previewCacheMutex);
    s_previewCache.insert(key, new QImage(image), previewCost(image));
}
}
#endif

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
    constexpr int maximumPreviewDimension = 2048;
    const QSize effectiveSize = (requestedSize.width() > 0 && requestedSize.height() > 0)
        ? requestedSize.boundedTo(QSize(maximumPreviewDimension, maximumPreviewDimension))
        : QSize(64, 64);

    KIO::PreviewJob::setDefaultDevicePixelRatio(qApp->devicePixelRatio());
    static const QStringList plugins = KIO::PreviewJob::availablePlugins();
    const QUrl sourceUrl = QUrl::fromUserInput(id, QStringLiteral("/"), QUrl::AssumeLocalFile);
    m_cacheKey = previewCacheKey(sourceUrl, effectiveSize);
    m_image = cachedPreview(m_cacheKey);
    if (!m_image.isNull())
    {
        finish();
        return;
    }

    m_job = new KIO::PreviewJob(KFileItemList() << KFileItem(sourceUrl), effectiveSize, &plugins);

    connect(m_job, &KIO::PreviewJob::gotPreview, this, [this](KFileItem, QPixmap pixmap) {
        if (m_done)
            return;
        m_image = pixmap.toImage();
        cachePreview(m_cacheKey, m_image);
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
