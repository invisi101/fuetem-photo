#include "mainwindow.h"

#include "foldermodel.h"
#include "formats.h"
#include "imageview.h"
#include "photogrid.h"
#include "thumbnails.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QKeyEvent>
#include <QLocale>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int SlideshowMs = 4000;

QColor mix(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString rgba(QColor c, qreal alpha = 1.0) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(alpha, 0, 'f', 2);
}

QString humanSize(qint64 bytes) {
    return QLocale(QLocale::English, QLocale::UnitedKingdom)
        .formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

class KeyFilter : public QObject {
public:
    explicit KeyFilter(std::function<bool(QObject *, QKeyEvent *)> handler, QObject *parent)
        : QObject(parent), m_handler(std::move(handler)) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress)
            return m_handler(obj, static_cast<QKeyEvent *>(event));
        return false;
    }
private:
    std::function<bool(QObject *, QKeyEvent *)> m_handler;
};

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Fuetem Photo"));
    m_theme = new OmarchyTheme(this);
    m_model = new FolderModel(this);
    m_loader = new ThumbnailLoader(this);

    m_fullPool.setMaxThreadCount(2);
    m_fullCache.setMaxCost(600 * 1024); // kilobytes of decoded pictures

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildTopBar();
    layout->addWidget(m_topBar);

    m_stack = new QStackedWidget(central);
    m_grid = new PhotoGrid(m_model, m_loader, m_stack);
    m_view = new ImageView(m_stack);
    m_stack->addWidget(m_grid);
    m_stack->addWidget(m_view);
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    m_toast = new QLabel(central);
    m_toast->setAlignment(Qt::AlignCenter);
    m_toast->hide();
    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(1800);
    connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    m_help = new QLabel(central);
    m_help->setTextFormat(Qt::RichText);
    m_help->setAlignment(Qt::AlignCenter);
    m_help->hide();

    m_slideshow.setInterval(SlideshowMs);
    connect(&m_slideshow, &QTimer::timeout, this, [this]() {
        if (!inSingleView()) {
            m_slideshow.stop();
            return;
        }
        int next = m_model->neighbourImage(m_row, 1);
        if (next < 0)
            next = m_model->neighbourImage(m_model->firstImageRow() - 1, 1);
        if (next >= 0)
            showSingle(next);
    });

    connect(m_grid, &PhotoGrid::openRequested, this, [this](int row) {
        if (row < 0 || row >= m_model->rowCount())
            return;
        const FolderEntry &e = m_model->entry(row);
        if (e.isDir)
            setFolder(e.path);
        else
            showSingle(row);
    });
    connect(m_grid, &PhotoGrid::thumbSizeChanged, this, [](int size) {
        QSettings().setValue(QStringLiteral("grid/thumbSize"), size);
    });
    connect(m_view, &ImageView::backRequested, this, &MainWindow::showGrid);
    connect(m_model, &FolderModel::loaded, this, [this]() {
        int select = -1;
        if (!m_pendingSelect.isEmpty())
            select = m_model->rowOfPath(m_pendingSelect);
        if (select < 0 && m_model->rowCount() > 0)
            select = 0;
        m_pendingSelect.clear();
        if (select >= 0) {
            m_grid->setCurrentIndex(m_model->index(select, 0));
            m_grid->scrollTo(m_model->index(select, 0), QAbstractItemView::PositionAtCenter);
        }
        updateStatus();
        if (!m_pendingSingle.isEmpty()) {
            const int row = m_model->rowOfPath(m_pendingSingle);
            m_pendingSingle.clear();
            if (row >= 0)
                showSingle(row);
        }
    });
    connect(m_loader, &ThumbnailLoader::ready, this, [this](const QString &path) {
        // The stand-in for a picture that is still decoding.
        if (inSingleView() && path == currentPath() && !m_view->hasFullImage()) {
            if (const QImage *thumb = m_loader->cached(path))
                m_view->setPlaceholder(*thumb);
        }
    });
    connect(m_theme, &OmarchyTheme::changed, this, &MainWindow::applyAppearance);

    auto *filter = new KeyFilter([this](QObject *obj, QKeyEvent *event) {
        if (QApplication::activeModalWidget())
            return false;
        auto *w = qobject_cast<QWidget *>(obj);
        if (!w || w->window() != this)
            return false;
        // Typing into a text field types; it never triggers the letter keys.
        if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w)
                || qobject_cast<QPlainTextEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w))
            return false;
        keyPressEvent(event);
        return event->isAccepted();
    }, this);
    qApp->installEventFilter(filter);

    QSettings settings;
    const int sortKey = settings.value(QStringLiteral("sort/key"), 0).toInt();
    m_model->setSort(sortKey == 1 ? FolderModel::Sort::Date : FolderModel::Sort::Name,
                     settings.value(QStringLiteral("sort/descending"), false).toBool());
    m_grid->setThumbSize(settings.value(QStringLiteral("grid/thumbSize"), 220).toInt());
    m_grid->setDetails(settings.value(QStringLiteral("grid/details"), false).toBool());
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1400, 900);

    applyAppearance();
    m_stack->setCurrentWidget(m_grid);
    m_grid->setFocus();
}

