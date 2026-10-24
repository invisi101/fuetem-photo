#pragma once

#include <QSize>
#include <QString>

struct PhotoInfo {
    QString dateTaken;  // "2018:12:24 14:03:11" as written by the camera
    QString camera;     // make and model
    QString lens;
    QString exposure;   // "1/125 s · f/2.8 · ISO 200 · 50 mm"
};

namespace Metadata {
// Reads EXIF with exiv2. Never writes. Empty fields when unavailable.
PhotoInfo read(const QString &path);

// Turns "2018:12:24 14:03:11" into "24 December 2018, 14:03".
QString prettyDate(const QString &exifDate);
}
