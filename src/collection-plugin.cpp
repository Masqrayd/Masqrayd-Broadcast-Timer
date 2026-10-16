#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include "collection-window.hpp"

OBS_DECLARE_MODULE()
static QPointer<CollectionWindow> panel;
static QPointer<QAction> action;
static bool exiting = false, changing = false;
MODULE_EXPORT const char *obs_module_name(void) { return "Masqrayd Scene Collections"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Masqrayd"; }
MODULE_EXPORT const char *obs_module_description(void) { return "Scene collection buttons, standalone or attached to Broadcast Stack."; }

static CollectionWindow::Snapshot readCollections() {
    CollectionWindow::Snapshot result;
    if (exiting) return result;
    char **names = obs_frontend_get_scene_collections();
    if (names) { for (char **name = names; *name; ++name) result.collections << QString::fromUtf8(*name); bfree(names); }
    char *current = obs_frontend_get_current_scene_collection();
    if (current) { result.current = QString::fromUtf8(current); bfree(current); }
    char *profile = obs_frontend_get_current_profile();
    if (profile) { result.profile = QString::fromUtf8(profile); bfree(profile); }
    result.changing = changing; return result;
}
static void event(enum obs_frontend_event e, void *) {
    if (e == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) changing = true;
    if (e == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || e == OBS_FRONTEND_EVENT_FINISHED_LOADING) changing = false;
    if (e == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || e == OBS_FRONTEND_EVENT_EXIT) {
        exiting = true; if (panel) panel->shutdown(); return;
    }
    if (panel && !exiting) QTimer::singleShot(0, panel, [] { if (panel && !exiting) panel->refresh(); });
}
bool obs_module_load(void) { blog(LOG_INFO, "[Masqrayd Scene Collections] Loading version 1.2.0"); return true; }
void obs_module_post_load(void) {
    auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window()); if (!main) return;
    char *path = obs_module_config_path("collections.ini"); if (!path) return;
    const QString file = QString::fromUtf8(path); bfree(path);
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) return;
    CollectionWindow::Backend backend;
    backend.read = readCollections;
    backend.activate = [](const QString &name) -> QString {
        if (exiting || changing) return "OBS is not ready to switch collections.";
        if (!readCollections().collections.contains(name)) return "Collection unavailable. Update Collections shown.";
        // Same frontend path as OBS's Scene Collection menu. OBS saves and loads
        // the collection; this plugin never edits scene JSON or changes profiles.
        const QByteArray utf8 = name.toUtf8();
        obs_frontend_set_current_scene_collection(utf8.constData());
        return {};
    };
    panel = new CollectionWindow(file, std::move(backend), main);
    action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Masqrayd Scene Collections"));
    if (action) {
        action->setObjectName("masqraydSceneCollectionsAction");
        QObject::connect(action, &QAction::triggered, panel, [] { if (panel && !exiting) panel->showCollections(); });
    }
    obs_frontend_add_event_callback(event, nullptr);
    QTimer::singleShot(0, panel, [] { if (panel && !exiting && panel->opensWithOBS()) panel->showCollections(); });
}
void obs_module_unload(void) {
    obs_frontend_remove_event_callback(event, nullptr);
    if (panel) { panel->shutdown(); delete panel.data(); }
    if (action) delete action.data();
}
