#include <QtTest>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>

#include "foldermodel.h"
#include "formats.h"
#include "imageview.h"
#include "mainwindow.h"
#include "photogrid.h"
#include "theme.h"
#include "thumbnails.h"

namespace {
QImage testImage(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    QPainter p(&img);
    p.fillRect(0, 0, w / 4, h / 4, Qt::red); // a marker in the top-left corner
    return img;
}

QByteArray fileHash(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}
}

class TestFuetem : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void recognisesPictures();
    void sortsNamesNaturallyAndMonthsByCalendar();
    void parsesOmarchyColors();
    void followsOmarchyThemeSwitch();
    void listsFoldersFirstAndSkipsOtherFiles();
    void thumbnailIsSmallAndCachedWithoutTouchingTheOriginal();
    void thumbnailRespectsExifOrientation();
    void fitSize();
    void windowNavigatesAndNeverDeletes();
    void decodesSampleFolder();

private:
    QTemporaryDir m_cache;
};

void TestFuetem::initTestCase() {
    QVERIFY(m_cache.isValid());
    // Keep the test's thumbnails and settings out of the real ones.
    qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_cache.path() + "/config").toUtf8());
}

void TestFuetem::recognisesPictures() {
    for (const char *name : {"a.jpg", "B.JPEG", "c.heic", "d.CR2", "e.dng", "f.psd", "g.tif",
                             "h.png", "i.avif", "j.jxl", "k.nef", "l.arw", "m.webp", "n.cr3"})
        QVERIFY2(Formats::isImageFile(QString::fromLatin1(name)), name);
    for (const char *name : {"clip.MOV", "clip.mp4", "a.xmp", "notes.txt", "noext", "a.zip"})
        QVERIFY2(!Formats::isImageFile(QString::fromLatin1(name)), name);
    QVERIFY(Formats::isRawFile(QStringLiteral("x.CR2")));
    QVERIFY(!Formats::isRawFile(QStringLiteral("x.heic")));
}

void TestFuetem::sortsNamesNaturallyAndMonthsByCalendar() {
    QStringList names = {QStringLiteral("December"), QStringLiteral("April"),
                         QStringLiteral("January"), QStringLiteral("IMG_10.jpg"),
                         QStringLiteral("IMG_2.jpg"), QStringLiteral("2019"), QStringLiteral("2018")};
    std::sort(names.begin(), names.end(), FolderModel::lessByName);
    QCOMPARE(names, QStringList({QStringLiteral("2018"), QStringLiteral("2019"),
                                 QStringLiteral("IMG_2.jpg"), QStringLiteral("IMG_10.jpg"),
                                 QStringLiteral("January"), QStringLiteral("April"),
                                 QStringLiteral("December")}));
    QCOMPARE(FolderModel::monthNumber(QStringLiteral("Sep")), 9);
    QCOMPARE(FolderModel::monthNumber(QStringLiteral("Mayday")), 0);
}

void TestFuetem::parsesOmarchyColors() {
    const ThemeColors c = ThemeColors::fromToml(QStringLiteral(
        "accent = \"#e80f3e\"\nselection = \"#f17e97\"\n\n# comment\nbackground = \"#050007\"\n"
        "foreground = '#f17e97'\ncolor0 = \"#000000\"\n"));
    QCOMPARE(c.accent, QColor(QStringLiteral("#e80f3e")));
    QCOMPARE(c.background, QColor(QStringLiteral("#050007")));
    QCOMPARE(c.foreground, QColor(QStringLiteral("#f17e97")));
    QVERIFY(c.dark);
    const ThemeColors light = ThemeColors::fromToml(QStringLiteral("background = \"#fafafa\""));
    QVERIFY(!light.dark);
    QCOMPARE(light.accent, ThemeColors().accent); // missing keys keep defaults
}

