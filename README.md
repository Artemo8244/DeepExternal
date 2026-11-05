# DeepExternal

External CS2 overlay: rpm only, zero writes into the game, input through `SendInput`.

## Build

Open `DeepExternal.slnx` in Visual Studio 2026, pick `x64 | Release`, build.
Console output is the log; `DeepExternal.exe` can be started before CS2.

## Controls

| key | action |
| --- | --- |
| INSERT | menu |
| END | quit |
| mouse4 (default) | aimbot |
| mouse5 (default) | triggerbot |

Every key is rebindable from the menu (click the bind button, press a key, ESC cancels).

`DeepExternal.exe --uitest` renders the menu over the desktop for 12 seconds without touching
CS2 - handy for checking the overlay and rebinding flow.

`DeepExternal.exe --diag` attaches to a running CS2 and prints the reader state once per
second (view matrix, view angles, entity list, local pawn/controller, stride, bone offset).
Use it when something looks off after a game update.

## Offsets

All offsets live in `src/deps/`:

- `offsets.hpp` - `cs2_dumper::offsets::client_dll::*` (entity list, local pawn, local
  controller, view matrix, view angles, global vars)
- `client_dll.hpp` - `cs2_dumper::schemas::client_dll::*` (health, team, life state, scene
  node, model state, spotted state, controller pawn handle)

Snapshot: 2026-11-05, taken from the live `cs2.exe` build 26445667. Regenerate after every
CS2 update (the game updates roughly weekly):

```
cs2-dumper.exe -o src\deps -f hpp
```

then rebuild. The namespaces match the dumper output, so nothing else changes.

Two values are not part of the schema dump and are resolved at runtime:

- entity-list chunk stride (`ProbeStride`): tries 0x78/0x70/0x80/0x88/0x68/0x60/0x90 and
  keeps the one that matches the local pawn / local controller pointer and yields sane
  pawns (health 1..100, team 2..3). The 2026-11-05 build uses **0x70** - it changed from
  0x78, so hardcoding it breaks everything.
- bone-array pointer inside `CModelState` (`ProbeBoneArray`): tries 0x80/0x78/0x88/0x70/0x90/0x1E0

The console prints the chosen values once the player is alive:

```
[deep] stride 0x70  bones +0x80  local idx 12  enemies 9  angles ok
```

If that line never appears, run `--diag` and check `view`, `list`, `ctrl`, `handle`:
a zero `view` means the view matrix offset is stale, a zero `ctrl` means the controller
offset is stale.

## Threading / hangs

The layered window is owned by the main thread, which pumps messages every tick
(`PumpMessages`). Without the pump Windows flags the process as "not responding" after
~5 seconds even while `UpdateLayeredWindow` keeps producing frames. ESP is presented at
60 fps; aimbot and triggerbot run on the 4 ms tick independent of rendering.

## Visibility ("wall check")

CS2 no longer ships `.bsp` world geometry in the map vpk (Source 2 maps are
`.vmap_c`/`.vmdl_c`), so an external has nothing to raycast against. The visibility check
uses the engine's own `m_entitySpottedState` (`m_bSpotted` + `m_bSpottedByMask` bit for the
local player index) - the same signal the radar uses. It is LOS driven but lags by a network
tick, so expect a short tail when someone disappears behind cover. Both aimbot and triggerbot
have a "visible only" toggle if the lag gets in the way.

`m_bSpotted` + mask live at `C_CSPlayerPawn + 0x1E88` in the current dump.

## Structure

```
DeepExternal.cpp      entry point, tick loop, message pump, --uitest/--diag
src/config.hpp        settings + defaults
src/game.hpp          attach, entity walk, bones, spotted state
src/features.hpp      aimbot, triggerbot, esp
src/menu.hpp          in-overlay menu
src/overlay.hpp/.cpp  layered per-pixel-alpha window (GDI+)
src/input.hpp         keyboard state + SendInput mouse
src/deps/             cs2-dumper output
```

Aim is angle based: the target direction is converted to yaw/pitch deltas, the mouse is moved
a fraction of the delta (`smoothing`), and the counts-per-degree ratio is learned at runtime
from the previous move (so it works at any sensitivity). Nothing is ever written to the game.
If the view-angle pointer is unavailable, the aimbot falls back to pixel deltas from the view
matrix.
<img width="1919" height="1079" alt="изображение" src="https://github.com/user-attachments/assets/ddcd7292-b3e9-4ef9-b92d-74484a5b558b" />
