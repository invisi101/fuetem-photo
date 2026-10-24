#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QVector>

struct FolderEntry {
    QString path;
    QString name;
    bool isDir = false;
    QString cover;      // for folders: the first picture inside, if any
    QDateTime modified;
    qint64 size = 0;
};

// One folder's contents: its subfolders first, then its pictures. Listing
// happens off the UI thread; non-picture files are left out.
class FolderModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, IsDirRole, CoverRole };
    enum class Sort { Name, Date };

    explicit FolderModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setFolder(const QString &path);
    QString folder() const { return m_folder; }
    bool loading() const { return m_watcher.isRunning(); }

    void setSort(Sort sort, bool descending);
    Sort sortKey() const { return m_sort; }
    bool descending() const { return m_descending; }

    const FolderEntry &entry(int row) const { return m_entries.at(row); }
    int rowOfPath(const QString &path) const { return m_rowOfPath.value(path, -1); }
    int imageCount() const { return m_imageCount; }
    int folderCount() const { return m_entries.size() - m_imageCount; }
    int firstImageRow() const { return folderCount(); }
    // Rows of pictures only (never folders); -1 when there is none.
    int neighbourImage(int row, int step) const;

    // Public so the tests can check them.
    static QVector<FolderEntry> list(const QString &folder);
    static bool lessByName(const QString &a, const QString &b);
    static int monthNumber(const QString &name);
    static QString findCover(const QString &folder, int depth);

signals:
    void loaded();

private:
    void applySort();

    QString m_folder;
    QVector<FolderEntry> m_entries;
    QHash<QString, int> m_rowOfPath;
    int m_imageCount = 0;
    Sort m_sort = Sort::Name;
    bool m_descending = false;
    QFutureWatcher<QVector<FolderEntry>> m_watcher;
    int m_generation = 0;
};
