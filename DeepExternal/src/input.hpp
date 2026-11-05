#pragma once

#include <windows.h>

#include <cmath>
#include <cstring>

class Keyboard {
public:
    void Update() {
        std::memcpy(prev_, cur_, sizeof(cur_));
        for (int i = 1; i < 256; ++i)
            cur_[i] = (::GetAsyncKeyState(i) & 0x8000) != 0;
    }

    bool Down(int vk) const { return cur_[vk & 0xFF]; }
    bool Pressed(int vk) const { return cur_[vk & 0xFF] && !prev_[vk & 0xFF]; }

    int FirstPressed(int skipVk = 0) const {
        for (int i = 1; i < 256; ++i) {
            if (i == skipVk || i == VK_LBUTTON || i == VK_RBUTTON) continue;
            if (Pressed(i)) return i;
        }
        return 0;
    }

private:
    bool cur_[256]{};
    bool prev_[256]{};
};

namespace mouse {

inline void Move(float dx, float dy) {
    static float accX = 0.f;
    static float accY = 0.f;

    accX += dx;
    accY += dy;

    LONG mx = static_cast<LONG>(accX);
    LONG my = static_cast<LONG>(accY);
    if (mx == 0 && my == 0) return;

    accX -= static_cast<float>(mx);
    accY -= static_cast<float>(my);

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = mx;
    in.mi.dy = my;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    ::SendInput(1, &in, sizeof(in));
}

inline void Button(int flag) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = static_cast<DWORD>(flag);
    ::SendInput(1, &in, sizeof(in));
}

inline void LeftDown() { Button(MOUSEEVENTF_LEFTDOWN); }
inline void LeftUp() { Button(MOUSEEVENTF_LEFTUP); }

} // namespace mouse