void TestFuetem::followsOmarchyThemeSwitch() {
    QTemporaryDir home;
    const QByteArray realHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    const QString current = home.path() + QStringLiteral("/.local/state/omarchy/current");
    auto writeTheme = [](const QString &dir, const char *accent) {
        QDir().mkpath(dir);
        QFile f(dir + QStringLiteral("/colors.toml"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("background = \"#101010\"\naccent = \"") + accent + "\"\n");
    };
    writeTheme(current + QStringLiteral("/theme"), "#ff0000");
    {
        OmarchyTheme theme;
        QCOMPARE(theme.colors().accent, QColor(Qt::red));
        QSignalSpy changed(&theme, &OmarchyTheme::changed);
        // What omarchy-theme-set does: build next-theme, remove theme, rename.
        writeTheme(current + QStringLiteral("/next-theme"), "#00ff00");
        QVERIFY(QDir(current + QStringLiteral("/theme")).removeRecursively());
        QVERIFY(QDir().rename(current + QStringLiteral("/next-theme"), current + QStringLiteral("/theme")));
        QTRY_COMPARE_WITH_TIMEOUT(theme.colors().accent, QColor(Qt::green), 3000);
        QVERIFY(changed.count() >= 1);
    }
    qputenv("HOME", realHome);
}

void TestFuetem::listsFoldersFirstAndSkipsOtherFiles() {
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("2018/March")));
    QVERIFY(testImage(40, 30).save(dir.filePath(QStringLiteral("2018/March/inside.jpg"))));
    QVERIFY(testImage(40, 30).save(dir.filePath(QStringLiteral("b.png"))));
    QVERIFY(testImage(40, 30).save(dir.filePath(QStringLiteral("a.jpg"))));
    QFile video(dir.filePath(QStringLiteral("clip.mov")));
    QVERIFY(video.open(QIODevice::WriteOnly));
    video.write("not a picture");
    video.close();

    FolderModel model;
    QSignalSpy loaded(&model, &FolderModel::loaded);
    model.setFolder(dir.path());
    QVERIFY(loaded.wait(5000));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.entry(0).isDir);
    QCOMPARE(model.entry(0).name, QStringLiteral("2018"));
    QCOMPARE(model.entry(0).cover, dir.filePath(QStringLiteral("2018/March/inside.jpg")));
    QCOMPARE(model.entry(1).name, QStringLiteral("a.jpg"));
    QCOMPARE(model.entry(2).name, QStringLiteral("b.png"));
    QCOMPARE(model.imageCount(), 2);
    QCOMPARE(model.neighbourImage(1, 1), 2);
    QCOMPARE(model.neighbourImage(1, -1), -1); // folders are never "pictures"

    model.setSort(FolderModel::Sort::Name, true);
    QVERIFY(model.entry(0).isDir);
    QCOMPARE(model.entry(1).name, QStringLiteral("b.png"));
}

void TestFuetem::thumbnailIsSmallAndCachedWithoutTouchingTheOriginal() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("big.jpg"));
    QVERIFY(testImage(3000, 2000).save(path, "JPEG", 90));
    const QByteArray before = fileHash(path);
    const QDateTime modified = QFileInfo(path).lastModified();

    const QImage thumb = ThumbnailLoader::loadOrCreate(path);
    QVERIFY(!thumb.isNull());
    QCOMPARE(qMax(thumb.width(), thumb.height()), ThumbnailLoader::Edge);
    QVERIFY(ThumbnailLoader::cacheFileFor(path).startsWith(m_cache.path()));
    QVERIFY(QFileInfo::exists(ThumbnailLoader::cacheFileFor(path)));
    // Second time from the disk cache.
    QCOMPARE(ThumbnailLoader::loadOrCreate(path).size(), thumb.size());

    QCOMPARE(fileHash(path), before);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList({QStringLiteral("big.jpg")}));
}

void TestFuetem::thumbnailRespectsExifOrientation() {
    if (QStandardPaths::findExecutable(QStringLiteral("exiftool")).isEmpty())
        QSKIP("exiftool not installed");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("turned.jpg"));
    QVERIFY(testImage(1600, 1200).save(path, "JPEG", 90));
    QProcess exiftool;
    exiftool.start(QStringLiteral("exiftool"),
                   {QStringLiteral("-q"), QStringLiteral("-overwrite_original"),
                    QStringLiteral("-Orientation=6"), QStringLiteral("-n"), path});
    QVERIFY(exiftool.waitForFinished(10000));
    const QImage thumb = Formats::loadThumbnail(path, 600);
    QCOMPARE(thumb.size(), QSize(450, 600)); // rotated to portrait
    const QImage full = Formats::loadFull(path);
    QCOMPARE(full.size(), QSize(1200, 1600));
    // The red corner of the stored image ends up top-right after a 90° turn.
    const QColor topRight = full.pixelColor(full.width() - 20, 20);
    QVERIFY(topRight.red() > 200 && topRight.green() < 80);
}

