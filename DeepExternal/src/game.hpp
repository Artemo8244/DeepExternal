#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "config.hpp"
#include "deps/client_dll.hpp"
#include "deps/offsets.hpp"
#include "math.hpp"
#include "process.hpp"

namespace o_cl   = cs2_dumper::offsets::client_dll;
namespace s_ent  = cs2_dumper::schemas::client_dll::C_BaseEntity;
namespace s_bme  = cs2_dumper::schemas::client_dll::C_BaseModelEntity;
namespace s_ctrl = cs2_dumper::schemas::client_dll::CBasePlayerController;
namespace s_pawn = cs2_dumper::schemas::client_dll::C_CSPlayerPawn;
namespace s_node = cs2_dumper::schemas::client_dll::CGameSceneNode;
namespace s_skel = cs2_dumper::schemas::client_dll::CSkeletonInstance;
namespace s_spot = cs2_dumper::schemas::client_dll::EntitySpottedState_t;

constexpr int kMaxBones    = 128;
constexpr int kBoneStride  = 32;   // CTransform: Vector + quat, 32 bytes
constexpr int kMaxPlayers  = 64;

// Bone ids of the CS2 biped skeleton. Bone 6 is the head; if a model disagrees
// the lowest-level code falls back to the highest bone, see Player::headBone.
enum BoneId : int {
    kBonePelvis    = 0,
    kBoneSpine     = 3,
    kBoneChest     = 4,
    kBoneNeck      = 5,
    kBoneHead      = 6,
    kBoneLUpperArm = 8,
    kBoneLLowerArm = 9,
    kBoneLHand     = 10,
    kBoneRUpperArm = 13,
    kBoneRLowerArm = 14,
    kBoneRHand     = 15,
    kBoneLThigh    = 22,
    kBoneLCalf     = 23,
    kBoneLFoot     = 24,
    kBoneRThigh    = 25,
    kBoneRCalf     = 26,
    kBoneRFoot     = 27,
};

struct RawBone {
    float x, y, z;
    float pad[5];
};
static_assert(sizeof(RawBone) == kBoneStride, "CS2 bone stride must be 32 bytes");

struct Player {
    uintptr_t pawn = 0;
    uintptr_t node = 0;
    int       index = -1;
    int       health = 0;
    int       team = 0;
    int       lifeState = 0;
    float     dist = 0.f;
    Vec3      origin{};
    Vec3      eye{};
    Vec3      bones[kMaxBones]{};
    bool      bonesOk = false;
    int       headBone = kBoneHead;
    bool      spotted = false;
    uint32_t  spottedMask[2]{};

    bool alive() const { return health > 0 && health <= 100 && lifeState == 0; }

    Vec3 head() const {
        if (bonesOk && headBone >= 0 && headBone < kMaxBones) {
            const Vec3 b = bones[headBone];
            if (b.valid()) return b;
        }
        return origin + Vec3{ 0.f, 0.f, 72.f };
    }

    Vec3 chest() const {
        if (bonesOk) {
            const Vec3 b = bones[kBoneChest];
            if (b.valid()) return b;
        }
        return origin + Vec3{ 0.f, 0.f, 54.f };
    }
};

// Snapshot of the last Update, for the console status line. Purely diagnostic.
struct GameDiag {
    bool      viewOk = false;
    bool      anglesOk = false;
    uintptr_t listPtr = 0;
    uintptr_t localPawn = 0;
    uintptr_t controller = 0;
    int       pawnHandle = 0;
    uintptr_t localNode = 0;
    int       localHealth = -1;
    int       localTeam = -1;
    int       localIndex = 0;
    int       localSlot = -1;
    uint32_t  stride = 0;
    int       strideScore = 0;
    std::ptrdiff_t boneOffset = 0;
};

class Game {
public:
    bool Attach() {
        const DWORD pid = proc::FindProcess(config::kProcessName);
        if (!pid) return false;
        if (!p_.Open(pid)) return false;

        client_ = proc::ModuleBase(pid, config::kModuleName);
        if (!client_) {
            p_.Close();
            return false;
        }

        pid_ = pid;
        hwnd_ = FindGameWindow(pid);
        stride_ = 0;
        boneArrayOffset_ = 0;
        lastStrideProbe_ = 0;
        lastBoneProbe_ = 0;
        lastWindowSearch_ = 0;
        return true;
    }

