#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.hpp"
#include "game.hpp"
#include "input.hpp"
#include "overlay.hpp"

namespace esp {
const Color kEnemy(235, 236, 82, 82);
const Color kTeammate(210, 92, 168, 255);
} // namespace esp

inline void DrawFovCircle(Overlay& ov, const Game& g, const Settings& s) {
    if (!s.aimEnabled || !s.aimDrawFov) return;
    if (g.width() <= 0 || g.height() <= 0) return;
    const float cx = static_cast<float>(g.width()) * 0.5f;
    const float cy = static_cast<float>(g.height()) * 0.5f;
    ov.Circle(cx, cy, s.aimFov, Color(150, 200, 210, 255), 1.f);
}

struct Aimbot {
    void Run(const Game& g, const Settings& s, const Keyboard& kb) {
        if (!s.aimEnabled || !kb.Down(s.aimKey)) {
            havePrev_ = false;
            return;
        }

        const Player* me = g.local();
        if (!me || !me->alive() || !me->eye.valid()) return;

        const ViewMatrix& vm = g.view();
        const float sw = static_cast<float>(g.width());
        const float sh = static_cast<float>(g.height());
        if (sw <= 0.f || sh <= 0.f) return;

        const float cx = sw * 0.5f;
        const float cy = sh * 0.5f;

        const Player* best = nullptr;
        Vec3  bestPoint{};
        Vec2  bestScreen{};
        float bestPx = 1e9f;

        for (const auto& p : g.players()) {
            if (!p.alive()) continue;
            if (s.aimTeam && p.team == me->team) continue;

            const Vec3 point = s.aimHead ? p.head() : p.chest();
            if (!point.valid()) continue;

            Vec2 sp{};
            if (!vm.worldToScreen(point, sp, sw, sh)) continue;

            const float dx = sp.x - cx;
            const float dy = sp.y - cy;
            const float dpx = std::sqrt(dx * dx + dy * dy);
            if (dpx > s.aimFov || dpx >= bestPx) continue;
            if (s.aimVisible && !g.VisibleFromLocal(p)) continue;

            best = &p;
            bestPoint = point;
            bestScreen = sp;
            bestPx = dpx;
        }

        if (!best) {
            havePrev_ = false;
            return;
        }

        if (g.anglesOk()) {
            AimByAngles(g, s, *me, bestPoint, bestScreen, cx, cy);
            return;
        }

        // Fallback: pixel delta from the view matrix. Works without the view
        // angle pointer, only slower to settle.
        const float dx = bestScreen.x - cx;
        const float dy = bestScreen.y - cy;

        const float smoothing = std::max(1.f, s.aimSmoothing);
        float mx = dx / smoothing;
        float my = dy / smoothing;

        if (std::fabs(mx) < 0.6f && std::fabs(my) < 0.6f) {
            mx = dx;
            my = dy;
        }
        if (std::fabs(mx) < 0.5f) mx = 0.f;
        if (std::fabs(my) < 0.5f) my = 0.f;
        mouse::Move(mx, my);
    }

private:
    void AimByAngles(const Game& g, const Settings& s, const Player& me,
                     const Vec3& point, const Vec2& sp, float cx, float cy) {
        const Vec3 cur = g.viewAngles();
        const Vec3 want = AnglesToTarget(me.eye, point);

        const float dYaw = NormalizeDeg(want.y - cur.y);
        const float dPitch = NormalizeDeg(want.x - cur.x);

        Learn(cur);

        if (std::fabs(dYaw) < 0.03f && std::fabs(dPitch) < 0.03f) {
            prevAngles_ = cur;
            havePrev_ = true;
            sentYaw_ = 0.f;
            sentPitch_ = 0.f;
            return;
        }

        // Direction comes from where the target sits on screen, so it stays
        // correct whatever sign convention the game uses for yaw. Magnitude
        // comes from the angle error, so the smoothing feels the same at every
        // sensitivity. counts-per-degree is still learned from the result.
        const float dirX = sp.x >= cx ? 1.f : -1.f;
        const float dirY = sp.y >= cy ? 1.f : -1.f;

        const float div = std::max(1.f, s.aimSmoothing);
        float mx = dirX * std::fabs(dYaw) / div * cpdYaw_;
        float my = dirY * std::fabs(dPitch) / div * cpdPitch_;

        // Fine adjustment: a single tick's smoothed step can round down to zero
        // while the crosshair is still visibly off the bone.
        if (std::fabs(mx) < 0.5f && std::fabs(my) < 0.5f) {
            mx = dirX * std::fabs(dYaw) * cpdYaw_;
            my = dirY * std::fabs(dPitch) * cpdPitch_;
        }

        mx = std::clamp(mx, -700.f, 700.f);
        my = std::clamp(my, -700.f, 700.f);
        if (std::fabs(mx) < 0.5f) mx = 0.f;
        if (std::fabs(my) < 0.5f) my = 0.f;
        if (mx == 0.f && my == 0.f) {
            prevAngles_ = cur;
            havePrev_ = true;
            sentYaw_ = 0.f;
            sentPitch_ = 0.f;
            return;
        }

        mouse::Move(mx, my);

        sentYaw_ = mx;
        sentPitch_ = my;
        prevAngles_ = cur;
        havePrev_ = true;
    }

