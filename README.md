# Delta Force: Black Hawk Down — Unreal Engine 5.8 port

> **Disclaimer.** This is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by or
> sponsored by NovaLogic or any current owner of the *Delta Force* franchise. The game assets in this repository
> (models, textures, animations, sounds, fonts, maps and game data) were extracted from the retail game and remain the
> property of their owners. See [Legal](#legal) below.

A port of *Delta Force: Black Hawk Down – Team Sabre* to Unreal Engine 5.8 C++, built from the original game's assets
and the gameplay rules recovered from `DFBHD.EXE`: 62 Hz soldier tick, animation-driven movement speeds, stances, lean,
prone roll, jump and fall, weapon fire, spread, recoil, reload, sights and the full weapon selection / switch / deploy
state machine.

You play a Delta operator with the original first-person arms and weapons (driven by the game's own skeletal
animations), the third-person soldier with the original run / crouch / prone / jump / roll cycles, the original HUD
and the original sounds, on two maps:

* `DF_Compound`: a desert compound test map.
* `DF_PanicAttack`: the multiplayer map *Panic Attack* (`TDMM_01e.bms`), with the real terrain heightfield and all placed
  buildings and props.

Weapons:

| Key | Weapon | Notes |
|---|---|---|
| 1 | Knife | kill zone in front of the player |
| 2 | Colt .45 | semi-automatic, 7 / 21 rounds |
| 3 | CAR-15 | spawn weapon; press 3 again to switch auto (531 rpm) / semi; 30 / 270 rounds |
| 4 | Flashbang | 5; white-out and deafness |
| 5 | Frag grenade | 5 |
| 6 | Smoke grenade | 3; the cloud blocks vision |

## Requirements

* Unreal Engine 5.8 and Visual Studio 2022 with the C++ game development workload (Windows).
* [Git LFS](https://git-lfs.com/): `.uasset`, `.umap` and the source art are stored in LFS. Run `git lfs install`
  before cloning.

## Build

```
<UE_5.8>\Engine\Build\BatchFiles\Build.bat ProjectDFEditor Win64 Development -Project="<repo>\ProjectDF.uproject" -WaitMutex
```

or right-click `ProjectDF.uproject` → *Generate Visual Studio project files* and build `ProjectDFEditor` from the IDE.

## Play

* Editor: open `ProjectDF.uproject`. The startup map is `/Game/DF/Maps/DF_Compound`. Press Play and click into the
  viewport. `BP_DFGameMode` is the global default game mode.
* Standalone (uncooked):
  `<UE_5.8>\Engine\Binaries\Win64\UnrealEditor.exe <repo>\ProjectDF.uproject /Game/DF/Maps/DF_Compound -game -windowed -ResX=1280 -ResY=720`.
  Use `/Game/DF/Maps/PanicAttack/DF_PanicAttack` for Panic Attack.
* Listen server: add `?listen` to the map (`/Game/DF/Maps/DF_Compound?listen`); clients join with
  `UnrealEditor.exe <repo>\ProjectDF.uproject 127.0.0.1 -game`.

| Input | Action |
|---|---|
| W A S D | move (speed comes from the playing animation, like the original) |
| Mouse | look |
| Space | jump |
| Z / X / C | prone / crouch / stand (the original keys) |
| Q / E | lean left / right (hold); roll sideways when prone |
| Left mouse | fire; grenades: throw at full power |
| Right mouse | sights up / down (sighted weapons); grenades: hold to set the power, release to throw |
| R | reload |
| 1 – 6 | select a weapon category (see the table above) |
| Mouse wheel, `[` / `]` | previous / next weapon |
| V or F1 | first / third person |
| F6 | HUD detail level |
| `=` / `-` | map zoom |
| F3 | FPS counter |

## Tests

* Automation tests (weapon state machine, grenade flight, effects):
  `<UE_5.8>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <repo>\ProjectDF.uproject -ExecCmds="Automation RunTests DF.Weapons+DF.Effects;Quit" -unattended -nullrhi -nosplash`
* Scripted game runs: add `-DFShot=<absolute path.png> -DFShotDelay=<s>` to the standalone command to take a
  screenshot after that many seconds and quit. Other flags: `-DFView=3 -DFYaw=<deg> -DFPitch=<deg>
  -DFStance=stand|crouch|prone -DFLean=-1|1 -DFPos=x,z -DFWalk -DFFire -DFReload -DFAds -DFRoll=-1|1 -DFJump=<s>
  -DFCam=dx,dy,dz,lx,ly,lz -DFWarp=<s> -DFWeapon=<name> -DFKeys=<t>:<key>,... -DFDbg`. `-DFDbg` logs a `DBG {...}`
  state line every second and `DFWPN {...}` weapon events. Real mouse input is ignored in test runs.

## Layout

| Path | What |
|---|---|
| `Source/ProjectDF/DF/` | the port: `DFPlayerPawn` (player, input, weapons glue), `DFCharacterMovementComponent` (the 62 Hz soldier sim, network prediction), `DFWeapons` (weapon specs and the selection / fire state machine), `DFRig` (first- and third-person rigs), `DFWorld` (maps, collision, terrain), `DFEffects`, `DFHUD`, `DFGameData.h` (constants with their EXE addresses), `DFMath.h` (coordinate mapping), `DFData` (`dfdata.json` loader), tests (`*Tests.cpp`) |
| `Source/ProjectDFEditor/` | editor-only helpers (Sound Cue builder, input mappings) |
| `Content/DF/` | the imported assets, Blueprints, data assets, input, maps and `Data/` (JSON game data) |
| `SourceArt/` | the exported source files (glTF, PNG, WAV) the Unreal assets were imported from |

## Not reproduced

Enemies and AI, health and damage (frag and knife hits are only logged; no fall damage), mission scripts and game-mode rules,
vehicles and mounted guns, ladders, and the game's particle effects (stand-ins are used). In multiplayer, movement,
stance and the held weapon are replicated; weapon fire, grenades, effects and sounds are still local only. The lighting
and look are not matched to the original (gameplay was the goal).

## Legal

* *Delta Force* and *Delta Force: Black Hawk Down* are trademarks of their respective owners. This project is
  unofficial and non-commercial, and is not affiliated with, endorsed by or sponsored by NovaLogic or any current rights
  holder.
* **Original game assets.** Everything under `Content/DF/` and `SourceArt/` that comes from the game (3D models,
  textures, animations, sounds, fonts, map layouts, terrain and the data in `Content/DF/Data/`) was extracted from the
  retail *Delta Force: Black Hawk Down – Team Sabre* and is © NovaLogic, Inc. and its successors in interest. It is
  included only so that the port can be built, played and studied, for preservation and research. No ownership is
  claimed and this repository grants no license to these assets: do not sell them, repackage them or use them in other
  projects.
* **Own the game.** Please play this only if you own a legitimate copy of the game.
* **No original program code.** `DFBHD.EXE` and the game's `.pff` archives are not included. Gameplay behaviour was
  reimplemented from analysis of the original; the EXE addresses in the code are references, not copied code.
* **Takedown.** If you hold rights to any of this material and want it removed, open an issue on this repository and it
  will be taken down promptly.
* The C++ source code is original work of this project.
