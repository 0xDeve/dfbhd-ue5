// Gameplay constants recovered from Delta Force: Black Hawk Down – Team Sabre (v1.5.0.11).
// 1:1 port of DFBHD-WebLite src/gamedata.js; see that file and EXE_FINDINGS.md for the EXE address of each value.
// Tags: [EXE 0x......] disassembly, [DEF] decrypted game data, [ANIM] .BAD root keys, [INFERRED], [TUNED].
// Engine units: positions 16.16 fixed point (65536 = 1 m), angles 32-bit (2^32 = 360 deg). Converted here to
// meters, seconds, degrees. All vectors are in the web project's three space (see DFMath.h).
#pragma once

#include "CoreMinimal.h"

namespace DFData
{
	// Simulation tick: clips advance fps/62 per tick [EXE 0x4170f0(0x40, 62)]. [INFERRED]
	inline constexpr int32 SimHz = 62;
	inline constexpr double Tick = 1.0 / SimHz;
	inline constexpr double BAM = 360.0 / 4294967296.0;   // degrees per binary-angle unit
	inline constexpr double FX = 1.0 / 65536.0;           // meters per 16.16 unit

	namespace CAR15
	{
		inline constexpr int32 ClipSize = 30;                 // [DEF weapon.def WPN_CAR15AUTO clipsize]
		inline constexpr int32 StartRounds = 300;             // [DEF startrounds]
		// HUD clip bars [DEF weapon.def WPN_CAR15AUTO hudrndgfx 4 0 4 0 1 H_round.tga; EXE 0x50a350, Docs/HUD.md 5.2]:
		// start offset, step per bar (virtual 1024x768 px, from the clip frame origin) and rounds per bar
		inline constexpr int32 HudRndStartDx = 4, HudRndStartDy = 0;
		inline constexpr int32 HudRndStepDx = 4, HudRndStepDy = 0;
		inline constexpr int32 HudRndPerBar = 1;
		// Spread cone half-angle, uniform angle, r = err*cos(rand*pi/2) [EXE 0x48e2c0]. error_up while the sights are up
		// (and not reloading) or prone; otherwise error_hip.
		inline constexpr double ErrorHipDeg = 0.8;            // [DEF error_hip]
		inline constexpr double ErrorUpDeg = 0.05;            // [DEF error_up]
		// Fire cadence / reload timing come from the action state machine (DFWeapons.h FDFArsenal, Docs/WEAPONS.md 2): `auto`
		// delays resolve to the clip length [EXE 0x4cc8a1], i.e. 7 ticks per round and a 132-tick reload with the ammo moved at its start.
		inline constexpr int32 ReloadTicks = 100;             // [DEF reload delaystart 100] (busy time before the reload's delayend)
		inline constexpr double RecoilDeg = (10 << 20) * BAM; // [DEF ammo.def recoil 10] << 20 per shot [EXE 0x48e451] = 0.879 deg
		inline constexpr double RecoilDecay = 1.0 / 8;        // [EXE 0x47d8bd] acc -= (acc + 4) >> 3 per tick
		inline constexpr double RecoilCamera = 1;             // camera pitch = view pitch + acc [EXE 0x43b6b4]
		inline constexpr double RecoilWeaponModel = 2;        // weapon model drawn at view pitch + 2 acc [EXE 0x4892f0]
		inline constexpr double RecoilClimb = 1.0 / 4;        // [EXE 0x47d8cb] Normal difficulty
		inline constexpr double MuzzleVelocity = 990;         // [DEF ammo.def velocity] m/s
		inline constexpr double MaxAge = 1.5;                 // [DEF ammo.def max_age] s
		inline constexpr double Damage = 51;                  // [DEF ammo.def impact_damage]
		// First-person rig placement, eye frame (x forward, y left, z up), 1/256 m [EXE 0x4892f0, DEF pos/tpos]
		inline constexpr double Pos[3] = { -18, 29, -171 };   // hip
		inline constexpr double TPos[3] = { -62, 45, -160 };  // sights up
		inline constexpr int32 AdsBlendTicks = 15;            // [EXE 0x48b140] pos <-> tpos linear blend
		inline constexpr double KickBack = 0x800 * FX;        // [EXE 0x4c9d04] 0.031 m per shot
		inline constexpr double KickJitter = 0x200 * FX;      // [EXE 0x4c9d04] + random 0.0078 m sideways/up
		inline constexpr double KickReturn = 0x80 * FX;       // [EXE 0x487829] returns 0.002 m per tick per axis
		inline constexpr double SightsFovScale = 0.8;         // [EXE 0x4b73c7]
		inline constexpr double DotScale = 0.75;              // [EXE 0x48862d] Sights Dot Size default
		inline constexpr double RangeM = MuzzleVelocity * MaxAge;
	}

	// Tracers: trcr render function [EXE 0x494290] + Tracer1R.3di node controller [EXE 0x4fd98c].
	namespace TRACER
	{
		inline constexpr double StretchMin = 0x842 * FX;            // 0.032 m
		inline constexpr double StretchMax = 0x14a529 * FX;         // 20.6 m
		inline constexpr double LengthScale[2] = { 64.0 / 256, 640.0 / 256 };
		inline constexpr double WidthNear = 0xa0000 * FX;           // 10 m
		inline constexpr double WidthRange = 0x5000000 * FX;        // 1280 m
		inline constexpr double WidthScale[2] = { 25.0 / 256, 1792.0 / 256 };
	}

