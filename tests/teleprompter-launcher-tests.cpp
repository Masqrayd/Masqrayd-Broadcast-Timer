#include "teleprompter-launcher.hpp"
#include "broadcast-stack.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>
#include <cstdlib>
static void check(bool ok, const char *text) {
    if (!ok) { std::cerr << "FAILED: " << text << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir; int opened = 0;
    BroadcastStack::Providers providers;
    providers.openTeleprompter = [&opened] { ++opened; };
    BroadcastStack stack(dir.filePath("stack.ini"), providers);
    stack.showStack(); app.processEvents();
    auto *button = stack.findChild<QPushButton *>("stackOpenTeleprompter");
    check(button && button->isVisible(), "Launcher is present without including a panel");
    button->click(); check(opened == 1, "Launcher dispatches its callback");
    stack.resize(240, 300); app.processEvents();
    check(button->width() <= stack.width() && button->width() >= 220, "Launcher fits the narrow stack");
    check(!stack.included(BroadcastStack::Timer), "Launcher does not change saved panel membership");

    const QString script = dir.filePath("a folder with spaces/reader & script.py");
    QDir().mkpath(QFileInfo(script).absolutePath());
    QFile file(script); check(file.open(QIODevice::WriteOnly), "Probe script created"); file.write("probe"); file.close();
    const auto command = TeleprompterLauncher::command("C:/Python Folder/pythonw.exe", script);
    check(command.program == "C:/Python Folder/pythonw.exe", "Interpreter path stays a separate executable");
    check(command.arguments.size() == 1 && command.arguments.first() == script, "Script path is one argument, including spaces and shell characters");
    check(command.directory == QFileInfo(script).absolutePath(), "Working directory follows the script, preserving adjacent assets");
#ifdef _WIN32
    QDialog overlay, controls;
    overlay.setWindowTitle("Advanced Overlay Teleprompter v7.11");
    controls.setWindowTitle("Advanced Overlay Teleprompter v7.11 Controls");
    overlay.showMinimized(); controls.showMinimized(); app.processEvents();
    TeleprompterLauncher launcher(dir.filePath("launcher.ini"), &stack);
    launcher.open(); app.processEvents();
    check(!overlay.isMinimized() && !controls.isMinimized(), "Existing reader and controls restore without launching another interpreter");
#endif
    if (argc > 1) stack.grab().save(QString::fromLocal8Bit(argv[1]));
    std::cout << "Teleprompter launcher checks passed\n";
}
