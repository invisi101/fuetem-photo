#pragma once

#include <QImage>
#include <QString>

// Everything that knows about picture files: which ones count as pictures,
// and how to decode them. All loaders are read-only and safe to call from
// worker threads.
namespace Formats {

// True when the file name has a picture extension we can show. Videos,
// sidecars (.xmp) and everything else are ignored.
bool isImageFile(const QString &fileName);

// Camera RAW files, decoded through LibRaw rather than Qt's plugins.
bool isRawFile(const QString &fileName);

// A small, upright copy of the picture whose longer edge is at most maxEdge,
// decoded as cheaply as the format allows (scaled JPEG decoding, the RAW
// file's embedded preview). Null on failure.
QImage loadThumbnail(const QString &path, int maxEdge);

// The whole picture, upright and in sRGB, ready to paint. RAW files use the
// embedded preview when it is large enough and are developed otherwise.
QImage loadFull(const QString &path);

// Applies a LibRaw "flip" value (0, 3, 5, 6) to an image.
QImage applyRawFlip(const QImage &image, int flip);

} // namespace Formats
