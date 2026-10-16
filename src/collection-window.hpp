#pragma once
#include <QDialog>
#include <QSettings>
#include <QTimer>
#include <QStringList>
#include <functional>
class QLabel;
class QGridLayout;
class QPushButton;

// UI has no OBS source references: collections can unload all their scenes.
class CollectionWindow : public QDialog {
public:
    struct Snapshot { QStringList collections; QString current, profile; bool changing = false; };
    struct Backend {
        std::function<Snapshot()> read;
        std::function<QString(const QString &)> activate; // Empty on success; error otherwise.
    };
    CollectionWindow(const QString &settingsFile, Backend backend, QWidget *parent = nullptr);
    ~CollectionWindow() override;
    void refresh();
    void showCollections();
    void shutdown();
    void savePreferences();
    bool opensWithOBS() const { return openWithOBS_; }
    void setShownCollections(const QStringList &names);
    QStringList shownCollections() const { return shown_; }
    void setColumns(int columns);
protected:
    void contextMenuEvent(QContextMenuEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void reject() override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    void activate(const QString &name);
    void rebuild();
    void applyWindowFlags();
    QSettings prefs_;
    Backend backend_;
    Snapshot snapshot_;
    QTimer poll_;
    QStringList shown_;
    QList<QPushButton *> buttons_;
    QLabel *heading_, *status_;
    QGridLayout *grid_;
    QString error_;
    int columns_ = 2;
    bool initialized_ = false, busy_ = false, shuttingDown_ = false;
    bool topmost_ = true, borderless_ = true, openWithOBS_ = false;
    bool dragging_ = false;
    Qt::Edges resizeEdges_;
    QPoint dragStart_, windowStart_;
    QRect resizeStart_;
};
