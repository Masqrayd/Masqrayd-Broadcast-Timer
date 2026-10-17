#include "broadcast-stack.hpp"
#include "native-camera-surface.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDynamicPropertyChangeEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

namespace {
const char *keys[] = {"camera", "controller", "timer", "collections"};
const char *names[] = {"Camera", "Scene Controller", "Timer", "Scene Collections"};
bool resizeCursor(Qt::CursorShape shape) {
    return shape == Qt::SizeHorCursor || shape == Qt::SizeVerCursor ||
           shape == Qt::SizeFDiagCursor || shape == Qt::SizeBDiagCursor ||
           shape == Qt::SplitHCursor || shape == Qt::SplitVCursor || shape == Qt::SizeAllCursor;
}
void resetEmbeddedCursors(QWidget *panel) {
    // A floating panel may still have its last edge-hover cursor when adopted.
    // Give the panel an explicit default so it also blocks the stack's inherited
    // edge cursor; retain meaningful non-resize cursors on other controls.
    panel->setCursor(Qt::ArrowCursor);
    for (auto *child : panel->findChildren<QWidget *>()) {
        if (child->window() != panel->window()) continue;
        if (qobject_cast<QAbstractButton *>(child)) child->setCursor(Qt::PointingHandCursor);
        else if (resizeCursor(child->cursor().shape())) child->unsetCursor();
    }
}
}
BroadcastStack::BroadcastStack(const QString &file, Providers providers)
    : QDialog(nullptr), prefs_(file, QSettings::IniFormat), providers_(std::move(providers)) {
    setObjectName("masqraydBroadcastStack"); setWindowTitle("Masqrayd Broadcast Stack");
    topmost_ = prefs_.value("topmost", true).toBool(); borderless_ = prefs_.value("borderless", true).toBool();
    openWithOBS_ = prefs_.value("openWithOBS", false).toBool();
    startupGeometry_ = prefs_.value("geometry").toByteArray();
    startupRect_ = prefs_.value("windowRect").toRect();
    startupSplitter_ = prefs_.value("splitter").toByteArray();
    startupPending_ = !startupGeometry_.isEmpty() || startupRect_.isValid();
    setMinimumSize(240, 150); resize(350, 750);
    setStyleSheet("QDialog#masqraydBroadcastStack { background:#1b1b23; border:1px solid #555562; }"
                  "QWidget[stackHeader=\"true\"] { background:#30303b; color:#eeeef4; }"
                  "QSplitter::handle { background:#484854; } QSplitter::handle:hover { background:#d6af52; }"
                  "QToolButton { color:#eeeef4; background:#3d3d4b; border:0; padding:2px 5px; border-radius:3px; }"
                  "QLabel[stackPlaceholder=\"true\"] { color:#c7c7d0; padding:10px; }");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(4);
    bar_ = new QWidget(this); bar_->setProperty("stackHeader", true);
    auto *barLayout = new QHBoxLayout(bar_); barLayout->setContentsMargins(5, 2, 3, 2);
    title_ = new QLabel("Broadcast Stack", bar_); title_->setStyleSheet("color:#eeeef4; font-weight:bold;");
    auto *menu = new QToolButton(bar_); menu->setText("Panels"); menu->setObjectName("stackPanelsMenu");
    menu->setCursor(Qt::PointingHandCursor);
    connect(menu, &QToolButton::clicked, this, [this, menu] { options(menu->mapToGlobal(QPoint(0, menu->height()))); });
    barLayout->addWidget(title_, 1); barLayout->addWidget(menu); layout->addWidget(bar_);
    splitter_ = new QSplitter(Qt::Vertical, this); splitter_->setObjectName("stackSplitter");
    splitter_->setChildrenCollapsible(false); splitter_->setHandleWidth(5); layout->addWidget(splitter_, 1);
    camera_ = nullptr;
    for (int i = 0; i < PanelCount; ++i) {
        auto panel = static_cast<Panel>(i); auto &slot = slots_[i];
        slot.wanted = prefs_.value(QString("include/%1").arg(keys[i]), false).toBool();
        slot.frame = new QWidget(splitter_); slot.frame->setObjectName(QString("stackSlot_%1").arg(keys[i]));
        slot.layout = new QVBoxLayout(slot.frame); slot.layout->setContentsMargins(0, 0, 0, 0); slot.layout->setSpacing(0);
        auto *header = new QWidget(slot.frame); header->setProperty("stackHeader", true);
        auto *headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(4, 1, 3, 1); headerLayout->setSpacing(3);
        auto *label = new QLabel(names[i], header); label->setStyleSheet("color:#c7c7d0; font-size:11px;");
        auto *detachButton = new QToolButton(header); detachButton->setText("Detach"); detachButton->setObjectName(QString("detach_%1").arg(keys[i]));
        detachButton->setCursor(Qt::PointingHandCursor);
        detachButton->setToolTip("Return this panel to its own window");
        connect(detachButton, &QToolButton::clicked, this, [this, panel] { setIncluded(panel, false); });
        headerLayout->addWidget(label, 1); headerLayout->addWidget(detachButton); slot.layout->addWidget(header);
        slot.placeholder = new QLabel(QString("Open %1 in OBS, then it will connect here.\nUse Panels to remove this section.").arg(names[i]), slot.frame);
        slot.placeholder->setWordWrap(true); slot.placeholder->setAlignment(Qt::AlignCenter); slot.placeholder->setProperty("stackPlaceholder", true);
        slot.placeholder->setMinimumSize(160, 1);
        slot.placeholder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); slot.layout->addWidget(slot.placeholder, 1);
        if (panel == Camera) { camera_ = new NativeCameraSurface(slot.frame); camera_->hide(); slot.layout->addWidget(camera_, 1); }
        splitter_->addWidget(slot.frame); slot.frame->setVisible(slot.wanted);
        for (auto *target : {header, static_cast<QWidget *>(label)}) { target->setMouseTracking(true); target->installEventFilter(this); }
    }
    empty_ = new QLabel("Choose panels from the Panels menu.\nOnly the panels you choose will be grouped.", this);
    empty_->setAlignment(Qt::AlignCenter); empty_->setWordWrap(true); empty_->setProperty("stackPlaceholder", true); layout->addWidget(empty_, 1);
    applyWindowFlags(); restoreGeometry(startupGeometry_);
    if (startupRect_.isValid()) setGeometry(startupRect_);
    QStringList order = prefs_.value("order").toStringList();
    // Append newly introduced panels after saved panels during upgrades.
    for (const char *key : keys) if (!order.contains(key)) order << key;
    for (const QString &key : order) for (int i = 0; i < PanelCount; ++i) if (key == keys[i]) splitter_->insertWidget(splitter_->count(), slots_[i].frame);
    const bool horizontal = prefs_.value("horizontal", false).toBool(); splitter_->setOrientation(horizontal ? Qt::Horizontal : Qt::Vertical);
    splitter_->restoreState(startupSplitter_);
    connect(splitter_, &QSplitter::splitterMoved, this, [this] { cancelStartupRestore(); savePreferences(); });
    saveLater_.setSingleShot(true); saveLater_.setInterval(150);
    connect(&saveLater_, &QTimer::timeout, this, &BroadcastStack::savePreferences);
    restoreLater_.setSingleShot(true); restoreLater_.setInterval(0);
    connect(&restoreLater_, &QTimer::timeout, this, &BroadcastStack::restoreStartupLayout);
    for (auto *target : {static_cast<QWidget *>(this), bar_, static_cast<QWidget *>(title_), static_cast<QWidget *>(empty_)}) {
        target->setMouseTracking(true); target->installEventFilter(this);
    }
    connect(&poll_, &QTimer::timeout, this, &BroadcastStack::scan); poll_.start(250);
    restoring_ = false; updateEmpty();
}
BroadcastStack::~BroadcastStack() { shutdown(); }
bool BroadcastStack::opensWithOBS() const { return openWithOBS_; }
bool BroadcastStack::included(Panel panel) const { return slots_[panel].wanted; }
bool BroadcastStack::isAttached(Panel panel) const { return panel == Camera ? camera_->attached() : !slots_[panel].widget.isNull(); }
void BroadcastStack::setIncluded(Panel panel, bool included) {
    auto &slot = slots_[panel]; if (slot.wanted == included) return;
    cancelStartupRestore();
    slot.wanted = included;
    if (!included) detach(panel, true);
    slot.frame->setVisible(included); updateEmpty();
    if (included && providers_.open) providers_.open(panel);
    scan(); savePreferences();
}
void BroadcastStack::attachQt(Panel panel, QWidget *widget) {
    auto &slot = slots_[panel];
    if (!widget || widget->property("masqraydStacked").toBool() || !widget->isWindow()) return;
    QScopedValueRollback<bool> guard(changing_, true);
    slot.oldParent = widget->parentWidget(); slot.oldFlags = widget->windowFlags(); slot.oldGeometry = widget->saveGeometry();
    slot.oldRect = widget->geometry();
    slot.widget = widget;
    widget->setProperty("masqraydStacked", true);
    widget->hide(); widget->setParent(slot.frame, Qt::Widget); slot.layout->addWidget(widget, 1);
    resetEmbeddedCursors(widget);
    widget->installEventFilter(this); slot.placeholder->hide(); widget->show();
    if (startupPending_) restoreLater_.start();
}
void BroadcastStack::detach(Panel panel, bool show) {
    auto &slot = slots_[panel]; QScopedValueRollback<bool> guard(changing_, true);
    if (panel == Camera) { camera_->detach(show); camera_->hide(); }
    else if (auto *widget = slot.widget.data()) {
        widget->removeEventFilter(this); widget->hide(); slot.layout->removeWidget(widget);
        widget->setParent(slot.oldParent, slot.oldFlags); widget->restoreGeometry(slot.oldGeometry);
        // restoreGeometry can apply frame/screen corrections after a widget was
        // embedded. Restore its exact client rectangle on a still-present screen.
        for (auto *screen : QGuiApplication::screens()) {
            const QRect visible = slot.oldRect.intersected(screen->availableGeometry());
            if (visible.width() >= 100 && visible.height() >= 100) { widget->setGeometry(slot.oldRect); break; }
        }
        widget->setProperty("masqraydStacked", false); slot.widget.clear();
        if (show) { widget->show(); widget->raise(); }
    }
    slot.placeholder->show();
}
void BroadcastStack::scan() {
    if (changing_ || shuttingDown_ || !activated_) return;
    for (int i = 0; i < PanelCount; ++i) {
        auto panel = static_cast<Panel>(i); auto &slot = slots_[i];
        if (!slot.wanted) continue;
        if (panel == Camera) {
            if (camera_->attached()) camera_->synchronize();
            if (!camera_->attached() && providers_.camera) {
                camera_->detach(false);
                auto native = providers_.camera();
                if (!native && providers_.open && slot.openRetry++ % 4 == 0) {
                    providers_.open(panel); native = providers_.camera();
                }
                if (native && camera_->attach(native)) {
                    slot.placeholder->hide(); camera_->show(); slot.layout->activate(); camera_->synchronize();
                    if (startupPending_) restoreLater_.start();
                }
                else { camera_->hide(); slot.placeholder->show(); }
            }
        } else if (!slot.widget && providers_.qtPanel) {
            auto *widget = providers_.qtPanel(panel);
            if (!widget && providers_.open && slot.openRetry++ % 4 == 0) {
                providers_.open(panel); widget = providers_.qtPanel(panel);
            }
            if (widget) attachQt(panel, widget);
            else slot.placeholder->show();
        }
    }
}
void BroadcastStack::showStack() {
    if (shuttingDown_) return;
    activated_ = true; ensureOnScreen(); showNormal();
    if (providers_.open) for (int i = 0; i < PanelCount; ++i)
        if (slots_[i].wanted && !isAttached(static_cast<Panel>(i))) providers_.open(static_cast<Panel>(i));
    scan();
    if (startupPending_) restoreStartupLayout();
    raise(); activateWindow();
}
void BroadcastStack::setHorizontal(bool horizontal) {
    cancelStartupRestore();
    splitter_->setOrientation(horizontal ? Qt::Horizontal : Qt::Vertical);
    splitter_->setSizes({250, 230, 250, 180}); savePreferences();
}
void BroadcastStack::movePanel(Panel panel, int offset) {
    cancelStartupRestore();
    const int index = splitter_->indexOf(slots_[panel].frame);
    splitter_->insertWidget(std::clamp(index + offset, 0, PanelCount - 1), slots_[panel].frame); savePreferences();
}
void BroadcastStack::updateEmpty() {
    const bool any = std::any_of(slots_.begin(), slots_.end(), [](const Slot &s) { return s.wanted; });
    splitter_->setVisible(any); empty_->setVisible(!any);
}
void BroadcastStack::savePreferences() {
    if (restoring_ || shuttingDown_) return;
    for (int i = 0; i < PanelCount; ++i) prefs_.setValue(QString("include/%1").arg(keys[i]), slots_[i].wanted);
    QStringList order;
    for (int n = 0; n < PanelCount; ++n) for (int i = 0; i < PanelCount; ++i) if (splitter_->widget(n) == slots_[i].frame) order << keys[i];
    prefs_.setValue("order", order);
    // Preserve the last complete layout while a saved panel is still loading.
    if (!startupPending_) {
        prefs_.setValue("splitter", splitter_->saveState());
        prefs_.setValue("geometry", saveGeometry());
        prefs_.setValue("windowRect", isMinimized() && normalGeometry().isValid() ? normalGeometry() : geometry());
    }
    prefs_.setValue("horizontal", splitter_->orientation() == Qt::Horizontal);
    prefs_.setValue("topmost", topmost_);
    prefs_.setValue("borderless", borderless_); prefs_.setValue("openWithOBS", openWithOBS_); prefs_.sync();
}
void BroadcastStack::shutdown() {
    if (shuttingDown_) return;
    saveLater_.stop(); restoreLater_.stop(); poll_.stop();
    savePreferences(); shuttingDown_ = true;
    for (int i = 0; i < PanelCount; ++i) detach(static_cast<Panel>(i), false);
    hide();
}
void BroadcastStack::cancelStartupRestore() {
    if (applyingLayout_) return;
    startupPending_ = false; restoreLater_.stop();
}
void BroadcastStack::restoreStartupLayout() {
    if (!startupPending_ || shuttingDown_ || !activated_) return;
    QScopedValueRollback<bool> guard(applyingLayout_, true);
    if (startupRect_.isValid()) setGeometry(startupRect_);
    else if (!startupGeometry_.isEmpty()) restoreGeometry(startupGeometry_);
    ensureOnScreen(); layout()->activate();
    if (!startupSplitter_.isEmpty()) splitter_->restoreState(startupSplitter_);
    bool complete = true;
    for (int i = 0; i < PanelCount; ++i)
        if (slots_[i].wanted && !isAttached(static_cast<Panel>(i))) complete = false;
    if (complete) { startupPending_ = false; saveLater_.start(); }
    camera_->synchronize();
}
void BroadcastStack::moveEvent(QMoveEvent *event) {
    QDialog::moveEvent(event);
    if (restoring_ || applyingLayout_ || shuttingDown_ || !activated_) return;
    if (event->spontaneous()) cancelStartupRestore();
    saveLater_.start();
}
void BroadcastStack::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (restoring_ || applyingLayout_ || shuttingDown_ || !activated_) return;
    if (event->spontaneous()) cancelStartupRestore();
    saveLater_.start();
}
void BroadcastStack::applyWindowFlags() {
    // Qt may recreate native ancestors when window flags change. Remove the
    // foreign camera first so recreating the frame cannot destroy its HWND.
    const bool hadCamera = camera_ && camera_->attached();
    if (hadCamera) camera_->detach(false);
    const bool visible = isVisible(); const QRect r = geometry();
    auto flags = Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint;
    if (topmost_) flags |= Qt::WindowStaysOnTopHint;
    if (borderless_) flags |= Qt::FramelessWindowHint;
    setWindowFlags(flags); setGeometry(r); if (visible) show();
    if (hadCamera) scan();
}
void BroadcastStack::ensureOnScreen() {
    for (auto *s : QGuiApplication::screens()) {
        const QRect visible = geometry().intersected(s->availableGeometry());
        if (visible.width() >= 100 && visible.height() >= 100) return;
    }
    if (auto *s = QGuiApplication::primaryScreen()) { resize(size().boundedTo(s->availableGeometry().size())); move(s->availableGeometry().topLeft() + QPoint(10, 10)); }
}
void BroadcastStack::closeEvent(QCloseEvent *event) { savePreferences(); hide(); event->ignore(); }
void BroadcastStack::contextMenuEvent(QContextMenuEvent *event) { options(event->globalPos()); }
void BroadcastStack::options(const QPoint &position) {
    QMenu menu(this); menu.setObjectName("broadcastStackMenu");
    for (int i = 0; i < PanelCount; ++i) {
        auto panel = static_cast<Panel>(i); auto *action = menu.addAction(QString("Include %1").arg(names[i]));
        action->setObjectName(QString("include_%1").arg(keys[i])); action->setCheckable(true); action->setChecked(included(panel));
        connect(action, &QAction::triggered, this, [this, panel](bool checked) { setIncluded(panel, checked); });
    }
    menu.addSeparator();
    auto *vertical = menu.addAction("Stack vertically"); vertical->setCheckable(true); vertical->setChecked(splitter_->orientation() == Qt::Vertical);
    connect(vertical, &QAction::triggered, this, [this] { setHorizontal(false); });
    auto *horizontal = menu.addAction("Arrange side by side"); horizontal->setCheckable(true); horizontal->setChecked(splitter_->orientation() == Qt::Horizontal);
    connect(horizontal, &QAction::triggered, this, [this] { setHorizontal(true); });
    auto *order = menu.addMenu("Panel order");
    for (int i = 0; i < PanelCount; ++i) {
        auto panel = static_cast<Panel>(i);
        order->addAction(QString("Move %1 earlier").arg(names[i]), this, [this, panel] { movePanel(panel, -1); });
        order->addAction(QString("Move %1 later").arg(names[i]), this, [this, panel] { movePanel(panel, 1); });
    }
    menu.addSeparator();
    auto *top = menu.addAction("Always on top"); top->setCheckable(true); top->setChecked(topmost_);
    top->setObjectName("stackAlwaysOnTop");
    connect(top, &QAction::triggered, this, [this](bool checked) { topmost_ = checked; applyWindowFlags(); savePreferences(); });
    auto *border = menu.addAction("Borderless window"); border->setCheckable(true); border->setChecked(borderless_);
    border->setObjectName("stackBorderless");
    connect(border, &QAction::triggered, this, [this](bool checked) { borderless_ = checked; applyWindowFlags(); savePreferences(); });
    auto *open = menu.addAction("Open stack with OBS"); open->setCheckable(true); open->setChecked(openWithOBS_);
    open->setObjectName("stackOpenWithOBS");
    connect(open, &QAction::triggered, this, [this](bool checked) { openWithOBS_ = checked; savePreferences(); });
    menu.addSeparator();
    menu.addAction("Detach all panels", this, [this] { for (int i = 0; i < PanelCount; ++i) setIncluded(static_cast<Panel>(i), false); });
    menu.addAction("Hide stack (panels keep running)", this, &QWidget::hide);
    menu.exec(position);
}
bool BroadcastStack::eventFilter(QObject *object, QEvent *event) {
    if (!changing_ && !shuttingDown_) for (int i = 1; i < PanelCount; ++i) {
        if (object != slots_[i].widget.data()) continue;
        if (event->type() == QEvent::DynamicPropertyChange &&
            static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "masqraydShuttingDown" &&
            slots_[i].widget->property("masqraydShuttingDown").toBool()) {
            // A partner can receive OBS's early shutdown notification first.
            // Snapshot the complete layout before it hides or destroys a panel.
            shutdown(); return false;
        }
        if (event->type() == QEvent::Close) {
            slots_[i].wanted = false; detach(static_cast<Panel>(i), false); slots_[i].frame->hide(); updateEmpty(); savePreferences(); return true;
        }
        // Hide can mean plugin shutdown, minimizing, or a temporary refresh.
        // Only an explicit close/detach is allowed to remove saved membership.
        return QDialog::eventFilter(object, event);
    }
    if (event->type() == QEvent::Leave && !dragging_ && !edges_) {
        setCursor(Qt::ArrowCursor);
        if (auto *widget = qobject_cast<QWidget *>(object)) widget->setCursor(Qt::ArrowCursor);
    }
    if (event->type() == QEvent::ContextMenu) { options(static_cast<QContextMenuEvent *>(event)->globalPos()); return true; }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseMove)
        return QDialog::eventFilter(object, event);
    auto *mouse = static_cast<QMouseEvent *>(event); const QPoint global = mouse->globalPosition().toPoint(); const QPoint local = mapFromGlobal(global);
    Qt::Edges edges;
    if (borderless_) {
        if (local.x() < 6) edges |= Qt::LeftEdge; else if (local.x() >= width() - 6) edges |= Qt::RightEdge;
        if (local.y() < 6) edges |= Qt::TopEdge; else if (local.y() >= height() - 6) edges |= Qt::BottomEdge;
    }
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        cancelStartupRestore();
        dragging_ = !edges; edges_ = edges; mouseStart_ = global; startRect_ = geometry(); grabMouse(); return true;
    }
    if (event->type() == QEvent::MouseMove && (dragging_ || edges_)) {
        const QPoint delta = global - mouseStart_; QRect r = startRect_;
        if (dragging_) r.moveTopLeft(startRect_.topLeft() + delta);
        else {
            const QSize min = minimumSizeHint().expandedTo(minimumSize());
            if (edges_.testFlag(Qt::LeftEdge)) r.setLeft(std::min(startRect_.left() + delta.x(), r.right() - min.width() + 1));
            if (edges_.testFlag(Qt::RightEdge)) r.setRight(std::max(startRect_.right() + delta.x(), r.left() + min.width() - 1));
            if (edges_.testFlag(Qt::TopEdge)) r.setTop(std::min(startRect_.top() + delta.y(), r.bottom() - min.height() + 1));
            if (edges_.testFlag(Qt::BottomEdge)) r.setBottom(std::max(startRect_.bottom() + delta.y(), r.top() + min.height() - 1));
        }
        setGeometry(r); return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && (dragging_ || edges_)) {
        dragging_ = false; edges_ = {}; releaseMouse(); setCursor(Qt::ArrowCursor); savePreferences(); return true;
    }
    if (event->type() == QEvent::MouseMove) {
        const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        auto cursor = horizontal && vertical ? ((edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge)) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor) : horizontal ? Qt::SizeHorCursor : vertical ? Qt::SizeVerCursor : Qt::ArrowCursor;
        if (auto *widget = qobject_cast<QWidget *>(object)) widget->setCursor(cursor);
    }
    return QDialog::eventFilter(object, event);
}
