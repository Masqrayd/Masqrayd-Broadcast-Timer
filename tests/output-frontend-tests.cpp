#include "obs-timer-events.hpp"
#include "broadcast-stack.hpp"
#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

static void check(bool ok, const char *message) {
    if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static void settle() { for (int i = 0; i < 5; ++i) QApplication::processEvents(); }
static void toggle(TimerWindow &timer, const char *name) {
    QTimer::singleShot(0, &timer, [name] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        check(menu, "timer context menu opens");
        auto *action = menu->findChild<QAction *>(name);
        check(action && action->isCheckable(), "OBS option is independently selectable");
        action->trigger(); menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), timer.mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(&timer, &event); settle();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir; qint64 now = 1000;
    const QString settings = dir.filePath("timer.ini");
    bool active[2]{}; int requests[2]{};
    TimerWindow::OutputBackend backend{
        [&](TimerWindow::Output output) { return active[static_cast<int>(output)]; },
        [&](TimerWindow::Output output) { ++requests[static_cast<int>(output)]; }};
    {
        TimerWindow timer(settings, nullptr, [&] { return now; }); timer.setOutputBackend(backend); timer.showTimer(); settle();
        auto *record = timer.findChild<QPushButton *>("startRecording");
        auto *stream = timer.findChild<QPushButton *>("startStreaming");
        auto *start = timer.findChild<QPushButton *>("start");
        auto *pause = timer.findChild<QPushButton *>("pause");
        auto *reset = timer.findChild<QPushButton *>("reset");
        check(record && record->isVisible() && stream && !stream->isVisible(), "recording-only buttons default for this workflow");
        record->click(); record->click(); settle();
        check(requests[0] == 1 && requests[1] == 0 && !record->isEnabled(), "record button requests only recording once while pending");
        check(timer.state().status == TimerState::Status::Ready, "click alone does not start timer before OBS confirms");
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTING);
        now += 2000;
        check(timer.state().status == TimerState::Status::Ready, "starting event does not count connection/setup time");
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STOPPED);
        check(record->isEnabled() && timer.state().status == TimerState::Status::Ready, "failed or cancelled start leaves timer ready and permits retry");
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED);
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED);
        check(timer.state().status == TimerState::Status::Ready, "unrelated OBS output events do not start timer");
        // No timer button click: this is the same route used by OBS controls/hotkeys.
        active[0] = true; timer.hide();
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().status == TimerState::Status::Running && timer.state().story == 1, "external OBS recording start auto-starts hidden timer at story one");
        now += 3500;
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().elapsed(now) == 3500 && !record->isEnabled(), "duplicate started event preserves elapsed reading and marks output active");
        pause->click();
        active[1] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STARTED);
        check(timer.state().status == TimerState::Status::Paused && timer.state().elapsed(now) == 3500, "second output does not reset or undo a manual pause");
        reset->click(); forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().status == TimerState::Status::Ready, "manual reset remains effective while output is already active");
        active[0] = false; active[1] = false;
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STOPPED);
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STOPPED);
        active[1] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STARTED);
        check(timer.state().status == TimerState::Status::Running, "stream start auto-start works even with stream button hidden");
        now += 1000; active[1] = false; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STOPPED);
        check(timer.state().status == TimerState::Status::Running && timer.state().elapsed(now) == 1000, "stopping OBS output does not stop or reset story timer");
        timer.nextStory(); now += 2000;
        active[0] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().story == 2 && timer.state().elapsed(now) == 2000, "existing running story is never restarted or incremented by OBS");
        timer.findChild<QPushButton *>("stop")->click();
        active[0] = false; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STOPPED);
        active[0] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().story == 2 && timer.state().elapsed(now) == 0 && timer.state().status == TimerState::Status::Running,
              "new session starts a stopped current story fresh without advancing story number");
        active[0] = false; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STOPPED); reset->click();
        timer.showTimer(); toggle(timer, "autoStartRecording");
        active[0] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().status == TimerState::Status::Ready, "recording auto-start can be disabled independently");
        active[1] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STARTED);
        check(timer.state().status == TimerState::Status::Running, "enabled streaming trigger works when recording trigger is disabled");
        active[0] = active[1] = false;
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STOPPED);
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STOPPED); reset->click();
        toggle(timer, "showStreamingButton"); check(record->isVisible() && stream->isVisible(), "both OBS start buttons can be shown");
        stream->click(); settle(); check(requests[1] == 1 && requests[0] == 1, "stream button requests streaming without recording");
        now += 10001; timer.refresh();
        check(stream->isEnabled() && timer.state().status == TimerState::Status::Ready, "unacknowledged start request times out without starting timer");
        for (QSize size : {QSize(250, 110), QSize(160, 130), QSize(600, 110), QSize(250, 160)}) {
            timer.resize(size); settle();
            check(timer.size() == size, "both OBS controls retain compact height support");
            for (auto *button : timer.findChildren<QPushButton *>()) {
                check(button->isVisible() && button->height() >= 10 &&
                      timer.rect().contains(QRect(button->mapTo(&timer, QPoint()), button->size())),
                      "all eight buttons remain visible and reachable in compact timer");
            }
        }
        // Same button/event flow while embedded inside the Broadcast Stack.
        BroadcastStack::Providers providers;
        providers.qtPanel = [&](BroadcastStack::Panel p) -> QWidget * { return p == BroadcastStack::Timer ? &timer : nullptr; };
        {
            BroadcastStack stack(dir.filePath("stack.ini"), providers); stack.showStack(); stack.setIncluded(BroadcastStack::Timer, true); settle();
            stream->click(); settle(); check(requests[1] == 2, "stacked streaming button requests OBS output");
            active[1] = true; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STARTED);
            check(timer.state().status == TimerState::Status::Running, "OBS starts timer while embedded");
            stack.shutdown();
        }
        active[1] = false; forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_STREAMING_STOPPED);
        timer.showTimer(); toggle(timer, "showRecordingButton");
        check(!record->isVisible() && stream->isVisible(), "streaming-only choice is available");
        if (argc > 1) { timer.resize(300, 230); settle(); timer.grab().save(QString::fromLocal8Bit(argv[1])); }
        timer.savePreferences(); timer.shutdown();
        const auto elapsed = timer.state().elapsed(now);
        forwardTimerOutputEvent(timer, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(timer.state().elapsed(now) == elapsed, "shutdown ignores late output events");
    }
    {
        active[0] = active[1] = false;
        TimerWindow restored(settings, nullptr, [&] { return now; }); restored.setOutputBackend(backend); restored.showTimer(); settle();
        check(!restored.findChild<QPushButton *>("startRecording")->isVisible() && restored.findChild<QPushButton *>("startStreaming")->isVisible(), "button choices persist across reopen");
        forwardTimerOutputEvent(restored, OBS_FRONTEND_EVENT_RECORDING_STARTED);
        check(restored.state().status == TimerState::Status::Ready, "disabled recording trigger persists");
        forwardTimerOutputEvent(restored, OBS_FRONTEND_EVENT_STREAMING_STARTED);
        check(restored.state().status == TimerState::Status::Running, "enabled stream trigger persists");
    }
    std::cout << "OBS frontend event mapping, start requests, failures, preferences and compact buttons passed\n";
}