MainWindow::~MainWindow() {
    m_fullPool.clear();
    m_fullPool.waitForDone();
}

void MainWindow::buildTopBar() {
    m_topBar = new QWidget(this);
    m_topBar->setObjectName(QStringLiteral("topBar"));
    m_topBar->setAttribute(Qt::WA_StyledBackground, true);
    auto *bar = new QHBoxLayout(m_topBar);
    bar->setContentsMargins(14, 8, 14, 8);
    bar->setSpacing(6);

    auto *crumbHost = new QWidget(m_topBar);
    m_crumbs = new QHBoxLayout(crumbHost);
    m_crumbs->setContentsMargins(0, 0, 0, 0);
    m_crumbs->setSpacing(2);
    bar->addWidget(crumbHost, 0);
    bar->addStretch(1);

    m_status = new QLabel(m_topBar);
    m_status->setObjectName(QStringLiteral("status"));
    bar->addWidget(m_status);
    bar->addSpacing(10);

    auto makeButton = [this, bar](const QString &text, const QString &tip) {
        auto *b = new QToolButton(m_topBar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        bar->addWidget(b);
        return b;
    };
    m_sortButton = makeButton(QString(), QStringLiteral("Change the order (S)"));
    connect(m_sortButton, &QToolButton::clicked, this, &MainWindow::cycleSort);
    m_openButton = makeButton(QStringLiteral("Open"), QStringLiteral("Open a folder (Ctrl+O)"));
    connect(m_openButton, &QToolButton::clicked, this, &MainWindow::openFolderDialog);
    m_helpButton = makeButton(QStringLiteral("?"), QStringLiteral("Keyboard shortcuts (?)"));
    connect(m_helpButton, &QToolButton::clicked, this, &MainWindow::toggleHelp);
}

void MainWindow::rebuildBreadcrumb() {
    while (QLayoutItem *item = m_crumbs->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QString folder = m_model->folder();
    const QString home = QDir::homePath();
    QStringList parts;
    QStringList paths;
    QString rest;
    if (folder == home || folder.startsWith(home + QLatin1Char('/'))) {
        parts << QStringLiteral("Home");
        paths << home;
        rest = folder.mid(home.size());
    } else {
        parts << QStringLiteral("/");
        paths << QStringLiteral("/");
        rest = folder;
    }
    QString built = paths.first();
    for (const QString &segment : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        built = built.endsWith(QLatin1Char('/')) ? built + segment : built + QLatin1Char('/') + segment;
        parts << segment;
        paths << built;
    }

    // Long paths keep the root and the last few steps.
    constexpr int MaxShown = 5;
    int start = 0;
    if (parts.size() > MaxShown + 1)
        start = parts.size() - MaxShown;

    auto addCrumb = [this](const QString &text, const QString &path, bool last) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("crumb", true);
        b->setProperty("current", last);
        connect(b, &QToolButton::clicked, this, [this, path]() {
            if (path != m_model->folder())
                setFolder(path, QString());
        });
        m_crumbs->addWidget(b);
    };
    auto addSeparator = [this]() {
        auto *l = new QLabel(QStringLiteral("›"));
        l->setProperty("separator", true);
        m_crumbs->addWidget(l);
    };

    addCrumb(parts.first(), paths.first(), parts.size() == 1);
    if (start > 1) {
        addSeparator();
        addCrumb(QStringLiteral("…"), paths.at(start - 1), false);
    }
    for (int i = qMax(1, start); i < parts.size(); ++i) {
        addSeparator();
        addCrumb(parts.at(i), paths.at(i), i == parts.size() - 1);
    }
    applyAppearance();
}

void MainWindow::applyAppearance() {
    const ThemeColors &c = m_theme->colors();
    QColor background = c.background;
    if (m_lights == 1)
        background = mix(c.background, Qt::black, 0.7);
    else if (m_lights == 2)
        background = Qt::black; // lights out is black whatever the theme

    QPalette pal = palette();
    pal.setColor(QPalette::Window, background);
    pal.setColor(QPalette::Base, background);
    pal.setColor(QPalette::WindowText, c.foreground);
    pal.setColor(QPalette::Text, c.foreground);
    pal.setColor(QPalette::ButtonText, c.foreground);
    pal.setColor(QPalette::Highlight, c.selection);
    pal.setColor(QPalette::Accent, c.accent);
    setPalette(pal);

    m_grid->setColors(c, background, m_lights > 0);
    m_view->setBackground(background);
    m_view->setTextColor(c.foreground);
    m_view->setShowTitle(m_lights == 0);
    m_view->setAutoHideCursor(m_lights == 2 || isFullScreen());
    m_topBar->setVisible(m_lights < 2 && !inSingleView());

    const qreal textAlpha = m_lights == 1 ? 0.45 : 1.0;
    const QColor hover = mix(background, c.foreground, 0.12);
    setStyleSheet(QStringLiteral(
        "QWidget#topBar { background: %1; }"
        "QToolButton { color: %2; background: transparent; border: none; border-radius: 8px;"
        "  padding: 6px 10px; font-size: 18px; }"
        "QToolButton:hover { background: %3; }"
        "QToolButton[crumb=\"true\"][current=\"true\"] { color: %4; font-weight: 600; }"
        "QLabel[separator=\"true\"] { color: %5; font-size: 18px; padding: 0 2px; }"
        "QLabel#status { color: %5; font-size: 17px; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %6; border-radius: 4px; min-height: 40px; }"
        "QScrollBar::handle:vertical:hover { background: %7; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QToolTip { color: %2; background: %1; border: 1px solid %6; font-size: 17px; padding: 4px; }")
        .arg(rgba(background), rgba(c.foreground, textAlpha), rgba(hover),
             rgba(c.accent, qMax(textAlpha, 0.6)), rgba(c.foreground, 0.6 * textAlpha),
             rgba(c.foreground, 0.22), rgba(c.foreground, 0.4)));

    const QColor panel = mix(c.background, Qt::black, 0.25);
    m_toast->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 12px;"
        " padding: 12px 22px; font-size: 18px; }")
        .arg(rgba(c.foreground), rgba(panel, 0.95), rgba(c.accent, 0.7)));
    m_help->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 16px;"
        " padding: 26px 34px; font-size: 18px; }")
        .arg(rgba(c.foreground), rgba(panel), rgba(c.accent, 0.7)));
    update();
}

