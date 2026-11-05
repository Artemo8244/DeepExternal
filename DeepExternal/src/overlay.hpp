#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
using std::max;
using std::min;

#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <unordered_map>

#include "math.hpp"

#pragma comment(lib, "gdiplus.lib")

using Gdiplus::Color;

class Overlay {
public:
    bool Create(HWND target);
    void Destroy();

    void Sync(HWND target);
    bool Begin();
    void Present();
    void Hide();

    bool  created() const { return hwnd_ != nullptr; }
    HWND  hwnd() const { return hwnd_; }
    HWND  target() const { return target_; }
    int   width() const { return w_; }
    int   height() const { return h_; }

    void ClickThrough(bool on);

    enum class Align { Left, Center, Right };

    void Line(float x1, float y1, float x2, float y2, const Color& c, float thickness = 1.f);
    void Box(float x, float y, float w, float h, const Color& c, float thickness = 1.f);
    void Fill(float x, float y, float w, float h, const Color& c);
    void Circle(float cx, float cy, float r, const Color& c, float thickness = 1.f);
    void Text(float x, float y, const wchar_t* text, const Color& c, float size,
              Align align = Align::Left);

private:
    void EnsureBuffer(int w, int h);
    Gdiplus::Font* FontFor(float size);

    HWND    hwnd_ = nullptr;
    HWND    target_ = nullptr;
    HDC     memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    void*   bits_ = nullptr;

    Gdiplus::Bitmap*   bmp_ = nullptr;
    Gdiplus::Graphics* g_ = nullptr;

    int stride_ = 0;
    int w_ = 0;
    int h_ = 0;
    POINT dst_{};

    std::unordered_map<int, Gdiplus::Font*> fonts_;
};
