#include "photogrid.h"

#include "foldermodel.h"
#include "thumbnails.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyle>
#include <QWheelEvent>

namespace {
constexpr int Gap = 5; // space around each thumbnail, in logical pixels

QColor mix(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
}

ThumbDelegate::ThumbDelegate(ThumbnailLoader *loader, QObject *parent)
    : QStyledItemDelegate(parent), m_loader(loader) {
    m_scaled.setMaxCost(160 * 1024); // kilobytes of ready-to-paint pixmaps
}

void ThumbDelegate::setColors(const ThemeColors &colors, const QColor &background, bool dimmed) {
    m_colors = colors;
    m_background = background;
    m_dimmed = dimmed;
}

QSize ThumbDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const {
    return m_cell;
}

void ThumbDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const {
    const bool isDir = index.data(FolderModel::IsDirRole).toBool();
    const QString path = index.data(FolderModel::PathRole).toString();
    const QString source = isDir ? index.data(FolderModel::CoverRole).toString() : path;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QRect cell = option.rect;
    const QRect box = cell.adjusted(Gap, Gap, -Gap, -Gap);
    const qreal dpr = p->device()->devicePixelRatioF();

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QColor placeholder = mix(m_background, m_colors.foreground, 0.07);

    // The picture, fitted inside the box (folders fill the box, cropped).
    QRect imageRect;
    QPixmap pix;
    if (!source.isEmpty()) {
        const QString key = source + (isDir ? QStringLiteral("|crop") : QString());
        if (QPixmap *cachedPix = m_scaled.object(key)) {
            pix = *cachedPix;
        } else if (const QImage *image = m_loader->cached(source)) {
            const QSize target = (QSizeF(box.size()) * dpr).toSize();
            QImage scaled;
            if (isDir) {
                scaled = image->scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                const QRect crop((scaled.width() - target.width()) / 2,
                                 (scaled.height() - target.height()) / 2,
                                 target.width(), target.height());
                scaled = scaled.copy(crop);
            } else {
                scaled = image->scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            pix = QPixmap::fromImage(scaled);
            pix.setDevicePixelRatio(dpr);
            m_scaled.insert(key, new QPixmap(pix), qMax(1, int(scaled.sizeInBytes() / 1024)));
        }
    }

    if (!pix.isNull()) {
        const QSize logical = (QSizeF(pix.size()) / dpr).toSize();
        imageRect = QRect(QPoint(0, 0), logical);
        imageRect.moveCenter(box.center());
    } else {
        imageRect = isDir ? box : box.adjusted(box.width() / 8, box.height() / 8,
                                                -box.width() / 8, -box.height() / 8);
    }

    QPainterPath clip;
    clip.addRoundedRect(QRectF(imageRect), 6, 6);
    if (!pix.isNull()) {
        p->setClipPath(clip);
        p->drawPixmap(imageRect, pix);
        p->setClipping(false);
    } else {
        p->fillPath(clip, placeholder);
        if (!isDir && m_loader->failed(source)) {
            QFont f = option.font;
            f.setPixelSize(17);
            p->setFont(f);
            p->setPen(mix(m_background, m_colors.foreground, 0.5));
            p->drawText(imageRect.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                        index.data(Qt::DisplayRole).toString());
        }
    }

    if (isDir) {
        // Darken the cover and write the folder name across it.
        QLinearGradient shade(imageRect.topLeft(), imageRect.bottomLeft());
        shade.setColorAt(0.0, QColor(0, 0, 0, pix.isNull() ? 0 : 40));
        shade.setColorAt(1.0, QColor(0, 0, 0, pix.isNull() ? 0 : 190));
        p->fillPath(clip, shade);
        QFont f = option.font;
        f.setPixelSize(qBound(17, box.height() / 9, 26));
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(pix.isNull() ? m_colors.foreground : QColor(Qt::white));
        const QRect text = imageRect.adjusted(14, 10, -14, -12);
        const QString name = QFontMetrics(f).elidedText(index.data(Qt::DisplayRole).toString(),
                                                         Qt::ElideRight, text.width());
        p->drawText(text, Qt::AlignLeft | Qt::AlignBottom, name);
    }

    if (m_dimmed && !selected) {
        p->fillPath(clip, QColor(0, 0, 0, 110));
    }

    if (selected) {
        QPen pen(m_colors.accent, 3);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(imageRect).adjusted(-2.5, -2.5, 2.5, 2.5), 8, 8);
    } else if (hovered) {
        QColor c = m_colors.foreground;
        c.setAlphaF(0.35);
        p->setPen(QPen(c, 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(imageRect).adjusted(-2, -2, 2, 2), 8, 8);
    }
    p->restore();
}

PhotoGrid::PhotoGrid(FolderModel *model, ThumbnailLoader *loader, QWidget *parent)
    : QListView(parent), m_model(model), m_loader(loader) {
    setModel(model);
    m_delegate = new ThumbDelegate(loader, this);
    setItemDelegate(m_delegate);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Always present, so the column count never flips as it comes and goes.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setFrameShape(QFrame::NoFrame);
    setSpacing(0);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setDragEnabled(false);
    setTextElideMode(Qt::ElideRight);

    m_requestTimer.setSingleShot(true);
    m_requestTimer.setInterval(30);
    connect(&m_requestTimer, &QTimer::timeout, this, &PhotoGrid::requestVisible);
    m_repaintTimer.setSingleShot(true);
    m_repaintTimer.setInterval(16);
    connect(&m_repaintTimer, &QTimer::timeout, viewport(), [this]() { viewport()->update(); });

    connect(loader, &ThumbnailLoader::ready, this, [this](const QString &) {
        if (!m_repaintTimer.isActive())
            m_repaintTimer.start();
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        relayout();
        scheduleRequests();
    });
    connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        emit openRequested(index.row());
    });
    relayout();
}

