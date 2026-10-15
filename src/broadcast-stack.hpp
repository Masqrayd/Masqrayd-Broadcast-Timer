#pragma once
#include <QDialog>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <array>
#include <cstdint>
#include <functional>
class QBoxLayout;
class QLabel;
class QSplitter;
class NativeCameraSurface;

// Panel discovery is injected so the same grouping/ownership code is exercised
// by the UI tests without opening cameras or controlling real OBS scenes.
class BroadcastStack : public QDialog {
public:
    enum Panel { Camera = 0, Controller = 1, Timer = 2 };
    struct Providers {
        std::function<QWidget *(Panel)> qtPanel;
        std::function<std::uintptr_t()> camera;
        std::function<void(Panel)> open;
    };
    BroadcastStack(const QString &settingsFile, Providers providers);
    ~BroadcastStack() override;
    void showStack();
    void setIncluded(Panel panel, bool included);
    bool included(Panel panel) const;
    bool isAttached(Panel panel) const;
    void scan();
    void shutdown();
    void savePreferences();
    bool opensWithOBS() const;
    void setHorizontal(bool horizontal);
protected:
    void closeEvent(QCloseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    struct Slot {
        bool wanted = false;
        QWidget *frame = nullptr;
        QBoxLayout *layout = nullptr;
        QLabel *placeholder = nullptr;
        QPointer<QWidget> widget, oldParent;
        Qt::WindowFlags oldFlags;
        QByteArray oldGeometry;
    };
    void attachQt(Panel panel, QWidget *widget);
    void detach(Panel panel, bool show);
    void ensureOnScreen();
    void applyWindowFlags();
    void movePanel(Panel panel, int offset);
    void options(const QPoint &position);
    void updateEmpty();
    QSettings prefs_;
    Providers providers_;
    std::array<Slot, 3> slots_;
    NativeCameraSurface *camera_;
    QSplitter *splitter_;
    QWidget *bar_;
    QLabel *title_, *empty_;
    QTimer poll_;
    bool changing_ = false, shuttingDown_ = false, restoring_ = true;
    bool activated_ = false;
    bool topmost_ = true, borderless_ = true, openWithOBS_ = false;
    bool dragging_ = false;
    Qt::Edges edges_;
    QPoint mouseStart_;
    QRect startRect_;
};
