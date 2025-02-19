// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#ifndef USE_EXTERNAL_SINGLEAPPLICATION
#include "singleapplication.h"
#else
#include "QtSolutions/qtsingleapplication.h"
#endif


#include "abstractlogger.h"
#include "src/cli/commandlineparser.h"
#include "src/config/cacheutils.h"
#include "src/config/styleoverride.h"
#include "src/core/capturerequest.h"
#include "src/core/flameshot.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/confighandler.h"
#include "src/utils/filenamehandler.h"
#include "src/utils/pathinfo.h"
#include "src/utils/valuehandler.h"
#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QSharedMemory>
#include <QTimer>
#include <QTranslator>
#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#include "abstractlogger.h"
#include "src/core/flameshotdbusadapter.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <desktopinfo.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

#define LOG_FILE "logfile.txt"

void logMsg(const char *format, ...) {
    // Open log file in append mode
    return;
    FILE *log_file = fopen(LOG_FILE, "a");
    if (!log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // Get current time
    time_t now;
    time(&now);
    struct tm *local_time = localtime(&now);

    // Print time to log file
    fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            local_time->tm_year + 1900,
            local_time->tm_mon + 1,
            local_time->tm_mday,
            local_time->tm_hour,
            local_time->tm_min,
            local_time->tm_sec);

    // Print the log message
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    // Add a newline and close the log file
    fprintf(log_file, "\n");
    fclose(log_file);
}


#ifdef Q_OS_LINUX
// source: https://github.com/ksnip/ksnip/issues/416
void wayland_hacks()
{
    // Workaround to https://github.com/ksnip/ksnip/issues/416
    DesktopInfo info;
    if (info.windowManager() == DesktopInfo::GNOME) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
}
#endif

void requestCaptureAndWait(const CaptureRequest& req)
{
    logMsg("func requestCaptureAndWait");
    Flameshot* flameshot = Flameshot::instance();
    logMsg("func requestCaptureAndWait  requestCapture");
    flameshot->requestCapture(req);
    logMsg("func requestCaptureAndWait  Qobject::connect ");
    QObject::connect(flameshot, &Flameshot::captureTaken, [&](const QPixmap&) {
#if defined(Q_OS_MACOS)
        // Only useful on MacOS because each instance hosts its own widgets
        if (!FlameshotDaemon::isThisInstanceHostingWidgets()) {
            qApp->exit(0);
        }
#else
        // if this instance is not daemon, make sure it exit after caputre finish
	logMsg("check instance and haveExternalWidget");
        if (FlameshotDaemon::instance() == nullptr && !Flameshot::instance()->haveExternalWidget()) {
	    logMsg("haveExternalWidget  and instance=nullptr exit");
            qApp->exit(0);
        }
#endif
    });
    logMsg("func requestCaptureAndWait  Qobject::connect  22");
    QObject::connect(flameshot, &Flameshot::captureFailed, []() {
        AbstractLogger::info() << "Screenshot aborted.";
    	logMsg("func requestCaptureAndWait  Qobject::connect  exit");
        qApp->exit(1);
    });
   logMsg("func requestCaptureAndWait  exec");
    qApp->exec();
   logMsg("func requestCaptureAndWait  exec end");
}

QSharedMemory* guiMutexLock()
{
    QString key = "org.flameshot.Flameshot-" APP_VERSION;
    auto* shm = new QSharedMemory(key);
#ifdef Q_OS_UNIX
    // Destroy shared memory if the last instance crashed on Unix
    shm->attach();
    delete shm;
    shm = new QSharedMemory(key);
#endif
    if (!shm->create(1)) {
        return nullptr;
    }
    return shm;
}

QTranslator translator, qtTranslator;

void configureApp(bool gui)
{
    if (gui) {
        QApplication::setStyle(new StyleOverride);
    }

    // Configure translations
    for (const QString& path : PathInfo::translationsPaths()) {
        bool match = translator.load(QLocale(),
                                     QStringLiteral("Internationalization"),
                                     QStringLiteral("_"),
                                     path);
        if (match) {
            break;
        }
    }

    qtTranslator.load(QLocale::system(),
                      "qt",
                      "_",
                      QLibraryInfo::location(QLibraryInfo::TranslationsPath));

    auto app = QCoreApplication::instance();
    app->installTranslator(&translator);
    app->installTranslator(&qtTranslator);
    app->setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);
}

// TODO find a way so we don't have to do this
/// Recreate the application as a QApplication
void reinitializeAsQApplication(int& argc, char* argv[])
{
    delete QCoreApplication::instance();
    new QApplication(argc, argv);
    configureApp(true);
}

