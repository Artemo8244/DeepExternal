#pragma once

#include <windows.h>

namespace config {

// ---- loop / window -------------------------------------------------------
constexpr int           kTickMs      = 4;          // poll loop period
constexpr int           kRenderFps   = 60;         // overlay present rate cap
constexpr int           kMenuKey     = VK_INSERT;
constexpr int           kExitKey     = VK_END;

// ---- aimbot --------------------------------------------------------------
constexpr int           kAimKey      = VK_XBUTTON2; // mouse4
constexpr float         kFovPx       = 110.f;
constexpr bool          kDrawFov     = true;
constexpr float         kSmoothing   = 4.0f;        // 1 = snap, higher = slower
constexpr int           kHeadBone    = 6;
constexpr bool          kTeamCheck   = true;
constexpr bool          kVisibleOnly = true;

// ---- triggerbot ----------------------------------------------------------
constexpr int           kTriggerKey   = VK_XBUTTON1; // mouse5
constexpr int           kTriggerDelay = 40;          // ms before the shot
constexpr int           kTriggerHold  = 60;          // ms the button stays down

// ---- esp -----------------------------------------------------------------
constexpr bool          kEspTeamCheck = true;

// ---- process -------------------------------------------------------------
constexpr const wchar_t* kProcessName = L"cs2.exe";
constexpr const wchar_t* kModuleName  = L"client.dll";

} // namespace config

struct Settings {
    // aimbot
    bool  aimEnabled    = true;
    int   aimKey        = config::kAimKey;
    float aimFov        = config::kFovPx;
    bool  aimDrawFov    = config::kDrawFov;
    float aimSmoothing  = config::kSmoothing;
    bool  aimTeam       = config::kTeamCheck;
    bool  aimVisible    = config::kVisibleOnly;
    bool  aimHead       = true;            // true = head bone, false = chest

    // triggerbot
    bool  triggerEnabled = true;
    int   triggerKey     = config::kTriggerKey;
    float triggerDelay   = static_cast<float>(config::kTriggerDelay);
    float triggerHold    = static_cast<float>(config::kTriggerHold);
    bool  triggerTeam    = config::kTeamCheck;
    bool  triggerVisible = config::kVisibleOnly;

    // esp
    bool  espEnabled   = true;
    bool  espBox       = true;
    bool  espSkeleton  = true;
    bool  espHealth    = true;
    bool  espDistance  = true;
    bool  espTeam      = config::kEspTeamCheck;
};
