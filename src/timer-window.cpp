#include "timer-window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <chrono>
#include <cmath>

namespace {
qint64 monotonicNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
QString timeText(qint64 seconds) {
    seconds = std::max<qint64>(0, seconds);
    const auto m = (seconds / 60) % 60;
    const auto s = seconds % 60;
    if (seconds >= 3600) return QString("%1:%2:%3").arg(seconds / 3600).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
QFont fitted(QFont font, const QString &text, const QRect &bounds, int maximum, int flags) {
    for (int px = std::max(6, maximum); px >= 6; --px) {
        font.setPixelSize(px);
        const QRect measured = QFontMetrics(font).boundingRect(bounds, flags, text);
        if (measured.width() <= bounds.width() && measured.height() <= bounds.height()) return font;
    }
    font.setPixelSize(6); return font;
}
}

class FitLabel : public QWidget {
public:
    QString text;
    QColor color = QColor("#eeeef2");
    bool bold = false, mono = false;
    int maxPixels = 96;
    explicit FitLabel(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }
    QSize sizeHint() const override { return QSize(1, 1); }
    QSize minimumSizeHint() const override { return QSize(1, 1); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::TextAntialiasing);
        QFont f = mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : font();
        f.setBold(bold);
        p.setFont(fitted(f, text, rect().adjusted(2, 0, -2, 0), maxPixels, Qt::AlignCenter | Qt::TextSingleLine));
        p.setPen(color); p.drawText(rect(), Qt::AlignCenter | Qt::TextSingleLine, text);
    }
};

class FitButton : public QPushButton {
public:
    explicit FitButton(const QString &text, QWidget *parent) : QPushButton(text, parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        setMinimumSize(1, 1); setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return QSize(1, 1); }
    QSize minimumSizeHint() const override { return QSize(1, 1); }
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option; initStyleOption(&option); option.text.clear();
        QPainter p(this); style()->drawControl(QStyle::CE_PushButton, &option, &p, this);
        const QRect area = rect().adjusted(5, 3, -5, -3);
        QFont f = font(); f.setBold(true);
        p.setFont(fitted(f, text(), area, std::min(20, height() / 2), Qt::AlignCenter | Qt::TextWordWrap));
        p.setPen(isEnabled() ? QColor("#f6f6f8") : QColor("#777784"));
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, text());
    }
};

