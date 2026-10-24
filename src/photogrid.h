#pragma once

#include <QCache>
#include <QListView>
#include <QStyledItemDelegate>
#include <QTimer>

#include "theme.h"

class FolderModel;
class ThumbnailLoader;

class ThumbDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    ThumbDelegate(ThumbnailLoader *loader, QObject *parent);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void setCell(QSize cell) { m_cell = cell; m_scaled.clear(); }
    void setColors(const ThemeColors &colors, const QColor &background, bool dimmed);
    void forget(const QString &path) { m_scaled.remove(path); }

private:
    ThumbnailLoader *m_loader;
    QSize m_cell = QSize(200, 200);
    ThemeColors m_colors;
    QColor m_background;
    bool m_dimmed = false;
    mutable QCache<QString, QPixmap> m_scaled;
};

// The thumbnail grid. Columns always fill the width exactly; thumbnails are
// requested only for what is on screen plus a screen either side.
class PhotoGrid : public QListView {
    Q_OBJECT
public:
    PhotoGrid(FolderModel *model, ThumbnailLoader *loader, QWidget *parent = nullptr);

    int thumbSize() const { return m_target; }
    void setThumbSize(int size);
    void setColors(const ThemeColors &colors, const QColor &background, bool dimmed);
    void selectRow(int row);

    static constexpr int MinSize = 110;
    static constexpr int MaxSize = 520;

signals:
    void openRequested(int row);
    void thumbSizeChanged(int size);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyboardSearch(const QString &) override {} // letters are shortcuts here

private:
    void relayout();
    int layoutWidth() const;
    void scheduleRequests();
    void requestVisible();

    FolderModel *m_model;
    ThumbnailLoader *m_loader;
    ThumbDelegate *m_delegate;
    int m_target = 220;
    int m_cell = 220;
    QTimer m_requestTimer;
    QTimer m_repaintTimer;
};
