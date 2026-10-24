#include <QApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QImageReader>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>
#include <QKeyEvent>
#include <QKeySequence>
#include <QStyleFactory>
#include <QTimer>

#include "mainwindow.h"

namespace {
// FUETEM_PHOTO_SCRIPT drives the window for testing without touching the
// rest of the desktop: "wait:1500;key:Return;shot:/tmp/a.png;key:Right;quit".
void runScript(MainWindow *window, QStringList steps) {
    if (steps.isEmpty())
        return;
    const QString step = steps.takeFirst().trimmed();
    const QString arg = step.section(QLatin1Char(':'), 1);
    int delay = 150;
    if (step.startsWith(QStringLiteral("wait:"))) {
        delay = arg.toInt();
    } else if (step.startsWith(QStringLiteral("key:"))) {
        const QKeySequence seq = QKeySequence::fromString(arg);
        if (!seq.isEmpty()) {
            const QKeyCombination combo = seq[0];
            QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : window;
            QKeyEvent press(QEvent::KeyPress, combo.key(), combo.keyboardModifiers(),
                            arg.size() == 1 ? arg : QString());
            QApplication::sendEvent(target, &press);
            QKeyEvent release(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
            QApplication::sendEvent(target, &release);
        }
    } else if (step.startsWith(QStringLiteral("shot:"))) {
        window->grab().save(arg);
    } else if (step == QStringLiteral("quit")) {
        window->close();
        return;
    }
    QTimer::singleShot(delay, window, [window, steps]() { runScript(window, steps); });
}
} // namespace

int main(int argc, char *argv[]) {
    // Open-folder windows come from the desktop portal, as in Fuetem Writer:
    // the GTK chooser running inside the app is the thing that crashed there.
    if (!qEnvironmentVariableIsSet("FUETEM_KEEP_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");

    QApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.services.warning=false\n"
                                                    "qt.gui.imageio.warning=false"));
    app.setApplicationName(QStringLiteral("fuetem-photo"));
    app.setApplicationDisplayName(QStringLiteral("Fuetem Photo"));
    app.setOrganizationName(QStringLiteral("Fuetem"));
    app.setOrganizationDomain(QStringLiteral("fuetem"));
    app.setDesktopFileName(QStringLiteral("fuetem-photo"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("fuetem-photo")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Big pictures (panoramas, layered PSDs) are past Qt's 256 MB default.
    QImageReader::setAllocationLimit(2048);

    // No small text anywhere: 18 px base.
    QFont font = app.font();
    font.setPixelSize(18);
    app.setFont(font);

    MainWindow window;
    const QStringList args = app.arguments();
    QString start;
    for (int i = 1; i < args.size(); ++i) {
        if (!args.at(i).startsWith(QLatin1Char('-'))) {
            start = args.at(i);
            break;
        }
    }
    if (start.isEmpty()) {
        start = QSettings().value(QStringLiteral("lastFolder")).toString();
        if (start.isEmpty() || !QDir(start).exists())
            start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (start.isEmpty() || !QDir(start).exists())
            start = QDir::homePath();
    }
    window.openPath(start);
    window.show();
    const QString script = qEnvironmentVariable("FUETEM_PHOTO_SCRIPT");
    if (!script.isEmpty())
        QTimer::singleShot(0, &window, [&window, script]() {
            runScript(&window, script.split(QLatin1Char(';'), Qt::SkipEmptyParts));
        });
    return app.exec();
}