TimerWindow::TimerWindow(const QString &settingsFile, QWidget *parent, std::function<qint64()> clock)
    : QDialog(parent), prefs_(settingsFile, QSettings::IniFormat), clock_(clock ? clock : monotonicNow) {
    setObjectName("MasqraydBroadcastTimer"); setWindowTitle("Masqrayd Broadcast Timer");
    setModal(false); setMinimumSize(220, 220); resize(420, 355);
    storySeconds_ = std::clamp(prefs_.value("storySeconds", 600).toInt(), 1, 86399);
    breakSeconds_ = std::clamp(prefs_.value("breakSeconds", 300).toInt(), 1, 86399);
    warningSeconds_ = std::clamp(prefs_.value("warningSeconds", 30).toInt(), 0, 86400);
    countUp_ = prefs_.value("countUp", false).toBool(); flash_ = prefs_.value("flash", true).toBool();
    sound_ = prefs_.value("sound", false).toBool(); use24Hour_ = prefs_.value("24hour", false).toBool();
    topmost_ = prefs_.value("topmost", true).toBool(); borderless_ = prefs_.value("borderless", true).toBool();
    openWithOBS_ = prefs_.value("openWithOBS", true).toBool();
    setStyleSheet("QDialog#MasqraydBroadcastTimer { background:#17171e; border:1px solid #42424d; }"
                  "QPushButton { background:#30303c; border:1px solid #505060; border-radius:5px; }"
                  "QPushButton:hover { background:#444454; } QPushButton:pressed { background:#595968; }"
                  "QPushButton:focus { border:2px solid #d6af52; }"
                  "QPushButton#nextStory { background:#715721; } QPushButton#takeBreak { background:#21576a; }"
                  "QProgressBar { background:#30303c; border:0; border-radius:3px; }"
                  "QProgressBar::chunk { background:#d6af52; border-radius:3px; }");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(5);
    heading_ = new FitLabel(this); heading_->maxPixels = 22; heading_->bold = true;
    heading_->setObjectName("heading");
    digits_ = new FitLabel(this); digits_->maxPixels = 220; digits_->bold = true; digits_->mono = true;
    digits_->setObjectName("timerDisplay");
    clockLabel_ = new FitLabel(this); clockLabel_->maxPixels = 19; clockLabel_->color = QColor("#c1c1cb");
    statusLabel_ = new FitLabel(this); statusLabel_->maxPixels = 18;
    progress_ = new QProgressBar(this); progress_->setRange(0, 1000); progress_->setTextVisible(false); progress_->setFixedHeight(5);
    layout->addWidget(heading_, 2); layout->addWidget(digits_, 8); layout->addWidget(clockLabel_, 2);
    layout->addWidget(progress_); layout->addWidget(statusLabel_, 2);
    auto *row = new QHBoxLayout; row->setSpacing(5);
    start_ = new FitButton("Start", this); pause_ = new FitButton("Pause", this);
    stop_ = new FitButton("Stop", this); reset_ = new FitButton("Reset", this);
    start_->setObjectName("start"); pause_->setObjectName("pause"); stop_->setObjectName("stop"); reset_->setObjectName("reset");
    for (auto *b : {start_, pause_, stop_, reset_}) row->addWidget(b, 1);
    layout->addLayout(row, 3);
    auto *segments = new QHBoxLayout; segments->setSpacing(5);
    next_ = new FitButton("Next Story", this); next_->setObjectName("nextStory");
    break_ = new FitButton("Take a Break", this); break_->setObjectName("takeBreak");
    segments->addWidget(next_, 1); segments->addWidget(break_, 1); layout->addLayout(segments, 3);
    start_->setToolTip("Start or continue the current timer"); next_->setToolTip("Start a fresh story timer immediately");
    reset_->setToolTip("Reset the current timer without changing the story number. Right-click for story navigation and Restart from Story 1.");
    break_->setToolTip("Start a fresh break countdown immediately");
    connect(start_, &QPushButton::clicked, this, [this] { state_.start(now()); refresh(); });
    connect(pause_, &QPushButton::clicked, this, &TimerWindow::togglePause);
    connect(stop_, &QPushButton::clicked, this, [this] { state_.stop(now()); refresh(); });
    connect(reset_, &QPushButton::clicked, this, &TimerWindow::resetTimer);
    connect(next_, &QPushButton::clicked, this, &TimerWindow::nextStory);
    connect(break_, &QPushButton::clicked, this, &TimerWindow::takeBreak);
    state_.reset(duration()); applyWindowFlags();
    restoreGeometry(prefs_.value("geometry").toByteArray());
    // Mouse tracking on child widgets keeps borderless resize edges reachable.
    installEventFilter(this); setMouseTracking(true);
    for (auto *child : findChildren<QWidget *>()) { child->installEventFilter(this); child->setMouseTracking(true); }
    connect(&ticker_, &QTimer::timeout, this, &TimerWindow::refresh); ticker_.start(100);
    refresh();
}

