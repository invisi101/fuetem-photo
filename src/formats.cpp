#include "formats.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTransform>

#include <libraw/libraw.h>

#include <cstring>
#include <memory>

namespace {

const QSet<QString> &rawExtensions() {
    static const QSet<QString> exts = {
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"),
        QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("arw"),
        QStringLiteral("srf"), QStringLiteral("sr2"), QStringLiteral("dng"),
        QStringLiteral("raf"), QStringLiteral("orf"), QStringLiteral("rw2"),
        QStringLiteral("rwl"), QStringLiteral("pef"), QStringLiteral("srw"),
        QStringLiteral("x3f"), QStringLiteral("3fr"), QStringLiteral("iiq"),
        QStringLiteral("erf"), QStringLiteral("kdc"), QStringLiteral("dcr"),
        QStringLiteral("mrw"), QStringLiteral("mos"), QStringLiteral("mef"),
        QStringLiteral("raw"),
    };
    return exts;
}

const QSet<QString> &plainExtensions() {
    static const QSet<QString> exts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("jfif"), QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("dib"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"),
        QStringLiteral("heif"), QStringLiteral("hif"), QStringLiteral("avif"),
        QStringLiteral("jxl"), QStringLiteral("psd"), QStringLiteral("psb"),
        QStringLiteral("tga"), QStringLiteral("qoi"), QStringLiteral("jp2"),
        QStringLiteral("j2k"), QStringLiteral("ppm"), QStringLiteral("pgm"),
        QStringLiteral("pbm"), QStringLiteral("pnm"), QStringLiteral("xcf"),
        QStringLiteral("kra"), QStringLiteral("ora"), QStringLiteral("exr"),
        QStringLiteral("hdr"), QStringLiteral("ico"),
    };
    return exts;
}

QString extensionOf(const QString &fileName) {
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return {};
    return fileName.mid(dot + 1).toLower();
}

// Converts to sRGB and to a format the raster painter draws quickly.
QImage finish(QImage image) {
    if (image.isNull())
        return image;
    if (image.format() == QImage::Format_CMYK8888)
        image = image.convertToFormat(QImage::Format_RGB32);
    const QColorSpace cs = image.colorSpace();
    if (cs.isValid() && cs != QColorSpace(QColorSpace::SRgb)) {
        QImage converted = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
        if (!converted.isNull())
            image = converted;
    }
    const QImage::Format target = image.hasAlphaChannel()
        ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (image.format() != target)
        image = image.convertToFormat(target);
    return image;
}

QImage scaledToEdge(const QImage &image, int maxEdge) {
    if (image.isNull() || maxEdge <= 0)
        return image;
    if (qMax(image.width(), image.height()) <= maxEdge)
        return image;
    return image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// A small reader for the flattened composite Photoshop stores at the end of
// a PSD/PSB, used when the kimageformats plugin gives up (it refuses some
// 16-bit files). Handles RGB and greyscale, 8 or 16 bits, raw or RLE.
QImage readPsdComposite(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    const uchar *data = file.map(0, size);
    if (!data || size < 26 || memcmp(data, "8BPS", 4) != 0)
        return {};
    auto u16 = [data](qint64 at) { return quint32(data[at] << 8 | data[at + 1]); };
    auto u32 = [data](qint64 at) {
        return quint32(data[at]) << 24 | quint32(data[at + 1]) << 16 | quint32(data[at + 2]) << 8 | data[at + 3];
    };
    const int version = int(u16(4));
    const int channels = int(u16(12));
    const int height = int(u32(14));
    const int width = int(u32(18));
    const int depth = int(u16(22));
    const int mode = int(u16(24));
    if ((version != 1 && version != 2) || width <= 0 || height <= 0
            || (depth != 8 && depth != 16) || (mode != 3 && mode != 1))
        return {};
    const int colorChannels = mode == 3 ? 3 : 1;
    if (channels < colorChannels)
        return {};

    qint64 at = 26;
    auto skipSection = [&](bool wide) {
        if (wide) {
            if (at + 8 > size) return false;
            const quint64 len = quint64(u32(at)) << 32 | u32(at + 4);
            at += 8 + qint64(len);
        } else {
            if (at + 4 > size) return false;
            at += 4 + qint64(u32(at));
        }
        return at <= size;
    };
    if (!skipSection(false) || !skipSection(false) || !skipSection(version == 2))
        return {};
    if (at + 2 > size)
        return {};
    const int compression = int(u16(at));
    at += 2;

    const int bytesPerSample = depth / 8;
    const qint64 rowBytes = qint64(width) * bytesPerSample;
    // Decoded planes, one per colour channel, each height * rowBytes.
    QVector<QByteArray> planes(colorChannels);
    if (compression == 0) {
        for (int c = 0; c < colorChannels; ++c) {
            const qint64 planeBytes = rowBytes * height;
            const qint64 start = at + planeBytes * c;
            if (start + planeBytes > size)
                return {};
            planes[c] = QByteArray::fromRawData(reinterpret_cast<const char *>(data + start), planeBytes);
        }
    } else if (compression == 1) {
        const int countBytes = version == 2 ? 4 : 2;
        const qint64 table = at;
        qint64 pos = at + qint64(channels) * height * countBytes;
        for (int c = 0; c < colorChannels; ++c) {
            QByteArray plane(rowBytes * height, Qt::Uninitialized);
            for (int y = 0; y < height; ++y) {
                const qint64 entry = table + (qint64(c) * height + y) * countBytes;
                const qint64 len = countBytes == 4 ? u32(entry) : u16(entry);
                if (pos + len > size)
                    return {};
                uchar *out = reinterpret_cast<uchar *>(plane.data()) + rowBytes * y;
                qint64 o = 0, i = pos;
                const qint64 end = pos + len;
                while (i < end && o < rowBytes) {
                    const int n = qint8(data[i++]);
                    if (n >= 0) {
                        const qint64 count = qMin<qint64>(n + 1, qMin(rowBytes - o, end - i));
                        memcpy(out + o, data + i, size_t(count));
                        o += count;
                        i += n + 1;
                    } else if (n != -128) {
                        const qint64 count = qMin<qint64>(1 - n, rowBytes - o);
                        if (i < end)
                            memset(out + o, data[i], size_t(count));
                        o += count;
                        ++i;
                    }
                }
                pos = end;
            }
            planes[c] = plane;
        }
    } else {
        return {};
    }

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull())
        return {};
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const qint64 row = rowBytes * y;
        for (int x = 0; x < width; ++x) {
            int v[3];
            for (int c = 0; c < 3; ++c) {
                const QByteArray &plane = planes[colorChannels == 3 ? c : 0];
                const uchar *px = reinterpret_cast<const uchar *>(plane.constData()) + row + qint64(x) * bytesPerSample;
                v[c] = px[0]; // the high byte is enough for display
            }
            line[x] = qRgb(v[0], v[1], v[2]);
        }
    }
    return image;
}

