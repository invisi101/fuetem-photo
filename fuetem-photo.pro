QT += core gui widgets concurrent

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libraw exiv2
TARGET = fuetem-photo
TEMPLATE = app

HEADERS += \
    src/foldermodel.h \
    src/formats.h \
    src/imageview.h \
    src/mainwindow.h \
    src/metadata.h \
    src/photogrid.h \
    src/theme.h \
    src/thumbnails.h

SOURCES += \
    src/main.cpp \
    src/foldermodel.cpp \
    src/formats.cpp \
    src/imageview.cpp \
    src/mainwindow.cpp \
    src/metadata.cpp \
    src/photogrid.cpp \
    src/theme.cpp \
    src/thumbnails.cpp
