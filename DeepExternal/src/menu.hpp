#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "config.hpp"
#include "input.hpp"
#include "overlay.hpp"

namespace ui {
const Color kPanel(240, 14, 15, 20);
const Color kBorder(255, 66, 70, 88);
const Color kText(255, 226, 228, 236);
const Color kDim(255, 138, 141, 154);
const Color kAccent(255, 108, 162, 255);
const Color kGood(255, 84, 214, 138);
const Color kBad(255, 226, 92, 92);
} // namespace ui

inline void KeyName(int vk, wchar_t* out, size_t count) {
    if (vk == VK_XBUTTON1) { wcscpy_s(out, count, L"mouse5"); return; }
    if (vk == VK_XBUTTON2) { wcscpy_s(out, count, L"mouse4"); return; }
    if (vk == VK_LBUTTON)  { wcscpy_s(out, count, L"mouse1"); return; }
    if (vk == VK_RBUTTON)  { wcscpy_s(out, count, L"mouse2"); return; }

    const UINT sc = ::MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (sc && ::GetKeyNameTextW(static_cast<LONG>(sc << 16), out, static_cast<int>(count)) > 0)
        return;

    swprintf_s(out, count, L"vk 0x%02X", vk);
}

class Menu {
public:
    bool open = false;

    void Toggle(Overlay& ov) {
        open = !open;
        ov.ClickThrough(!open);
    }

    void Render(Overlay& ov, Settings& s, Keyboard& kb) {
        ov_ = &ov;
        ReadMouse(ov);

        if (capture_) {
            const int vk = kb.FirstPressed(VK_LBUTTON);
            if (vk) {
                *capture_ = vk;
                capture_ = nullptr;
            } else if (kb.Pressed(VK_ESCAPE)) {
                capture_ = nullptr;
            }
        }

        constexpr float px = 40.f;
        constexpr float py = 90.f;
        constexpr float pw = 340.f;
        constexpr float rowH = 24.f;
        constexpr int   rows = 25;
        const float panelH = rows * rowH + 34.f;

        ov.Fill(px, py, pw, panelH, ui::kPanel);
        ov.Box(px, py, pw, panelH, ui::kBorder, 1.f);
        ov.Text(px + 14.f, py + 10.f, L"deep external", ui::kAccent, 15.f);
        ov.Text(px + pw - 14.f, py + 10.f, L"insert", ui::kDim, 12.f, Overlay::Align::Right);

        x_ = px + 14.f;
        w_ = pw - 28.f;
        y_ = py + 36.f;
        row_ = 0;

        Section(L"aimbot");
        Check(L"enabled", &s.aimEnabled);
        KeyBind(L"key", &s.aimKey);
        Slider(L"fov px", &s.aimFov, 10.f, 400.f, L"%.0f");
        Check(L"draw fov", &s.aimDrawFov);
        Slider(L"smoothing", &s.aimSmoothing, 1.f, 20.f, L"%.1f");
        Check(L"team check", &s.aimTeam);
        Check(L"visible only", &s.aimVisible);
        Check(L"aim head", &s.aimHead);

        Section(L"triggerbot");
        Check(L"enabled", &s.triggerEnabled);
        KeyBind(L"key", &s.triggerKey);
        Slider(L"delay ms", &s.triggerDelay, 0.f, 200.f, L"%.0f");
        Slider(L"hold ms", &s.triggerHold, 10.f, 200.f, L"%.0f");
        Check(L"team check", &s.triggerTeam);
        Check(L"visible only", &s.triggerVisible);

        Section(L"esp");
        Check(L"enabled", &s.espEnabled);
        Check(L"box", &s.espBox);
        Check(L"skeleton", &s.espSkeleton);
        Check(L"health bar", &s.espHealth);
        Check(L"distance", &s.espDistance);
        Check(L"team check", &s.espTeam);

        // keep the int settings in sync with the float sliders

        ov.Text(x_, RowY() + 4.f, L"end - quit", ui::kDim, 12.f);
        ++row_;
    }

private:
    void ReadMouse(Overlay& ov) {
        POINT p{};
        ::GetCursorPos(&p);
        ::ScreenToClient(ov.hwnd(), &p);
        mx_ = static_cast<float>(p.x);
        my_ = static_cast<float>(p.y);

        const bool down = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        clicked_ = down && !down_;
        down_ = down;

        if (!down_) dragId_ = nullptr;
    }