void MainWindow::openPath(const QString &rawPath) {
    QString path = rawPath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();
    const QFileInfo info(path);
    if (info.isDir()) {
        setFolder(info.absoluteFilePath());
    } else if (info.isFile()) {
        m_pendingSingle = info.absoluteFilePath();
        setFolder(info.absolutePath(), info.absoluteFilePath());
    } else {
        setFolder(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        toast(QStringLiteral("Not found: %1").arg(path));
    }
}

void MainWindow::setFolder(const QString &folder, const QString &selectPath) {
    QString target = QDir(folder).absolutePath();
    if (!QFileInfo(target).isDir())
        target = QDir::homePath();
    m_slideshow.stop();
    m_pendingSelect = selectPath;
    m_model->setFolder(target);
    QSettings().setValue(QStringLiteral("lastFolder"), target);
    if (inSingleView() && m_pendingSingle.isEmpty())
        showGrid();
    m_row = -1;
    m_status->setText(QStringLiteral("Loading…"));
    rebuildBreadcrumb();
    updateTitle();
}

bool MainWindow::inSingleView() const {
    return m_stack->currentWidget() == m_view;
}

QString MainWindow::currentPath() const {
    if (m_row < 0 || m_row >= m_model->rowCount())
        return {};
    return m_model->entry(m_row).path;
}

void MainWindow::updateStatus() {
    const int images = m_model->imageCount();
    const int folders = m_model->folderCount();
    QStringList bits;
    if (folders)
        bits << QStringLiteral("%1 folder%2").arg(folders).arg(folders == 1 ? "" : "s");
    if (images || !folders)
        bits << QStringLiteral("%1 picture%2").arg(images).arg(images == 1 ? "" : "s");
    m_status->setText(bits.join(QStringLiteral("  ·  ")));
    const bool desc = m_model->descending();
    const QString arrow = desc ? QStringLiteral("↓") : QStringLiteral("↑");
    m_sortButton->setText((m_model->sortKey() == FolderModel::Sort::Name ? QStringLiteral("Name ")
                                                                       : QStringLiteral("Date "))
                          + arrow);
}

void MainWindow::updateTitle() {
    QString title = QStringLiteral("Fuetem Photo");
    if (inSingleView() && m_row >= 0)
        title = m_model->entry(m_row).name + QStringLiteral(" — Fuetem Photo");
    else if (!m_model->folder().isEmpty())
        title = QFileInfo(m_model->folder()).fileName() + QStringLiteral(" — Fuetem Photo");
    setWindowTitle(title);
}

void MainWindow::showGrid() {
    m_slideshow.stop();
    m_stack->setCurrentWidget(m_grid);
    if (m_row >= 0)
        m_grid->selectRow(m_row);
    m_view->clear();
    m_grid->setFocus();
    applyAppearance();
    updateTitle();
}

void MainWindow::showSingle(int row) {
    if (row < 0 || row >= m_model->rowCount() || m_model->entry(row).isDir)
        return;
    m_row = row;
    if (!inSingleView()) {
        m_stack->setCurrentWidget(m_view);
        m_view->setFocus();
        applyAppearance();
    }
    m_grid->selectRow(row);
    loadCurrent();
    updateTitle();
}

void MainWindow::step(int delta) {
    const int next = m_model->neighbourImage(m_row, delta);
    if (next >= 0)
        showSingle(next);
}

void MainWindow::loadCurrent() {
    const QString path = currentPath();
    if (path.isEmpty())
        return;
    const FolderEntry &e = m_model->entry(m_row);
    const int index = m_row - m_model->firstImageRow() + 1;
    m_view->setTitle(QStringLiteral("%1      %2 / %3").arg(e.name).arg(index).arg(m_model->imageCount()));

    if (const QImage *full = m_fullCache.object(path)) {
        m_view->setImage(*full);
    } else {
        if (const QImage *thumb = m_loader->cached(path))
            m_view->setPlaceholder(*thumb);
        else {
            m_view->clear();
            m_loader->setQueue({path});
        }
        requestFull(path, true);
    }
    if (m_showInfo)
        requestInfo(path);
    refreshInfo();
    prefetch();
}

void MainWindow::prefetch() {
    // The next two and the previous one, so arrowing along is instant.
    const int next = m_model->neighbourImage(m_row, 1);
    const int next2 = next >= 0 ? m_model->neighbourImage(next, 1) : -1;
    const int prev = m_model->neighbourImage(m_row, -1);
    for (int r : {next, prev, next2}) {
        if (r >= 0)
            requestFull(m_model->entry(r).path, false);
    }
}

void MainWindow::requestFull(const QString &path, bool) {
    if (m_fullCache.contains(path) || m_fullLoading.contains(path))
        return;
    m_fullLoading.insert(path);
    const QSize viewSize = m_view->area().size().toSize();
    const qreal dpr = m_view->devicePixelRatioF();
    QPointer<MainWindow> self(this);
    m_fullPool.start([self, path, viewSize, dpr]() {
        const QImage image = Formats::loadFull(path);
        QImage fitted;
        const QSize fit = ImageView::fitSizeFor(image.size(), viewSize, dpr);
        if (!image.isNull() && fit.isValid() && fit != image.size())
            fitted = image.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QMetaObject::invokeMethod(qApp, [self, path, image, fitted]() {
            if (self)
                self->fullLoaded(path, image, fitted);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::fullLoaded(const QString &path, const QImage &image, const QImage &fitted) {
    m_fullLoading.remove(path);
    if (!image.isNull())
        m_fullCache.insert(path, new QImage(image), qMax<int>(1, int(image.sizeInBytes() / 1024)));
    if (!inSingleView() || path != currentPath())
        return;
    if (image.isNull()) {
        m_view->clear();
        m_view->setMessage(QStringLiteral("Can't show %1").arg(QFileInfo(path).fileName()));
        return;
    }
    m_view->setImage(image, fitted);
    refreshInfo(); // now the dimensions are known
}

void MainWindow::requestInfo(const QString &path) {
    if (m_infoCache.contains(path) || m_infoLoading.contains(path))
        return;
    m_infoLoading.insert(path);
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([self, path]() {
        const PhotoInfo info = Metadata::read(path);
        QMetaObject::invokeMethod(qApp, [self, path, info]() {
            if (!self)
                return;
            self->m_infoLoading.remove(path);
            self->m_infoCache.insert(path, info);
            if (self->m_showInfo && path == self->currentPath())
                self->refreshInfo();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::toggleInfo() {
    m_showInfo = !m_showInfo;
    m_view->setShowInfo(m_showInfo);
    refreshInfo();
}

void MainWindow::refreshInfo() {
    const QString path = currentPath();
    if (!m_showInfo || path.isEmpty())
        return;
    const FolderEntry &e = m_model->entry(m_row);
    QStringList lines;
    lines << e.name;
    QString dims;
    if (m_view->imageSize().isValid())
        dims = QStringLiteral("%1 × %2  ·  ").arg(m_view->imageSize().width()).arg(m_view->imageSize().height());
    lines << dims + humanSize(e.size);
    auto it = m_infoCache.constFind(path);
    if (it == m_infoCache.constEnd()) {
        requestInfo(path);
    } else {
        if (!it->dateTaken.isEmpty())
            lines << Metadata::prettyDate(it->dateTaken);
        if (!it->camera.isEmpty())
            lines << it->camera;
        if (!it->lens.isEmpty())
            lines << it->lens;
        if (!it->exposure.isEmpty())
            lines << it->exposure;
    }
    lines << QStringLiteral("%1 of %2").arg(m_row - m_model->firstImageRow() + 1).arg(m_model->imageCount());
    m_view->setInfoLines(lines);
}

void MainWindow::cycleLights() {
    m_lights = (m_lights + 1) % 3;
    applyAppearance();
    static const char *names[] = {"Lights on", "Lights dimmed", "Lights out"};
    toast(QString::fromLatin1(names[m_lights]));
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
    } else {
        m_wasMaximized = isMaximized();
        showFullScreen();
    }
    QTimer::singleShot(50, this, &MainWindow::applyAppearance);
}

void MainWindow::toggleSlideshow() {
    if (m_slideshow.isActive()) {
        m_slideshow.stop();
        toast(QStringLiteral("Slideshow stopped"));
        return;
    }
    if (!inSingleView()) {
        int row = m_grid->currentIndex().isValid() ? m_grid->currentIndex().row() : -1;
        if (row < 0 || m_model->entry(row).isDir)
            row = m_model->neighbourImage(m_model->firstImageRow() - 1, 1);
        if (row < 0) {
            toast(QStringLiteral("No pictures here"));
            return;
        }
        showSingle(row);
    }
    m_slideshow.start();
    toast(QStringLiteral("Slideshow: a new picture every %1 seconds").arg(SlideshowMs / 1000));
}

void MainWindow::openFolderDialog() {
    const QString start = m_model->folder().isEmpty() ? QDir::homePath() : m_model->folder();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open a folder"), start);
    if (!dir.isEmpty())
        setFolder(dir);
}

void MainWindow::goUp() {
    const QString folder = m_model->folder();
    QDir dir(folder);
    if (!dir.cdUp())
        return;
    setFolder(dir.absolutePath(), folder);
}

void MainWindow::cycleSort() {
    // Name ↑ → Name ↓ → Date ↑ → Date ↓
    const bool name = m_model->sortKey() == FolderModel::Sort::Name;
    const bool desc = m_model->descending();
    FolderModel::Sort sort = m_model->sortKey();
    bool nextDesc = !desc;
    if (desc)
        sort = name ? FolderModel::Sort::Date : FolderModel::Sort::Name;
    const QString keep = m_grid->currentIndex().isValid()
        ? m_model->entry(m_grid->currentIndex().row()).path : QString();
    m_model->setSort(sort, nextDesc);
    QSettings settings;
    settings.setValue(QStringLiteral("sort/key"), sort == FolderModel::Sort::Date ? 1 : 0);
    settings.setValue(QStringLiteral("sort/descending"), nextDesc);
    const int row = m_model->rowOfPath(keep);
    if (row >= 0)
        m_grid->selectRow(row);
    updateStatus();
    toast(QStringLiteral("Sorted by %1, %2")
              .arg(sort == FolderModel::Sort::Name ? QStringLiteral("name") : QStringLiteral("date taken"),
                   nextDesc ? (sort == FolderModel::Sort::Name ? QStringLiteral("Z to A") : QStringLiteral("newest first"))
                            : (sort == FolderModel::Sort::Name ? QStringLiteral("A to Z") : QStringLiteral("oldest first"))));
}

void MainWindow::copyPath() {
    QString path = inSingleView() ? currentPath() : QString();
    if (path.isEmpty() && m_grid->currentIndex().isValid())
        path = m_model->entry(m_grid->currentIndex().row()).path;
    if (path.isEmpty())
        path = m_model->folder();
    QApplication::clipboard()->setText(path);
    toast(QStringLiteral("Copied the path"));
}

void MainWindow::showInFileManager() {
    QString folder = m_model->folder();
    if (!inSingleView() && m_grid->currentIndex().isValid()) {
        const FolderEntry &e = m_model->entry(m_grid->currentIndex().row());
        if (e.isDir)
            folder = e.path;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::toast(const QString &text) {
    m_toast->setText(text);
    m_toast->adjustSize();
    positionOverlays();
    m_toast->show();
    m_toast->raise();
    m_toastTimer.start();
}

void MainWindow::toggleHelp() {
    if (m_help->isVisible()) {
        m_help->hide();
        return;
    }
    const QColor accent = m_theme->colors().accent;
    const QString keyStyle = QStringLiteral("color:%1; font-weight:600; padding-right:28px;").arg(accent.name());
    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("Enter, double-click"), QStringLiteral("Open the picture or folder")},
        {QStringLiteral("E"), QStringLiteral("Open the selected picture")},
        {QStringLiteral("G, Esc, double-click"), QStringLiteral("Back to the grid")},
        {QStringLiteral("← →"), QStringLiteral("Previous / next picture")},
        {QStringLiteral("Backspace"), QStringLiteral("Up one folder")},
        {QStringLiteral("Scroll, pinch, + −"), QStringLiteral("Zoom the picture")},
        {QStringLiteral("Z or 1  /  0"), QStringLiteral("Actual size  /  fit to window")},
        {QStringLiteral("Ctrl+scroll, + −"), QStringLiteral("Thumbnail size (grid)")},
        {QStringLiteral("D"), QStringLiteral("Details under thumbnails (grid)")},
        {QStringLiteral("L"), QStringLiteral("Lights: on, dimmed, out")},
        {QStringLiteral("F, F11"), QStringLiteral("Fullscreen")},
        {QStringLiteral("I"), QStringLiteral("Picture information")},
        {QStringLiteral("Space"), QStringLiteral("Slideshow")},
        {QStringLiteral("S"), QStringLiteral("Order by name or date")},
        {QStringLiteral("Ctrl+O"), QStringLiteral("Open a folder")},
        {QStringLiteral("Ctrl+C"), QStringLiteral("Copy the path")},
        {QStringLiteral("Ctrl+Shift+O"), QStringLiteral("Show the folder in Files")},
        {QStringLiteral("Ctrl+Q"), QStringLiteral("Quit")},
    };
    QString html = QStringLiteral("<div style='font-size:22px; font-weight:600; margin-bottom:14px;'>Fuetem Photo</div>"
                                  "<table cellspacing='0' cellpadding='5'>");
    for (const auto &row : rows)
        html += QStringLiteral("<tr><td style='%1'>%2</td><td>%3</td></tr>")
                    .arg(keyStyle, row.first.toHtmlEscaped(), row.second.toHtmlEscaped());
    html += QStringLiteral("</table><div style='margin-top:14px; font-size:17px;'>"
                           "A viewer only: pictures are never changed, moved or deleted.</div>");
    m_help->setText(html);
    m_help->adjustSize();
    positionOverlays();
    m_help->show();
    m_help->raise();
}

void MainWindow::positionOverlays() {
    QWidget *c = centralWidget();
    if (!c)
        return;
    m_toast->move((c->width() - m_toast->width()) / 2, c->height() - m_toast->height() - 36);
    m_help->move((c->width() - m_help->width()) / 2, qMax(10, (c->height() - m_help->height()) / 2));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    positionOverlays();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool plain = !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    event->accept();

    if (m_help->isVisible() && (key == Qt::Key_Escape || key == Qt::Key_Question || key == Qt::Key_F1)) {
        m_help->hide();
        return;
    }

    // Everywhere.
    if (ctrl && key == Qt::Key_Q) { close(); return; }
    if (ctrl && key == Qt::Key_W) { close(); return; }
    if (ctrl && shift && key == Qt::Key_O) { showInFileManager(); return; }
    if (ctrl && key == Qt::Key_O) { openFolderDialog(); return; }
    if (ctrl && key == Qt::Key_C) { copyPath(); return; }
    if (key == Qt::Key_F11 || (plain && key == Qt::Key_F)) { toggleFullscreen(); return; }
    if (plain && key == Qt::Key_L) { cycleLights(); return; }
    if (plain && key == Qt::Key_I) {
        if (inSingleView())
            toggleInfo();
        else
            toast(QStringLiteral("Open a picture to see its information"));
        return;
    }
    if (plain && key == Qt::Key_Space) { toggleSlideshow(); return; }
    // Lightroom's G (grid) and E (loupe).
    if (plain && key == Qt::Key_G) {
        if (inSingleView())
            showGrid();
        return;
    }
    if (plain && key == Qt::Key_E) {
        if (!inSingleView()) {
            const QModelIndex current = m_grid->currentIndex();
            if (current.isValid() && !m_model->entry(current.row()).isDir)
                showSingle(current.row());
            else
                toast(QStringLiteral("Select a picture first"));
        }
        return;
    }
    if (key == Qt::Key_Question || key == Qt::Key_F1 || (plain && key == Qt::Key_H)) { toggleHelp(); return; }
    if (key == Qt::Key_Delete) {
        toast(QStringLiteral("Fuetem Photo is a viewer: it never deletes pictures"));
        return;
    }

    if (inSingleView()) {
        if (plain) {
            switch (key) {
            case Qt::Key_Escape:
                if (m_slideshow.isActive()) { m_slideshow.stop(); toast(QStringLiteral("Slideshow stopped")); }
                else showGrid();
                return;
            case Qt::Key_Return: case Qt::Key_Enter: showGrid(); return;
            case Qt::Key_Right: case Qt::Key_PageDown: case Qt::Key_Down: step(1); return;
            case Qt::Key_Left: case Qt::Key_PageUp: case Qt::Key_Up: step(-1); return;
            case Qt::Key_Home: showSingle(m_model->neighbourImage(m_model->firstImageRow() - 1, 1)); return;
            case Qt::Key_End: showSingle(m_model->neighbourImage(m_model->rowCount(), -1)); return;
            case Qt::Key_Z: case Qt::Key_1: m_view->toggleActualSize(); return;
            case Qt::Key_0: m_view->fit(); return;
            case Qt::Key_Plus: case Qt::Key_Equal: m_view->zoomBy(1.25); return;
            case Qt::Key_Minus: m_view->zoomBy(0.8); return;
            case Qt::Key_Backspace: showGrid(); return;
            default: break;
            }
        }
        event->ignore();
        return;
    }

    // Grid.
    if (key == Qt::Key_Escape && plain) {
        if (isFullScreen()) toggleFullscreen();
        return;
    }
    if ((plain && key == Qt::Key_Backspace) || ((mods & Qt::AltModifier) && key == Qt::Key_Up)) {
        goUp();
        return;
    }
    if (key == Qt::Key_Plus || key == Qt::Key_Equal) { m_grid->setThumbSize(m_grid->thumbSize() + 40); return; }
    if (key == Qt::Key_Minus) { m_grid->setThumbSize(m_grid->thumbSize() - 40); return; }
    if (plain && key == Qt::Key_S) { cycleSort(); return; }
    if (plain && key == Qt::Key_D) {
        m_grid->setDetails(!m_grid->details());
        QSettings().setValue(QStringLiteral("grid/details"), m_grid->details());
        toast(m_grid->details() ? QStringLiteral("Details on") : QStringLiteral("Details off"));
        return;
    }
    if (plain && key >= Qt::Key_A && key <= Qt::Key_Z) {
        return; // swallow, rather than jumping by typed letters
    }
    event->ignore();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (!m_model->folder().isEmpty())
        settings.setValue(QStringLiteral("lastFolder"), m_model->folder());
    m_slideshow.stop();
    QMainWindow::closeEvent(event);
}