int main(int argc, char* argv[])
{
#ifdef Q_OS_LINUX
    wayland_hacks();
#endif

    // required for the button serialization
    // TODO: change to QVector in v1.0
    qRegisterMetaTypeStreamOperators<QList<int>>("QList<int>");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    QCoreApplication::setApplicationName(QStringLiteral("flameshot"));
    QCoreApplication::setOrganizationName(QStringLiteral("flameshot"));

    // no arguments, just launch Flameshot
    if (argc == 1) {
#ifndef USE_EXTERNAL_SINGLEAPPLICATION
	logMsg("USE_EXTERNAL_SINGLEAPPLICATION");
        SingleApplication app(argc, argv);
	logMsg("USE_EXTERNAL_SINGLEAPPLICATION End");
	printf("any wind singleapp\n");
#else
	logMsg("NOT USE_EXTERNAL_SINGLEAPPLICATION");
        QtSingleApplication app(argc, argv);
	logMsg("NOT USE_EXTERNAL_SINGLEAPPLICATION End");
#endif
        configureApp(true);
        auto c = Flameshot::instance();
        FlameshotDaemon::start();

#if !(defined(Q_OS_MACOS) || defined(Q_OS_WIN))
        new FlameshotDBusAdapter(c);
        QDBusConnection dbus = QDBusConnection::sessionBus();
        if (!dbus.isConnected()) {
            AbstractLogger::error()
              << QObject::tr("Unable to connect via DBus");
        }
        dbus.registerObject(QStringLiteral("/"), c);
        dbus.registerService(QStringLiteral("org.flameshot.Flameshot"));
#endif
	logMsg("start exec");
        return qApp->exec();
    }
#if defined(Q_OS_WIN) ||  defined(Q_OS_UNIX)
	logMsg("init win");
        SingleApplication app(argc, argv);
    	//new QCoreApplication(argc, argv);
	logMsg("configApp true");
    	configureApp(true);
	logMsg("setOrigin");
    	Flameshot::setOrigin(Flameshot::CLI);
	logMsg("init allowMultipleGuiInstances ");
        if (!ConfigHandler().allowMultipleGuiInstances()) {
            auto* mutex = guiMutexLock();
            if (!mutex) {
                return 1;
            }
            QObject::connect(
              qApp, &QCoreApplication::aboutToQuit, qApp, [mutex]() {
                  mutex->detach();
                  delete mutex;
              });
        }
	logMsg("argv[1]=%s",argv[1]);
	QString pathw= QString::fromUtf8(argv[1]);
	logMsg("new req 0 ");
        CaptureRequest req(CaptureRequest::GRAPHICAL_MODE, 0, pathw);
	logMsg("addSaveTask");
        req.addSaveTask(pathw);
	logMsg("request");
        requestCaptureAndWait(req);
	logMsg("return 0");
	return 0;
#endif

#if !defined(Q_OS_WIN)
    logMsg("no win cli arg>1");
    /*--------------|
     * CLI parsing  |
     * ------------*/
    new QCoreApplication(argc, argv);
    configureApp(false);
    CommandLineParser parser;
    // Add description
    parser.setDescription(
      QObject::tr("Powerful yet simple to use screenshot software."));
    parser.setGeneralErrorMessage(QObject::tr("See") + " flameshot --help.");
    // Arguments
    CommandArgument fullArgument(
      QStringLiteral("full"),
      QObject::tr("Capture screenshot of all monitors at the same time."));
    CommandArgument launcherArgument(QStringLiteral("launcher"),
                                     QObject::tr("Open the capture launcher."));
    CommandArgument guiArgument(
      QStringLiteral("gui"),
      QObject::tr("Start a manual capture in GUI mode."));
    CommandArgument configArgument(QStringLiteral("config"),
                                   QObject::tr("Configure") + " flameshot.");
    CommandArgument screenArgument(
      QStringLiteral("screen"),
      QObject::tr("Capture a screenshot of the specified monitor."));

    // Options
    CommandOption pathOption(
      { "p", "path" },
      QObject::tr("Existing directory or new file to save to"),
      QStringLiteral("path"));
    CommandOption clipboardOption(
      { "c", "clipboard" }, QObject::tr("Save the capture to the clipboard"));
    CommandOption pinOption("pin",
                            QObject::tr("Pin the capture to the screen"));
    CommandOption uploadOption({ "u", "upload" },
                               QObject::tr("Upload screenshot"));
    CommandOption delayOption({ "d", "delay" },
                              QObject::tr("Delay time in milliseconds"),
                              QStringLiteral("milliseconds"));

    CommandOption useLastRegionOption(
      "last-region",
      QObject::tr("Repeat screenshot with previously selected region"));

    CommandOption regionOption("region",
                               QObject::tr("Screenshot region to select"),
                               QStringLiteral("WxH+X+Y or string"));
    CommandOption filenameOption({ "f", "filename" },
                                 QObject::tr("Set the filename pattern"),
                                 QStringLiteral("pattern"));
    CommandOption acceptOnSelectOption(
      { "s", "accept-on-select" },
      QObject::tr("Accept capture as soon as a selection is made"));
    CommandOption trayOption({ "t", "trayicon" },
                             QObject::tr("Enable or disable the trayicon"),
                             QStringLiteral("bool"));
    CommandOption autostartOption(
      { "a", "autostart" },
      QObject::tr("Enable or disable run at startup"),
      QStringLiteral("bool"));
    CommandOption checkOption(
      "check", QObject::tr("Check the configuration for errors"));
    CommandOption showHelpOption(
      { "s", "showhelp" },
      QObject::tr("Show the help message in the capture mode"),
      QStringLiteral("bool"));
    CommandOption mainColorOption({ "m", "maincolor" },
                                  QObject::tr("Define the main UI color"),
                                  QStringLiteral("color-code"));
    CommandOption contrastColorOption(
      { "k", "contrastcolor" },
      QObject::tr("Define the contrast UI color"),
      QStringLiteral("color-code"));
    CommandOption rawImageOption({ "r", "raw" },
                                 QObject::tr("Print raw PNG capture"));
    CommandOption selectionOption(
      { "g", "print-geometry" },
      QObject::tr("Print geometry of the selection in the format WxH+X+Y. Does "
                  "nothing if raw is specified"));
    CommandOption screenNumberOption(
      { "n", "number" },
      QObject::tr("Define the screen to capture (starting from 0)") + ",\n" +
        QObject::tr("default: screen containing the cursor"),
      QObject::tr("Screen number"),
      QStringLiteral("-1"));

    // Add checkers
    auto colorChecker = [](const QString& colorCode) -> bool {
        QColor parsedColor(colorCode);
        return parsedColor.isValid() && parsedColor.alphaF() == 1.0;
    };
    QString colorErr =
      QObject::tr("Invalid color, "
                  "this flag supports the following formats:\n"
                  "- #RGB (each of R, G, and B is a single hex digit)\n"
                  "- #RRGGBB\n- #RRRGGGBBB\n"
                  "- #RRRRGGGGBBBB\n"
                  "- Named colors like 'blue' or 'red'\n"
                  "You may need to escape the '#' sign as in '\\#FFF'");

    const QString delayErr =
      QObject::tr("Invalid delay, it must be a number greater than 0");
    const QString numberErr =
      QObject::tr("Invalid screen number, it must be non negative");
    const QString regionErr = QObject::tr(
      "Invalid region, use 'WxH+X+Y' or 'all' or 'screen0/screen1/...'.");
    auto numericChecker = [](const QString& delayValue) -> bool {
        bool ok;
        int value = delayValue.toInt(&ok);
        return ok && value >= 0;
    };
    auto regionChecker = [](const QString& region) -> bool {
        Region valueHandler;
        return valueHandler.check(region);
    };

    const QString pathErr =
      QObject::tr("Invalid path, must be an existing directory or a new file "
                  "in an existing directory");
    auto pathChecker = [pathErr](const QString& pathValue) -> bool {
        QFileInfo fileInfo(pathValue);
        if (fileInfo.isDir() || fileInfo.dir().exists()) {
            return true;
        } else {
            AbstractLogger::error() << QObject::tr(pathErr.toLatin1().data());
            return false;
        }
    };

    const QString booleanErr =
      QObject::tr("Invalid value, it must be defined as 'true' or 'false'");
    auto booleanChecker = [](const QString& value) -> bool {
        return value == QLatin1String("true") ||
               value == QLatin1String("false");
    };

    contrastColorOption.addChecker(colorChecker, colorErr);
    mainColorOption.addChecker(colorChecker, colorErr);
    delayOption.addChecker(numericChecker, delayErr);
    regionOption.addChecker(regionChecker, regionErr);
    useLastRegionOption.addChecker(booleanChecker, booleanErr);
    pathOption.addChecker(pathChecker, pathErr);
    trayOption.addChecker(booleanChecker, booleanErr);
    autostartOption.addChecker(booleanChecker, booleanErr);
    showHelpOption.addChecker(booleanChecker, booleanErr);
    screenNumberOption.addChecker(numericChecker, numberErr);

    // Relationships
    parser.AddArgument(guiArgument);
    parser.AddArgument(screenArgument);
    parser.AddArgument(fullArgument);
    parser.AddArgument(launcherArgument);
    parser.AddArgument(configArgument);
    auto helpOption = parser.addHelpOption();
    auto versionOption = parser.addVersionOption();
    parser.AddOptions({ pathOption,
                        clipboardOption,
                        delayOption,
                        regionOption,
                        useLastRegionOption,
                        rawImageOption,
                        selectionOption,
                        uploadOption,
                        pinOption,
                        acceptOnSelectOption },
                      guiArgument);
    parser.AddOptions({ screenNumberOption,
                        clipboardOption,
                        pathOption,
                        delayOption,
                        regionOption,
                        rawImageOption,
                        uploadOption,
                        pinOption },
                      screenArgument);
    parser.AddOptions({ pathOption,
                        clipboardOption,
                        delayOption,
                        regionOption,
                        rawImageOption,
                        uploadOption },
                      fullArgument);
    parser.AddOptions({ autostartOption,
                        filenameOption,
                        trayOption,
                        showHelpOption,
                        mainColorOption,
                        contrastColorOption,
                        checkOption },
                      configArgument);
    // Parse
    if (!parser.parse(qApp->arguments())) {
        goto finish;
    }

    // PROCESS DATA
    //--------------
    Flameshot::setOrigin(Flameshot::CLI);
    if (parser.isSet(helpOption) || parser.isSet(versionOption)) {
    } else if (parser.isSet(launcherArgument)) { // LAUNCHER
        reinitializeAsQApplication(argc, argv);
        Flameshot* flameshot = Flameshot::instance();
        flameshot->launcher();
        qApp->exec();
    } else if (parser.isSet(guiArgument)) { // GUI
        reinitializeAsQApplication(argc, argv);
        // Prevent multiple instances of 'flameshot gui' from running if not
        // configured to do so.
        if (!ConfigHandler().allowMultipleGuiInstances()) {
            auto* mutex = guiMutexLock();
            if (!mutex) {
                return 1;
            }
            QObject::connect(
              qApp, &QCoreApplication::aboutToQuit, qApp, [mutex]() {
                  mutex->detach();
                  delete mutex;
              });
        }

        // Option values
        QString path = parser.value(pathOption);
        if (!path.isEmpty()) {
            path = QDir(path).absolutePath();
        }
        int delay = parser.value(delayOption).toInt();
        QString region = parser.value(regionOption);
        bool useLastRegion = parser.isSet(useLastRegionOption);
        bool clipboard = parser.isSet(clipboardOption);
        bool raw = parser.isSet(rawImageOption);
        bool printGeometry = parser.isSet(selectionOption);
        bool pin = parser.isSet(pinOption);
        bool upload = parser.isSet(uploadOption);
        bool acceptOnSelect = parser.isSet(acceptOnSelectOption);
        CaptureRequest req(CaptureRequest::GRAPHICAL_MODE, delay, path);
        if (!region.isEmpty()) {
            auto selectionRegion = Region().value(region).toRect();
            req.setInitialSelection(selectionRegion);
        } else if (useLastRegion) {
            req.setInitialSelection(getLastRegion());
        }
        if (clipboard) {
            req.addTask(CaptureRequest::COPY);
        }
        if (raw) {
            req.addTask(CaptureRequest::PRINT_RAW);
        }
        if (!path.isEmpty()) {
            req.addSaveTask(path);
        }
        if (printGeometry) {
            req.addTask(CaptureRequest::PRINT_GEOMETRY);
        }
        if (pin) {
            req.addTask(CaptureRequest::PIN);
        }
        if (upload) {
            req.addTask(CaptureRequest::UPLOAD);
        }
        if (acceptOnSelect) {
            req.addTask(CaptureRequest::ACCEPT_ON_SELECT);
            if (!clipboard && !raw && path.isEmpty() && !printGeometry &&
                !pin && !upload) {
                req.addSaveTask();
            }
        }
        requestCaptureAndWait(req);
    } else if (parser.isSet(fullArgument)) { // FULL
        reinitializeAsQApplication(argc, argv);

        // Option values
        QString path = parser.value(pathOption);
        if (!path.isEmpty()) {
            path = QDir(path).absolutePath();
        }
        int delay = parser.value(delayOption).toInt();
        QString region = parser.value(regionOption);
        bool clipboard = parser.isSet(clipboardOption);
        bool raw = parser.isSet(rawImageOption);
        bool upload = parser.isSet(uploadOption);
        // Not a valid command

        CaptureRequest req(CaptureRequest::FULLSCREEN_MODE, delay);
        if (!region.isEmpty()) {
            req.setInitialSelection(Region().value(region).toRect());
        }
        if (clipboard) {
            req.addTask(CaptureRequest::COPY);
        }
        if (!path.isEmpty()) {
            req.addSaveTask(path);
        }
        if (raw) {
            req.addTask(CaptureRequest::PRINT_RAW);
        }
        if (upload) {
            req.addTask(CaptureRequest::UPLOAD);
        }
        if (!clipboard && path.isEmpty() && !raw && !upload) {
            req.addSaveTask();
        }
        requestCaptureAndWait(req);
    } else if (parser.isSet(screenArgument)) { // SCREEN
        reinitializeAsQApplication(argc, argv);

        QString numberStr = parser.value(screenNumberOption);
        // Option values
        int screenNumber =
          numberStr.startsWith(QLatin1String("-")) ? -1 : numberStr.toInt();
        QString path = parser.value(pathOption);
        if (!path.isEmpty()) {
            path = QDir(path).absolutePath();
        }
        int delay = parser.value(delayOption).toInt();
        QString region = parser.value(regionOption);
        bool clipboard = parser.isSet(clipboardOption);
        bool raw = parser.isSet(rawImageOption);
        bool pin = parser.isSet(pinOption);
        bool upload = parser.isSet(uploadOption);

        CaptureRequest req(CaptureRequest::SCREEN_MODE, delay, screenNumber);
        if (!region.isEmpty()) {
            if (region.startsWith("screen")) {
                AbstractLogger::error()
                  << "The 'screen' command does not support "
                     "'--region screen<N>'.\n"
                     "See flameshot --help.\n";
                exit(1);
            }
            req.setInitialSelection(Region().value(region).toRect());
        }
        if (clipboard) {
            req.addTask(CaptureRequest::COPY);
        }
        if (raw) {
            req.addTask(CaptureRequest::PRINT_RAW);
        }
        if (!path.isEmpty()) {
            req.addSaveTask(path);
        }
        if (pin) {
            req.addTask(CaptureRequest::PIN);
        }
        if (upload) {
            req.addTask(CaptureRequest::UPLOAD);
        }

        if (!clipboard && !raw && path.isEmpty() && !pin && !upload) {
            req.addSaveTask();
        }

        requestCaptureAndWait(req);
    } else if (parser.isSet(configArgument)) { // CONFIG
        bool autostart = parser.isSet(autostartOption);
        bool filename = parser.isSet(filenameOption);
        bool tray = parser.isSet(trayOption);
        bool mainColor = parser.isSet(mainColorOption);
        bool contrastColor = parser.isSet(contrastColorOption);
        bool check = parser.isSet(checkOption);
        bool someFlagSet =
          (filename || tray || mainColor || contrastColor || check);
        if (check) {
            AbstractLogger err = AbstractLogger::error(AbstractLogger::Stderr);
            bool ok = ConfigHandler().checkForErrors(&err);
            if (ok) {
                AbstractLogger::info()
                  << QStringLiteral("No errors detected.\n");
                goto finish;
            } else {
                return 1;
            }
        }
        if (!someFlagSet) {
            // Open gui when no options are given
            reinitializeAsQApplication(argc, argv);
            QObject::connect(
              qApp, &QApplication::lastWindowClosed, qApp, &QApplication::quit);
            Flameshot::instance()->config();
            qApp->exec();
        } else {
            ConfigHandler config;

            if (autostart) {
                config.setStartupLaunch(parser.value(autostartOption) ==
                                        "true");
            }
            if (filename) {
                QString newFilename(parser.value(filenameOption));
                config.setFilenamePattern(newFilename);
                FileNameHandler fh;
                QTextStream(stdout)
                  << QStringLiteral("The new pattern is '%1'\n"
                                    "Parsed pattern example: %2\n")
                       .arg(newFilename)
                       .arg(fh.parsedPattern());
            }
            if (tray) {
                config.setDisabledTrayIcon(parser.value(trayOption) == "false");
            }
            if (mainColor) {
                // TODO use value handler
                QString colorCode = parser.value(mainColorOption);
                QColor parsedColor(colorCode);
                config.setUiColor(parsedColor);
            }
            if (contrastColor) {
                QString colorCode = parser.value(contrastColorOption);
                QColor parsedColor(colorCode);
                config.setContrastUiColor(parsedColor);
            }
        }
    }
finish:

#endif
	printf("any wind singleapp end1\n");
    return 0;
}
