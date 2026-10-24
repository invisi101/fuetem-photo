#include "imageview.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

ImageView::ImageView(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(1500);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this]() {
        if (m_autoHide && !m_dragging)
            setCursor(Qt::BlankCursor);
    });
}

void ImageView::setBackground(const QColor &color) {
    m_background = color;
    update();
}

void ImageView::setAutoHideCursor(bool on) {
    m_autoHide = on;
    if (on)
        m_cursorTimer.start();
    else
        updateCursor();
}

void ImageView::setPlaceholder(const QImage &image) {
    m_image = image;
    m_full = false;
    m_fitPixmap = QPixmap();
    m_message.clear();
    fit();
}

QSize ImageView::fitSizeFor(QSize imageSize, QSize viewSize, qreal dpr) {
    if (imageSize.isEmpty() || viewSize.isEmpty())
        return {};
    const qreal s = qMin<qreal>(1.0, qMin(viewSize.width() * dpr / imageSize.width(),
                                          viewSize.height() * dpr / imageSize.height()));
    return (QSizeF(imageSize) * s).toSize();
}

void ImageView::setImage(const QImage &image, const QImage &prescaled) {
    m_image = image;
    m_full = !image.isNull();
    m_fitPixmap = QPixmap();
    if (!prescaled.isNull()) {
        m_fitPixmap = QPixmap::fromImage(prescaled);
        m_fitPixmap.setDevicePixelRatio(devicePixelRatioF());
    }
    m_message.clear();
    fit();
}

void ImageView::clear() {
    m_image = QImage();
    m_full = false;
    m_fitPixmap = QPixmap();
    update();
}

QRectF ImageView::area() const {
    // In normal light the title has its own strip; the picture sits below it.
    const int top = (m_showTitle && !m_title.isEmpty()) ? TitleHeight : 0;
    return QRectF(rect()).adjusted(0, top, 0, 0);
}

qreal ImageView::fitScale() const {
    const QRectF a = area();
    if (m_image.isNull() || a.width() <= 0 || a.height() <= 0)
        return 1.0;
    const qreal dpr = devicePixelRatioF();
    const qreal s = qMin(a.width() * dpr / m_image.width(), a.height() * dpr / m_image.height());
    // Small pictures are shown at their real size rather than blown up;
    // the stand-in thumbnail is always stretched to fill.
    return m_full ? qMin<qreal>(s, 1.0) : s;
}

QSizeF ImageView::scaledSize() const {
    const qreal dpr = devicePixelRatioF();
    return QSizeF(m_image.width() * m_scale / dpr, m_image.height() * m_scale / dpr);
}

void ImageView::fit() {
    m_fit = true;
    m_scale = fitScale();
    m_center = QPointF(m_image.width() / 2.0, m_image.height() / 2.0);
    updateCursor();
    update();
    emit zoomChanged();
}

void ImageView::toggleActualSize() {
    if (m_image.isNull())
        return;
    // Zoom in on the pointer when it is over the picture, else the middle.
    const QPointF at = underMouse() && m_lastMouse.x() >= 0 ? m_lastMouse : area().center();
    if (!m_fit && qFuzzyCompare(m_scale, 1.0)) {
        fit();
        return;
    }
    zoomBy(1.0 / m_scale, at);
}

void ImageView::zoomBy(qreal factor) {
    zoomBy(factor, area().center());
}

void ImageView::zoomBy(qreal factor, const QPointF &anchor) {
    if (m_image.isNull())
        return;
    const qreal minScale = qMin<qreal>(fitScale(), 1.0) * 0.5;
    const qreal newScale = qBound(minScale, m_scale * factor, 32.0);
    const QPointF imagePoint = viewToImage(anchor);
    m_scale = newScale;
    m_fit = false;
    // Keep the image point under the anchor where it was.
    const qreal dpr = devicePixelRatioF();
    const QPointF offset = (anchor - area().center()) * dpr / m_scale;
    m_center = imagePoint - offset;
    clampCenter();
    updateCursor();
    update();
    emit zoomChanged();
}

void ImageView::clampCenter() {
    const qreal dpr = devicePixelRatioF();
    const qreal halfW = area().width() * dpr / m_scale / 2.0;
    const qreal halfH = area().height() * dpr / m_scale / 2.0;
    const qreal iw = m_image.width();
    const qreal ih = m_image.height();
    if (iw <= halfW * 2)
        m_center.setX(iw / 2.0);
    else
        m_center.setX(qBound(halfW, m_center.x(), iw - halfW));
    if (ih <= halfH * 2)
        m_center.setY(ih / 2.0);
    else
        m_center.setY(qBound(halfH, m_center.y(), ih - halfH));
}

QPointF ImageView::imageToView(const QPointF &p) const {
    const qreal dpr = devicePixelRatioF();
    return area().center() + (p - m_center) * m_scale / dpr;
}