void PhotoGrid::setColors(const ThemeColors &colors, const QColor &background, bool dimmed) {
    m_delegate->setColors(colors, background, dimmed);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, background);
    pal.setColor(QPalette::Window, background);
    pal.setColor(QPalette::Text, colors.foreground);
    setPalette(pal);
    viewport()->setPalette(pal);
    viewport()->update();
}

void PhotoGrid::setThumbSize(int size) {
    size = qBound(MinSize, size, MaxSize);
    if (size == m_target)
        return;
    const QModelIndex keep = currentIndex();
    m_target = size;
    relayout();
    if (keep.isValid())
        scrollTo(keep, QAbstractItemView::PositionAtCenter);
    scheduleRequests();
    emit thumbSizeChanged(size);
}

void PhotoGrid::selectRow(int row) {
    if (row < 0 || row >= m_model->rowCount())
        return;
    const QModelIndex index = m_model->index(row, 0);
    setCurrentIndex(index);
    scrollTo(index, QAbstractItemView::EnsureVisible);
}

int PhotoGrid::layoutWidth() const {
    // QListView lays out within the viewport less one scrollbar's width,
    // whether or not the scrollbar is already counted in the viewport.
    const int bar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    return qMax(1, viewport()->width() - bar - 1);
}

void PhotoGrid::relayout() {
    const int width = layoutWidth();
    const int columns = qMax(1, width / m_target);
    m_cell = qMax(MinSize / 2, width / columns);
    m_delegate->setCell(QSize(m_cell, m_cell));
    setGridSize(QSize(m_cell, m_cell));
    verticalScrollBar()->setSingleStep(qMax(20, m_cell / 6));
    doItemsLayout();
}

void PhotoGrid::resizeEvent(QResizeEvent *event) {
    QListView::resizeEvent(event);
    relayout();
    scheduleRequests();
}

void PhotoGrid::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0)
            setThumbSize(m_target + steps * 30);
        event->accept();
        return;
    }
    QListView::wheelEvent(event);
}

void PhotoGrid::keyPressEvent(QKeyEvent *event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentIndex().isValid()) {
        emit openRequested(currentIndex().row());
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void PhotoGrid::scrollContentsBy(int dx, int dy) {
    QListView::scrollContentsBy(dx, dy);
    scheduleRequests();
}

void PhotoGrid::scheduleRequests() {
    if (!m_requestTimer.isActive())
        m_requestTimer.start();
}

void PhotoGrid::requestVisible() {
    const int count = m_model->rowCount();
    if (count == 0) {
        m_loader->setQueue({});
        return;
    }
    const int columns = qMax(1, layoutWidth() / m_cell);
    const int top = verticalScrollBar()->value();
    const int firstRow = top / m_cell;
    const int visibleRows = viewport()->height() / m_cell + 2;
    const int first = qBound(0, firstRow * columns, count - 1);
    const int last = qBound(0, (firstRow + visibleRows) * columns - 1, count - 1);
    const int page = last - first + 1;

    QStringList queue;
    auto add = [this, &queue](int row) {
        const FolderEntry &e = m_model->entry(row);
        queue << (e.isDir ? e.cover : e.path);
    };
    // On screen first, then the screen below, then the screen above.
    for (int r = first; r <= last; ++r)
        add(r);
    for (int r = last + 1; r <= qMin(count - 1, last + page); ++r)
        add(r);
    for (int r = first - 1; r >= qMax(0, first - page); --r)
        add(r);
    m_loader->setQueue(queue);
}
