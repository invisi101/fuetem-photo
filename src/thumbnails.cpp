#include "thumbnails.h"

#include "formats.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>

ThumbnailLoader::ThumbnailLoader(QObject *parent) : QObject(parent) {
    // Memory budget in kilobytes: about 450 MB, several screens of thumbnails.
    m_memory.setMaxCost(450 * 1024);
    const int cores = QThread::idealThreadCount();
    m_maxInFlight = qBound(2, cores - 1, 8);
    m_pool.setMaxThreadCount(m_maxInFlight);
    QDir().mkpath(cacheRoot());
}

ThumbnailLoader::~ThumbnailLoader() {
    m_pending.clear();
    m_pool.clear();
    m_pool.waitForDone();
}

QString ThumbnailLoader::cacheRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/fuetem-photo/thumbs-%1").arg(Edge);
}

QString ThumbnailLoader::cacheFileFor(const QString &path) {
    const QFileInfo info(path);
    const QByteArray key = info.absoluteFilePath().toUtf8() + '\n'
        + QByteArray::number(info.size()) + '\n'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return cacheRoot() + QLatin1Char('/') + hash.left(2) + QLatin1Char('/') + hash.mid(2);
}

QImage ThumbnailLoader::loadOrCreate(const QString &path) {
    const QString cacheFile = cacheFileFor(path);
    if (QFileInfo::exists(cacheFile)) {
        QImageReader reader(cacheFile);
        reader.setDecideFormatFromContent(true);
        QImage image = reader.read();
        if (!image.isNull())
            return image.convertToFormat(image.hasAlphaChannel()
                ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    }

    const QImage image = Formats::loadThumbnail(path, Edge);
    if (image.isNull())
        return image;

    // Only the cache is ever written, never anything next to the photos.
    QDir().mkpath(QFileInfo(cacheFile).absolutePath());
    QSaveFile out(cacheFile);
    if (out.open(QIODevice::WriteOnly)) {
        const bool alpha = image.hasAlphaChannel();
        if (image.save(&out, alpha ? "PNG" : "JPEG", alpha ? -1 : 88))
            out.commit();
        else
            out.cancelWriting();
    }
    return image;
}

const QImage *ThumbnailLoader::cached(const QString &path) const {
    return m_memory.object(path);
}

void ThumbnailLoader::setQueue(const QStringList &paths) {
    m_pending.clear();
    QSet<QString> seen;
    for (const QString &path : paths) {
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        if (m_memory.contains(path) || m_inFlight.contains(path) || m_failed.contains(path))
            continue;
        m_pending.append(path);
    }
    pump();
}

void ThumbnailLoader::pump() {
    while (m_inFlight.size() < m_maxInFlight && !m_pending.isEmpty()) {
        const QString path = m_pending.takeFirst();
        m_inFlight.insert(path);
        QPointer<ThumbnailLoader> self(this);
        m_pool.start([self, path]() {
            const QImage image = ThumbnailLoader::loadOrCreate(path);
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, path, image]() {
                if (self)
                    self->finished(path, image);
            }, Qt::QueuedConnection);
        });
    }
}

void ThumbnailLoader::finished(const QString &path, const QImage &image) {
    m_inFlight.remove(path);
    if (image.isNull()) {
        m_failed.insert(path);
    } else {
        const int cost = qMax<qsizetype>(1, image.sizeInBytes() / 1024);
        m_memory.insert(path, new QImage(image), int(cost));
    }
    emit ready(path);
    pump();
}
