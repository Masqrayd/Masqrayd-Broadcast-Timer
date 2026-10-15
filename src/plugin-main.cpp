#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include "timer-window.hpp"

OBS_DECLARE_MODULE()

static QPointer<TimerWindow> timerWindow;
static QPointer<QAction> toolsAction;
static bool exiting = false;

MODULE_EXPORT const char *obs_module_name(void) { return "Masqrayd Broadcast Timer"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Masqrayd"; }
MODULE_EXPORT const char *obs_module_description(void) {
    return "A floating story and break timer with adjustable warnings and a local date/time clock.";
}

static void frontendEvent(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        exiting = true;
        if (timerWindow) timerWindow->shutdown();
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[Masqrayd Broadcast Timer] Loading version 1.0.0");
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
    toolsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Masqrayd Broadcast Timer"));
    if (toolsAction) QObject::connect(toolsAction, &QAction::triggered, timerWindow, [] {
        if (timerWindow && !exiting) timerWindow->showTimer();
    });
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    QTimer::singleShot(0, timerWindow, [] {
        if (timerWindow && !exiting && timerWindow->opensWithOBS()) timerWindow->showTimer();
    });
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (timerWindow) { timerWindow->shutdown(); delete timerWindow.data(); }
    if (toolsAction) delete toolsAction.data();
}
