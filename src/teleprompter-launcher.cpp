#include "teleprompter-launcher.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QCursor>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
bool focusReader() {
#ifdef _WIN32
    // The updated reader handles activation on Tk's UI thread, including its
    // own overlay-visible state. This also covers the startup interval before
    // the first window exists.
    const auto activate = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\MasqraydTeleprompter_v7_11_activate");
    bool signaled = false;
    if (activate) { signaled = SetEvent(activate) != FALSE; CloseHandle(activate); }
    // Also recognizes the user's unmodified v7.11 reader when it is already open.
    const auto controls = FindWindowW(nullptr, L"Advanced Overlay Teleprompter v7.11 Controls");
    const auto overlay = FindWindowW(nullptr, L"Advanced Overlay Teleprompter v7.11");
    if (!controls && !overlay) return signaled;
    for (auto window : {overlay, controls}) if (window) {
        if (window == overlay && !IsWindowVisible(window)) continue;
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        else ShowWindow(window, SW_SHOW);
        BringWindowToTop(window);
    }
    SetForegroundWindow(controls ? controls : overlay);
    return true;
#else
    return false;
#endif
}
}
TeleprompterLauncher::TeleprompterLauncher(const QString &file, QWidget *parent)
    : QObject(parent), prefs_(file, QSettings::IniFormat), parent_(parent) {}

TeleprompterLauncher::Command TeleprompterLauncher::command(const QString &python, const QString &script) {
    const QFileInfo file(script);
    return {python, {file.absoluteFilePath()}, file.absolutePath()};
}
bool TeleprompterLauncher::chooseScript() {
    const auto path = QFileDialog::getOpenFileName(parent_, "Choose your teleprompter Python file",
        prefs_.value("script").toString(), "Python teleprompter (*.py)");
    if (path.isEmpty()) return false;
    prefs_.setValue("script", path); prefs_.sync(); return true;
}
bool TeleprompterLauncher::choosePython() {
    const auto path = QFileDialog::getOpenFileName(parent_, "Choose the Python used by your teleprompter (pythonw.exe)",
        prefs_.value("python").toString(), "Python executable (*.exe);;All files (*)");
    if (path.isEmpty()) return false;
    prefs_.setValue("python", path); prefs_.sync(); return true;
}
void TeleprompterLauncher::configure() {
    QMenu menu(parent_);
    menu.addAction("Choose teleprompter file...", this, [this] { chooseScript(); });
    menu.addAction("Choose Python executable...", this, [this] { choosePython(); });
    menu.addAction("Use Python from PATH", this, [this] { prefs_.remove("python"); prefs_.sync(); });
    menu.exec(QCursor::pos());
}
void TeleprompterLauncher::open() {
    if (focusReader()) return;
    // A double-click can arrive before Python has created its first window.
    if (lastLaunch_.isValid() && lastLaunch_.elapsed() < 5000) return;
    auto script = prefs_.value("script").toString();
    if (!QFileInfo::exists(script)) {
        if (!chooseScript()) return;
        script = prefs_.value("script").toString();
    }
    auto python = prefs_.value("python").toString();
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable("pythonw.exe");
        if (python.isEmpty()) python = QStandardPaths::findExecutable("python.exe");
    }
    if (!QFileInfo::exists(python)) {
        if (!choosePython()) return;
        python = prefs_.value("python").toString();
    }
    const auto plan = command(python, script);
    QProcess process;
    process.setProgram(plan.program); process.setArguments(plan.arguments);
    process.setWorkingDirectory(plan.directory);
    if (!process.startDetached()) {
        QMessageBox::warning(parent_, "Teleprompter could not start",
            "Check Teleprompter settings and choose the Python executable that runs your existing teleprompter.\n\n" + process.errorString());
        return;
    }
    lastLaunch_.start();
}