void TestFuetem::fitSize() {
    QCOMPARE(ImageView::fitSizeFor(QSize(4000, 3000), QSize(800, 800), 1.0), QSize(800, 600));
    QCOMPARE(ImageView::fitSizeFor(QSize(4000, 3000), QSize(800, 800), 2.0), QSize(1600, 1200));
    QCOMPARE(ImageView::fitSizeFor(QSize(400, 300), QSize(800, 800), 1.0), QSize(400, 300));
}

void TestFuetem::windowNavigatesAndNeverDeletes() {
    QTemporaryDir dir;
    for (int i = 1; i <= 3; ++i)
        QVERIFY(testImage(800, 600).save(dir.filePath(QStringLiteral("p%1.jpg").arg(i))));
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QSignalSpy loaded(window.model(), &FolderModel::loaded);
    window.openPath(dir.filePath(QStringLiteral("p1.jpg")));
    QVERIFY(loaded.wait(5000));
    QTRY_VERIFY(window.inSingleView());
    QCOMPARE(window.currentRow(), 0);

    QTest::keyClick(window.imageView(), Qt::Key_Right);
    QCOMPARE(window.currentRow(), 1);
    QTest::keyClick(window.imageView(), Qt::Key_Right);
    QTest::keyClick(window.imageView(), Qt::Key_Right); // stays on the last
    QCOMPARE(window.currentRow(), 2);
    QTest::keyClick(window.imageView(), Qt::Key_Left);
    QCOMPARE(window.currentRow(), 1);

    QTRY_VERIFY(window.imageView()->hasFullImage());
    QVERIFY(window.imageView()->isFit());
    QTest::keyClick(window.imageView(), Qt::Key_Z);
    QVERIFY(!window.imageView()->isFit());
    QCOMPARE(window.imageView()->scale(), 1.0);
    QTest::keyClick(window.imageView(), Qt::Key_0);
    QVERIFY(window.imageView()->isFit());

    QCOMPARE(window.lights(), 0);
    QTest::keyClick(window.imageView(), Qt::Key_L);
    QCOMPARE(window.lights(), 1);
    QTest::keyClick(window.imageView(), Qt::Key_L);
    QCOMPARE(window.lights(), 2);
    QTest::keyClick(window.imageView(), Qt::Key_L);
    QCOMPARE(window.lights(), 0);

    QTest::keyClick(window.imageView(), Qt::Key_Delete);
    QTest::keyClick(window.imageView(), Qt::Key_Delete, Qt::ShiftModifier);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 3);

    QTest::keyClick(window.imageView(), Qt::Key_Escape);
    QVERIFY(!window.inSingleView());
    QCOMPARE(window.grid()->currentIndex().row(), 1); // back where we were

    // Lightroom keys: E opens the selected picture, G returns; D does nothing.
    QTest::keyClick(window.grid(), Qt::Key_E);
    QVERIFY(window.inSingleView());
    QCOMPARE(window.currentRow(), 1);
    QTest::keyClick(window.imageView(), Qt::Key_G);
    QVERIFY(!window.inSingleView());
    QTest::keyClick(window.grid(), Qt::Key_D);
    QVERIFY(!window.inSingleView());

    // Letters typed into a text field stay there.
    auto *field = new QLineEdit(window.centralWidget());
    field->show();
    field->setFocus();
    QTest::keyClicks(field, QStringLiteral("glide"));
    QCOMPARE(field->text(), QStringLiteral("glide"));
    QVERIFY(!window.inSingleView());
    QCOMPARE(window.lights(), 0);
}

// Set FUETEM_SAMPLE_DIR to a folder of real photos to check every format
// found there decodes (e.g. HEIC, CR2, DNG, PSD).
void TestFuetem::decodesSampleFolder() {
    const QString sampleDir = qEnvironmentVariable("FUETEM_SAMPLE_DIR");
    if (sampleDir.isEmpty())
        QSKIP("FUETEM_SAMPLE_DIR not set");
    QHash<QString, int> ok;
    QStringList failures;
    QDirIterator it(sampleDir, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!Formats::isImageFile(path))
            continue;
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ok.value(ext) >= 3)
            continue;
        if (Formats::loadThumbnail(path, 600).isNull())
            failures << path;
        else
            ok[ext]++;
    }
    qInfo() << "decoded per format:" << ok;
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
}

QTEST_MAIN(TestFuetem)
#include "tst_fuetem.moc"
