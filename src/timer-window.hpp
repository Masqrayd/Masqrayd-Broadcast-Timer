#pragma once
#include "timer-state.hpp"
#include <QDialog>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <array>

class FitLabel;
class FitButton;
class QProgressBar;
class QMouseEvent;
class TimerWindow : public QDialog {
public:
    enum class Output { Recording = 0, Streaming = 1 };
    enum class OutputPhase { Starting, Started, Stopping, Stopped };
    struct OutputBackend {
        std::function<bool(Output)> active;
        std::function<void(Output)> start;
    };
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
    void setOutputBackend(OutputBackend backend);
    void outputEvent(Output output, OutputPhase phase);
protected:
    void contextMenuEvent(QContextMenuEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    void settingsDialog();
    void applyWindowFlags();
    void resetTimer();
    void selectStory(int number);
    void storyNumberDialog();
    void resetAlerts();
    void ensureOnScreen();
    void requestOutput(Output output);
    void refreshOutputButtons();
    std::int64_t duration() const;
    qint64 now() const;
    TimerState state_;
    QSettings prefs_;
    QTimer ticker_;
    std::function<qint64()> clock_;
    FitLabel *heading_, *digits_, *clockLabel_, *statusLabel_;
    FitButton *start_, *pause_, *stop_, *reset_, *next_, *break_;
    QWidget *outputRow_;
    std::array<FitButton *, 2> outputButtons_{};
    OutputBackend outputBackend_;
    std::array<bool, 2> showOutput_{{true, false}}, autoStartOutput_{{true, true}};
    std::array<bool, 2> outputActive_{}, outputBusy_{}, outputRequested_{};
    std::array<qint64, 2> outputRequestDeadline_{};
    bool outputSessionLinked_ = false;
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