    // Learn how many mouse counts CS2 turns into one degree. The ratio depends
    // on the user's sensitivity, so it is measured from the previous move.
    void Learn(const Vec3& cur) {
        if (!havePrev_) return;
        LearnAxis(sentYaw_, NormalizeDeg(cur.y - prevAngles_.y), cpdYaw_);
        LearnAxis(sentPitch_, NormalizeDeg(cur.x - prevAngles_.x), cpdPitch_);
    }

    static void LearnAxis(float sent, float moved, float& cpd) {
        if (std::fabs(sent) < 2.f) return;
        const float m = std::fabs(moved);
        if (m < 0.01f || m > 45.f) return;
        // Magnitude only: the sign of the yaw delta depends on the engine's
        // convention, but counts-per-degree does not.
        const float est = std::fabs(sent) / m;
        if (est < 2.f || est > 800.f) return;
        cpd = cpd * 0.7f + est * 0.3f;
    }

    Vec3  prevAngles_{};
    bool  havePrev_ = false;
    float sentYaw_ = 0.f;
    float sentPitch_ = 0.f;
    float cpdYaw_ = 22.f;    // counts per degree (sensitivity 2.0-ish)
    float cpdPitch_ = 22.f;
};

struct Triggerbot {
    // Crosshair target: the engine's own entity-under-crosshair index first,
    // screen-space proximity as a fallback if that field is empty.
    static const Player* CrosshairTarget(const Game& g, const Settings& s) {
        if (const Player* t = g.ByHandle(g.CrosshairHandle())) return t;

        const float sw = static_cast<float>(g.width());
        const float sh = static_cast<float>(g.height());
        if (sw <= 0.f || sh <= 0.f) return nullptr;

        const float cx = sw * 0.5f;
        const float cy = sh * 0.5f;
        const ViewMatrix& vm = g.view();
        const Player* me = g.local();

        const Player* best = nullptr;
        float bestDist = 18.f;

        for (const auto& p : g.players()) {
            if (!p.alive()) continue;
            if (s.triggerTeam && me && p.team == me->team) continue;

            Vec3 probes[4] = { p.head(), p.chest(),
                               p.origin + Vec3{ 0.f, 0.f, 40.f },
                               p.origin + Vec3{ 0.f, 0.f, 64.f } };

            for (const Vec3& point : probes) {
                if (!point.valid()) continue;
                Vec2 sp{};
                if (!vm.worldToScreen(point, sp, sw, sh)) continue;
                const float dx = sp.x - cx;
                const float dy = sp.y - cy;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < bestDist) {
                    bestDist = d;
                    best = &p;
                }
            }
        }
        return best;
    }

    void Run(const Game& g, const Settings& s, const Keyboard& kb,
             double nowMs) {
        const Player* me = g.local();

        const Player* target = nullptr;
        if (s.triggerEnabled && kb.Down(s.triggerKey) && me && me->alive()) {
            const Player* t = CrosshairTarget(g, s);
            if (t && t != me && t->alive()) {
                const bool teamOk = !s.triggerTeam || t->team != me->team;
                const bool visOk = !s.triggerVisible || g.VisibleFromLocal(*t);
                if (teamOk && visOk) target = t;
            }
        }

        if (!target) {
            engaged_ = false;
            if (fired_) {
                mouse::LeftUp();
                fired_ = false;
            }
            return;
        }

        if (!engaged_) {
            engaged_ = true;
            startMs_ = nowMs;
        }

        if (!fired_ && nowMs - startMs_ >= static_cast<double>(s.triggerDelay)) {
            mouse::LeftDown();
            fired_ = true;
            fireMs_ = nowMs;
        }

        if (fired_ && nowMs - fireMs_ >= static_cast<double>(s.triggerHold)) {
            mouse::LeftUp();
            fired_ = false;
            engaged_ = false; // re-arm, so the next burst waits for the delay again
        }
    }

    void Release() {
        engaged_ = false;
        if (fired_) {
            mouse::LeftUp();
            fired_ = false;
        }
    }

