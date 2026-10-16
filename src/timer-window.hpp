#pragma once
#include "timer-state.hpp"
#include <QDialog>
#include <QSettings>
#include <QTimer>
#include <functional>

class FitLabel;
class FitButton;
class QProgressBar;
class QMouseEvent;
class TimerWindow : public QDialog {
public:
    explicit TimerWindow(const QString &settingsFile, QWidget *parent = nullptr,
                         std::function<qint64()> clock = {});
    ~TimerWindow() override;
    bool opensWithOBS() const;
    void savePreferences();
    void showTimer();
    void shutdown();
    void nextStory();
    void takeBreak();
    void togglePause();
    const TimerState &state() const { return state_; }
    void refresh();
protected:
    void contextMenuEvent(QContextMenuEvent *) override;
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    void settingsDialog();
    void applyWindowFlags();
    void resetTimer();
    void selectStory(int number);
    void storyNumberDialog();
    void resetAlerts();
    void ensureOnScreen();
    std::int64_t duration() const;
    qint64 now() const;
    TimerState state_;
    QSettings prefs_;
    QTimer ticker_;
    std::function<qint64()> clock_;
    FitLabel *heading_, *digits_, *clockLabel_, *statusLabel_;
    FitButton *start_, *pause_, *stop_, *reset_, *next_, *break_;
    QProgressBar *progress_;
    int storySeconds_ = 600, breakSeconds_ = 300, warningSeconds_ = 30;
    bool countUp_ = false, flash_ = true, sound_ = false, use24Hour_ = false;
    bool topmost_ = true, borderless_ = true, openWithOBS_ = true;
    bool warningSounded_ = false, endSounded_ = false, shuttingDown_ = false;
    QPoint dragStart_, windowStart_;
    QRect resizeStart_;
    Qt::Edges resizeEdges_;
    bool dragging_ = false;
};
