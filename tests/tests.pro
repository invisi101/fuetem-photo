QT += core gui widgets concurrent testlib
CONFIG += testcase c++17 link_pkgconfig
PKGCONFIG += libraw exiv2
TEMPLATE = app
TARGET = tst_fuetem

INCLUDEPATH += ../src
SOURCES += \
    tst_fuetem.cpp \
    ../src/foldermodel.cpp \
    ../src/formats.cpp \
    ../src/imageview.cpp \
    ../src/mainwindow.cpp \
    ../src/metadata.cpp \
    ../src/photogrid.cpp \
    ../src/theme.cpp \
    ../src/thumbnails.cpp
HEADERS += \
    ../src/foldermodel.h \
    ../src/formats.h \
    ../src/imageview.h \
    ../src/mainwindow.h \
    ../src/metadata.h \
    ../src/photogrid.h \
    ../src/theme.h \
    ../src/thumbnails.h
