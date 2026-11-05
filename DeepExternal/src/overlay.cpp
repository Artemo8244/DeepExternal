#include "overlay.hpp"

#include <algorithm>
#include <cstring>

using std::max;
using std::min;

namespace {
constexpr wchar_t kClassName[] = L"DeepExternalOverlay";

// The overlay lives on the main thread, which must pump messages every tick.
// Without a pump Windows marks the window (and the process) as "not responding"
// after a few seconds, even though UpdateLayeredWindow keeps producing frames.
LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        ::BeginPaint(hwnd, &ps);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        // WS_EX_TRANSPARENT already handles click-through; when the menu is
        // open that style is cleared and the window must accept clicks.
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    default:
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}
} // namespace

bool Overlay::Create(HWND target) {
    target_ = target;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    hwnd_ = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd_) return false;

    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void Overlay::Destroy() {
    delete g_;
    g_ = nullptr;
    delete bmp_;
    bmp_ = nullptr;

    for (auto& [key, font] : fonts_) delete font;
    fonts_.clear();

    if (dib_) {
        ::DeleteObject(dib_);
        dib_ = nullptr;
    }
    if (memDC_) {
        ::DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    bits_ = nullptr;
    w_ = h_ = 0;
}

void Overlay::ClickThrough(bool on) {
    if (!hwnd_) return;
    LONG_PTR style = ::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (on) style |= WS_EX_TRANSPARENT;
    else    style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    ::SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

void Overlay::EnsureBuffer(int w, int h) {
    w = max(1, w);
    h = max(1, h);

    delete g_;
    g_ = nullptr;
    delete bmp_;
    bmp_ = nullptr;

    if (dib_) {
        ::DeleteObject(dib_);
        dib_ = nullptr;
    }
    if (memDC_) {
        ::DeleteDC(memDC_);
        memDC_ = nullptr;
    }

    HDC screen = ::GetDC(nullptr);
    memDC_ = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dib_ = ::CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!dib_) return;

    ::SelectObject(memDC_, dib_);

    stride_ = w * 4;
    bmp_ = new Gdiplus::Bitmap(w, h, stride_, PixelFormat32bppPARGB,
                               static_cast<BYTE*>(bits_));
    g_ = new Gdiplus::Graphics(bmp_);

    w_ = w;
    h_ = h;
}

void Overlay::Sync(HWND target) {
    if (!hwnd_ || !target) return;
    if (target != target_) target_ = target;

    if (::IsIconic(target)) {
        Hide();
        return;
    }

    RECT rc{};
    if (!::GetClientRect(target, &rc)) return;

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        Hide();
        return;
    }

    const bool resized = (w != w_ || h != h_);
    if (w != w_ || h != h_ || !bits_) EnsureBuffer(w, h);
    if (!bits_) return;

    POINT tl{ 0, 0 };
    ::ClientToScreen(target, &tl);

    if (resized || tl.x != dst_.x || tl.y != dst_.y || !::IsWindowVisible(hwnd_)) {
        dst_ = tl;
        ::SetWindowPos(hwnd_, HWND_TOPMOST, tl.x, tl.y, w, h,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

bool Overlay::Begin() {
    if (!bits_ || !g_) return false;
    std::memset(bits_, 0, static_cast<size_t>(stride_) * static_cast<size_t>(h_));
    g_->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g_->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g_->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    return true;
}

void Overlay::Present() {
    if (!hwnd_ || !memDC_) return;

    HDC screen = ::GetDC(nullptr);
    POINT src{ 0, 0 };
    SIZE size{ w_, h_ };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::UpdateLayeredWindow(hwnd_, screen, &dst_, &size, memDC_, &src, 0, &blend, ULW_ALPHA);
    ::ReleaseDC(nullptr, screen);
}

void Overlay::Hide() {
    if (hwnd_ && ::IsWindowVisible(hwnd_)) ::ShowWindow(hwnd_, SW_HIDE);
}

Gdiplus::Font* Overlay::FontFor(float size) {
    const int key = static_cast<int>(size * 10.f);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;

    auto* font = new Gdiplus::Font(L"Segoe UI", size, Gdiplus::FontStyleRegular,
                                   Gdiplus::UnitPixel);
    if (font->GetLastStatus() != Gdiplus::Ok) {
        delete font;
        return nullptr;
    }
    fonts_[key] = font;
    return font;
}

void Overlay::Line(float x1, float y1, float x2, float y2, const Color& c, float thickness) {
    if (!g_) return;
    Gdiplus::Pen pen(c, thickness);
    g_->DrawLine(&pen, x1, y1, x2, y2);
}

void Overlay::Box(float x, float y, float w, float h, const Color& c, float thickness) {
    if (!g_) return;
    Gdiplus::Pen pen(c, thickness);
    g_->DrawRectangle(&pen, x, y, w, h);
}

void Overlay::Fill(float x, float y, float w, float h, const Color& c) {
    if (!g_) return;
    Gdiplus::SolidBrush brush(c);
    g_->FillRectangle(&brush, x, y, w, h);
}

void Overlay::Circle(float cx, float cy, float r, const Color& c, float thickness) {
    if (!g_) return;
    Gdiplus::Pen pen(c, thickness);
    g_->DrawEllipse(&pen, cx - r, cy - r, r * 2.f, r * 2.f);
}

void Overlay::Text(float x, float y, const wchar_t* text, const Color& c, float size,
                   Align align) {
    if (!g_ || !text) return;

    Gdiplus::Font* font = FontFor(size);
    if (!font) return;

    Gdiplus::RectF bounds{};
    g_->MeasureString(text, -1, font, Gdiplus::PointF(0.f, 0.f), &bounds);

    float ox = x;
    if (align == Align::Center) ox = x - bounds.Width * 0.5f;
    else if (align == Align::Right) ox = x - bounds.Width;
    const float oy = y;

    Gdiplus::SolidBrush shadow(Color(170, 0, 0, 0));
    g_->DrawString(text, -1, font, Gdiplus::PointF(ox + 1.f, oy + 1.f), &shadow);

    Gdiplus::SolidBrush brush(c);
    g_->DrawString(text, -1, font, Gdiplus::PointF(ox, oy), &brush);
}
