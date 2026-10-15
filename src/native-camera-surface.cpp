#include "native-camera-surface.hpp"
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
constexpr wchar_t marker[] = L"MasqraydBroadcastStack";
constexpr wchar_t cameraClass[] = L"MasqraydCameraPopoutNativeV1";
bool isCamera(HWND window) {
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    return process == GetCurrentProcessId() && wcscmp(name, cameraClass) == 0;
}
}
#endif
NativeCameraSurface::NativeCameraSurface(QWidget *parent) : QWidget(parent) {
    setMinimumSize(160, 90); setAttribute(Qt::WA_NativeWindow);
}
NativeCameraSurface::~NativeCameraSurface() { detach(false); }
std::uintptr_t NativeCameraSurface::findCamera() {
#ifdef _WIN32
    HWND found = nullptr;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        if (!isCamera(window) || GetPropW(window, marker)) return TRUE;
        *reinterpret_cast<HWND *>(data) = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&found));
    return reinterpret_cast<std::uintptr_t>(found);
#else
    return 0;
#endif
}
bool NativeCameraSurface::attached() const {
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(camera_);
    return window && IsWindow(window) && isCamera(window) &&
           GetPropW(window, marker) == reinterpret_cast<HANDLE>(winId());
#else
    return false;
#endif
}
bool NativeCameraSurface::attach(std::uintptr_t native) {
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(native);
    if (camera_ || !IsWindow(window) || !isCamera(window) || GetPropW(window, marker)) return false;
    oldParent_ = reinterpret_cast<std::uintptr_t>(GetParent(window));
    oldStyle_ = GetWindowLongPtrW(window, GWL_STYLE); oldExStyle_ = GetWindowLongPtrW(window, GWL_EXSTYLE);
    RECT r{}; GetWindowRect(window, &r); oldRect_ = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (!SetPropW(window, marker, reinterpret_cast<HANDLE>(winId()))) return false;
    SetWindowLongPtrW(window, GWL_STYLE, (oldStyle_ & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD | WS_CLIPSIBLINGS);
    SetWindowLongPtrW(window, GWL_EXSTYLE, oldExStyle_ & ~(WS_EX_APPWINDOW | WS_EX_TOPMOST));
    SetLastError(0);
    HWND previous = SetParent(window, reinterpret_cast<HWND>(winId()));
    if (!previous && GetLastError()) {
        SetWindowLongPtrW(window, GWL_STYLE, oldStyle_); SetWindowLongPtrW(window, GWL_EXSTYLE, oldExStyle_);
        RemovePropW(window, marker); return false;
    }
    camera_ = native;
    SetWindowPos(window, nullptr, 0, 0, 1, 1, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    fit(); ShowWindow(window, SW_SHOW); return true;
#else
    Q_UNUSED(native); return false;
#endif
}
void NativeCameraSurface::detach(bool show) {
#ifdef _WIN32
    if (attached()) {
        auto window = reinterpret_cast<HWND>(camera_);
        ShowWindow(window, SW_HIDE);
        SetParent(window, reinterpret_cast<HWND>(oldParent_));
        SetWindowLongPtrW(window, GWL_STYLE, oldStyle_); SetWindowLongPtrW(window, GWL_EXSTYLE, oldExStyle_);
        SetWindowPos(window, (oldExStyle_ & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST,
                     oldRect_.x(), oldRect_.y(), oldRect_.width(), oldRect_.height(), SWP_FRAMECHANGED | SWP_NOACTIVATE);
        RemovePropW(window, marker);
        if (show) ShowWindow(window, SW_SHOW);
    }
#else
    Q_UNUSED(show);
#endif
    camera_ = 0;
}
void NativeCameraSurface::fit() {
#ifdef _WIN32
    if (attached()) {
        RECT r{}; GetClientRect(reinterpret_cast<HWND>(winId()), &r);
        SetWindowPos(reinterpret_cast<HWND>(camera_), nullptr, 0, 0, std::max<LONG>(1, r.right), std::max<LONG>(1, r.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}
void NativeCameraSurface::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); fit(); }
void NativeCameraSurface::showEvent(QShowEvent *event) { QWidget::showEvent(event); fit(); }
