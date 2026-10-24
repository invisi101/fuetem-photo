#include "foldermodel.h"

#include "formats.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

const QCollator &collator() {
    thread_local QCollator c = []() {
        QCollator col(QLocale(QLocale::English, QLocale::UnitedKingdom));
        col.setNumericMode(true);
        col.setCaseSensitivity(Qt::CaseInsensitive);
        return col;
    }();
    return c;
}

QStringList sortedNames(QStringList names) {
    std::sort(names.begin(), names.end(), FolderModel::lessByName);
    return names;
}

} // namespace

FolderModel::FolderModel(QObject *parent) : QAbstractListModel(parent) {
    connect(&m_watcher, &QFutureWatcher<QVector<FolderEntry>>::finished, this, [this]() {
        if (m_watcher.property("generation").toInt() != m_generation)
            return;
        beginResetModel();
        m_entries = m_watcher.result();
        applySort();
        endResetModel();
        emit loaded();
    });
}

int FolderModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FolderModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const FolderEntry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole: return e.name;
    case Qt::ToolTipRole: return e.name;
    case PathRole: return e.path;
    case IsDirRole: return e.isDir;
    case CoverRole: return e.cover;
    default: return {};
    }
}

int FolderModel::monthNumber(const QString &name) {
    static const QStringList months = {
        QStringLiteral("january"), QStringLiteral("february"), QStringLiteral("march"),
        QStringLiteral("april"), QStringLiteral("may"), QStringLiteral("june"),
        QStringLiteral("july"), QStringLiteral("august"), QStringLiteral("september"),
        QStringLiteral("october"), QStringLiteral("november"), QStringLiteral("december"),
    };
    const QString n = name.trimmed().toLower();
    if (n.size() < 3)
        return 0;
    for (int i = 0; i < months.size(); ++i) {
        if (n == months.at(i) || (n.size() == 3 && months.at(i).startsWith(n))
                || (n == QStringLiteral("sept") && i == 8))
            return i + 1;
    }
    return 0;
}

// Natural order ("IMG_2" before "IMG_10"), with month folders in calendar
// order so "January" comes before "April".
bool FolderModel::lessByName(const QString &a, const QString &b) {
    const int ma = monthNumber(a);
    const int mb = monthNumber(b);
    if (ma && mb && ma != mb)
        return ma < mb;
    if (ma != 0 && mb == 0)
        return false;
    if (ma == 0 && mb != 0)
        return true;
    return collator().compare(a, b) < 0;
}

QString FolderModel::findCover(const QString &folder, int depth) {
    QDir dir(folder);
    const QStringList files = sortedNames(dir.entryList(QDir::Files | QDir::Readable, QDir::NoSort));
    for (const QString &f : files) {
        if (Formats::isImageFile(f))
            return dir.filePath(f);
    }
    if (depth <= 0)
        return {};
    const QStringList dirs = sortedNames(dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort));
    for (const QString &d : dirs) {
        const QString cover = findCover(dir.filePath(d), depth - 1);
        if (!cover.isEmpty())
            return cover;
    }
    return {};
}

QVector<FolderEntry> FolderModel::list(const QString &folder) {
    QVector<FolderEntry> out;
    QDirIterator it(folder, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        FolderEntry e;
        e.path = info.absoluteFilePath();
        e.name = info.fileName();
        e.isDir = info.isDir();
        if (!e.isDir && !Formats::isImageFile(e.name))
            continue;
        e.modified = info.lastModified();
        e.size = info.size();
        if (e.isDir)
            e.cover = findCover(e.path, 2);
        out.append(e);
    }
    return out;
}

void FolderModel::setFolder(const QString &path) {
    m_folder = QDir(path).absolutePath();
    ++m_generation;
    beginResetModel();
    m_entries.clear();
    m_rowOfPath.clear();
    m_imageCount = 0;
    endResetModel();
    m_watcher.setProperty("generation", m_generation);
    const QString folder = m_folder;
    m_watcher.setFuture(QtConcurrent::run([folder]() { return FolderModel::list(folder); }));
}

void FolderModel::setSort(Sort sort, bool descending) {
    if (sort == m_sort && descending == m_descending)
        return;
    m_sort = sort;
    m_descending = descending;
    beginResetModel();
    applySort();
    endResetModel();
}

void FolderModel::applySort() {
    const Sort sort = m_sort;
    const bool desc = m_descending;
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [sort, desc](const FolderEntry &a, const FolderEntry &b) {
        if (a.isDir != b.isDir)
            return a.isDir; // folders first, always
        if (a.isDir)
            return desc ? lessByName(b.name, a.name) : lessByName(a.name, b.name);
        if (sort == Sort::Date && a.modified != b.modified)
            return desc ? b.modified < a.modified : a.modified < b.modified;
        return desc ? lessByName(b.name, a.name) : lessByName(a.name, b.name);
    });
    m_rowOfPath.clear();
    m_imageCount = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        m_rowOfPath.insert(m_entries.at(i).path, i);
        if (!m_entries.at(i).isDir)
            ++m_imageCount;
    }
}

int FolderModel::neighbourImage(int row, int step) const {
    int r = row + step;
    while (r >= 0 && r < m_entries.size()) {
        if (!m_entries.at(r).isDir)
            return r;
        r += step;
    }
    return -1;
}