    bool Hit(float y, float height = 22.f) const {
        return clicked_ && mx_ >= x_ && mx_ <= x_ + w_ && my_ >= y && my_ <= y + height;
    }

    float RowY() const { return y_ + static_cast<float>(row_) * 24.f; }

    void Section(const wchar_t* title) {
        const float y = RowY();
        ov_->Text(x_, y + 3.f, title, ui::kAccent, 14.f);
        ov_->Line(x_, y + 19.f, x_ + w_, y + 19.f, Color(80, 108, 162, 255), 1.f);
        ++row_;
    }

    void Check(const wchar_t* label, bool* value) {
        const float y = RowY();
        const float bx = x_;
        const float by = y + 3.f;
        constexpr float bs = 15.f;

        if (Hit(y)) *value = !*value;

        ov_->Fill(bx, by, bs, bs, *value ? ui::kGood : Color(255, 40, 43, 52));
        ov_->Box(bx, by, bs, bs, ui::kBorder, 1.f);
        if (*value) {
            const Color ink(255, 14, 24, 16);
            ov_->Line(bx + 3.f, by + 7.5f, bx + 6.f, by + 11.f, ink, 2.f);
            ov_->Line(bx + 6.f, by + 11.f, bx + 12.f, by + 3.5f, ink, 2.f);
        }
        ov_->Text(bx + 24.f, by - 1.f, label, ui::kText, 14.f);
        ++row_;
    }

    void Slider(const wchar_t* label, float* value, float lo, float hi, const wchar_t* fmt) {
        const float y = RowY();
        const float bx = x_ + 140.f;
        const float bw = w_ - 140.f - 46.f;
        const float by = y + 10.f;

        ov_->Text(x_, y + 3.f, label, ui::kDim, 14.f);

        const float t = (hi > lo) ? (*value - lo) / (hi - lo) : 0.f;
        ov_->Fill(bx, by, bw, 4.f, Color(255, 44, 47, 58));
        ov_->Fill(bx, by, bw * (t < 0.f ? 0.f : (t > 1.f ? 1.f : t)), 4.f, ui::kAccent);

        wchar_t buffer[32]{};
        swprintf_s(buffer, fmt, *value);
        ov_->Text(x_ + w_, y + 3.f, buffer, ui::kText, 13.f, Overlay::Align::Right);

        const bool inBar = mx_ >= bx - 4.f && mx_ <= bx + bw + 4.f && my_ >= y && my_ <= y + 20.f;
        if (clicked_ && inBar) dragId_ = value;
        if (dragId_ == value && down_) {
            float nv = lo + (mx_ - bx) / bw * (hi - lo);
            nv = nv < lo ? lo : (nv > hi ? hi : nv);
            *value = nv;
        }
        ++row_;
    }

    void KeyBind(const wchar_t* label, int* vk) {
        const float y = RowY();
        const float bx = x_ + 140.f;
        const float bw = 130.f;
        const float bh = 19.f;

        if (Hit(y + 1.f, bh)) capture_ = vk;

        wchar_t name[48]{};
        KeyName(*vk, name, 48);

        ov_->Fill(bx, y + 2.f, bw, bh, Color(255, 36, 39, 48));
        ov_->Box(bx, y + 2.f, bw, bh,
                 capture_ == vk ? ui::kAccent : ui::kBorder, 1.f);
        ov_->Text(bx + bw * 0.5f, y + 4.5f, capture_ == vk ? L"press key" : name,
                  capture_ == vk ? ui::kAccent : ui::kText, 13.f, Overlay::Align::Center);
        ov_->Text(x_, y + 4.f, label, ui::kDim, 14.f);
        ++row_;
    }

    Overlay* ov_ = nullptr;
    float    mx_ = 0.f;
    float    my_ = 0.f;
    bool     down_ = false;
    bool     clicked_ = false;

    float    x_ = 0.f;
    float    y_ = 0.f;
    float    w_ = 0.f;
    int      row_ = 0;

    float*   dragId_ = nullptr;
    int*     capture_ = nullptr;
};