private:
    bool   engaged_ = false;
    bool   fired_ = false;
    double startMs_ = 0.0;
    double fireMs_ = 0.0;
};

inline void DrawEsp(const Game& g, const Settings& s, Overlay& ov) {
    if (!s.espEnabled) return;

    const Player* me = g.local();

    const ViewMatrix& vm = g.view();
    const float sw = static_cast<float>(g.width());
    const float sh = static_cast<float>(g.height());
    if (sw <= 0.f || sh <= 0.f) return;

    static const int kSkeleton[][2] = {
        { kBoneHead, kBoneNeck },
        { kBoneNeck, kBoneChest },
        { kBoneChest, kBoneSpine },
        { kBoneSpine, kBonePelvis },
        { kBoneNeck, kBoneLUpperArm },
        { kBoneLUpperArm, kBoneLLowerArm },
        { kBoneLLowerArm, kBoneLHand },
        { kBoneNeck, kBoneRUpperArm },
        { kBoneRUpperArm, kBoneRLowerArm },
        { kBoneRLowerArm, kBoneRHand },
        { kBonePelvis, kBoneLThigh },
        { kBoneLThigh, kBoneLCalf },
        { kBoneLCalf, kBoneLFoot },
        { kBonePelvis, kBoneRThigh },
        { kBoneRThigh, kBoneRCalf },
        { kBoneRCalf, kBoneRFoot },
    };

    for (const auto& p : g.players()) {
        if (!p.alive()) continue;
        const bool teammate = me && p.team == me->team;
        if (s.espTeam && me && teammate) continue;

        const Color color = teammate ? esp::kTeammate : esp::kEnemy;

        Vec2 head{}, feet{};
        if (!vm.worldToScreen(p.head(), head, sw, sh)) continue;
        if (!vm.worldToScreen(p.origin, feet, sw, sh)) continue;

        float boxH = feet.y - head.y;
        if (boxH < 10.f || boxH > 1400.f) continue;

        float boxW = boxH * 0.55f;
        float left = head.x - boxW * 0.5f;
        float top = head.y;

        if (s.espBox) {
            ov.Box(left, top, boxW, boxH, Color(140, 0, 0, 0), 3.f);
            ov.Box(left, top, boxW, boxH, color, 1.4f);
        }

        if (s.espSkeleton && p.bonesOk) {
            for (const auto& link : kSkeleton) {
                const Vec3& a = p.bones[link[0]];
                const Vec3& b = p.bones[link[1]];
                if (!a.valid() || !b.valid()) continue;

                Vec2 sa{}, sb{};
                if (!vm.worldToScreen(a, sa, sw, sh)) continue;
                if (!vm.worldToScreen(b, sb, sw, sh)) continue;

                ov.Line(sa.x, sa.y, sb.x, sb.y, Color(120, 0, 0, 0), 3.f);
                ov.Line(sa.x, sa.y, sb.x, sb.y, color, 1.2f);
            }
        }

        if (s.espHealth) {
            const float barH = boxH;
            const float hp = std::clamp(static_cast<float>(p.health) / 100.f, 0.f, 1.f);
            ov.Fill(left - 6.f, top - 1.f, 3.f, barH + 2.f, Color(170, 0, 0, 0));
            ov.Fill(left - 6.f, top + barH * (1.f - hp), 3.f, barH * hp,
                    Color(220, static_cast<BYTE>(255 * (1.f - hp)),
                         static_cast<BYTE>(255 * hp), 90));
        }

        if (s.espDistance) {
            wchar_t line[64]{};
            swprintf_s(line, L"%.0fm", static_cast<double>(p.dist) * 0.0254);
            ov.Text(left + boxW * 0.5f, top + boxH + 6.f, line, color, 12.f,
                    Overlay::Align::Center);
        }
    }
}
