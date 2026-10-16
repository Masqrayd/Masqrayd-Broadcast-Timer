#pragma once
#include <QWidget>
#include <QTimer>
#include <cstdint>

// Adopts only this process's known native camera class. The camera plugin keeps
// ownership of the HWND, source and OBS display. This class never destroys them.
class NativeCameraSurface : public QWidget {
public:
    explicit NativeCameraSurface(QWidget *parent = nullptr);
    ~NativeCameraSurface() override;
    bool attach(std::uintptr_t window);
    void detach(bool show = true);
    bool attached() const;
    void synchronize();
    static std::uintptr_t findCamera();
protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    void fit();
    void requestFit();
    QTimer fitLater_;
    std::uintptr_t camera_ = 0, oldParent_ = 0;
    std::intptr_t oldStyle_ = 0, oldExStyle_ = 0;
    QRect oldRect_;
};
