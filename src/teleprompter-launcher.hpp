#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QSettings>
#include <QStringList>
class QWidget;

// The reader stays in its own Python process and can outlive OBS.
class TeleprompterLauncher : public QObject {
public:
    struct Command { QString program; QStringList arguments; QString directory; };
    TeleprompterLauncher(const QString &settingsFile, QWidget *parent);
    void open();
    void configure();
    static Command command(const QString &python, const QString &script);
private:
    bool chooseScript();
    bool choosePython();
    QSettings prefs_;
    QWidget *parent_;
    QElapsedTimer lastLaunch_;
};
