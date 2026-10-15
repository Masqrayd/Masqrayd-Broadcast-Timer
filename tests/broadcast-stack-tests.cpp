#include "broadcast-stack.hpp"
#include "native-camera-surface.hpp"
#include "timer-window.hpp"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <iostream>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
static int checks = 0;
static void check(bool ok, const char *message) {
    ++checks; if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static void selectMenu(BroadcastStack &stack, const char *name) {
    QTimer::singleShot(0, &stack, [name] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); check(menu, "Stack menu opens");
        auto *action = menu->findChild<QAction *>(name); check(action, "Include action exists"); action->trigger(); menu->close();
    });
    const QPoint p(15, 15); QContextMenuEvent event(QContextMenuEvent::Mouse, p, stack.mapToGlobal(p)); QApplication::sendEvent(&stack, &event);
    QApplication::processEvents();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); QTemporaryDir dir; qint64 t = 1000;
    QWidget owner; owner.setWindowTitle("Test owner");
    TimerWindow timer(dir.filePath("timer.ini"), &owner, [&t] { return t; });
    timer.showTimer(); timer.nextStory();
    QPointer<QDialog> controller = new QDialog(nullptr, Qt::Window | Qt::FramelessWindowHint);
    controller->setObjectName("masqraydSceneController"); controller->setMinimumSize(220, 160);
    controller->resize(320, 220); controller->move(30, 30);
    auto *layout = new QVBoxLayout(controller);
    auto *button = new QPushButton("Transition test", controller); layout->addWidget(button);
    int transitions = 0; QObject::connect(button, &QPushButton::clicked, [&transitions] { ++transitions; });
    controller->show(); app.processEvents();
    const QRect controllerGeometry = controller->geometry(); const auto controllerFlags = controller->windowFlags();
    const QRect timerGeometry = timer.geometry(); const auto timerFlags = timer.windowFlags();
    // Reproduce adopting panels immediately after their floating resize edge
    // was hovered. Both the panel and a child can retain the resize cursor.
    controller->setCursor(Qt::SizeHorCursor); button->setCursor(Qt::SizeHorCursor);
    timer.setCursor(Qt::SizeHorCursor);
    auto *timerStart = timer.findChild<QPushButton *>("start"); check(timerStart, "Timer Start button exists");
    timerStart->setCursor(Qt::SizeHorCursor);
    auto *timerDisplay = timer.findChild<QWidget *>("timerDisplay"); check(timerDisplay, "Timer display exists");
    timerDisplay->setCursor(Qt::SizeHorCursor);
    BroadcastStack::Providers providers;
    providers.qtPanel = [&timer, &controller](BroadcastStack::Panel panel) -> QWidget * {
        return panel == BroadcastStack::Timer ? static_cast<QWidget *>(&timer) : controller.data();
    };
    providers.camera = [] { return std::uintptr_t(0); };
    QString path = dir.filePath("stack.ini");
    {
        BroadcastStack stack(path, providers); stack.scan();
        check(!stack.isAttached(BroadcastStack::Timer) && timer.isWindow(), "Stack is optional and leaves standalone timer alone");
        stack.showStack(); selectMenu(stack, "include_timer");
        check(stack.isAttached(BroadcastStack::Timer) && !timer.isWindow(), "Timer becomes an embedded panel");
        check(timer.property("masqraydStacked").toBool(), "Attachment protocol marks timer");
        stack.setIncluded(BroadcastStack::Controller, true); app.processEvents();
        check(!controller->isWindow() && stack.isAttached(BroadcastStack::Controller), "Controller and timer coexist in one window");
        check(controller->cursor().shape() == Qt::ArrowCursor && timer.cursor().shape() == Qt::ArrowCursor, "Attached panels clear stale floating resize cursors");
        check(button->cursor().shape() == Qt::PointingHandCursor && timerStart->cursor().shape() == Qt::PointingHandCursor, "Embedded scene and timer buttons regain click cursors");
        check(timerDisplay->cursor().shape() == Qt::ArrowCursor, "Embedded display clears stale child resize cursor");
        const QPoint edge(1, stack.height() / 2);
        QMouseEvent edgeHover(QEvent::MouseMove, QPointF(edge), QPointF(stack.mapToGlobal(edge)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&stack, &edgeHover);
        check(stack.cursor().shape() == Qt::SizeHorCursor, "Outer edge still offers horizontal resizing");
        check(button->cursor().shape() == Qt::PointingHandCursor && timerStart->cursor().shape() == Qt::PointingHandCursor && timerDisplay->cursor().shape() == Qt::ArrowCursor, "Stack edge cursor cannot leak into embedded controls");
        auto *detachCursor = stack.findChild<QToolButton *>("detach_timer");
        check(detachCursor && detachCursor->cursor().shape() == Qt::PointingHandCursor, "Detach button blocks inherited resize cursor");
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&stack, &leave);
        check(stack.cursor().shape() == Qt::ArrowCursor, "Leaving a resize edge clears its cursor");
        button->click(); check(transitions == 1, "Embedded control remains interactive");
        t += 2000; check(timer.state().elapsed(t) == 2000, "Timer keeps elapsed time through grouping");
        const QPoint before = timer.mapToGlobal(QPoint(0, 0)); const QPoint groupBefore = stack.pos();
        stack.move(groupBefore + QPoint(30, 20)); app.processEvents();
        check(timer.mapToGlobal(QPoint(0, 0)) - before == stack.pos() - groupBefore, "Moving group moves embedded timer by the same amount");
        stack.resize(420, 800); app.processEvents();
        check(timer.width() > 200 && timer.height() >= 220, "Resize respects panel minimums");
        stack.setHorizontal(true); app.processEvents();
        check(stack.findChild<QSplitter *>("stackSplitter")->orientation() == Qt::Horizontal, "Side-by-side layout is available");
        stack.setHorizontal(false);
        stack.setIncluded(BroadcastStack::Camera, true); stack.scan();
        check(stack.included(BroadcastStack::Camera) && !stack.isAttached(BroadcastStack::Camera), "Missing camera shows a waiting section without inventing a camera");
        stack.setIncluded(BroadcastStack::Camera, false);
        auto *detach = stack.findChild<QToolButton *>("detach_controller");
        check(detach, "Each panel has a detach button"); detach->click(); app.processEvents();
        check(controller->isWindow() && !controller->property("masqraydStacked").toBool(), "Detach restores a standalone window");
        check(controller->windowFlags() == controllerFlags && controller->geometry() == controllerGeometry, "Controller flags and geometry restored exactly");
        check(stack.isAttached(BroadcastStack::Timer), "Detaching controller leaves timer in group");
        stack.setIncluded(BroadcastStack::Controller, true); controller->close(); app.processEvents();
        check(!stack.included(BroadcastStack::Controller) && !controller->isVisible() && controller->isWindow(), "Closing one panel removes only that panel");
        stack.setIncluded(BroadcastStack::Controller, true); app.processEvents();
        stack.close(); t += 5000; app.processEvents();
        check(stack.isAttached(BroadcastStack::Timer) && timer.state().elapsed(t) == 7000, "Hiding group keeps panels attached and timer running");
        stack.showStack(); app.processEvents();
        check(timer.isVisible(), "Reopening group restores attached panels");
        timer.showTimer(); check(!timer.isWindow(), "Opening attached timer raises the group instead of breaking attachment");
        stack.savePreferences(); stack.shutdown(); app.processEvents();
        if (timer.windowFlags() != timerFlags || timer.geometry() != timerGeometry) {
            const QRect got = timer.geometry();
            std::cerr << "Timer flags expected=" << int(timerFlags) << " actual=" << int(timer.windowFlags())
                      << " rect expected=" << timerGeometry.x() << ',' << timerGeometry.y() << ',' << timerGeometry.width() << ',' << timerGeometry.height()
                      << " actual=" << got.x() << ',' << got.y() << ',' << got.width() << ',' << got.height() << '\n';
        }
        check(timer.isWindow() && timer.windowFlags() == timerFlags && timer.geometry() == timerGeometry, "Shutdown restores timer flags, parent and original geometry");
        check(timer.parentWidget() == &owner, "Original QObject ownership is restored");
        check(timer.state().elapsed(t) == 7000, "Detach never resets story timing");
    }
    {
        BroadcastStack restored(path, providers); restored.scan();
        check(restored.included(BroadcastStack::Timer) && restored.included(BroadcastStack::Controller), "Panel choices persist");
        check(!restored.isAttached(BroadcastStack::Timer), "A closed stack never steals panels on startup");
        restored.showStack(); app.processEvents();
        check(restored.isAttached(BroadcastStack::Timer), "Opening saved stack reconnects chosen panels");
        delete controller.data(); restored.scan(); app.processEvents();
        check(!restored.isAttached(BroadcastStack::Controller) && restored.isAttached(BroadcastStack::Timer), "Destroying one plugin panel leaves other panels intact");
    }
    check(timer.isWindow() && !timer.property("masqraydStacked").toBool(), "Destroying group does not destroy its plugins");
