#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>

struct ThemeColors {
    QColor background = QColor(QStringLiteral("#101010"));
    QColor foreground = QColor(QStringLiteral("#eeeeee"));
    QColor accent = QColor(QStringLiteral("#5584aa"));
    QColor selection = QColor(QStringLiteral("#186a9a"));
    bool dark = true;

    // Parses an Omarchy colors.toml; keys that are missing keep the defaults.
    static ThemeColors fromToml(const QString &text);
};

// Follows the active Omarchy theme (~/.local/state/omarchy/current/theme/
// colors.toml), the same file Fuetem Writer reads, and announces changes
// as the theme is switched.
class OmarchyTheme : public QObject {
    Q_OBJECT
public:
    explicit OmarchyTheme(QObject *parent = nullptr);
    const ThemeColors &colors() const { return m_colors; }
    static QString colorsPath();

signals:
    void changed();

private:
    void load();
    void watch();

    ThemeColors m_colors;
    QFileSystemWatcher m_watcher;
};
