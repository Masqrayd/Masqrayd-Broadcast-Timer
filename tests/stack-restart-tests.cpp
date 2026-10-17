#include "broadcast-stack.hpp"
#include "collection-window.hpp"
#include "timer-window.hpp"
#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QPushButton>
#include <cstdlib>
#include <iostream>
#include <memory>

static void check(bool ok, const char *message) {
    if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static void settle() { for (int i = 0; i < 10; ++i) QApplication::processEvents(); }
static void nearSizes(const QList<int> &actual, const QList<int> &expected) {
    check(actual.size() == expected.size(), "same number of saved sections restored");
    for (int i = 0; i < actual.size(); ++i)
        check(std::abs(actual[i] - expected[i]) <= 2, "each section restores its previous size");
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir;
    CollectionWindow::Snapshot snapshot{{"Celebrity Masqrayd", "Criminal Masqrayd"}, "Celebrity Masqrayd", "Main", false};
    CollectionWindow::Backend backend{[&] { return snapshot; }, [](const QString &) { return QString(); }};
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    for (bool horizontal : {false, true}) {
        const QString file = dir.filePath(horizontal ? "horizontal.ini" : "vertical.ini");
        { QSettings prefs(file, QSettings::IniFormat); prefs.setValue("openWithOBS", true); prefs.sync(); }
        QRect expectedGeometry; QList<int> expectedSizes; QStringList expectedOrder;
        QByteArray savedSplitter;
        for (int cycle = 0; cycle < 4; ++cycle) {
            QWidget owner;
            auto timer = std::make_unique<TimerWindow>(dir.filePath("timer.ini"), &owner);
            auto controller = std::make_unique<QDialog>(&owner, Qt::Window | Qt::FramelessWindowHint);
            controller->setMinimumSize(160, 110);
            auto *controllerLayout = new QVBoxLayout(controller.get());
            controllerLayout->addWidget(new QPushButton("Scene control", controller.get()));
            auto collections = std::make_unique<CollectionWindow>(dir.filePath("collections.ini"), backend, &owner);
            bool ready = cycle != 1 && cycle != 3;
            int openAttempts = 0;
            BroadcastStack::Providers providers;
            providers.qtPanel = [&](BroadcastStack::Panel p) -> QWidget * {
                if (p == BroadcastStack::Timer) return timer.get();
                if (p == BroadcastStack::Controller) return controller.get();
                if (p == BroadcastStack::Collections && ready) return collections.get();
                return nullptr;
            };
            providers.open = [&](BroadcastStack::Panel p) { if (p == BroadcastStack::Collections) ++openAttempts; };
            auto stack = std::make_unique<BroadcastStack>(file, providers);
            check(stack->opensWithOBS(), "open-with-OBS choice survives every restart");
            stack->showStack(); settle();
            auto *splitter = stack->findChild<QSplitter *>("stackSplitter");
            if (cycle == 0) {
                for (int i = 0; i < BroadcastStack::PanelCount; ++i) stack->setIncluded(static_cast<BroadcastStack::Panel>(i), true);
                stack->setHorizontal(horizontal); settle();
                // Simulate the user's chosen panel order, outer dimensions and position.
                splitter->insertWidget(0, stack->findChild<QWidget *>("stackSlot_collections"));
                const QSize requested(horizontal ? std::min(1000, screen.width() - 40) : 340,
                                      horizontal ? 300 : std::min(680, screen.height() - 70));
                stack->resize(requested); stack->move(screen.topLeft() + QPoint(20, 25)); settle();
                splitter->setSizes(horizontal ? QList<int>{180, 250, 280, 260} : QList<int>{110, 160, 240, 150}); settle();
                // A temporary hide must never act like clicking Detach.
                collections->hide(); settle();
                check(stack->included(BroadcastStack::Collections), "temporary hide never clears collection membership");
                collections->show(); settle();
                expectedGeometry = stack->geometry(); expectedSizes = splitter->sizes();
                for (int i = 0; i < splitter->count(); ++i) expectedOrder << splitter->widget(i)->objectName();
                stack->savePreferences();
                QSettings prefs(file, QSettings::IniFormat); savedSplitter = prefs.value("splitter").toByteArray();
                stack->showMinimized(); settle(); stack->savePreferences();
                { QSettings minimized(file, QSettings::IniFormat);
                  check(minimized.value("windowRect").toRect() == expectedGeometry, "minimizing never replaces the last normal placement"); }
                stack->showNormal(); settle();
            } else {
                for (int i = 0; i < BroadcastStack::PanelCount; ++i)
                    check(stack->included(static_cast<BroadcastStack::Panel>(i)), "all chosen panel memberships persist");
                if (!ready) {
                    check(!stack->isAttached(BroadcastStack::Collections) && openAttempts > 0,
                          "late-loading collection stays selected and is requested automatically");
                    for (int i = 0; i < 5; ++i) stack->scan();
                    check(openAttempts >= 2, "startup retries discovery until the partner is available");
                    stack->savePreferences();
                    QSettings pending(file, QSettings::IniFormat);
                    check(pending.value("splitter").toByteArray() == savedSplitter,
                          "incomplete startup cannot overwrite the last complete splitter layout");
                    ready = true; stack->scan(); settle();
                }
                check(stack->isAttached(BroadcastStack::Collections) && !collections->isWindow(),
                      "collection attaches automatically once its plugin is ready");
                check(stack->geometry() == expectedGeometry, "window restores exact outer size and placement");
                check(splitter->orientation() == (horizontal ? Qt::Horizontal : Qt::Vertical), "layout orientation survives restart");
                nearSizes(splitter->sizes(), expectedSizes);
                QStringList order;
                for (int i = 0; i < splitter->count(); ++i) order << splitter->widget(i)->objectName();
                check(order == expectedOrder, "chosen panel order survives restart");
            }
            // OBS can notify the collection plugin before it notifies the stack.
            // Process queued events in between, reproducing the original bug.
            if (cycle % 2 == 0) collections->shutdown(); else timer->shutdown();
            settle();
            check(stack->included(BroadcastStack::Collections), "partner-first shutdown never forgets collection membership");
            QSettings after(file, QSettings::IniFormat);
            check(after.value("include/collections").toBool(), "shutdown keeps collection attached in saved configuration");
            check(after.value("windowRect").toRect() == expectedGeometry, "shutdown snapshots placement before detaching panels");
            check(after.value("splitter").toByteArray() == savedSplitter, "shutdown saves full section dimensions before teardown");
            // Later close/save/destructor callbacks must not replace the frozen layout.
            stack->resize(700, 400); stack->savePreferences(); stack->shutdown();
            timer->shutdown(); collections->shutdown(); settle();
            stack.reset(); collections.reset(); controller.reset(); timer.reset();
            QSettings final(file, QSettings::IniFormat);
            check(final.value("windowRect").toRect() == expectedGeometry && final.value("splitter").toByteArray() == savedSplitter,
                  "late shutdown and destruction cannot overwrite the frozen layout");
        }
    }
    // Deliberate detachment remains persistent; shutdown must not re-add it.
    const QString explicitFile = dir.filePath("explicit.ini");
    {
        CollectionWindow collection(dir.filePath("explicit-collection.ini"), backend);
        BroadcastStack::Providers providers;
        providers.qtPanel = [&](BroadcastStack::Panel) { return &collection; };
        BroadcastStack stack(explicitFile, providers); stack.showStack(); stack.setIncluded(BroadcastStack::Collections, true);
        settle(); stack.setIncluded(BroadcastStack::Collections, false); stack.shutdown();
    }
    { BroadcastStack restored(explicitFile, {}); check(!restored.included(BroadcastStack::Collections), "intentional detach survives restart"); }
    std::cout << "Repeated partner-first shutdown, delayed startup, order, dimensions and placement checks passed\n";
}
