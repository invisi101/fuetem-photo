#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>

struct PhotoInfo {
    QString dateTaken;  // "2018:12:24 14:03:11" as written by the camera
    QString camera;     // make and model
    QString lens;
    QString exposure;   // "1/125 s · f/2.8 · ISO 200 · 50 mm"
};

// The little the grid shows under a thumbnail.
struct BasicInfo {
    QString dateTaken; // EXIF, as written by the camera; empty when absent
    QSize size;        // upright pixel size; invalid when unknown
};

namespace Metadata {
// Date taken and upright dimensions, read from the file's headers only.
BasicInfo basic(const QString &path);

// When the picture was taken, from EXIF; invalid when the file has no date.
QDateTime dateTaken(const QString &path);

// Reads EXIF with exiv2. Never writes. Empty fields when unavailable.
PhotoInfo read(const QString &path);

// Turns "2018:12:24 14:03:11" into "24 December 2018, 14:03".
QString prettyDate(const QString &exifDate);
}
