#include "collection-window.hpp"
#include "fit-text-label.hpp"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class CollectionButton : public QPushButton {
public:
    CollectionButton(const QString &name, QWidget *parent) : QPushButton(name, parent) {
        setCheckable(true); setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); setMinimumSize(24, 20);
    }
    QSize sizeHint() const override { return {1, 1}; }
    QSize minimumSizeHint() const override { return {24, 20}; }
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option; initStyleOption(&option); option.text.clear();
        QPainter p(this); style()->drawControl(QStyle::CE_PushButton, &option, &p, this);
        const QRect area = rect().adjusted(6, 3, -6, -3);
        QFont f = font(); f.setBold(true);
        for (int px = 19; px >= 6; --px) {
            f.setPixelSize(px);
            const QRect measured = QFontMetrics(f).boundingRect(area, Qt::AlignCenter | Qt::TextWordWrap, text());
            if (measured.width() <= area.width() && measured.height() <= area.height()) break;
        }
        p.setFont(f); p.setPen(!isEnabled() ? QColor("#9a9aa6") : isChecked() ? QColor("#17171e") : QColor("#f1f1f5"));
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, text());
    }
};
}
CollectionWindow::CollectionWindow(const QString &file, Backend backend, QWidget *parent)
    : QDialog(parent), prefs_(file, QSettings::IniFormat), backend_(std::move(backend)) {
    setObjectName("masqraydSceneCollections"); setWindowTitle("Masqrayd Scene Collections");
    setModal(false); setMinimumSize(160, 80); resize(360, 235);
    topmost_ = prefs_.value("topmost", true).toBool(); borderless_ = prefs_.value("borderless", true).toBool();
    openWithOBS_ = prefs_.value("openWithOBS", false).toBool();
    columns_ = std::clamp(prefs_.value("columns", 2).toInt(), 1, 4);
    initialized_ = prefs_.contains("collections"); shown_ = prefs_.value("collections").toStringList();
    setStyleSheet("QDialog#masqraydSceneCollections { background:#1b1b23; border:1px solid #555562; }"
                  "QLabel { color:#d8d8e1; background:transparent; }"
                  "QPushButton { background:#30303c; border:1px solid #555562; border-radius:5px; }"
                  "QPushButton:hover { background:#454553; } QPushButton:checked { background:#d6af52; border-color:#f4d88f; }"
                  "QPushButton:disabled { border-color:#41414b; } QPushButton:focus { border:2px solid #eeeef4; }");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(3);
    heading_ = new FitTextLabel("Scene Collections", this); heading_->setStyleSheet("font-weight:bold;");
    heading_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); layout->addWidget(heading_, 1);
    grid_ = new QGridLayout; grid_->setSpacing(3); layout->addLayout(grid_, 4);
    status_ = new FitTextLabel(this);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); layout->addWidget(status_, 1);
    for (auto *w : {static_cast<QWidget *>(this), static_cast<QWidget *>(heading_), static_cast<QWidget *>(status_)}) {
        w->setMouseTracking(true); w->installEventFilter(this);
    }
    applyWindowFlags(); restoreGeometry(prefs_.value("geometry").toByteArray());
    connect(&poll_, &QTimer::timeout, this, &CollectionWindow::refresh); poll_.start(1000); refresh();
}
CollectionWindow::~CollectionWindow() { shutdown(); }
void CollectionWindow::refresh() {
    if (shuttingDown_ || !backend_.read) return;
    snapshot_ = backend_.read();
    if (!initialized_ && !snapshot_.collections.isEmpty()) {
        const QStringList preferred{"Celebrity Masqrayd", "Criminal Masqrayd", "Political Masqrayd", "Masqrayd-Mayhem"};
        for (const auto &name : preferred) if (snapshot_.collections.contains(name)) shown_ << name;
        if (shown_.isEmpty()) shown_ = snapshot_.collections;
        initialized_ = true; rebuild();
    } else if (buttons_.size() != shown_.size()) rebuild();
    for (int i = 0; i < buttons_.size(); ++i) {
        const bool exists = snapshot_.collections.contains(shown_[i]);
        buttons_[i]->setChecked(snapshot_.current == shown_[i]);
        buttons_[i]->setEnabled(exists && !busy_ && !snapshot_.changing);
        buttons_[i]->setToolTip(exists ? QString("Load scene collection: %1").arg(shown_[i]) :
            "Collection unavailable or renamed. Right-click > Collections shown to update this button.");
    }
    heading_->setToolTip(QString("Active collection: %1\nProfile: %2").arg(snapshot_.current, snapshot_.profile));
    status_->setText(busy_ || snapshot_.changing ? "Switching collection…" : !error_.isEmpty() ? error_ :
        shown_.isEmpty() ? "Right-click > Collections shown to add buttons." : QString("Active: %1").arg(snapshot_.current));
    status_->setToolTip(QString("Profile: %1").arg(snapshot_.profile));
}
void CollectionWindow::rebuild() {
    for (auto *b : buttons_) { grid_->removeWidget(b); delete b; } buttons_.clear();
    for (int i = 0; i < shown_.size(); ++i) {
        auto *b = new CollectionButton(shown_[i], this); b->setObjectName(QString("collection_%1").arg(i));
        b->setMouseTracking(true); b->installEventFilter(this); buttons_ << b;
        const QString name = shown_[i]; connect(b, &QPushButton::clicked, this, [this, name] { activate(name); });
        grid_->addWidget(b, i / columns_, i % columns_);
    }
}
void CollectionWindow::setShownCollections(const QStringList &names) {
    shown_ = names; shown_.removeDuplicates(); initialized_ = true; error_.clear(); rebuild(); refresh(); savePreferences();
}
void CollectionWindow::setColumns(int columns) {
    columns_ = std::clamp(columns, 1, 4); rebuild(); refresh(); savePreferences();
}
void CollectionWindow::activate(const QString &name) {
    // Refresh before acting; a collection may have been renamed by OBS's menu.
    refresh();
    if (busy_ || snapshot_.changing || shuttingDown_) return;
    if (!snapshot_.collections.contains(name)) { error_ = "Collection unavailable. Update Collections shown."; refresh(); return; }
    if (snapshot_.current == name) return;
    busy_ = true; error_.clear(); refresh();
    // Return from the button event before OBS unloads scenes and pumps Qt events.
    QTimer::singleShot(0, this, [this, name] {
        if (shuttingDown_) return;
        snapshot_ = backend_.read();
        if (!snapshot_.collections.contains(name)) error_ = "Collection unavailable. Update Collections shown.";
        else if (snapshot_.changing) error_ = "OBS is switching collections. Try again when it finishes.";
        else if (snapshot_.current != name) {
            error_ = backend_.activate ? backend_.activate(name) : "OBS connection unavailable.";
            if (shuttingDown_) return;
            snapshot_ = backend_.read();
            if (error_.isEmpty() && snapshot_.current != name) error_ = "OBS did not switch collections. Check OBS for a dialog.";
        }
        busy_ = false; refresh();
    });
}
void CollectionWindow::savePreferences() {
    if (initialized_) prefs_.setValue("collections", shown_);
    prefs_.setValue("columns", columns_); prefs_.setValue("topmost", topmost_);
    prefs_.setValue("borderless", borderless_); prefs_.setValue("openWithOBS", openWithOBS_);
    if (!property("masqraydStacked").toBool()) prefs_.setValue("geometry", saveGeometry());
    prefs_.sync();
}
void CollectionWindow::applyWindowFlags() {
    if (property("masqraydStacked").toBool()) return;
    const bool visible = isVisible(); const QRect old = geometry();
    Qt::WindowFlags flags = Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint;
    if (topmost_) flags |= Qt::WindowStaysOnTopHint;
    if (borderless_) flags |= Qt::FramelessWindowHint;
    setWindowFlags(flags); setGeometry(old); if (visible) show();
}
void CollectionWindow::showCollections() {
    refresh();
    if (property("masqraydStacked").toBool()) { window()->showNormal(); window()->raise(); window()->activateWindow(); return; }
    bool onScreen = false;
    for (auto *s : QGuiApplication::screens()) {
        const QRect visible = geometry().intersected(s->availableGeometry());
        if (visible.width() >= 100 && visible.height() >= 100) onScreen = true;
    }
    if (!onScreen) if (auto *s = QGuiApplication::primaryScreen()) { resize(size().boundedTo(s->availableGeometry().size())); move(s->availableGeometry().center() - rect().center()); }
    showNormal(); raise(); activateWindow();
}
void CollectionWindow::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    setProperty("masqraydShuttingDown", true);
    poll_.stop(); savePreferences(); hide();
}
void CollectionWindow::closeEvent(QCloseEvent *event) { savePreferences(); event->ignore(); hide(); }
void CollectionWindow::reject() { close(); }
void CollectionWindow::contextMenuEvent(QContextMenuEvent *event) {
    refresh(); QMenu menu(this);
    auto *collections = menu.addMenu("Collections shown");
    QStringList names = snapshot_.collections;
    for (const auto &name : shown_) if (!names.contains(name)) names << name;
    for (const auto &name : names) {
        auto *a = collections->addAction(name + (snapshot_.collections.contains(name) ? "" : " (unavailable)"));
        a->setCheckable(true); a->setChecked(shown_.contains(name));
        connect(a, &QAction::triggered, this, [this, name](bool checked) {
            auto names = shown_; if (checked) names << name; else names.removeAll(name); setShownCollections(names);
        });
    }
    auto *columns = menu.addMenu("Button columns");
    for (int n : {1, 2, 4}) {
        auto *a = columns->addAction(QString::number(n)); a->setCheckable(true); a->setChecked(columns_ == n);
        connect(a, &QAction::triggered, this, [this, n] { setColumns(n); });
    }
    menu.addSeparator(); const bool floating = !property("masqraydStacked").toBool();
    auto *top = menu.addAction("Always on top"); top->setCheckable(true); top->setChecked(topmost_); top->setEnabled(floating);
    auto *border = menu.addAction("Borderless window"); border->setCheckable(true); border->setChecked(borderless_); border->setEnabled(floating);
    auto *open = menu.addAction("Open with OBS"); open->setCheckable(true); open->setChecked(openWithOBS_);
    connect(top, &QAction::triggered, this, [this](bool v) { topmost_ = v; applyWindowFlags(); savePreferences(); });
    connect(border, &QAction::triggered, this, [this](bool v) { borderless_ = v; applyWindowFlags(); savePreferences(); });
    connect(open, &QAction::triggered, this, [this](bool v) { openWithOBS_ = v; savePreferences(); });
    menu.addAction("Refresh collections", this, [this] { error_.clear(); refresh(); });
    menu.addAction("Hide collections", this, &QWidget::close);
    menu.exec(event->globalPos());
}

