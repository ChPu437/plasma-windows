// Plasma Windows - Phase 1
//
// Qt-based shell with the same lifecycle as the Phase 0 Win32 shell
// (see AGENTS.md):
//   * start on Windows 10 LTSC 2021
//   * create a top-level window covering the primary monitor work area
//   * process the normal Qt event loop
//   * accept keyboard (ESC / Alt+F4) and mouse input
//   * exit cleanly
//   * startup logging, exit codes, optional debug logging (--debug)
//
// No KDE dependencies yet. Win32 is used only for console attachment and
// diagnostics parity.

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QString>
#include <QSysInfo>
#include <QWidget>

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace {

// Exit codes (documented in README.md).
enum ExitCode : int {
    kExitOk = 0,       // clean shutdown requested by the user
    kExitGeneric = 1,  // startup failure
};

// ---------------------------------------------------------------------------
// Diagnostics (AGENTS.md section 7)

bool g_debug = false;
QFile g_logFile;

void LogOpen(const QString& path)
{
    g_logFile.setFileName(path);
    if (!g_logFile.open(QIODevice::Append)) {
        OutputDebugStringA("warning: could not open shell.log\n");
    }
}

void LogClose()
{
    if (g_logFile.isOpen()) {
        g_logFile.flush();
        g_logFile.close();
    }
}

enum class Level { kInfo, kWarn, kError, kDebug };

const char* LevelTag(Level level)
{
    switch (level) {
    case Level::kWarn:  return "WARN";
    case Level::kError: return "ERROR";
    case Level::kDebug: return "DEBUG";
    default:            return "INFO";
    }
}

void LogWrite(Level level, const char* fmt, ...)
{
    if (level == Level::kDebug && !g_debug) {
        return;
    }

    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(text, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1400];
    snprintf(line, sizeof(line),
             "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds, LevelTag(level), text);

    if (g_logFile.isOpen()) {
        g_logFile.write(line);
        g_logFile.flush();
    }
    OutputDebugStringA(line);
    fputs(line, stdout);
    fflush(stdout);
}

#define LOG_INFO(...)  LogWrite(Level::kInfo, __VA_ARGS__)
#define LOG_WARN(...)  LogWrite(Level::kWarn, __VA_ARGS__)
#define LOG_ERROR(...) LogWrite(Level::kError, __VA_ARGS__)
#define LOG_DEBUG(...) LogWrite(Level::kDebug, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Shell window

class ShellWindow : public QWidget {
public:
    explicit ShellWindow()
    {
        setWindowTitle(QStringLiteral("Plasma Windows (Phase 1)"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x23, 0x26, 0x29));

        const int w = rect().width();
        const int h = rect().height();

        DrawCentered(&p, QRect(0, h / 4, w, 160),
                     QStringLiteral("Plasma Windows"), 96,
                     QColor(0xF0, 0xF0, 0xF0), QFont::DemiBold);
        DrawCentered(&p, QRect(0, h / 4 + 160, w, 80),
                     QStringLiteral("Phase 1 - Qt shell"), 40,
                     QColor(0x9A, 0xA0, 0xA6), QFont::Normal);
        DrawCentered(&p, QRect(0, h - 96, w, 64),
                     QStringLiteral("Press ESC or Alt+F4 to exit"), 28,
                     QColor(0x6E, 0x74, 0x7A), QFont::Normal);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        LOG_DEBUG("key pressed: key=0x%X, text=%ls", event->key(),
                  qUtf16Printable(event->text()));
        if (event->key() == Qt::Key_Escape) {
            LOG_INFO("ESC pressed, shutting down");
            close();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        LOG_DEBUG("mouse button %d at (%d, %d)", event->button(),
                  event->pos().x(), event->pos().y());
        QWidget::mousePressEvent(event);
    }

    void closeEvent(QCloseEvent* event) override
    {
        LOG_INFO("window closing, shutting down");
        QWidget::closeEvent(event);
    }

private:
    static void DrawCentered(QPainter* p, const QRect& rc, const QString& text,
                             int pixelSize, const QColor& color, int weight)
    {
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(pixelSize);
        font.setWeight(static_cast<QFont::Weight>(weight));
        p->setFont(font);
        p->setPen(color);
        p->drawText(rc, Qt::AlignCenter, text);
    }
};

void FitToWorkArea(ShellWindow* window)
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        LOG_WARN("no primary screen available");
        return;
    }
    const QRect wa = screen->availableGeometry();
    window->setGeometry(wa);
    LOG_INFO("work area: (%d, %d) - (%d, %d), %d x %d", wa.x(), wa.y(),
             wa.x() + wa.width(), wa.y() + wa.height(), wa.width(),
             wa.height());
}

} // namespace

int main(int argc, char** argv)
{
    // When launched from a console (cmd.exe), attach to it so log output is
    // visible. Harmless when there is no parent console.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        setvbuf(stdout, nullptr, _IONBF, 0);
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("shell"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Plasma Windows Phase 1 Qt shell"));
    parser.addHelpOption();
    const QCommandLineOption debugOption(
        QStringLiteral("debug"),
        QStringLiteral("enable verbose debug logging"));
    parser.addOption(debugOption);
    parser.process(app);

    g_debug = parser.isSet(debugOption);

    const QString exePath = QCoreApplication::applicationFilePath();
    LogOpen(QCoreApplication::applicationDirPath() + QStringLiteral("/shell.log"));

    LOG_INFO("Plasma Windows Phase 1 Qt shell starting");
    LOG_INFO("executable: %ls", qUtf16Printable(exePath));
    LOG_INFO("OS: %ls (kernel %ls)", qUtf16Printable(QSysInfo::prettyProductName()),
             qUtf16Printable(QSysInfo::kernelVersion()));
    LOG_INFO("Qt version: %s", qVersion());
    LOG_INFO("command line: %ls",
             qUtf16Printable(QCoreApplication::arguments().join(QLatin1Char(' '))));
    LOG_INFO("debug logging: %s", g_debug ? "on" : "off");

    ShellWindow window;
    FitToWorkArea(&window);
    window.show();
    LOG_INFO("window shown");

    QObject::connect(&window, &QWidget::destroyed, [&window] {
        LOG_INFO("window destroyed");
    });
    QObject::connect(qApp, &QGuiApplication::primaryScreenChanged, [&window] {
        LOG_INFO("primary screen changed, re-fitting");
        FitToWorkArea(&window);
    });
    QObject::connect(qApp, &QGuiApplication::screenAdded, [&window] {
        LOG_INFO("screen added, re-fitting");
        FitToWorkArea(&window);
    });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
        LOG_INFO("application exiting");
        LogClose();
    });

    const int code = app.exec();
    return code;
}
