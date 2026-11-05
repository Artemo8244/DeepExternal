#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

inline constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(dot(*this)); }

    bool valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
               std::fabs(x) < 100000.f && std::fabs(y) < 100000.f && std::fabs(z) < 100000.f;
    }
};

// CS2 stores the view matrix row-major at client.dll + dwViewMatrix.
struct ViewMatrix {
    float m[4][4]{};

    bool sane() const {
        float sum = 0.f;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                if (!std::isfinite(m[r][c])) return false;
                sum += std::fabs(m[r][c]);
            }
        return sum > 0.001f;
    }

    bool worldToScreen(const Vec3& w, Vec2& out, float screenW, float screenH) const {
        const float ww = m[3][0] * w.x + m[3][1] * w.y + m[3][2] * w.z + m[3][3];
        if (ww < 0.001f) return false;

        const float cx = m[0][0] * w.x + m[0][1] * w.y + m[0][2] * w.z + m[0][3];
        const float cy = m[1][0] * w.x + m[1][1] * w.y + m[1][2] * w.z + m[1][3];

        out.x = (screenW * 0.5f) * (1.f + cx / ww);
        out.y = (screenH * 0.5f) * (1.f - cy / ww);

        return std::isfinite(out.x) && std::isfinite(out.y);
    }
};

// ---- angle helpers -------------------------------------------------------
// CS2 view angles are (pitch, yaw, roll) in degrees. Looking along -z gives a
// positive pitch, yaw grows to the right (clockwise seen from above).
inline float NormalizeDeg(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

inline Vec3 AnglesToTarget(const Vec3& eye, const Vec3& target) {
    const Vec3 d = target - eye;
    const float horizontal = std::sqrt(d.x * d.x + d.y * d.y);
    Vec3 out{};
    out.x = -std::atan2(d.z, horizontal) * (180.f / 3.14159265358979f); // pitch
    out.y = std::atan2(d.y, d.x) * (180.f / 3.14159265358979f);         // yaw
    out.z = 0.f;
    return out;
}

inline bool SaneAngles(const Vec3& a) {
    return a.valid() && a.x > -90.5f && a.x < 90.5f &&
           a.y > -360.5f && a.y < 360.5f;
}
