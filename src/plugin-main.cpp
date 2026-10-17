#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include "timer-window.hpp"
#include "broadcast-stack.hpp"
#include "native-camera-surface.hpp"
#include "obs-timer-events.hpp"

OBS_DECLARE_MODULE()

static QPointer<TimerWindow> timerWindow;
static QPointer<QAction> toolsAction;
static QPointer<BroadcastStack> stackWindow;
static QPointer<QAction> stackAction;
static bool exiting = false;

MODULE_EXPORT const char *obs_module_name(void) { return "Masqrayd Broadcast Timer"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Masqrayd"; }
MODULE_EXPORT const char *obs_module_description(void) {
    return "Story/break timer and optional Broadcast Stack for the camera, scene controller, timer and scene collections.";
}

static void frontendEvent(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        exiting = true;
        if (stackWindow) stackWindow->shutdown();
        if (timerWindow) timerWindow->shutdown();
        return;
    }
    if (timerWindow && !exiting) {
        const QPointer<TimerWindow> window = timerWindow;
        QMetaObject::invokeMethod(window.data(), [window, event] {
            if (window && !exiting) forwardTimerOutputEvent(*window, event);
        }, Qt::AutoConnection);
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[Masqrayd Broadcast Timer] Loading version 1.3.0");
    return true;
}

void obs_module_post_load(void) {
    auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!main) { blog(LOG_ERROR, "[Masqrayd Broadcast Timer] Main window unavailable"); return; }
    char *path = obs_module_config_path("timer.ini");
    if (!path) return;
    const QString config = QString::fromUtf8(path); bfree(path);
    if (!QDir().mkpath(QFileInfo(config).absolutePath())) {
        blog(LOG_ERROR, "[Masqrayd Broadcast Timer] Could not create settings directory"); return;
    }
    timerWindow = new TimerWindow(config, main);
    timerWindow->setOutputBackend({
        [](TimerWindow::Output output) {
            return output == TimerWindow::Output::Recording ? obs_frontend_recording_active() : obs_frontend_streaming_active();
        },
        [](TimerWindow::Output output) {
            if (exiting) return;
            if (output == TimerWindow::Output::Recording) obs_frontend_recording_start();
            else obs_frontend_streaming_start();
        }
    });
    BroadcastStack::Providers providers;
    providers.qtPanel = [](BroadcastStack::Panel panel) -> QWidget * {
        if (panel == BroadcastStack::Timer) return timerWindow;
        if (panel == BroadcastStack::Controller || panel == BroadcastStack::Collections) for (auto *widget : QApplication::allWidgets())
            if (widget->objectName() == (panel == BroadcastStack::Controller ? "masqraydSceneController" : "masqraydSceneCollections")) return widget;
        return nullptr;
    };
    providers.camera = &NativeCameraSurface::findCamera;
    providers.open = [main](BroadcastStack::Panel panel) {
        if (panel == BroadcastStack::Timer) return;
        if (panel == BroadcastStack::Camera && NativeCameraSurface::findCamera()) return;
        if (panel == BroadcastStack::Controller) for (auto *widget : QApplication::allWidgets())
            if (widget->objectName() == "masqraydSceneController") return;
        for (auto *action : main->findChildren<QAction *>()) {
            QString text = action->text(); text.remove('&');
            if ((panel == BroadcastStack::Controller && action->objectName() == "masqraydSceneControllerAction") ||
                (panel == BroadcastStack::Camera && text == "Camera Pop-Out Native") ||
                (panel == BroadcastStack::Collections && action->objectName() == "masqraydSceneCollectionsAction")) { action->trigger(); break; }
        }
    };
    stackWindow = new BroadcastStack(QFileInfo(config).dir().filePath("broadcast-stack.ini"), providers);
    stackAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Masqrayd Broadcast Stack"));
    if (stackAction) QObject::connect(stackAction, &QAction::triggered, stackWindow, [] {
        if (stackWindow && !exiting) stackWindow->showStack();
    });
    toolsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Masqrayd Broadcast Timer"));
    if (toolsAction) QObject::connect(toolsAction, &QAction::triggered, timerWindow, [] {
        if (timerWindow && !exiting) timerWindow->showTimer();
    });
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    QTimer::singleShot(0, timerWindow, [] {
        if (timerWindow && !exiting && timerWindow->opensWithOBS()) timerWindow->showTimer();
    });
    QTimer::singleShot(500, timerWindow, [] {
        if (stackWindow && !exiting && stackWindow->opensWithOBS()) stackWindow->showStack();
    });
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (stackWindow) { stackWindow->shutdown(); delete stackWindow.data(); }
    if (stackAction) delete stackAction.data();
    if (timerWindow) { timerWindow->shutdown(); delete timerWindow.data(); }
    if (toolsAction) delete toolsAction.data();
}