    void Detach() {
        p_.Close();
        client_ = 0;
        pid_ = 0;
        hwnd_ = nullptr;
        localPtr_ = nullptr;
        players_.clear();
        stride_ = 0;
        boneArrayOffset_ = 0;
        localIndex_ = 0;
        localSlot_ = -1;
    }

    bool attached() const { return p_.IsOpen() && client_ != 0; }

    bool Update() {
        if (!attached()) return false;
        if (!p_.Alive()) return false;

        const ULONGLONG now = ::GetTickCount64();
        diag_ = GameDiag{};

        // The window appears a few seconds after cs2.exe and is recreated when
        // the player changes resolution / display mode, so keep resolving it.
        RefreshWindow();

        list_ = p_.ReadOr<uintptr_t>(client_ + o_cl::dwEntityList);
        diag_.listPtr = list_;
        diag_.localPawn = p_.ReadOr<uintptr_t>(client_ + o_cl::dwLocalPlayerPawn);
        diag_.controller = p_.ReadOr<uintptr_t>(client_ + o_cl::dwLocalPlayerController);
        if (diag_.controller && diag_.controller > 0x10000)
            diag_.pawnHandle = p_.ReadOr<int32_t>(diag_.controller + s_ctrl::m_hPawn, 0);

        players_.clear();
        localPtr_ = nullptr;
        localIndex_ = 0;
        localSlot_ = -1;

        if (!p_.Read(client_ + o_cl::dwViewMatrix, view_) || !view_.sane()) {
            // Main menu / map load: the matrix is zeroed. Stay attached instead
            // of dropping the process, otherwise the overlay flickers away.
            return true;
        }
        diag_.viewOk = true;

        Vec3 angles{};
        diag_.anglesOk = p_.Read(client_ + o_cl::dwViewAngles, angles) && SaneAngles(angles);
        viewAngles_ = diag_.anglesOk ? angles : Vec3{};

        RefreshWindowSize();

        if (!stride_ && now - lastStrideProbe_ >= 200) {
            lastStrideProbe_ = now;
            ProbeStride();
        }
        diag_.stride = stride_;
        diag_.strideScore = strideScore_;

        if (!stride_) return true;

        uintptr_t pawn = diag_.localPawn;
        if (!pawn || pawn < 0x10000) pawn = LocalPawnFromController();
        if (!pawn) return true;
        diag_.localPawn = pawn;

        diag_.localHealth = p_.ReadOr<int32_t>(pawn + s_ent::m_iHealth, -1);
        diag_.localTeam = p_.ReadOr<uint8_t>(pawn + s_ent::m_iTeamNum, 0);
        diag_.localNode = p_.ReadOr<uintptr_t>(pawn + s_ent::m_pGameSceneNode);

        Player local;
        if (ReadPlayer(pawn, -1, local)) {
            localPlayer_ = local;
            localPtr_ = &localPlayer_;
        }

        if (localPtr_ && localPtr_->alive() && !boneArrayOffset_ &&
            now - lastBoneProbe_ >= 200) {
            lastBoneProbe_ = now;
            ProbeBoneArray(localPtr_->node);
        }
        diag_.boneOffset = boneArrayOffset_;

        // Players are reached through their controllers: controller entities
        // live at the low indices (1..64, one per slot), while the pawn entities
        // themselves sit at arbitrary high indices (e.g. 303), so scanning
        // 1..64 for pawns finds nothing on current CS2 builds.
        for (int i = 1; i <= kMaxPlayers; ++i) {
            const uintptr_t ctrl = EntityByIndex(i);
            if (!ctrl || ctrl < 0x10000) continue;

            const int32_t handle = p_.ReadOr<int32_t>(ctrl + s_ctrl::m_hPawn, 0);
            if (handle <= 0) continue;

            const int pawnIndex = handle & 0x7FFF;
            const uintptr_t ent = EntityByIndex(pawnIndex);
            if (!ent || ent < 0x10000) continue;

            Player p;
            if (!ReadPlayer(ent, pawnIndex, p)) continue;

            if (ent == pawn) {
                localSlot_ = i - 1;
                localIndex_ = pawnIndex;
                continue;
            }
            if (!p.alive()) continue;
            if (localPtr_) p.dist = (p.origin - localPtr_->origin).length();
            players_.push_back(p);
        }

        if (localIndex_ <= 0) localIndex_ = IndexFromController();
        if (localSlot_ < 0 && diag_.controller)
            localSlot_ = ControllerIndexOf(diag_.controller);
        diag_.localIndex = localIndex_;
        diag_.localSlot = localSlot_;
        return true;
    }