TimerWindow::~TimerWindow() { savePreferences(); }
qint64 TimerWindow::now() const { return clock_(); }
bool TimerWindow::opensWithOBS() const { return openWithOBS_; }
std::int64_t TimerWindow::duration() const { return std::int64_t(state_.segment == TimerState::Segment::Story ? storySeconds_ : breakSeconds_) * 1000; }
void TimerWindow::resetAlerts() { warningSounded_ = false; endSounded_ = false; }
void TimerWindow::nextStory() { state_.nextStory(now(), std::int64_t(storySeconds_) * 1000); resetAlerts(); refresh(); }
void TimerWindow::takeBreak() { state_.takeBreak(now(), std::int64_t(breakSeconds_) * 1000); resetAlerts(); refresh(); }
void TimerWindow::resetTimer() { state_.reset(duration()); resetAlerts(); refresh(); }
void TimerWindow::selectStory(int number) {
    state_.selectStory(number, std::int64_t(storySeconds_) * 1000);
    resetAlerts(); refresh();
}
void TimerWindow::storyNumberDialog() {
    bool accepted = false;
    const int number = QInputDialog::getInt(this, "Choose Story", "Story number (timer will be reset and ready):",
                                          std::max(1, state_.story), 1, 999999, 1, &accepted);
    if (accepted) selectStory(number);
}
void TimerWindow::togglePause() {
    if (state_.status == TimerState::Status::Running) state_.pause(now());
    else if (state_.status == TimerState::Status::Paused) state_.start(now());
    refresh();
}
void TimerWindow::refresh() {
    const auto t = now(); const auto elapsed = state_.elapsed(t); const auto left = state_.remaining(t);
    const bool over = state_.overtime(t), warning = state_.warning(t, warningSeconds_);
    const bool running = state_.status == TimerState::Status::Running;
    heading_->text = state_.segment == TimerState::Segment::Break ? "BREAK" : QString("STORY %1").arg(std::max(1, state_.story));
    if (countUp_ && state_.segment == TimerState::Segment::Story) digits_->text = timeText(elapsed / 1000);
    else digits_->text = left < 0 ? "+" + timeText((-left) / 1000) : timeText((left + 999) / 1000);
    const bool bright = !flash_ || !running || (elapsed / 500) % 2 == 0;
    digits_->color = (over || warning) ? QColor(bright ? "#ff505e" : "#b66b72") : QColor("#f6f6f8");
    clockLabel_->text = QDateTime::currentDateTime().toString(use24Hour_ ? "ddd, MMM d yyyy  |  HH:mm:ss" : "ddd, MMM d yyyy  |  h:mm:ss AP");
    QString status;
    switch (state_.status) {
    case TimerState::Status::Ready: status = "Ready"; break;
    case TimerState::Status::Paused: status = "Paused"; break;
    case TimerState::Status::Stopped: status = "Stopped"; break;
    case TimerState::Status::Running: status = "Running"; break;
    }
    if (over) status += "  |  Overtime +" + timeText(std::max<qint64>(0, -left) / 1000);
    else if (warning) status += "  |  Wrapping-up time";
    else status += "  |  Target " + timeText(state_.target / 1000);
    statusLabel_->text = status;
    statusLabel_->color = (warning || over) ? QColor("#ff858d") : QColor("#bcbcc8");
    progress_->setValue(static_cast<int>(std::clamp<double>(double(elapsed) / double(state_.target), 0.0, 1.0) * 1000));
    start_->setEnabled(!running); start_->setText(state_.status == TimerState::Status::Paused || state_.status == TimerState::Status::Stopped ? "Resume" : "Start");
    pause_->setEnabled(running || state_.status == TimerState::Status::Paused);
    pause_->setText(state_.status == TimerState::Status::Paused ? "Resume" : "Pause");
    stop_->setEnabled(running || state_.status == TimerState::Status::Paused);
    if (running && sound_) {
        if (over && !endSounded_) { QApplication::beep(); endSounded_ = true; warningSounded_ = true; }
        else if (warning && !warningSounded_) { QApplication::beep(); warningSounded_ = true; }
    }
    for (auto *label : {heading_, digits_, clockLabel_, statusLabel_}) label->update();
}

void TimerWindow::savePreferences() {
    prefs_.setValue("storySeconds", storySeconds_); prefs_.setValue("breakSeconds", breakSeconds_);
    prefs_.setValue("warningSeconds", warningSeconds_); prefs_.setValue("countUp", countUp_);
    prefs_.setValue("flash", flash_); prefs_.setValue("sound", sound_); prefs_.setValue("24hour", use24Hour_);
    prefs_.setValue("topmost", topmost_); prefs_.setValue("borderless", borderless_);
    prefs_.setValue("openWithOBS", openWithOBS_); prefs_.setValue("geometry", saveGeometry()); prefs_.sync();
}
void TimerWindow::applyWindowFlags() {
    const bool visible = isVisible(); const QRect old = geometry();
    Qt::WindowFlags flags = Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint;
    if (topmost_) flags |= Qt::WindowStaysOnTopHint;
    if (borderless_) flags |= Qt::FramelessWindowHint;
    setWindowFlags(flags); setGeometry(old); if (visible) show();
}
void TimerWindow::ensureOnScreen() {
    for (auto *screen : QGuiApplication::screens()) {
        const auto visible = geometry().intersected(screen->availableGeometry());
        if (visible.width() >= std::min(width(), 100) && visible.height() >= std::min(height(), 100)) return;
    }
    if (auto *screen = QGuiApplication::primaryScreen()) {
        QRect available = screen->availableGeometry(); resize(size().boundedTo(available.size()));
        move(available.center() - rect().center());
    }
}
void TimerWindow::showTimer() { ensureOnScreen(); showNormal(); raise(); activateWindow(); }
void TimerWindow::shutdown() { shuttingDown_ = true; ticker_.stop(); savePreferences(); hide(); }
void TimerWindow::closeEvent(QCloseEvent *event) {
    savePreferences(); if (shuttingDown_) event->accept(); else { event->ignore(); hide(); }
}
void TimerWindow::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.addAction("Restart current story", this, [this] { selectStory(state_.story); })->setObjectName("restartCurrentStory");
    auto *previous = menu.addAction("Previous story", this, [this] { selectStory(state_.story - 1); });
    previous->setObjectName("previousStory"); previous->setEnabled(state_.story > 1);
    menu.addAction("Set story number...", this, &TimerWindow::storyNumberDialog)->setObjectName("setStoryNumber");
    menu.addAction("Restart from Story 1", this, [this] { selectStory(1); })->setObjectName("restartFromBeginning");
    menu.addSeparator();
    menu.addAction("Timer settings...", this, &TimerWindow::settingsDialog);
    auto *count = menu.addAction("Count up for stories"); count->setCheckable(true); count->setChecked(countUp_);
    menu.addSeparator();
    auto *top = menu.addAction("Always on top"); top->setCheckable(true); top->setChecked(topmost_);
    auto *border = menu.addAction("Borderless window"); border->setCheckable(true); border->setChecked(borderless_);
    auto *startup = menu.addAction("Open with OBS"); startup->setCheckable(true); startup->setChecked(openWithOBS_);
    menu.addSeparator();
    auto *hint = menu.addAction("Move: drag the clock or Ctrl+drag | Resize: drag edges"); hint->setEnabled(false);
    auto *hide = menu.addAction("Hide timer (keeps running)");
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == count) countUp_ = count->isChecked();
    else if (chosen == top) { topmost_ = top->isChecked(); applyWindowFlags(); }
    else if (chosen == border) { borderless_ = border->isChecked(); applyWindowFlags(); }
    else if (chosen == startup) openWithOBS_ = startup->isChecked();
    else if (chosen == hide) this->hide();
    savePreferences(); refresh();
}

