// Generated using https://github.com/a2x/cs2-dumper
// 2026-11-05 11:52:10.047512700 UTC
//
// Subset of the schema dump: only the fields this project touches.
// Regenerating with cs2-dumper replaces this file with the full dump; names and
// namespace nesting stay identical, so the build keeps working.

#pragma once

#include <cstddef>
#include <cstdint>

namespace cs2_dumper {
    namespace schemas {
        namespace client_dll {
            namespace C_BaseEntity {
                constexpr std::ptrdiff_t m_pGameSceneNode = 0x330; // CGameSceneNode*
                constexpr std::ptrdiff_t m_iHealth = 0x34C; // int32
                constexpr std::ptrdiff_t m_lifeState = 0x354; // uint8
                constexpr std::ptrdiff_t m_iTeamNum = 0x3E7; // uint8
            }
            namespace C_BaseModelEntity {
                constexpr std::ptrdiff_t m_vecViewOffset = 0xF60; // CNetworkViewOffsetVector
            }
            namespace CBasePlayerController {
                constexpr std::ptrdiff_t m_hPawn = 0x6BC; // CHandle<C_BasePlayerPawn>
            }
            namespace C_CSPlayerPawn {
                constexpr std::ptrdiff_t m_iIDEntIndex = 0x36DC; // CEntityIndex
                constexpr std::ptrdiff_t m_entitySpottedState = 0x1E88; // EntitySpottedState_t
            }
            namespace EntitySpottedState_t {
                constexpr std::ptrdiff_t m_bSpotted = 0x8; // bool
                constexpr std::ptrdiff_t m_bSpottedByMask = 0xC; // uint32[2]
            }
            namespace CGameSceneNode {
                constexpr std::ptrdiff_t m_vecAbsOrigin = 0xC8; // VectorWS
            }
            namespace CSkeletonInstance {
                constexpr std::ptrdiff_t m_modelState = 0x140; // CModelState
            }
        }
    }
}
