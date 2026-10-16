#include "collection-window.hpp"
#include "broadcast-stack.hpp"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QSplitter>
#include <cstdlib>
#include <iostream>
static int checks;
static void check(bool value, const char *message) { ++checks; if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); } }
static void settle() { for (int i=0; i<4; ++i) QApplication::processEvents(); }
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir;
    CollectionWindow::Snapshot state;
    state.collections = {"Celebrity Masqrayd", "Criminal Masqrayd", "Masqrayd-Mayhem", "Political Masqrayd", "SusanaMaria", "Untitled"};
    state.current = "Political Masqrayd"; state.profile = "Celebrity Masqrayd";
    int switches = 0; bool refuse = false;
    CollectionWindow::Backend backend;
    backend.read = [&] { return state; };
    backend.activate = [&](const QString &name) -> QString { ++switches; if (!refuse) state.current = name; return {}; };
    const QString path = dir.filePath("collections.ini");
    QWidget owner;
    {
        CollectionWindow panel(path, backend, &owner); panel.showCollections(); settle();
        check(panel.shownCollections() == QStringList({"Celebrity Masqrayd", "Criminal Masqrayd", "Political Masqrayd", "Masqrayd-Mayhem"}), "Four channel collections selected by default; unrelated collections omitted");
        auto button = [&](int i) { return panel.findChild<QPushButton *>(QString("collection_%1").arg(i)); };
        check(button(2)->isChecked() && !button(0)->isChecked(), "Active highlight reflects OBS collection");
        button(2)->click(); settle(); check(switches == 0 && button(2)->isChecked(), "Current collection is a no-op and stays highlighted");
        button(0)->click(); check(switches == 0, "Collection load is deferred beyond button event");
        check(!button(1)->isEnabled(), "Repeated clicks disabled while switching");
        settle(); check(switches == 1 && state.current == "Celebrity Masqrayd" && button(0)->isChecked(), "Exact collection name switches and updates active highlight");
        check(state.profile == "Celebrity Masqrayd", "Profile remains unchanged");
        state.current = "Criminal Masqrayd"; panel.refresh(); check(button(1)->isChecked() && !button(0)->isChecked(), "External OBS menu changes synchronize highlight");
        state.changing = true; panel.refresh(); check(!button(0)->isEnabled(), "External collection change disables controls");
        state.changing = false; state.collections.removeAll("Political Masqrayd"); panel.refresh();
        check(!button(2)->isEnabled() && button(2)->toolTip().contains("renamed"), "Removed or renamed collection is unavailable without retargeting another collection");
        state.collections << "Political Masqrayd"; panel.refresh();
        refuse = true; button(2)->click(); settle();
        bool hasError = false; for (auto *label : panel.findChildren<QLabel *>()) hasError |= label->text().contains("did not switch");
        check(hasError && button(1)->isChecked() && !button(2)->isChecked(), "Refused switch reports failure and preserves actual active highlight"); refuse = false;
        // Rename/delete can race the deferred request.
        const int before = switches; button(2)->click(); state.collections.removeAll("Political Masqrayd"); settle();
        check(switches == before, "Collection existence rechecked immediately before deferred request");
        panel.setShownCollections({"Celebrity Masqrayd", "Criminal Masqrayd", "Masqrayd-Mayhem"});
        panel.setColumns(2); panel.resize(220, 160); settle();
        check(button(0)->geometry().right() < button(1)->geometry().left() && button(0)->width() < 120, "Two columns remain side by side in a narrow window");
        check(button(0)->cursor().shape() == Qt::PointingHandCursor, "Buttons use click cursor");
        const QRect standalone = panel.geometry(); const auto flags = panel.windowFlags();
        BroadcastStack::Providers providers;
        providers.qtPanel = [&](BroadcastStack::Panel p) -> QWidget * { return p == BroadcastStack::Collections ? &panel : nullptr; };
        const QString stackFile = dir.filePath("stack.ini");
        {
            BroadcastStack stack(stackFile, providers); stack.showStack(); stack.setIncluded(BroadcastStack::Collections, true); settle();
            check(stack.isAttached(BroadcastStack::Collections) && !panel.isWindow(), "Fourth panel embeds in Broadcast Stack");
            check(stack.findChild<QSplitter *>("stackSplitter")->count() == 4, "Stack supports four independently selectable panels");
            panel.showCollections(); check(!panel.isWindow(), "Tools action raises containing stack without detaching panel");
            button(0)->click(); settle(); check(state.current == "Celebrity Masqrayd", "Switching works while embedded");
            panel.savePreferences(); stack.setIncluded(BroadcastStack::Collections, false); settle();
            check(panel.isWindow() && panel.geometry() == standalone && panel.windowFlags() == flags, "Detach restores standalone geometry and flags");
            stack.setIncluded(BroadcastStack::Collections, true); settle(); stack.shutdown(); settle();
            check(panel.isWindow() && panel.parentWidget() == &owner, "Shutdown returns collection ownership to plugin");
        }
        {
            BroadcastStack restored(stackFile, providers); check(restored.included(BroadcastStack::Collections), "Fourth panel membership persists");
            restored.showStack(); settle(); check(restored.isAttached(BroadcastStack::Collections), "Saved stack reconnects collection panel");
        }
        panel.savePreferences(); panel.shutdown();
    }
    {
        CollectionWindow restored(path, backend, &owner);
        check(restored.shownCollections() == QStringList({"Celebrity Masqrayd", "Criminal Masqrayd", "Masqrayd-Mayhem"}), "Collection selection persists across restart");
        restored.setShownCollections({}); restored.savePreferences();
    }
    {
        CollectionWindow empty(path, backend, &owner); check(empty.shownCollections().isEmpty(), "Intentionally empty selection is preserved");
    }
    {
        const QString migrationFile = dir.filePath("old-stack.ini");
        QSettings old(migrationFile, QSettings::IniFormat);
        old.setValue("order", QStringList({"controller", "timer", "camera"})); old.sync();
        BroadcastStack migrated(migrationFile, {});
        auto *splitter = migrated.findChild<QSplitter *>("stackSplitter");
        check(splitter->widget(0)->objectName() == "stackSlot_controller" && splitter->widget(2)->objectName() == "stackSlot_camera" && splitter->widget(3)->objectName() == "stackSlot_collections", "Upgrade preserves saved panel order and appends collection panel");
    }
    if (argc > 1) {
        state.collections << "Political Masqrayd"; state.current = "Political Masqrayd";
        CollectionWindow preview(dir.filePath("preview.ini"), backend); preview.resize(460, 270); preview.showCollections(); settle(); preview.grab().save(QString::fromLocal8Bit(argv[1]));
    }
    std::cout << checks << " scene collection checks passed\n";
}
