#include "../gui/canvas.h"
#include "contextmanager.h"
#include "qsgrendererinterface.h"
#include "qsurfaceformat.h"
#include "rendercontext.h"
#include <QQmlContext>
#include <iostream>
#include <chrono>
#include <cstdlib>

using namespace Main;
using namespace std::chrono_literals;

#ifndef WITHOUT_SYNTH
GuiContext::GuiContext(Config *config, RenderContext *renderContext, SynthWrapper *synthWrapper, DataVisWrapper *dataVisWrapper)
#else
GuiContext::GuiContext(Config *config, RenderContext *renderContext, DataVisWrapper *dataVisWrapper)
#endif
    : mConfig(config),
      mRenderContext(renderContext),
      mSelectedView(nullptr)
{
    QCoreApplication::setApplicationName("InFormant");
    QCoreApplication::setApplicationVersion(INFORMANT_VERSION_STR);
    QCoreApplication::setOrganizationDomain("in-formant.app");
    QCoreApplication::setOrganizationName("InFormant");

    QQuickStyle::setStyle("Material");
    qmlRegisterType<Gui::CanvasItem>("IfCanvas", 1, 0, "IfCanvas");

    mApp = std::make_unique<QApplication>(argc, argv);

    // Only supports OpenGL for now.
    QQuickWindow::setSceneGraphBackend("rhi");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication::setWindowIcon(QIcon(":/icons/in-formant.png"));

    mQmlEngine = std::make_unique<QQmlApplicationEngine>();
    mQmlEngine->addImportPath(QCoreApplication::applicationDirPath() + "/qml");
#ifdef __APPLE__
    mQmlEngine->addImportPath(QCoreApplication::applicationDirPath() + "/../Resources/qml");
#endif

    mQmlEngine->rootContext()->setContextProperty("appName", "InFormant " INFORMANT_VERSION_STR);

    mQmlEngine->rootContext()->setContextProperty("config", mConfig);

#ifndef WITHOUT_SYNTH
    mQmlEngine->rootContext()->setContextProperty("synth", synthWrapper);
    mQmlEngine->rootContext()->setContextProperty("HAS_SYNTH", true);
#else
    mQmlEngine->rootContext()->setContextProperty("HAS_SYNTH", false);
#endif

#ifdef ENABLE_TORCH
    mQmlEngine->rootContext()->setContextProperty("HAS_TORCH", true);
#else
    mQmlEngine->rootContext()->setContextProperty("HAS_TORCH", false);
#endif

    mQmlEngine->rootContext()->setContextProperty("dataVis", dataVisWrapper);

    mQmlEngine->load(QUrl("qrc:/MainWindow.qml"));
   
    auto window = static_cast<QQuickWindow *>(mQmlEngine->rootObjects().first());
    auto canvasItem = window->findChild<Gui::CanvasItem *>("IfCanvas");

    canvasItem->setRenderContext(mRenderContext);

    canvasItem->installEventFilter(this);
    window->installEventFilter(this);

    // Opt-in probe (IF_DEBUG_JITTER=1): timestamp every actual buffer swap. The
    // in-render probe measures when we draw; this measures when frames are handed
    // to the compositor -- the two can disagree, and the difference is invisible
    // to any render-side measurement.
    static const bool debugJitter = (std::getenv("IF_DEBUG_JITTER") != nullptr);
    if (debugJitter) {
        QObject::connect(window, &QQuickWindow::frameSwapped, [] {
            const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::cout << "SWP]," << wall << std::endl;
        });
    }

    // The config was historically written only in ~Config(), which never runs
    // here: Qt's FBO-node teardown segfaults kill the process first, so no
    // setting ever survived a restart. Save while the event loop is still
    // healthy instead: on quit, and every 30 s as a backstop against crashes.
    QObject::connect(mApp.get(), &QCoreApplication::aboutToQuit,
                     [this] { mConfig->save(); });
    auto autosave = new QTimer(mApp.get());
    QObject::connect(autosave, &QTimer::timeout, [this] { mConfig->save(); });
    autosave->start(30000);

    window->show();
}

int GuiContext::exec()
{
    return mApp->exec();
}

void GuiContext::setView(GuiView *view)
{
    mSelectedView = view;
}

bool GuiContext::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto keyEvent = static_cast<QKeyEvent *>(event);
        if (mSelectedView != nullptr && mSelectedView->onKeyPress(keyEvent)) {
            return true;
        }
        else {
        }
    }
    else if (event->type() == QEvent::KeyRelease) {
        auto keyEvent = static_cast<QKeyEvent *>(event);
        if (mSelectedView != nullptr && mSelectedView->onKeyRelease(keyEvent)) {
            return true;
        }
        else {
        }
    }

    return false;
}

void GuiContext::setShowSpectrogram(bool b)
{
    mConfig->setViewShowSpectrogram(b);
}

void GuiContext::setShowPitch(bool b)
{
    mConfig->setViewShowPitch(b);
}

void GuiContext::setShowFormants(bool b)
{
    mConfig->setViewShowFormants(b);
}