void TimerWindow::settingsDialog() {
    QDialog dialog(this); dialog.setWindowTitle("Broadcast Timer Settings");
    auto *form = new QFormLayout(&dialog);
    auto *story = new QTimeEdit(QTime(0, 0).addSecs(storySeconds_), &dialog); story->setDisplayFormat("HH:mm:ss");
    auto *rest = new QTimeEdit(QTime(0, 0).addSecs(breakSeconds_), &dialog); rest->setDisplayFormat("HH:mm:ss");
    story->setMinimumTime(QTime(0, 0, 1)); rest->setMinimumTime(QTime(0, 0, 1));
    auto *warn = new QSpinBox(&dialog); warn->setRange(0, 86400); warn->setSuffix(" seconds"); warn->setValue(warningSeconds_); warn->setSpecialValueText("Off");
    auto *up = new QCheckBox("Show elapsed time for stories", &dialog); up->setChecked(countUp_);
    auto *blink = new QCheckBox("Pulse red when time is low or expired", &dialog); blink->setChecked(flash_);
    auto *beep = new QCheckBox("Sound at warning and expiry", &dialog); beep->setChecked(sound_);
    auto *hours = new QCheckBox("Use 24-hour clock", &dialog); hours->setChecked(use24Hour_);
    form->addRow("Story duration", story); form->addRow("Break duration", rest); form->addRow("Warn with time remaining", warn);
    form->addRow(up); form->addRow(blink); form->addRow(beep); form->addRow(hours);
    auto *note = new QLabel("Durations apply on Next Story, Take a Break, or Reset.\nBreaks always count down. Expired timers continue into overtime.\nSound uses your system audio and may be captured by OBS.", &dialog);
    note->setWordWrap(true); form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    storySeconds_ = QTime(0, 0).secsTo(story->time()); breakSeconds_ = QTime(0, 0).secsTo(rest->time());
    warningSeconds_ = warn->value(); countUp_ = up->isChecked(); flash_ = blink->isChecked();
    sound_ = beep->isChecked(); use24Hour_ = hours->isChecked();
    if (state_.status == TimerState::Status::Ready) state_.reset(duration());
    savePreferences(); refresh();
}

bool TimerWindow::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget) return QDialog::eventFilter(object, event);
    if (event->type() == QEvent::ContextMenu && widget != this) {
        contextMenuEvent(static_cast<QContextMenuEvent *>(event)); return true;
    }
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
        const bool blank = widget == this || widget == heading_ || widget == digits_ || widget == clockLabel_ || widget == statusLabel_;
        if (edges || blank || mouse->modifiers().testFlag(Qt::ControlModifier)) {
            resizeEdges_ = edges; dragging_ = !edges; dragStart_ = global; windowStart_ = pos(); resizeStart_ = geometry();
            widget->grabMouse(); return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && (dragging_ || resizeEdges_)) {
        dragging_ = false; resizeEdges_ = {}; widget->releaseMouse(); return true;
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
