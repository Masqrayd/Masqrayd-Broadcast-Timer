#include "timer-window.hpp"
#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <cstdlib>

static void check(bool ok, const char *why) {
    if (!ok) { std::cerr << "FAILED: " << why << '\n'; std::exit(1); }
}
static void click(TimerWindow &w, const char *name) {
    auto *b = w.findChild<QPushButton *>(name); check(b, "Control exists");
    b->click(); QApplication::processEvents();
}
static void menuAction(TimerWindow &w, const char *name, bool enabled = true) {
    QTimer::singleShot(0, &w, [&w, name, enabled] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        check(menu, "Right-click opens menu");
        auto *action = menu->findChild<QAction *>(name);
        check(action && action->isEnabled() == enabled, "Story menu action has expected enabled state");
        if (enabled) action->trigger();
        menu->close();
    });
    const QPoint position = w.rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, w.mapToGlobal(position));
    QApplication::sendEvent(&w, &event); QApplication::processEvents();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir; qint64 t = 1000;
    QString path = dir.filePath("timer.ini");
    {
        TimerWindow w(path, nullptr, [&t] { return t; }); w.showTimer(); app.processEvents();
        click(w, "start"); t += 2000; click(w, "pause");
        t += 50000; check(w.state().elapsed(t) == 2000, "Pause button excludes idle time");
        click(w, "pause"); t += 1000; click(w, "stop");
        check(w.state().elapsed(t) == 3000 && w.state().status == TimerState::Status::Stopped, "Stop preserves reading");
        click(w, "reset"); check(w.state().elapsed(t) == 0 && w.state().status == TimerState::Status::Ready, "Reset button readies");
        click(w, "nextStory"); check(w.state().story == 2, "Next Story button advances");
        click(w, "takeBreak"); check(w.state().target == 300000, "Break button uses break duration");
        click(w, "nextStory"); check(w.state().target == 600000 && w.state().story == 3, "Return from break uses story duration");
        t += 12000; click(w, "pause"); menuAction(w, "restartCurrentStory");
        check(w.state().story == 3 && w.state().elapsed(t) == 0 && w.state().status == TimerState::Status::Ready, "Right-click restart redoes same paused story");
        menuAction(w, "previousStory");
        check(w.state().story == 2 && w.state().status == TimerState::Status::Ready, "Right-click previous story returns to story two");
        click(w, "start"); t += 1000; click(w, "reset");
        check(w.state().story == 2 && w.state().elapsed(t) == 0, "Reset preserves story two when redoing it");
        QTimer::singleShot(0, &w, [&w] {
            // The outer callback selects the menu item, and the inner callback
            // answers the resulting modal story-number dialog.
            QTimer::singleShot(0, &w, [] {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                check(dialog, "Set Story Number opens dialog"); dialog->setIntValue(9); dialog->accept();
            });
        });
        menuAction(w, "setStoryNumber");
        check(w.state().story == 9 && w.state().status == TimerState::Status::Ready, "Story number dialog selects nine without starting");
        click(w, "takeBreak"); menuAction(w, "restartFromBeginning");
        check(w.state().story == 1 && w.state().segment == TimerState::Segment::Story && w.state().target == 600000 && w.state().elapsed(t) == 0, "Restart from break resets entire sequence to story one");
        menuAction(w, "previousStory", false); check(w.state().story == 1, "Previous is disabled at story one");
        click(w, "start"); click(w, "nextStory");
        check(w.state().story == 2, "Restarted sequence advances to story two");
        for (QSize size : {QSize(220, 220), QSize(280, 600), QSize(800, 250), QSize(420, 355)}) {
            w.resize(size); app.processEvents();
            for (const char *name : {"start", "pause", "stop", "reset", "nextStory", "takeBreak"}) {
                auto *b = w.findChild<QPushButton *>(name);
                check(b && b->width() > 20 && b->height() > 10, "Controls remain reachable on resize");
                check(w.rect().contains(b->geometry()), "Controls stay within window");
            }
            check(w.findChild<QWidget *>("nextStory")->geometry().y() == w.findChild<QWidget *>("takeBreak")->geometry().y(), "Story and break remain side by side");
        }
        w.close(); check(!w.isVisible(), "Close hides pop-out");
        check(w.state().status == TimerState::Status::Running, "Hidden timer continues");
        w.showTimer(); check(w.isVisible(), "Pop-out can reopen");
        if (argc > 1) { w.resize(420, 355); app.processEvents(); w.grab().save(QString::fromLocal8Bit(argv[1])); }
        w.savePreferences(); w.shutdown();
    }
    {
        QSettings prefs(path, QSettings::IniFormat); prefs.setValue("storySeconds", 90); prefs.setValue("breakSeconds", 45);
        prefs.setValue("warningSeconds", 10); prefs.setValue("openWithOBS", false); prefs.sync();
        TimerWindow w(path, nullptr, [&t] { return t; });
        check(!w.opensWithOBS(), "Startup preference survives reopening");
        check(w.state().target == 90000 && w.state().status == TimerState::Status::Ready, "Preset reloads without resuming old session");
        w.takeBreak(); check(w.state().target == 45000, "Custom break duration reloads");
        w.nextStory(); t += 80000; w.refresh(); check(w.state().warning(t, 10), "Custom warning activates at boundary");
        t += 10001; w.refresh(); check(w.state().overtime(t), "UI refresh handles expiry");
    }
    std::cout << "UI control, resizing, persistence and warning checks passed\n";
}