	namespace VIEW
	{
		inline constexpr double HFov = 80;          // [EXE 0x4f44ad] horizontal
		inline constexpr double FovEase = 1.0 / 32; // [EXE 0x4f5e9f] per tick
		inline constexpr double Near = 0.2;         // [EXE 0x64b4f0]
		inline constexpr double Far = 1000;         // [EXE 0x64b4ec]
		inline constexpr double VmHFov = 80;        // [EXE 0x4cb16c] weapon renderfov default
		inline constexpr double VmNear = 0.05;      // [EXE 0x488a10]
		inline constexpr double PitchMax = 80;      // [EXE 0x48a4c2]
		inline constexpr double PitchMaxProne = 40; // [EXE 0x48a4c2] looking up limited when prone
	}

	namespace PHYS
	{
		inline constexpr double JumpVelocity = 0x1600 * FX * SimHz;            // [EXE 0x47ee2a] 5.33 m/s
		inline constexpr double Gravity = 208 * FX * SimHz * SimHz;            // [EXE 0x47ebb3] 12.2 m/s^2
		inline constexpr double TerminalVelocity = 0x4000 * FX * SimHz;        // [EXE 0x47ebba] 15.5 m/s
		inline constexpr double AirThrust = 64 * FX * SimHz * SimHz;           // [EXE 0x47ea17] then vel *= 63/64
		inline constexpr int32 JumpCooldownTicks = 32;                         // [EXE 0x47ee2a]
		inline constexpr double StepDown = 0xF000 * FX;                        // drops under 0.94 m are walked
		inline constexpr double RunSpeed = 5.47;                               // [ANIM DT1RUNF] HUD/bob normalisation only
		inline constexpr double Radius = 0.35;                                 // [TUNED] collision radius
	}

	// Stances: Z = prone, X = crouch (both toggle back to standing), C = stand [EXE 0x489cc0].
	struct FStanceData { double Eye, Hip, Height; };
	namespace STANCE
	{
		inline constexpr FStanceData Stand = { 1.585, 0.869, 1.8 };   // [ANIM DT1IDLEA] height [TUNED]
		inline constexpr FStanceData Crouch = { 1.200, 0.486, 1.3 };  // [ANIM DT1KNEEL]
		inline constexpr FStanceData Prone = { 0.244, 0.164, 0.5 };   // [ANIM DT1PRONE]
	}

	inline constexpr double EyeAboveTrack = 0x2000 * FX;  // [EXE 0x4144f0] camera = feet + clip eye (t4) + 0.125 m

	namespace LEAN
	{
		inline constexpr double StepDeg = 0x03000000 * BAM;  // [EXE 0x47ed3a] 4.22 deg per tick while held
		inline constexpr double Decay = 1.0 / 16;            // [EXE 0x47d8e1] lean -= (lean + 8) >> 4
		inline constexpr double EyeLateral = 0.75;           // [EXE 0x47e832]
		inline constexpr double CameraRoll = 0.25;           // [EXE 0x43b6c9]
	}

	namespace BODY
	{
		inline constexpr double LegTurn = 1.0 / 4;
		inline constexpr double LegTurnMaxDeg = 0x05000000 * BAM;  // 7.03
		inline constexpr double LegMaxOffDeg = 0x30000000 * BAM;   // 67.5
		inline constexpr double StepDeg = 0x15555540 * BAM;        // 30
		inline constexpr double SettleDeg = 0x038e38e0 * BAM;      // 5
		inline constexpr int32 SettleTicks = 64;
	}

	// Third-person aim [EXE 0x484150]. Rows: {yaw*d, pitch*P, recoil*K, roll*L}.
	namespace AIM
	{
		inline constexpr double LowerSpine[4] = { 1.0 / 2, 1.0 / 4, 1.0 / 2, 1.0 / 2 };
		inline constexpr double UpperBody[4] = { 3.0 / 4, 1, 1, 1 };
		inline constexpr double Head[4] = { 1, 1, 0, 1.0 / 2 };
	}

	namespace ROLL
	{
		inline constexpr double Duration = 1.0;                    // [ANIM DT1ROLLL] 30 frames @ 30 fps
		inline constexpr double BodySpinDeg = 0x04050000 * BAM;    // 5.65 per tick
		inline constexpr double ViewSpinDeg = 0x04000000 * BAM;    // 5.625 per tick
		inline constexpr double BodyEaseProne = 1.0 / 4;
		inline constexpr double BodyDecay = 1.0 / 16;
		inline constexpr double ViewEase = 1.0 / 32;
	}

	// main.js tunables (not from the game)
	namespace TUNE
	{
		inline constexpr double MouseSens = 0.0022;   // rad per raw mouse count at 80 deg
		inline constexpr double TpBack = 2.8, TpRight = 0.6, TpUp = 0.2;   // third-person offset from the head pivot
		inline constexpr double HeadR = 0.18;          // [DEVIATION] lean wall probe radius
	}
}