    // Re-enumerate windows every 250 ms until the real game window shows up.
    void RefreshWindow() {
        const ULONGLONG now = ::GetTickCount64();
        if (hwnd_ && ::IsWindow(hwnd_)) return;
        if (now - lastWindowSearch_ < 250) return;
        lastWindowSearch_ = now;

        HWND fg = ::GetForegroundWindow();
        if (fg) {
            DWORD pid = 0;
            ::GetWindowThreadProcessId(fg, &pid);
            if (pid == pid_) {
                hwnd_ = fg;
                return;
            }
        }
        hwnd_ = FindGameWindow(pid_);
    }

    // Foreground detection by process, not by hwnd equality: CS2 sometimes
    // swaps between a splash/"SDL_app" window and the render window.
    bool IsForeground() const {
        HWND fg = ::GetForegroundWindow();
        if (!fg) return false;
        if (fg == hwnd_) return true;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(fg, &pid);
        return pid != 0 && pid == pid_;
    }

    uintptr_t     client() const { return client_; }
    DWORD         pid() const { return pid_; }
    HWND          window() const { return hwnd_; }
    int           width() const { return width_; }
    int           height() const { return height_; }
    const ViewMatrix& view() const { return view_; }
    bool          anglesOk() const { return diag_.anglesOk; }
    const Vec3&   viewAngles() const { return viewAngles_; }
    const Player* local() const { return localPtr_; }
    const std::vector<Player>& players() const { return players_; }
    const GameDiag& diag() const { return diag_; }

    const Player* ByIndex(int index) const {
        for (const auto& p : players_)
            if (p.index == index) return &p;
        return nullptr;
    }

    // m_iIDEntIndex holds an entity index; some builds bias it by one.
    const Player* ByHandle(int handle) const {
        if (handle <= 0) return nullptr;
        const int raw = handle & 0x7FFF;
        if (const Player* p = ByIndex(raw)) return p;
        if (const Player* p = ByIndex(raw - 1)) return p;
        return nullptr;
    }

    int CrosshairHandle() const {
        if (!localPtr_) return 0;
        return p_.ReadOr<int32_t>(localPtr_->pawn + s_pawn::m_iIDEntIndex, 0);
    }

    // Visibility as the engine tracks it: the spotted state / spotted-by-mask
    // bit for the local player. CS2 maps ship no .bsp geometry any more, so an
    // external has nothing to raycast against; this is the radar signal.
    bool VisibleFromLocal(const Player& target) const {
        if (target.spotted) return true;
        if (localSlot_ < 0) return true; // slot unknown: do not filter
        // m_bSpottedByMask is a per-player-slot bitfield. Some builds key it by
        // the slot itself, others by slot+1, so test both.
        for (int bit : { localSlot_, localSlot_ + 1 }) {
            if (bit < 0 || bit >= 64) continue;
            const int word = bit >> 5;
            if (target.spottedMask[word] & (1u << (bit & 31))) return true;
        }
        return false;
    }

    int localIndex() const { return localIndex_; }
    uint32_t entityStride() const { return stride_; }
    std::ptrdiff_t boneOffset() const { return boneArrayOffset_; }

