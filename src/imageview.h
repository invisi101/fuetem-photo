#pragma once

#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QWidget>

// Shows one picture: fitted to the window, zoomable with the wheel, a pinch
// or the keyboard, and dragged around when it is larger than the window.
class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);

    // A quick stand-in (the thumbnail) while the real picture decodes.
    void setPlaceholder(const QImage &image);
    // prescaled: optionally the picture already scaled to fitSizeFor().
    void setImage(const QImage &image, const QImage &prescaled = QImage());
    // The device-pixel size a picture of imageSize is fitted to in a view of viewSize.
    static QSize fitSizeFor(QSize imageSize, QSize viewSize, qreal dpr);
    void clear();
    bool hasFullImage() const { return m_full; }
    QSize imageSize() const { return m_full ? m_image.size() : QSize(); }

    void setBackground(const QColor &color);
    void setTextColor(const QColor &color) { m_text = color; update(); }
    // Lines shown along the top (title) and bottom left (info panel).
    void setTitle(const QString &title) {
        const bool strip = m_title.isEmpty() != title.isEmpty();
        m_title = title;
        if (strip) relayoutImage(); else update();
    }
    void setInfoLines(const QStringList &lines) { m_info = lines; update(); }
    void setShowTitle(bool on) { if (m_showTitle != on) { m_showTitle = on; relayoutImage(); } }
    // The part of the view the picture is fitted into.
    QRectF area() const;
    static constexpr int TitleHeight = 46;
    void setShowInfo(bool on) { m_showInfo = on; update(); }
    void setMessage(const QString &message) { m_message = message; update(); }
    // Hide the pointer after a moment of stillness (lights out, fullscreen).
    void setAutoHideCursor(bool on);

    bool isFit() const { return m_fit; }
    qreal scale() const { return m_scale; }
    void fit();
    void toggleActualSize();
    void zoomBy(qreal factor);
    void zoomBy(qreal factor, const QPointF &anchor);

signals:
    void backRequested();
    void zoomChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    qreal fitScale() const;
    void relayoutImage();
    QSizeF scaledSize() const;
    void clampCenter();
    void updateCursor();
    QPointF imageToView(const QPointF &p) const;
    QPointF viewToImage(const QPointF &p) const;
    void drawOverlayText(QPainter &p);

    QImage m_image;
    QPixmap m_fitPixmap;   // the picture pre-scaled to the fitted size
    bool m_full = false;
    bool m_fit = true;
    qreal m_scale = 1.0;   // device pixels per image pixel
    QPointF m_center;      // image point shown at the centre of the view
    QPoint m_dragFrom;
    QPointF m_lastMouse = QPointF(-1, -1);
    QPointF m_dragCenter;
    bool m_dragging = false;

    QColor m_background = Qt::black;
    QColor m_text = Qt::white;
    QString m_title;
    QStringList m_info;
    QString m_message;
    bool m_showTitle = true;
    bool m_showInfo = false;
    bool m_autoHide = false;
    QTimer m_cursorTimer;
};