#ifdef _WIN32
    // Exercise actual HWND reparenting, pixel resize and restoration on Windows.
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"MasqraydCameraPopoutNativeV1";
    check(RegisterClassW(&wc) != 0, "Native camera test class registered");
    HWND camera = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Test camera", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  100, 100, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    check(camera != nullptr, "Native camera test window created");
    RECT original{}; GetWindowRect(camera, &original);
    const auto style = GetWindowLongPtrW(camera, GWL_STYLE); const auto ex = GetWindowLongPtrW(camera, GWL_EXSTYLE);
    {
        QWidget host; auto *hostLayout = new QVBoxLayout(&host); auto *surface = new NativeCameraSurface(&host); hostLayout->addWidget(surface);
        host.resize(400, 320); host.show(); app.processEvents();
        check(NativeCameraSurface::findCamera() == reinterpret_cast<std::uintptr_t>(camera), "Discovery matches only known current-process camera");
        check(surface->attach(reinterpret_cast<std::uintptr_t>(camera)), "Native camera attaches"); app.processEvents();
        check(GetParent(camera) == reinterpret_cast<HWND>(surface->winId()), "Camera shares the group's native parent");
        check(IsWindow(camera) && (GetWindowLongPtrW(camera, GWL_STYLE) & WS_CHILD), "Native window is retained as a child");
        host.resize(550, 400); app.processEvents();
        RECT child{}, parent{}; GetClientRect(camera, &child); GetClientRect(reinterpret_cast<HWND>(surface->winId()), &parent);
        check(child.right == parent.right && child.bottom == parent.bottom, "Native camera resizes to its pane in physical pixels");
        surface->detach(); app.processEvents();
        RECT restored{}; GetWindowRect(camera, &restored);
        check(GetParent(camera) == nullptr && GetWindowLongPtrW(camera, GWL_STYLE) == style && GetWindowLongPtrW(camera, GWL_EXSTYLE) == ex, "Native styles and parent restored");
        check(EqualRect(&original, &restored), "Native standalone camera position and size restored");
        check(surface->attach(reinterpret_cast<std::uintptr_t>(camera)), "Native camera can reattach");
        surface->detach();
        BroadcastStack::Providers nativeProviders;
        nativeProviders.camera = &NativeCameraSurface::findCamera;
        BroadcastStack group(dir.filePath("native-stack.ini"), nativeProviders);
        group.showStack(); group.setIncluded(BroadcastStack::Camera, true); app.processEvents();
        check(group.isAttached(BroadcastStack::Camera), "Broadcast Stack adopts real native camera");
        selectMenu(group, "stackAlwaysOnTop");
        check(IsWindow(camera) && group.isAttached(BroadcastStack::Camera), "Changing group topmost preserves native camera handle");
        selectMenu(group, "stackBorderless");
        check(IsWindow(camera) && group.isAttached(BroadcastStack::Camera), "Changing group border preserves native camera handle");
        group.setIncluded(BroadcastStack::Camera, false); app.processEvents();
        check(GetParent(camera) == nullptr && IsWindowVisible(camera), "Group detach returns visible native camera");
        check(surface->attach(reinterpret_cast<std::uintptr_t>(camera)), "Native camera can attach again after group detach");
        DestroyWindow(camera); surface->detach(false);
        check(!surface->attached(), "Camera destruction while attached is handled safely");
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
#endif
    std::cout << checks << " grouping, detach, ownership and layout checks passed\n";
}
