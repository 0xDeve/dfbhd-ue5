// Small shared types for the Delta Force: Black Hawk Down port (all vectors in three space, see DFMath.h).
#pragma once

#include "CoreMinimal.h"

enum class EDFStance : uint8 { Stand, Crouch, Prone };

/** Axis-aligned box in three-space meters (world.js world.boxes / THREE.Box3). */
struct FDFBox
{
	FVector Min = FVector::ZeroVector;
	FVector Max = FVector::ZeroVector;

	FDFBox() = default;
	FDFBox(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}
	bool Contains(const FVector& P) const
	{
		return P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y && P.Z >= Min.Z && P.Z <= Max.Z;
	}
	FDFBox ExpandBy(double S) const { return FDFBox(Min - FVector(S), Max + FVector(S)); }
	/** THREE.Ray.intersectBox: distance along unit Dir to the box (0 if Origin is inside), or false. */
	bool IntersectRay(const FVector& Origin, const FVector& Dir, double& OutT) const
	{
		double TMin = 0.0, TMax = TNumericLimits<double>::Max();
		for (int32 A = 0; A < 3; ++A)
		{
			const double O = Origin[A], D = Dir[A];
			if (FMath::Abs(D) < 1e-12)
			{
				if (O < Min[A] || O > Max[A]) return false;
				continue;
			}
			double T0 = (Min[A] - O) / D, T1 = (Max[A] - O) / D;
			if (T0 > T1) Swap(T0, T1);
			TMin = FMath::Max(TMin, T0);
			TMax = FMath::Min(TMax, T1);
			if (TMin > TMax) return false;
		}
		OutT = TMin;
		return true;
	}
};

/** What the HUD draws this frame; written by the player pawn into ADFHUD::State every tick (see Docs/HUD.md). */
struct FDFHudState
{
	int32 Mag = 30;
	int32 Reserve = 300;
	/** Scope overlay (car15aim + car15gls + reddot1, the CAR-15's weapon.def `sights`) visible: !thirdPerson && adsT > 0.85. */
	bool bScope = false;
	/** Heading in degrees, 0 = north (-Z three = +X Unreal), clockwise: -yaw in degrees (= the Unreal actor yaw). */
	double HeadingDeg = 0.0;
	bool bThirdPerson = false;
	/** Stance code of the HUD figure (HUD.md 5.1): 0 stand (stance_1), 1 crouch, 2 prone. */
	int32 Stance = 0;
	/** Health / max health, 0..1 (the port has no damage yet: always 1 = green figure). */
	double Health01 = 1.0;
	/** Weapon id (EDFWeapon as int) for the change detection of the icon fade (weapon def pointer, HUD.md 4); 0 = no weapon. */
	int32 WeaponId = 0;
	/** Clip-bar fade trigger (0x50a350): changes when the weapon's clip slot / ammo class changes (with Reserve). */
	int32 ClipFadeKey = 0;
	/** Weapon name drawn at HUDWIPX/Y (Gametext WepDes[def name], e.g. "CAR15 - Auto"); empty = no weapon. */
	FString WeaponName;
	/** weapon.def hudicon (128x64); null = none. */
	class UTexture2D* WeaponIcon = nullptr;
	/** Ammo text (HUD.md 1.2): 0 = nothing (unlimited, e.g. knife), 1 = "%d/%d" Mag/Reserve, 2 = "%d" Reserve only (clipsize 1:
	 *  the pawn already adds the loaded round, e.g. grenades). */
	int32 AmmoMode = 1;
	/** Clip frame + bars only for weapons with hudclipgfx / hudrndgfx (CAR-15, Colt); bars from HudRnd* (weapon.def hudrndgfx). */
	bool bClipGfx = true;
	int32 HudRndStartDx = 4, HudRndStartDy = 0, HudRndStepDx = 4, HudRndStepDy = 0, HudRndPerBar = 1;
	/** Crosshair (coma20mm): a weapon exists and no optics overlay. */
	bool bCrosshair = true;
	/** Grenade throw power bar (HUD.md 5.5, 0x50a190): fraction 0..1, or < 0 = hidden. */
	double PowerBar = -1.0;
	/** Flashbang white-out: full-screen white with this alpha 0..1 ([0x96c428] / 255, drawn over the frame, 0x4b7842). */
	double FlashAlpha = 0.0;
	/** The pawn's game clock in seconds (S.T); the HUD fades run in 62 Hz ticks of it. */
	double TimeSec = 0.0;
	/** The local player's position, Unreal space (cm): the spin map centre and the map coordinates. */
	FVector PosUE = FVector::ZeroVector;
	/** Hooks for data the port does not have yet (HUD.md 5.6): mission title text ("" = hidden) and the multiplayer time-limit
	 *  countdown in whole seconds (< 0 = no clock). */
	FString MissionTitle;
	int32 ClockRemainingSec = -1;
};
