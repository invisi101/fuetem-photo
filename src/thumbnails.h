#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

// Makes and remembers grid thumbnails. Thumbnails are decoded on worker
// threads, kept on disk under ~/.cache/fuetem-photo and in memory for the
// current session. The grid hands over the paths it can see, nearest first;
// anything scrolled out of that list before it starts is never decoded.
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    // Longer edge of a cached thumbnail, in pixels.
    static constexpr int Edge = 600;

    explicit ThumbnailLoader(QObject *parent = nullptr);
    ~ThumbnailLoader() override;

    // The thumbnail if it is in memory; null otherwise.
    const QImage *cached(const QString &path) const;
    bool failed(const QString &path) const { return m_failed.contains(path); }

    // Replaces the waiting list with these paths, in priority order.
    void setQueue(const QStringList &paths);

    static QString cacheRoot();
    // Where the thumbnail for this file lives on disk (depends on the file's
    // path, size and modification time, so edits make a fresh one).
    static QString cacheFileFor(const QString &path);
    // Disk cache or decode-and-store. Runs on worker threads.
    static QImage loadOrCreate(const QString &path);

signals:
    void ready(const QString &path);

private:
    void pump();
    void finished(const QString &path, const QImage &image);

    QCache<QString, QImage> m_memory;
    QStringList m_pending;
    QSet<QString> m_inFlight;
    QSet<QString> m_failed;
    QThreadPool m_pool;
    int m_maxInFlight = 4;
};
