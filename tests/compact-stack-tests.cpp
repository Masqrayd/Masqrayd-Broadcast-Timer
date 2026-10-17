#include "broadcast-stack.hpp"
#include "timer-window.hpp"
#include "collection-window.hpp"
#include "scene-controller.hpp"
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QApplication>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

// Exercise the actual partner controller alongside the timer and collections.
// Only OBS's frontend/source contract is simulated; no camera/renderer needed.
struct FakeScene { const char *id; const char *name; };
static FakeScene scenes[] = {{"bumper", "Channel Bumper"}, {"host", "1-Host Only"},
    {"split", "2-Split Reaction"}, {"focus", "3-Host Focus"}, {"browser", "4-Browser Focus"}};
static obs_source_t *source(int i) { return reinterpret_cast<obs_source_t *>(&scenes[i]); }
static obs_source_t *program = source(1), *preview = source(3);
static bool studio = true;
extern "C" {
const char *obs_source_get_uuid(const obs_source_t *p) { return reinterpret_cast<const FakeScene *>(p)->id; }
const char *obs_source_get_name(const obs_source_t *p) { return reinterpret_cast<const FakeScene *>(p)->name; }
bool obs_source_is_scene(const obs_source_t *) { return true; }
void obs_source_release(obs_source_t *) {}
obs_source_t *obs_get_source_by_uuid(const char *id) {
    for (int i = 0; i < 5; ++i) if (QString::fromUtf8(scenes[i].id) == QString::fromUtf8(id)) return source(i);
    return nullptr;
}
void obs_frontend_get_scenes(obs_frontend_source_list *out) {
    out->sources.num = out->sources.capacity = 5;
    out->sources.array = static_cast<obs_source_t **>(bmalloc(5 * sizeof(obs_source_t *)));
    for (int i = 0; i < 5; ++i) out->sources.array[i] = source(i);
}
#if LIBOBS_API_MAJOR_VER >= 32
void obs_frontend_source_list_free(obs_frontend_source_list *out) { bfree(out->sources.array); *out = {}; }
#endif
obs_source_t *obs_frontend_get_current_scene() { return program; }
obs_source_t *obs_frontend_get_current_preview_scene() { return preview; }
bool obs_frontend_preview_program_mode_active() { return studio; }
void obs_frontend_set_preview_program_mode(bool value) { studio = value; }
void obs_frontend_set_current_preview_scene(obs_source_t *p) { preview = p; }
void obs_frontend_preview_program_trigger_transition() { std::swap(program, preview); }
}
static void check(bool value, const char *message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static void settle() { for (int i = 0; i < 8; ++i) QApplication::processEvents(); }
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir; qint64 now = 1000;
    TimerWindow timer(dir.filePath("timer.ini"), nullptr, [&] { return now; });
    SceneController controller(nullptr, [] { return QSet<QString>{}; }, dir.filePath("controller.ini"));
    CollectionWindow::Snapshot state{{"Celebrity Masqrayd", "Criminal Masqrayd", "Political Masqrayd", "Masqrayd-Mayhem"},
                                      "Celebrity Masqrayd", "Main Profile", false};
    CollectionWindow collections(dir.filePath("collections.ini"), {
        [&] { return state; }, [&](const QString &name) { state.current = name; return QString(); }});
    collections.setColumns(4);
    BroadcastStack::Providers providers;
    providers.qtPanel = [&](BroadcastStack::Panel p) -> QWidget * {
        if (p == BroadcastStack::Timer) return &timer;
        if (p == BroadcastStack::Controller) return &controller;
        if (p == BroadcastStack::Collections) return &collections;
        return nullptr;
    };
    int savedTimer = 0, savedCollections = 0;
    const QString settings = dir.filePath("stack.ini");
    {
        BroadcastStack stack(settings, providers); stack.resize(250, 800); stack.showStack();
        for (int i = 0; i < BroadcastStack::PanelCount; ++i) stack.setIncluded(static_cast<BroadcastStack::Panel>(i), true);
        settle();
        auto *splitter = stack.findChild<QSplitter *>("stackSplitter");
        splitter->setSizes({190, 240, 230, 106}); settle();
        const int initialController = controller.height(), initialTimer = timer.height();
        splitter->setSizes({190, 330, 140, 106}); settle();
        check(stack.width() == 250 && timer.height() < 160 && timer.height() < initialTimer,
              "timer compresses vertically at screenshot width");
        check(controller.height() > initialController && collections.height() < 100,
              "compact timer and collections free height for scene controls");
        check(controller.findChild<QScrollArea *>()->verticalScrollBar()->maximum() == 0,
              "all five scene buttons fit without scrolling in the four-panel stack");
        int sceneCount = 0;
        for (auto *button : controller.findChildren<QPushButton *>()) {
            if (!button->property("sceneButton").toBool()) continue;
            ++sceneCount;
            auto *viewport = controller.findChild<QScrollArea *>()->viewport();
            check(viewport->rect().contains(QRect(button->mapTo(viewport, QPoint()), button->size())),
                  "each scene button is entirely within the visible viewport");
        }
        check(sceneCount == 5, "five production scenes retained");
        for (const char *name : {"start", "pause", "stop", "reset", "nextStory", "takeBreak"}) {
            auto *button = timer.findChild<QPushButton *>(name);
            check(button && timer.rect().contains(button->geometry()) && button->height() >= 10,
                  "every timer control remains visible in compact mode");
        }
        timer.findChild<QPushButton *>("start")->click(); now += 3000;
        collections.findChild<QPushButton *>("collection_1")->click(); settle();
        check(state.current == "Criminal Masqrayd" && timer.state().elapsed(now) == 3000,
              "compact collections switch without resetting the running timer");
        savedTimer = timer.height(); savedCollections = collections.height();
        if (argc > 1) stack.grab().save(QString::fromLocal8Bit(argv[1]));
        stack.savePreferences(); stack.shutdown();
    }
    {
        BroadcastStack restored(settings, providers); restored.showStack(); settle();
        check(std::abs(timer.height() - savedTimer) <= 2 && std::abs(collections.height() - savedCollections) <= 2,
              "compact heights survive reopening after placeholder replacement");
        restored.setIncluded(BroadcastStack::Controller, false); settle();
        check(controller.isWindow(), "real partner controller detaches normally after compaction");
    }
    std::cout << "Compact four-panel layout, five scenes, controls and saved heights passed\n";
}