// Decodes through Qt's image plugins (qt6-imageformats, kimageformats).
QImage readWithQt(const QString &path, int maxEdge) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);

    // Scaled decoding is only worth asking for where the decoder does it
    // natively (JPEG's DCT scaling); everywhere else we scale afterwards so
    // orientation and aspect stay exactly right.
    if (maxEdge > 0 && reader.supportsOption(QImageIOHandler::ScaledSize)
            && reader.format() == "jpeg") {
        const QSize size = reader.size();
        if (size.isValid() && qMax(size.width(), size.height()) > maxEdge * 2) {
            // Decode at roughly twice the target, then smooth-scale down.
            reader.setScaledSize(size.scaled(maxEdge * 2, maxEdge * 2, Qt::KeepAspectRatio));
        }
    }

    QImage image = reader.read();
    if (image.isNull())
        image = readPsdComposite(path); // checks for the PSD signature itself
    if (image.isNull())
        return image;
    return scaledToEdge(image, maxEdge);
}

struct RawResult {
    QImage image;
    int rawLongEdge = 0;
};

// The JPEG (or bitmap) preview the camera stored inside the RAW file.
RawResult readRawEmbedded(const QString &path, int maxEdge) {
    RawResult result;
    auto raw = std::make_unique<LibRaw>();
    if (raw->open_file(QFile::encodeName(path).constData()) != LIBRAW_SUCCESS)
        return result;
    result.rawLongEdge = qMax<int>(raw->imgdata.sizes.width, raw->imgdata.sizes.height);
    const int flip = raw->imgdata.sizes.flip;
    if (raw->unpack_thumb() != LIBRAW_SUCCESS)
        return result;

    int err = 0;
    libraw_processed_image_t *thumb = raw->dcraw_make_mem_thumb(&err);
    if (!thumb)
        return result;

    QImage image;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char *>(thumb->data),
                                                  int(thumb->data_size));
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "jpeg");
        // The preview carries no orientation of its own; the RAW's flip says.
        reader.setAutoTransform(false);
        if (maxEdge > 0) {
            const QSize size = reader.size();
            if (size.isValid() && qMax(size.width(), size.height()) > maxEdge * 2)
                reader.setScaledSize(size.scaled(maxEdge * 2, maxEdge * 2, Qt::KeepAspectRatio));
        }
        image = reader.read();
    } else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3 && thumb->bits == 8) {
        image = QImage(thumb->data, thumb->width, thumb->height, thumb->width * 3,
                       QImage::Format_RGB888).copy();
    }
    LibRaw::dcraw_clear_mem(thumb);

    image = Formats::applyRawFlip(image, flip);
    result.image = scaledToEdge(image, maxEdge);
    return result;
}

