#include "metadata.h"

#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QStringList>

#include <QImageReader>

#include <exiv2/exiv2.hpp>
#include <libraw/libraw.h>

#include <memory>

#include "formats.h"

namespace {

QString value(const Exiv2::ExifData &exif, const char *key, bool pretty = false) {
    try {
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end())
            return {};
        const std::string text = pretty ? it->print(&exif) : it->toString();
        return QString::fromStdString(text).trimmed();
    } catch (...) {
        return {};
    }
}

} // namespace

namespace Metadata {

BasicInfo basic(const QString &path) {
    BasicInfo info;
    int orientation = 1;
    try {
        auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
        if (image) {
            image->readMetadata();
            const Exiv2::ExifData &exif = image->exifData();
            info.dateTaken = value(exif, "Exif.Photo.DateTimeOriginal");
            if (info.dateTaken.isEmpty())
                info.dateTaken = value(exif, "Exif.Image.DateTime");
            orientation = value(exif, "Exif.Image.Orientation").toInt();
            if (!Formats::isRawFile(path) && image->pixelWidth() > 0 && image->pixelHeight() > 0)
                info.size = QSize(int(image->pixelWidth()), int(image->pixelHeight()));
        }
    } catch (...) {
    }

    if (Formats::isRawFile(path)) {
        auto raw = std::make_unique<LibRaw>();
        if (raw->open_file(QFile::encodeName(path).constData()) == LIBRAW_SUCCESS) {
            QSize s(raw->imgdata.sizes.width, raw->imgdata.sizes.height);
            const int flip = raw->imgdata.sizes.flip;
            if (flip == 5 || flip == 6)
                s.transpose();
            info.size = s;
        }
        return info;
    }

    if (!info.size.isValid()) {
        // Qt reports the size before any rotation; turn it upright.
        QImageReader reader(path);
        reader.setDecideFormatFromContent(true);
        info.size = reader.size();
        if (info.size.isValid() && orientation >= 5 && orientation <= 8)
            info.size.transpose();
        return info;
    }
    if (orientation >= 5 && orientation <= 8)
        info.size.transpose();
    return info;
}

PhotoInfo read(const QString &path) {
    PhotoInfo info;
    try {
        auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
        if (!image)
            return info;
        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();
        if (exif.empty())
            return info;

        info.dateTaken = value(exif, "Exif.Photo.DateTimeOriginal");
        if (info.dateTaken.isEmpty())
            info.dateTaken = value(exif, "Exif.Image.DateTime");

        QString make = value(exif, "Exif.Image.Make");
        const QString model = value(exif, "Exif.Image.Model");
        // "Canon" + "Canon EOS 80D" should read "Canon EOS 80D".
        if (!make.isEmpty() && model.startsWith(make.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive))
            make.clear();
        info.camera = QStringList({make, model}).join(QLatin1Char(' ')).trimmed();

        info.lens = value(exif, "Exif.Photo.LensModel");

        QStringList parts;
        const QString shutter = value(exif, "Exif.Photo.ExposureTime", true);
        if (!shutter.isEmpty())
            parts << shutter;
        QString aperture = value(exif, "Exif.Photo.FNumber", true);
        if (!aperture.isEmpty()) {
            if (aperture.startsWith(QLatin1Char('F')))
                aperture = QStringLiteral("f/") + aperture.mid(1);
            parts << aperture;
        }
        const QString iso = value(exif, "Exif.Photo.ISOSpeedRatings");
        if (!iso.isEmpty())
            parts << QStringLiteral("ISO ") + iso;
        QString focal = value(exif, "Exif.Photo.FocalLength", true);
        if (!focal.isEmpty())
            parts << focal.replace(QStringLiteral(".0 mm"), QStringLiteral(" mm"));
        info.exposure = parts.join(QStringLiteral("  ·  "));
    } catch (...) {
        // Unsupported or damaged file: show what we have, which is nothing.
    }
    return info;
}

QString prettyDate(const QString &exifDate) {
    const QDateTime dt = QDateTime::fromString(exifDate.left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    if (!dt.isValid())
        return exifDate;
    return QLocale(QLocale::English, QLocale::UnitedKingdom)
        .toString(dt, QStringLiteral("d MMMM yyyy, HH:mm"));
}

} // namespace Metadata
