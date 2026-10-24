#include "metadata.h"

#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QStringList>

#include <exiv2/exiv2.hpp>

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