QPointF ImageView::viewToImage(const QPointF &p) const {
    const qreal dpr = devicePixelRatioF();
    return m_center + (p - area().center()) * dpr / m_scale;
}

void ImageView::updateCursor() {
    if (m_dragging) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const QSizeF s = scaledSize();
    const bool pannable = !m_image.isNull() && (s.width() > area().width() + 1 || s.height() > area().height() + 1);
    setCursor(pannable ? Qt::OpenHandCursor : Qt::ArrowCursor);
    if (m_autoHide)
        m_cursorTimer.start();
}

void ImageView::resizeEvent(QResizeEvent *) {
    relayoutImage();
}

void ImageView::relayoutImage() {
    m_fitPixmap = QPixmap();
    if (m_fit)
        fit();
    else {
        clampCenter();
        update();
    }
}

void ImageView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), m_background);

    if (!m_image.isNull()) {
        const QSizeF size = scaledSize();
        const QPointF topLeft = imageToView(QPointF(0, 0));
        const QRectF target(topLeft, size);
        const qreal dpr = devicePixelRatioF();

        if (m_fit) {
            const QSize device = (size * dpr).toSize();
            if (m_fitPixmap.isNull() || m_fitPixmap.size() != device) {
                m_fitPixmap = QPixmap::fromImage(
                    m_image.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                m_fitPixmap.setDevicePixelRatio(dpr);
            }
            p.drawPixmap(target.topLeft(), m_fitPixmap);
        } else {
            // Draw only the part of the picture that is on screen.
            const QRectF visible = target.intersected(area());
            const QRectF source(viewToImage(visible.topLeft()), viewToImage(visible.bottomRight()));
            p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 2.0);
            p.drawImage(visible, m_image, source);
        }
    }

    drawOverlayText(p);
}

void ImageView::drawOverlayText(QPainter &p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f = font();

    if (!m_message.isEmpty()) {
        f.setPixelSize(20);
        p.setFont(f);
        p.setPen(m_text);
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }

    if (m_showTitle && !m_title.isEmpty()) {
        f.setPixelSize(18);
        p.setFont(f);
        const QRect band(0, 0, width(), TitleHeight);
        QColor shade = m_background;
        shade.setAlpha(170);
        p.fillRect(band, shade);
        QColor c = m_text;
        c.setAlphaF(0.85);
        p.setPen(c);
        p.drawText(band.adjusted(20, 0, -20, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(m_title, Qt::ElideMiddle, band.width() - 40));
    }

    if (m_showInfo && !m_info.isEmpty()) {
        f.setPixelSize(18);
        p.setFont(f);
        const QFontMetrics fm(f);
        int w = 0;
        for (const QString &line : std::as_const(m_info))
            w = qMax(w, fm.horizontalAdvance(line));
        w = qMin(w + 40, width() - 40);
        const int lineH = fm.lineSpacing() + 4;
        const int h = lineH * m_info.size() + 28;
        const QRect panel(20, height() - h - 20, w, h);
        QPainterPath path;
        path.addRoundedRect(QRectF(panel), 10, 10);
        p.fillPath(path, QColor(0, 0, 0, 185));
        int y = panel.top() + 14;
        for (int i = 0; i < m_info.size(); ++i) {
            QColor c(Qt::white);
            c.setAlphaF(i == 0 ? 1.0 : 0.82);
            p.setPen(c);
            p.drawText(QRect(panel.left() + 20, y, panel.width() - 40, lineH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(m_info.at(i), Qt::ElideMiddle, panel.width() - 40));
            y += lineH;
        }
    }
}

void ImageView::wheelEvent(QWheelEvent *event) {
    const qreal delta = event->angleDelta().y();
    if (delta != 0) {
        // 120 units is one notch of a mouse wheel: about 20% per notch.
        zoomBy(std::pow(1.0015, delta), event->position());
    }
    event->accept();
}

bool ImageView::event(QEvent *event) {
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + gesture->value(), gesture->position());
            return true;
        }
        if (gesture->gestureType() == Qt::SmartZoomNativeGesture) {
            toggleActualSize();
            return true;
        }
    }
    return QWidget::event(event);
}

void ImageView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragFrom = event->position().toPoint();
        m_dragCenter = m_center;
        updateCursor();
    } else if (event->button() == Qt::MiddleButton) {
        toggleActualSize();
    }
}

void ImageView::mouseMoveEvent(QMouseEvent *event) {
    m_lastMouse = event->position();
    if (m_autoHide) {
        if (!m_dragging)
            updateCursor();
        m_cursorTimer.start();
    }
    if (!m_dragging)
        return;
    const qreal dpr = devicePixelRatioF();
    const QPointF moved = QPointF(event->position().toPoint() - m_dragFrom) * dpr / m_scale;
    m_center = m_dragCenter - moved;
    clampCenter();
    update();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        updateCursor();
    }
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        emit backRequested();
    }
}
