#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include "metadata.h"
#include "theme.h"

class FolderModel;
class ImageView;
class PhotoGrid;
class QHBoxLayout;
class QStackedWidget;
class QToolButton;
class ThumbnailLoader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // A folder opens in the grid; a picture opens on its own, in its folder.
    void openPath(const QString &path);

    // For tests and scripted checks.
    PhotoGrid *grid() const { return m_grid; }
    ImageView *imageView() const { return m_view; }
    FolderModel *model() const { return m_model; }
    bool inSingleView() const;
    int lights() const { return m_lights; }
    int currentRow() const { return m_row; }

    void showGrid();
    void showSingle(int row);
    void step(int delta);
    void cycleLights();
    void toggleFullscreen();
    void toggleInfo();
    void toggleSlideshow();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setFolder(const QString &folder, const QString &selectPath = QString());
    void buildTopBar();
    void rebuildBreadcrumb();
    void applyAppearance();
    void updateStatus();
    void updateTitle();
    void loadCurrent();
    void prefetch();
    void requestFull(const QString &path, bool forDisplay);
    void fullLoaded(const QString &path, const QImage &image, const QImage &fitted);
    void requestInfo(const QString &path);
    void refreshInfo();
    void openFolderDialog();
    void goUp();
    void cycleSort();
    void copyPath();
    void showInFileManager();
    void toast(const QString &text);
    void toggleHelp();
    void positionOverlays();
    QString currentPath() const;

    OmarchyTheme *m_theme;
    FolderModel *m_model;
    ThumbnailLoader *m_loader;
    PhotoGrid *m_grid;
    ImageView *m_view;
    QStackedWidget *m_stack;
    QWidget *m_topBar;
    QHBoxLayout *m_crumbs;
    QLabel *m_status;
    QToolButton *m_sortButton;
    QToolButton *m_openButton;
    QToolButton *m_helpButton;
    QLabel *m_toast;
    QLabel *m_help;
    QTimer m_toastTimer;
    QTimer m_slideshow;

    QString m_pendingSelect;
    QString m_pendingSingle;
    int m_row = -1;
    int m_lights = 0;      // 0 normal, 1 dimmed, 2 lights out
    bool m_showInfo = false;
    bool m_wasMaximized = false;

    QThreadPool m_fullPool;
    QCache<QString, QImage> m_fullCache;
    QSet<QString> m_fullLoading;
    QHash<QString, PhotoInfo> m_infoCache;
    QSet<QString> m_infoLoading;
};