bool CollectionWindow::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget) return QDialog::eventFilter(object, event);
    if (event->type() == QEvent::ContextMenu && widget != this) {
        contextMenuEvent(static_cast<QContextMenuEvent *>(event)); return true;
    }
    if (property("masqraydStacked").toBool()) return QDialog::eventFilter(object, event);
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseMove)
        return QDialog::eventFilter(object, event);
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint global = mouse->globalPosition().toPoint(); const QPoint local = mapFromGlobal(global);
    Qt::Edges edges;
    if (borderless_) {
        if (local.x() < 8) edges |= Qt::LeftEdge; else if (local.x() >= width() - 8) edges |= Qt::RightEdge;
        if (local.y() < 8) edges |= Qt::TopEdge; else if (local.y() >= height() - 8) edges |= Qt::BottomEdge;
    }
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        const bool blank = widget == this || widget == heading_ || widget == status_;
        if (edges || blank || mouse->modifiers().testFlag(Qt::ControlModifier)) {
            resizeEdges_ = edges; dragging_ = !edges; dragStart_ = global; windowStart_ = pos(); resizeStart_ = geometry();
            widget->grabMouse(); return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && (dragging_ || resizeEdges_)) {
        dragging_ = false; resizeEdges_ = {}; widget->releaseMouse(); savePreferences(); return true;
    } else if (event->type() == QEvent::MouseMove) {
        if (dragging_) { move(windowStart_ + global - dragStart_); return true; }
        if (resizeEdges_) {
            const QPoint delta = global - dragStart_; QRect r = resizeStart_;
            if (resizeEdges_.testFlag(Qt::LeftEdge)) r.setLeft(std::min(resizeStart_.left() + delta.x(), r.right() - minimumWidth() + 1));
            if (resizeEdges_.testFlag(Qt::RightEdge)) r.setRight(std::max(resizeStart_.right() + delta.x(), r.left() + minimumWidth() - 1));
            if (resizeEdges_.testFlag(Qt::TopEdge)) r.setTop(std::min(resizeStart_.top() + delta.y(), r.bottom() - minimumHeight() + 1));
            if (resizeEdges_.testFlag(Qt::BottomEdge)) r.setBottom(std::max(resizeStart_.bottom() + delta.y(), r.top() + minimumHeight() - 1));
            setGeometry(r); return true;
        }
        if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge))) widget->setCursor(Qt::SizeFDiagCursor);
        else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge)) || (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge))) widget->setCursor(Qt::SizeBDiagCursor);
        else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) widget->setCursor(Qt::SizeHorCursor);
        else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) widget->setCursor(Qt::SizeVerCursor);
        else widget->setCursor(qobject_cast<QPushButton *>(widget) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    return QDialog::eventFilter(object, event);
}
