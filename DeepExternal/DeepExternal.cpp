
// DeepExternal - CS2 external overlay (read-only, SendInput based).
//
// Controls:
//   INSERT  - menu
//   END     - quit
//   mouse4  - aimbot key (rebindable)
//   mouse5  - triggerbot key (rebindable)
//
// Offsets: regenerate src/deps with cs2-dumper after every CS2 update, see
// README.md.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include <cstdio>

#include "src/config.hpp"
#include "src/features.hpp"
#include "src/game.hpp"
#include "src/input.hpp"
#include "src/menu.hpp"
#include "src/overlay.hpp"

#pragma comment(lib, "winmm.lib")

namespace {

bool g_running = true;

double NowMs() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) ::QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

void EnableDpiAwareness() {
    using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

// The overlay window is owned by this thread. Without a message pump Windows
// flags the process as "not responding" after ~5 s, because it never answers
// WM_NULL / WM_PAINT. Pump every tick, even when only drawing ESP.
void PumpMessages() {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_running = false;
            return;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

} // namespace

// UI smoke test: renders the menu over the desktop without touching CS2.
static int RunUiTest() {
    Overlay overlay;
    if (!overlay.Create(::GetDesktopWindow())) {
        printf("[ui] overlay create failed\n");
        return 1;
    }
    overlay.ClickThrough(true);

    Settings settings;
    Keyboard keyboard;
    Menu menu;
    menu.open = true;

    printf("[ui] rendering 12s\n");
    const double start = NowMs();
    while (NowMs() - start < 12000.0) {
        PumpMessages();
        keyboard.Update();
        overlay.Sync(::GetDesktopWindow());
        if (overlay.Begin()) {
            const float cx = static_cast<float>(overlay.width()) * 0.5f;
            const float cy = static_cast<float>(overlay.height()) * 0.5f;

            overlay.Circle(cx, cy, settings.aimFov, Color(150, 200, 210, 255), 1.f);
            overlay.Box(cx + 200.f, cy - 120.f, 70.f, 160.f, Color(255, 236, 82, 82), 1.4f);
            overlay.Line(cx + 210.f, cy - 100.f, cx + 235.f, cy - 20.f, Color(255, 236, 82, 82), 1.2f);
            overlay.Text(cx + 235.f, cy + 60.f, L"42m", Color(255, 236, 82, 82), 12.f, Overlay::Align::Center);

            menu.Render(overlay, settings, keyboard);
            overlay.Present();
        }
        Sleep(16);
    }

    overlay.Destroy();
    return 0;
}

// Console diagnostic: attach and print what the reader sees, once per second.
static int RunDiag() {
    Game game;
    if (!game.Attach()) {
        printf("[diag] attach failed (cs2.exe not running?)\n");
        return 1;
    }

    printf("[diag] pid %lu client 0x%llX window %p\n",
           static_cast<unsigned long>(game.pid()),
           static_cast<unsigned long long>(game.client()),
           static_cast<void*>(game.window()));

    for (int i = 0; i < 15; ++i) {
        if (!game.Update()) printf("[diag] update returned false\n");

        const GameDiag& d = game.diag();
        const Vec3& a = game.viewAngles();
        printf("[diag] view %d angles %d (%.2f %.2f) list 0x%llX ctrl 0x%llX handle %d "
               "pawn 0x%llX hp %d team %d node 0x%llX idx %d slot %d stride 0x%X score %d "
               "bones 0x%X enemies %zu size %dx%d\n",
               d.viewOk ? 1 : 0, d.anglesOk ? 1 : 0,
               static_cast<double>(a.x), static_cast<double>(a.y),
               static_cast<unsigned long long>(d.listPtr),
               static_cast<unsigned long long>(d.controller),
               d.pawnHandle,
               static_cast<unsigned long long>(d.localPawn),
               d.localHealth, d.localTeam,
               static_cast<unsigned long long>(d.localNode),
               d.localIndex, d.localSlot, d.stride, d.strideScore,
               static_cast<unsigned>(d.boneOffset),
               game.players().size(), game.width(), game.height());
        Sleep(1000);
    }
    return 0;
}

// Dump the entity slab: shows where the controller and the pawns actually live.
static int RunScan() {
    Game game;
    if (!game.Attach()) {
        printf("[scan] attach failed (cs2.exe not running?)\n");
        return 1;
    }
    for (int i = 0; i < 4; ++i) {
        game.Update();
        Sleep(300);
    }
    printf("[scan] stride 0x%X bones 0x%X\n",
           game.entityStride(), static_cast<unsigned>(game.boneOffset()));
    game.ScanEntities(2048);
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    ::timeBeginPeriod(1); // make Sleep(4) actually sleep ~4 ms
    EnableDpiAwareness();

    if (argc >= 2 && _wcsicmp(argv[1], L"--scan") == 0) {
        const int rc = RunScan();
        ::timeEndPeriod(1);
        return rc;
    }

    if (argc >= 2 && _wcsicmp(argv[1], L"--diag") == 0) {
        const int rc = RunDiag();
        ::timeEndPeriod(1);
        return rc;
    }

    if (argc >= 2 && _wcsicmp(argv[1], L"--uitest") == 0) {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        const int rc = RunUiTest();
        Gdiplus::GdiplusShutdown(token);
        ::timeEndPeriod(1);
        return rc;
    }

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr) != Gdiplus::Ok) {
        printf("[deep] gdiplus init failed\n");
        ::timeEndPeriod(1);
        return 1;
    }

    Settings   settings;
    Keyboard   keyboard;
    Game       game;
    Overlay    overlay;
    Menu       menu;
    Aimbot     aimbot;
    Triggerbot triggerbot;

    const double frameInterval = 1000.0 / static_cast<double>(config::kRenderFps);

    double lastRender = 0.0;
    double nextStatus = 0.0;
    double nextProbeWarn = 0.0;
    bool   inMatch = false;
    bool   reportedReady = false;

    printf("[deep] waiting for %ls\n", config::kProcessName);

    while (g_running) {
        PumpMessages();

        keyboard.Update();
        if (keyboard.Pressed(config::kExitKey)) break;

        if (!game.attached()) {
            if (!game.Attach()) {
                Sleep(300);
                continue;
            }
            printf("[deep] attached to pid %lu\n", static_cast<unsigned long>(game.pid()));
        }

        if (!game.Update()) {
            printf("[deep] lost cs2\n");
            triggerbot.Release();
            overlay.Destroy();
            game.Detach();
            inMatch = false;
            reportedReady = false;
            Sleep(300);
            continue;
        }

        const GameDiag& diag = game.diag();
        const double now = NowMs();

        if (game.window() && !overlay.created()) {
            if (overlay.Create(game.window())) printf("[deep] overlay ready\n");
        }

        // The window shows up a few seconds after the process; keep retrying.
        if (overlay.created() && !game.window()) overlay.Destroy();

        if (keyboard.Pressed(config::kMenuKey)) menu.Toggle(overlay);

        const bool foreground = game.IsForeground();
        if (overlay.created()) {
            if (game.window() && (foreground || menu.open))
                overlay.Sync(game.window());
            else
                overlay.Hide();
        }

        if (diag.localPawn && (diag.localTeam == 2 || diag.localTeam == 3) && !inMatch) {
            inMatch = true;
            printf("[deep] in match: pawn 0x%llX hp %d team %d\n",
                   static_cast<unsigned long long>(diag.localPawn),
                   diag.localHealth, diag.localTeam);
        } else if ((!diag.localPawn || diag.localTeam < 2 || diag.localTeam > 3) && inMatch) {
            inMatch = false;
            reportedReady = false;
            printf("[deep] left match\n");
        }

        if (diag.localPawn && diag.localHealth > 0) {
            if (!reportedReady && diag.stride) {
                reportedReady = true;
                printf("[deep] stride 0x%X  bones +0x%X  local idx %d  enemies %zu  angles %s\n",
                       diag.stride, static_cast<unsigned>(diag.boneOffset),
                       diag.localIndex, game.players().size(),
                       diag.anglesOk ? "ok" : "missing");
            }
            if (!diag.stride && now >= nextProbeWarn) {
                nextProbeWarn = now + 3000.0;
                printf("[deep] entity stride not found (score %d, list 0x%llX, pawn 0x%llX hp %d)\n",
                       diag.strideScore,
                       static_cast<unsigned long long>(diag.listPtr),
                       static_cast<unsigned long long>(diag.localPawn),
                       diag.localHealth);
            }
            if (now >= nextStatus) {
                nextStatus = now + 5000.0;
                size_t vis = 0;
                for (const auto& p : game.players())
                    if (game.VisibleFromLocal(p)) ++vis;
                printf("[deep] status: hp %d team %d idx %d enemies %zu vis %zu stride 0x%X bones +0x%X view %d angles %d\n",
                       diag.localHealth, diag.localTeam, diag.localIndex,
                       game.players().size(), vis, diag.stride,
                       static_cast<unsigned>(diag.boneOffset),
                       diag.viewOk ? 1 : 0, diag.anglesOk ? 1 : 0);
            }
        } else if (now >= nextStatus) {
            nextStatus = now + 5000.0;
            printf("[deep] menu: view %d angles %d list 0x%llX ctrl 0x%llX handle %d pawn 0x%llX\n",
                   diag.viewOk ? 1 : 0, diag.anglesOk ? 1 : 0,
                   static_cast<unsigned long long>(diag.listPtr),
                   static_cast<unsigned long long>(diag.controller),
                   diag.pawnHandle,
                   static_cast<unsigned long long>(diag.localPawn));
        }

        const bool active = foreground && !menu.open && game.local() != nullptr;
        if (active) {
            aimbot.Run(game, settings, keyboard);
            triggerbot.Run(game, settings, keyboard, now);
        } else {
            triggerbot.Release();
        }

        const bool wantFrame = overlay.created() && (foreground || menu.open) &&
                               (settings.espEnabled || settings.aimDrawFov || menu.open);
        if (wantFrame && now - lastRender >= frameInterval) {
            lastRender = now;
            if (overlay.Begin()) {
                DrawFovCircle(overlay, game, settings);
                DrawEsp(game, settings, overlay);
                if (menu.open) menu.Render(overlay, settings, keyboard);
                overlay.Present();
            }
        } else if (!wantFrame && overlay.created()) {
            overlay.Hide();
        }

        Sleep(config::kTickMs);
    }

    triggerbot.Release();
    overlay.Destroy();
    game.Detach();
    Gdiplus::GdiplusShutdown(gdiToken);
    ::timeEndPeriod(1);
    return 0;
}
