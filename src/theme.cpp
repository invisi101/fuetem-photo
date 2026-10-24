#include "theme.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

ThemeColors ThemeColors::fromToml(const QString &text) {
    ThemeColors c;
    QString mode;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2
                && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);

        const QColor color(value);
        if (key == QStringLiteral("mode"))
            mode = value;
        else if (key == QStringLiteral("background") && color.isValid())
            c.background = color;
        else if (key == QStringLiteral("foreground") && color.isValid())
            c.foreground = color;
        else if (key == QStringLiteral("accent") && color.isValid())
            c.accent = color;
        else if (key == QStringLiteral("selection") && color.isValid())
            c.selection = color;
    }
    if (mode == QStringLiteral("dark"))
        c.dark = true;
    else if (mode == QStringLiteral("light"))
        c.dark = false;
    else
        c.dark = c.background.lightnessF() < 0.5;
    return c;
}

OmarchyTheme::OmarchyTheme(QObject *parent) : QObject(parent) {
    load();
    watch();
    // Omarchy swaps the whole theme directory when switching, so the watched
    // file can vanish and reappear; reload a moment later and re-arm.
    auto reload = [this]() {
        QTimer::singleShot(150, this, [this]() {
            load();
            watch();
            emit changed();
        });
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, reload);
}

QString OmarchyTheme::colorsPath() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

void OmarchyTheme::load() {
    QFile file(colorsPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        m_colors = ThemeColors::fromToml(QString::fromUtf8(file.readAll()));
    else
        m_colors = ThemeColors();
}

void OmarchyTheme::watch() {
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    const QString currentDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    if (QDir(currentDir).exists())
        m_watcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_watcher.addPath(themeDir);
    if (QFile::exists(colorsPath()))
        m_watcher.addPath(colorsPath());
}