// Develops the RAW data itself, for files whose preview is too small.
QImage readRawDeveloped(const QString &path) {
    auto raw = std::make_unique<LibRaw>();
    raw->imgdata.params.use_camera_wb = 1;
    raw->imgdata.params.output_bps = 8;
    if (raw->open_file(QFile::encodeName(path).constData()) != LIBRAW_SUCCESS)
        return {};
    if (raw->unpack() != LIBRAW_SUCCESS)
        return {};
    if (raw->dcraw_process() != LIBRAW_SUCCESS)
        return {};
    int err = 0;
    libraw_processed_image_t *out = raw->dcraw_make_mem_image(&err);
    if (!out)
        return {};
    QImage image;
    if (out->type == LIBRAW_IMAGE_BITMAP && out->colors == 3 && out->bits == 8) {
        image = QImage(out->data, out->width, out->height, out->width * 3,
                       QImage::Format_RGB888).copy();
    }
    LibRaw::dcraw_clear_mem(out);
    return image; // dcraw output is already rotated by LibRaw
}

} // namespace

namespace Formats {

bool isImageFile(const QString &fileName) {
    const QString ext = extensionOf(fileName);
    if (ext.isEmpty())
        return false;
    return plainExtensions().contains(ext) || rawExtensions().contains(ext);
}

bool isRawFile(const QString &fileName) {
    return rawExtensions().contains(extensionOf(fileName));
}

QImage applyRawFlip(const QImage &image, int flip) {
    if (image.isNull())
        return image;
    QTransform t;
    switch (flip) {
    case 3: t.rotate(180); break;
    case 5: t.rotate(-90); break;
    case 6: t.rotate(90); break;
    default: return image;
    }
    return image.transformed(t);
}

QImage loadThumbnail(const QString &path, int maxEdge) {
    QImage image;
    if (isRawFile(path)) {
        image = readRawEmbedded(path, maxEdge).image;
        if (image.isNull())
            image = scaledToEdge(readRawDeveloped(path), maxEdge);
        if (image.isNull()) // misnamed files: a PSD saved as .CR2 exists
            image = readWithQt(path, maxEdge);
    } else {
        image = readWithQt(path, maxEdge);
    }
    return finish(image);
}

QImage loadFull(const QString &path) {
    QImage image;
    if (isRawFile(path)) {
        RawResult embedded = readRawEmbedded(path, 0);
        const int previewEdge = embedded.image.isNull()
            ? 0 : qMax(embedded.image.width(), embedded.image.height());
        // Most cameras embed a full-size JPEG; drones and phones often do
        // not. Develop the RAW when the preview is under half its size.
        if (previewEdge >= 2400 || (embedded.rawLongEdge > 0 && previewEdge * 2 >= embedded.rawLongEdge))
            image = embedded.image;
        else {
            image = readRawDeveloped(path);
            if (image.isNull())
                image = embedded.image;
        }
        if (image.isNull())
            image = readWithQt(path, 0);
    } else {
        image = readWithQt(path, 0);
    }
    return finish(image);
}

} // namespace Formats