    // Diagnostic: dump the entity slab so the player/controller layout can be
    // inspected after an update.
    void ScanEntities(int maxIndex = 2048) const {
        const int highest = p_.ReadOr<int32_t>(
            list_ + o_cl::dwGameEntitySystem_highestEntityIndex, -1);
        printf("[scan] highest %d stride 0x%X ctrl 0x%llX pawn 0x%llX\n",
               highest, stride_,
               static_cast<unsigned long long>(diag_.controller),
               static_cast<unsigned long long>(diag_.localPawn));

        printf("[scan] --- controllers ---\n");
        for (int i = 1; i <= kMaxPlayers; ++i) {
            const uintptr_t c = EntityByIndex(i);
            if (!c || c < 0x10000) continue;
            const int32_t handle = p_.ReadOr<int32_t>(c + s_ctrl::m_hPawn, 0);
            if (handle <= 0) continue;
            const int pawnIndex = handle & 0x7FFF;
            const uintptr_t pe = EntityByIndex(pawnIndex);
            const int hp = (pe && pe >= 0x10000)
                               ? p_.ReadOr<int32_t>(pe + s_ent::m_iHealth, -1) : -999;
            const int team = (pe && pe >= 0x10000)
                                 ? p_.ReadOr<uint8_t>(pe + s_ent::m_iTeamNum, 0) : -1;
            const int life = (pe && pe >= 0x10000)
                                 ? p_.ReadOr<uint8_t>(pe + s_ent::m_lifeState, 0xFF) : -1;
            printf("[scan] ctrl %2d ent 0x%llX handle %d -> pawn %4d ent 0x%llX "
                   "hp %3d team %d life %d\n",
                   i, static_cast<unsigned long long>(c), handle, pawnIndex,
                   static_cast<unsigned long long>(pe), hp, team, life);
        }

        int shown = 0;
        for (int i = 1; i <= maxIndex && shown < 256; ++i) {
            const uintptr_t e = EntityByIndex(i);
            if (!e || e < 0x10000) continue;

            const int hp = p_.ReadOr<int32_t>(e + s_ent::m_iHealth, -1);
            const int team = p_.ReadOr<uint8_t>(e + s_ent::m_iTeamNum, -1);
            const int handle = p_.ReadOr<int32_t>(e + s_ctrl::m_hPawn, 0);

            const bool pawnish = (hp >= 0 && hp <= 100 && team >= 2 && team <= 3);
            if (!pawnish && e != diag_.controller && e != diag_.localPawn) continue;

            printf("[scan] idx %4d ent 0x%llX hp %3d team %2d handle %d%s%s\n",
                   i, static_cast<unsigned long long>(e), hp, team, handle,
                   e == diag_.controller ? "  <- controller" : "",
                   e == diag_.localPawn ? "  <- pawn" : "");
            ++shown;
        }
    }

private:
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM param) {
        auto* ctx = reinterpret_cast<FindCtx*>(param);
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ctx->pid || !::IsWindowVisible(hwnd)) return TRUE;

        RECT rc{};
        if (!::GetClientRect(hwnd, &rc)) return TRUE;
        const long area = (rc.right - rc.left) * (rc.bottom - rc.top);
        if (area <= 0) return TRUE;

        if (area > ctx->area) {
            ctx->area = area;
            ctx->hwnd = hwnd;
        }
        return TRUE;
    }

    struct FindCtx {
        DWORD pid = 0;
        HWND  hwnd = nullptr;
        long  area = 0;
    };

    static HWND FindGameWindow(DWORD pid) {
        if (!pid) return nullptr;
        FindCtx ctx{ pid, nullptr, 0 };
        ::EnumWindows(&Game::EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.hwnd;
    }

    void RefreshWindowSize() {
        if (!hwnd_) return;
        if (::IsIconic(hwnd_)) {
            width_ = 0;
            height_ = 0;
            return;
        }
        RECT rc{};
        if (!::GetClientRect(hwnd_, &rc)) return;
        width_ = rc.right - rc.left;
        height_ = rc.bottom - rc.top;
    }

    uintptr_t EntityByIndex(int index, uint32_t stride) const {
        if (!stride || index < 0 || index >= 0x8000) return 0;
        if (!list_) return 0;

        const uintptr_t chunk = p_.ReadOr<uintptr_t>(list_ + 8 * (index >> 9) + 0x10);
        if (!chunk) return 0;

        return p_.ReadOr<uintptr_t>(chunk + stride * (index & 0x1FF));
    }

    uintptr_t EntityByIndex(int index) const { return EntityByIndex(index, stride_); }

    bool SaneEntity(uintptr_t entity) const {
        if (!entity || entity < 0x10000) return false;
        const int health = p_.ReadOr<int32_t>(entity + s_ent::m_iHealth, 0);
        if (health < 1 || health > 100) return false;
        const uint8_t team = p_.ReadOr<uint8_t>(entity + s_ent::m_iTeamNum, 0);
        if (team < 2 || team > 3) return false;
        return true;
    }

    // Entity handles: low 15 bits are the index, +1 biased in some builds.
    int ReadLocalHandle() const {
        const uintptr_t ctrl = p_.ReadOr<uintptr_t>(client_ + o_cl::dwLocalPlayerController);
        if (!ctrl || ctrl < 0x10000) return -1;
        const int32_t handle = p_.ReadOr<int32_t>(ctrl + s_ctrl::m_hPawn, 0);
        if (handle <= 0) return -1;
        return handle & 0x7FFF;
    }

    int IndexFromController() const {
        const int idx = ReadLocalHandle();
        if (idx <= 0) return 0;
        for (int cand : { idx, idx - 1 }) {
            if (cand <= 0) continue;
            if (EntityByIndex(cand) == diag_.localPawn) return cand;
        }
        return 0;
    }

    uintptr_t LocalPawnFromController() const {
        const int idx = ReadLocalHandle();
        if (idx <= 0) return 0;
        for (int cand : { idx, idx - 1 }) {
            if (cand <= 0) continue;
            const uintptr_t entity = EntityByIndex(cand);
            if (entity && entity >= 0x10000) {
                const int health = p_.ReadOr<int32_t>(entity + s_ent::m_iHealth, -1);
                if (health >= 0 && health <= 100) return entity;
            }
        }
        return 0;
    }

    // Zero-based player slot of a controller entity (controllers occupy the
    // low entity indices, one per slot).
    int ControllerIndexOf(uintptr_t controller) const {
        if (!controller || controller < 0x10000) return -1;
        for (int i = 1; i <= kMaxPlayers; ++i)
            if (EntityByIndex(i) == controller) return i - 1;
        return -1;
    }

    // The identity stride inside an entity-list chunk is not part of the schema
    // dump and moved between updates. Probe it without needing a match: either
    // match a known pawn pointer, or score chunks by the pawns they produce.
    void ProbeStride() {
        const uintptr_t known = diag_.localPawn;
        const uintptr_t controller = diag_.controller;
        const int handleIdx = ReadLocalHandle();

        uint32_t bestStride = 0;
        int bestScore = 0;

        for (uint32_t candidate : { 0x78u, 0x70u, 0x80u, 0x88u, 0x68u, 0x60u, 0x90u }) {
            int score = 0;

            if (known && known > 0x10000) {
                if (handleIdx > 0) {
                    for (int d : { 0, -1 }) {
                        if (handleIdx + d > 0 &&
                            EntityByIndex(handleIdx + d, candidate) == known)
                            score += 64;
                    }
                }
                for (int i = 1; i <= kMaxPlayers; ++i)
                    if (EntityByIndex(i, candidate) == known) score += 64;
            }

            // The local controller is an entity too, so it pinpoints the stride
            // even at the main menu where no player pawn exists yet.
            if (controller && controller > 0x10000) {
                for (int i = 1; i <= kMaxPlayers; ++i)
                    if (EntityByIndex(i, candidate) == controller) score += 48;
            }

            for (int i = 1; i <= kMaxPlayers; ++i)
                if (SaneEntity(EntityByIndex(i, candidate))) ++score;

            if (score > bestScore) {
                bestScore = score;
                bestStride = candidate;
            }
        }

        stride_ = bestScore > 0 ? bestStride : 0;
        strideScore_ = bestScore;
    }

    bool TestBoneArray(uintptr_t node, const Vec3& origin, std::ptrdiff_t offset) const {
        if (!node) return false;
        const uintptr_t array = p_.ReadOr<uintptr_t>(node + s_skel::m_modelState + offset);
        if (!array || array < 0x10000) return false;

        RawBone raw[16]{};
        if (!p_.ReadRaw(array, raw, sizeof(raw))) return false;

        int close = 0;
        int above = 0;
        for (const auto& b : raw) {
            const Vec3 v{ b.x, b.y, b.z };
            if (!v.valid()) return false;
            const float d = (v - origin).length();
            if (d < 200.f) ++close;
            if (v.z - origin.z > 40.f && v.z - origin.z < 120.f) ++above;
        }
        return close >= 8 && above >= 1;
    }

    void ProbeBoneArray(uintptr_t node) {
        if (!node || !localPtr_) return;
        // 0x80 is the CModelState.m_pBoneArray slot on current builds; the rest
        // are historical / third-party skeleton variants.
        for (std::ptrdiff_t off : { std::ptrdiff_t(0x80), std::ptrdiff_t(0x78),
                                    std::ptrdiff_t(0x88), std::ptrdiff_t(0x70),
                                    std::ptrdiff_t(0x90), std::ptrdiff_t(0x1E0) }) {
            if (TestBoneArray(node, localPtr_->origin, off)) {
                boneArrayOffset_ = off;
                return;
            }
        }
    }

    bool ReadBones(uintptr_t node, Player& out) const {
        if (!boneArrayOffset_ || !node) return false;

        const uintptr_t array = p_.ReadOr<uintptr_t>(node + s_skel::m_modelState + boneArrayOffset_);
        if (!array || array < 0x10000) return false;

        RawBone raw[kMaxBones]{};
        if (!p_.ReadRaw(array, raw, sizeof(raw))) return false;

        // Trailing entries past the model's bone count can hold junk; treat any
        // bone that is not finite and near the model as missing (NaN sentinel).
        int valid = 0;
        float bestZ = -1e9f;
        int bestIdx = -1;

        for (int i = 0; i < kMaxBones; ++i) {
            const Vec3 v{ raw[i].x, raw[i].y, raw[i].z };
            if (!v.valid() || (v - out.origin).length() > 300.f) {
                out.bones[i] = Vec3{ kNan, kNan, kNan };
                continue;
            }
            out.bones[i] = v;
            ++valid;
            if (v.z > bestZ) {
                bestZ = v.z;
                bestIdx = i;
            }
        }

        out.bonesOk = valid >= 20;
        if (!out.bonesOk) return false;

        // Bone 6 is the head on every stock CS2 player model. If the model
        // disagrees (custom models, odd poses) fall back to the highest bone.
        const Vec3 head = out.bones[config::kHeadBone];
        const float dz = head.z - out.origin.z;
        out.headBone = (!head.valid() || dz < 40.f || dz > 110.f) && bestIdx >= 0
                           ? bestIdx
                           : config::kHeadBone;
        return true;
    }

    bool ReadPlayer(uintptr_t pawn, int index, Player& out) const {
        if (!pawn || pawn < 0x10000) return false;

        const int health = p_.ReadOr<int32_t>(pawn + s_ent::m_iHealth, -1);
        if (health < 0 || health > 100) return false;

        const uint8_t team = p_.ReadOr<uint8_t>(pawn + s_ent::m_iTeamNum, 0);
        if (team > 3) return false;

        const uint8_t lifeState = p_.ReadOr<uint8_t>(pawn + s_ent::m_lifeState, 0xFF);

        const uintptr_t node = p_.ReadOr<uintptr_t>(pawn + s_ent::m_pGameSceneNode);
        if (!node || node < 0x10000) return false;

        Vec3 origin;
        if (!p_.Read(node + s_node::m_vecAbsOrigin, origin) || !origin.valid()) return false;

        out.pawn = pawn;
        out.node = node;
        out.index = index;
        out.health = health;
        out.team = team;
        out.lifeState = lifeState;
        out.origin = origin;

        Vec3 viewOffset;
        if (p_.Read(pawn + s_bme::m_vecViewOffset, viewOffset) && viewOffset.valid())
            out.eye = origin + viewOffset;
        else
            out.eye = origin + Vec3{ 0.f, 0.f, 64.f };

        const uintptr_t spotted = pawn + s_pawn::m_entitySpottedState;
        out.spotted = p_.ReadOr<uint8_t>(spotted + s_spot::m_bSpotted, 0) != 0;
        p_.ReadRaw(spotted + s_spot::m_bSpottedByMask, out.spottedMask, sizeof(out.spottedMask));

        ReadBones(node, out);
        return true;
    }

    proc::Process p_;
    uintptr_t     client_ = 0;
    DWORD         pid_ = 0;
    HWND          hwnd_ = nullptr;

    ViewMatrix    view_{};
    Vec3          viewAngles_{};
    int           width_ = 0;
    int           height_ = 0;

    Player        localPlayer_{};
    const Player* localPtr_ = nullptr;
    std::vector<Player> players_;

    GameDiag      diag_{};
    mutable uintptr_t list_ = 0;

    uint32_t      stride_ = 0;
    int           strideScore_ = 0;
    std::ptrdiff_t boneArrayOffset_ = 0;
    int           localIndex_ = 0;
    int           localSlot_ = -1;

    ULONGLONG     lastStrideProbe_ = 0;
    ULONGLONG     lastBoneProbe_ = 0;
    ULONGLONG     lastWindowSearch_ = 0;
};
