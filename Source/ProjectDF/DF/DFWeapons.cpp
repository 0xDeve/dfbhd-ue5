// Weapon specs (weapon.def / ammo.def, decrypted from the game install) and the action state machine. See DFWeapons.h and
// Docs/WEAPONS.md. Tags: [DEF] weapon.def / ammo.def, [EXE 0x......] disassembly, [INFERRED].
#include "DFWeapons.h"
#include "DFGameData.h"
#include "DFMath.h"
#include "DFWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFWeapons, Log, All);

using namespace DFWeapons;

namespace
{
	constexpr int32 DAuto = AutoDelay;
	using EA = EAction;

	// Actions in EAction order: Idle, EmptyIdle, Fire, Recoil, Reload, Empty, SwitchTo, SwitchFrom, SwitchRank  [DEF]
	FWeaponSpec MakeKnife()
	{
		FWeaponSpec S;
		S.Id = EDFWeapon::Knife; S.DefName = TEXT("WPN_KNIFE"); S.HudName = TEXT("Knife");
		S.Category = 1; S.Rank = 0; S.Class = EClass::None; S.Flags = F_Underwater;
		S.AmmoClass = EAmmoClass::Unlimited; S.ClipSize = -1; S.StartRounds = -1;
		S.ErrorHipDeg = 0; S.ErrorUpDeg = 0;
		S.Pos[0] = 4; S.Pos[1] = -5; S.Pos[2] = -186;
		S.SpecialHold = 1; S.AttackAnim = 1; S.RunAnim = 1;
		const FActionSpec Act[] = { {0, DAuto}, {0, 0}, {0, DAuto}, {0, DAuto}, {0, DAuto}, {0, DAuto}, {1, 1}, {1, 1}, {1, 1} };
		for (int32 i = 0; i < int32(EA::Count); ++i) S.Actions[i] = Act[i];
		// AMMO_KNIFE01: instant kill zone rounds_kz_Knife, kz_damage 300, kz_minradius 1.5, kz_maxradius 2.75, kz_pieslice 35
		S.Ammo.Velocity = 1; S.Ammo.MaxAge = 1; S.Ammo.ImpactDamage = 100;
		S.Ammo.KillZone = EKillZone::Knife; S.Ammo.KzMinRadius = 1.5; S.Ammo.KzMaxRadius = 2.75; S.Ammo.KzDamage = 300;
		S.Ammo.KzHalfAngleDeg = 17;   // (35 >> 1) * 0xB60B60 [EXE 0x410xxx parser, 0x492d80]
		S.FpSkeleton = TEXT("fp_knife"); S.FpModel = TEXT("fp_knife");
		S.FpIdleClip = TEXT("fp_knife_idle"); S.FpFireClip = TEXT("fp_knife_fire");
		S.TpModel = TEXT("tp_knife"); S.TpAttach = TEXT("knife");
		return S;
	}

	FWeaponSpec MakeColt()
	{
		FWeaponSpec S;
		S.Id = EDFWeapon::Colt45; S.DefName = TEXT("WPN_colt45"); S.HudName = TEXT("Colt .45");
		S.Category = 2; S.Rank = 0; S.Class = EClass::Secondary; S.Flags = 0;   // semi-automatic, not Sighted
		S.AmmoClass = EAmmoClass::C45; S.ClipSize = 7; S.StartRounds = 28;
		S.ErrorHipDeg = 1.0; S.ErrorUpDeg = 0.5;
		S.Pos[0] = -76; S.Pos[1] = -13; S.Pos[2] = -174;
		S.TPos[0] = -47; S.TPos[1] = -13; S.TPos[2] = -159; S.bHasTPos = true;
		S.SpecialHold = 1;
		const FActionSpec Act[] = { {0, DAuto}, {0, DAuto}, {0, DAuto}, {0, 0}, {90, DAuto}, {0, DAuto}, {1, 1}, {1, 1}, {1, 1} };
		for (int32 i = 0; i < int32(EA::Count); ++i) S.Actions[i] = Act[i];
		// AMMO_COLT_45
		S.Ammo.Velocity = 270; S.Ammo.MaxAge = 3; S.Ammo.Recoil = 30; S.Ammo.ImpactDamage = 51;
		S.bHasClipGfx = true; S.HudRnd = { 8, 0, 18, 0, 1 };
		S.FpSkeleton = TEXT("fp_colt"); S.FpModel = TEXT("fp_colt");
		S.FpIdleClip = TEXT("fp_colt_idle"); S.FpFireClip = TEXT("fp_colt_fire"); S.FpReloadClip = TEXT("fp_colt_reload");
		S.TpModel = TEXT("tp_colt"); S.TpAttach = TEXT("colt");
		return S;
	}

	FWeaponSpec MakeCar15(bool bAuto)
	{
		FWeaponSpec S;
		S.Id = bAuto ? EDFWeapon::Car15Auto : EDFWeapon::Car15Semi;
		S.DefName = bAuto ? TEXT("WPN_CAR15AUTO") : TEXT("WPN_CAR15");
		S.HudName = bAuto ? TEXT("CAR15 - Auto") : TEXT("CAR15 - Semi");
		S.Category = 3; S.Rank = bAuto ? 0 : 1;
		S.Class = bAuto ? EClass::Primary : EClass::None;   // the semi variant has no weapon_class (needs ammo to be selected)
		S.Flags = bAuto ? (F_Auto | F_Sighted) : (F_Sighted | F_Primary);
		S.AmmoClass = EAmmoClass::C556; S.ClipSize = DFData::CAR15::ClipSize;
		S.StartRounds = bAuto ? DFData::CAR15::StartRounds : 0;
		S.ClipSlot = 1;
		S.ErrorHipDeg = DFData::CAR15::ErrorHipDeg; S.ErrorUpDeg = DFData::CAR15::ErrorUpDeg;
		S.Pos[0] = -18; S.Pos[1] = 29; S.Pos[2] = -171;
		S.TPos[0] = -62; S.TPos[1] = 45; S.TPos[2] = -160; S.bHasTPos = true;
		const FActionSpec Act[] = { {0, DAuto}, {0, DAuto}, {0, DAuto}, {0, 2}, {DFData::CAR15::ReloadTicks, DAuto}, {0, DAuto}, {1, 1}, {1, 1}, {1, 1} };
		for (int32 i = 0; i < int32(EA::Count); ++i) S.Actions[i] = Act[i];
		// AMMO_CAR15_556MM
		S.Ammo.Velocity = DFData::CAR15::MuzzleVelocity; S.Ammo.MaxAge = DFData::CAR15::MaxAge; S.Ammo.Recoil = 10;
		S.Ammo.ImpactDamage = DFData::CAR15::Damage;
		S.bHasClipGfx = true;
		S.HudRnd = { DFData::CAR15::HudRndStartDx, DFData::CAR15::HudRndStartDy, DFData::CAR15::HudRndStepDx,
			DFData::CAR15::HudRndStepDy, DFData::CAR15::HudRndPerBar };
		S.FpSkeleton = TEXT("fp"); S.FpModel = TEXT("fp_gun");
		S.FpIdleClip = TEXT("fp_idle"); S.FpReloadClip = TEXT("fp_reload");   // fire / recoil / empty / switch* use the idle clip (cr15_1st.adm)
		S.TpModel = TEXT("tp_gun"); S.TpAttach = TEXT("car15");
		return S;
	}

	FWeaponSpec MakeGrenade(EDFWeapon Id)
	{
		FWeaponSpec S;
		S.Id = Id;
		S.Class = EClass::Grenade;
		S.Flags = F_NoClipsNoDraw | F_Underwater | F_OkWhileJumping;
		S.ClipSize = 1;
		S.ErrorHipDeg = 0; S.ErrorUpDeg = 0;
		S.Pos[0] = -33; S.Pos[1] = 8; S.Pos[2] = -177;
		S.SpecialHold = 1; S.AttackAnim = 2;
		int32 Release = 0;
		// grenadehe / grenadefb / grenadesm: velocity 30, bullet_mass 3, drag 1, useownmove
		S.Ammo.Velocity = 30;
		switch (Id)
		{
		case EDFWeapon::Flashbang:
			S.DefName = TEXT("WPN_GRENADEFB"); S.HudName = TEXT("Flash Bang"); S.Category = 4;
			S.AmmoClass = EAmmoClass::GrenadeFB; S.StartRounds = 5; Release = 70;
			S.Ammo.MaxAge = 4; S.Ammo.KillZone = EKillZone::C4; S.Ammo.KzMinRadius = 20; S.Ammo.KzMaxRadius = 20; S.Ammo.KzPhysics = 3;
			S.FpSkeleton = TEXT("fp_flash"); S.FpModel = TEXT("fp_flash");
			S.FpIdleClip = TEXT("fp_flash_idle"); S.FpFireClip = TEXT("fp_flash_fire"); S.FpEmptyIdleClip = TEXT("fp_smoke_emptyidle");
			S.TpModel = TEXT("tp_flash"); S.TpAttach = TEXT("flash");
			break;
		case EDFWeapon::FragGrenade:
			S.DefName = TEXT("WPN_GRENADEHE"); S.HudName = TEXT("Frag Grenade"); S.Category = 5;
			S.AmmoClass = EAmmoClass::GrenadeHE; S.StartRounds = 5; Release = 80;
			S.Ammo.MaxAge = 4; S.Ammo.KillZone = EKillZone::C4; S.Ammo.KzMinRadius = 6; S.Ammo.KzMaxRadius = 10; S.Ammo.KzDamage = 105;
			S.Ammo.KzPhysics = 1;
			S.FpSkeleton = TEXT("fp_frag"); S.FpModel = TEXT("fp_frag");
			S.FpIdleClip = TEXT("fp_frag_idle"); S.FpFireClip = TEXT("fp_frag_fire"); S.FpEmptyIdleClip = TEXT("fp_smoke_emptyidle");
			S.TpModel = TEXT("tp_frag"); S.TpAttach = TEXT("frag");
			break;
		default:   // SmokeGrenade
			S.DefName = TEXT("WPN_GRENADESM"); S.HudName = TEXT("Smoke Grenade"); S.Category = 6;
			S.AmmoClass = EAmmoClass::GrenadeSM; S.StartRounds = 3; Release = 65;
			S.Ammo.MaxAge = 30; S.Ammo.ArmAge = 5;
			S.FpSkeleton = TEXT("fp_smoke"); S.FpModel = TEXT("fp_smoke");
			S.FpIdleClip = TEXT("fp_smoke_idle"); S.FpFireClip = TEXT("fp_smoke_fire");   // smok_1st.adm has no anim_wpn_empty_idle
			S.TpModel = TEXT("tp_smoke"); S.TpAttach = TEXT("smoke");
			break;
		}
		// fire: delaystart = release tick of the throw; emptyidle: delaystart auto
		const FActionSpec Act[] = { {0, DAuto}, {DAuto, 0}, {Release, DAuto}, {0, DAuto}, {0, DAuto}, {0, DAuto}, {1, 1}, {1, 1}, {1, 1} };
		for (int32 i = 0; i < int32(EA::Count); ++i) S.Actions[i] = Act[i];
		return S;
	}

	const TArray<FWeaponSpec>& Specs()
	{
		static const TArray<FWeaponSpec> Table = {
			MakeKnife(), MakeColt(), MakeCar15(true), MakeCar15(false),
			MakeGrenade(EDFWeapon::Flashbang), MakeGrenade(EDFWeapon::FragGrenade), MakeGrenade(EDFWeapon::SmokeGrenade),
		};
		return Table;
	}
}

const FWeaponSpec* DFWeapons::Find(EDFWeapon W)
{
	const int32 I = int32(W) - 1;
	const TArray<FWeaponSpec>& T = Specs();
	return T.IsValidIndex(I) ? &T[I] : nullptr;
}

TConstArrayView<FWeaponSpec> DFWeapons::All()
{
	return Specs();
}


// ====================================================================== arsenal (port of DFBHD.EXE 0x4c8ca0..0x4ca7c0, 0x489680, 0x487270, 0x4880b0, 0x48b090..0x48b280, 0x487650)
//
// One FSlot = the EXE's "winst" (stride 0x60): counter +0, burst +4, clip +0x18, deploy +0x14, state +0x2c, next +0x30, prev +0x34,
// phase +0x59, power byte +0x5b. Only the current slot ticks. The handlers are line-by-line ports; where WEAPONS.md differs from the
// code, the code wins (see the notes at the constants below and the report).

namespace
{
	/** weapon.def `auto` delays are resolved by the weapon loader (0x4cc8a1..0x4cc975): the length of the action's animation clip in
	 *  ticks, T = trunc(frames * 62.5 / fps + 0.5) + 1; `delaystart auto` = T, `delayend auto` = T - delaystart (T if T <= delaystart);
	 *  an action without an `anim` line gets 0. The earlier docs took `auto` as -1 = "no wait" (that gives the CAR-15 4 ticks per
	 *  round, the grenades re-throwing 3 ticks after the release); the clip lengths make it 7 ticks per round and ~115 ticks per grenade.
	 *  Set false to get the old behaviour. */
	constexpr bool kResolveAutoDelays = true;
	/** The reload handler (0x4c9d70) sends network message 0x25 (0x423e40) at its first call; the receiver 0x423e90 -> 0x4c9e10 moves the
	 *  ammo, i.e. at the START of the reload (the next message pump), not at delaystart as WEAPONS.md says. false = move at delaystart. */
	constexpr bool kReloadMovesAmmoAtStart = true;
	/** 0x488920: a NoClipsNoDraw weapon is not drawn when the class POOL is 0 (the loaded clip is not counted), so the last grenade of
	 *  a stack is invisible while it is loaded. false = clip + pool == 0 (WEAPONS.md). */
	constexpr bool kNoDrawChecksPoolOnly = true;
	constexpr bool kAutoReloadOption = true;   // [0x9f2184]
	constexpr int32 kRoundsPerClipUnit = 1;    // weapon def +0x60

	int32 ClipFrames(const TCHAR* Name)   // BAD header frame counts of the first-person clips used by the action animations (ASSETS_SURVEY.md 2)
	{
		if (!Name) return 1;
		static const struct { const TCHAR* N; int32 F; } Table[] = {
			{ TEXT("fp_colt_fire"), 2 }, { TEXT("fp_colt_reload"), 61 }, { TEXT("fp_reload"), 63 }, { TEXT("fp_knife_fire"), 15 },
			{ TEXT("fp_frag_fire"), 54 }, { TEXT("fp_flash_fire"), 52 }, { TEXT("fp_smoke_fire"), 52 } };
		for (const auto& E : Table) { if (FCString::Strcmp(E.N, Name) == 0) return E.F; }
		return 1;
	}

	/** Does the weapon.def action have an `anim` line (the knife's emptyidle and the grenades' empty do not)? */
	bool ActionHasAnim(const FWeaponSpec& S, EAction A)
	{
		if (S.Id == EDFWeapon::Knife && A == EAction::EmptyIdle) return false;
		if (S.Class == EClass::Grenade && A == EAction::Empty) return false;
		return true;
	}

	int32 AnimTicks(const FWeaponSpec& S, EAction A)
	{
		const TCHAR* Clip = nullptr;
		switch (A)
		{
		case EAction::Fire: Clip = S.FpFireClip; break;
		case EAction::Reload: Clip = S.FpReloadClip; break;
		case EAction::EmptyIdle: Clip = S.FpEmptyIdleClip; break;
		default: break;
		}
		const int32 F = ClipFrames(Clip);
		return int32(double(F) * 62.5 / 30.0 + 0.5) + 1;
	}

	/** The loader-resolved delays of weapon S, action A. */
	FActionSpec ResolveAction(const FWeaponSpec& S, EAction A)
	{
		FActionSpec R = S.Actions[int32(A)];
		if (!kResolveAutoDelays) return R;
		const bool bAnim = ActionHasAnim(S, A);
		const int32 T = bAnim ? AnimTicks(S, A) : 0;
		if (R.DelayStart == AutoDelay) R.DelayStart = T;
		if (R.DelayEnd == AutoDelay) R.DelayEnd = (T > R.DelayStart) ? T - R.DelayStart : T;
		return R;
	}

	constexpr int32 NumSlots = 250;
	constexpr int32 PerCat = 25;
}

struct FDFArsenal::FImpl
{
	/** The EXE's weapon instance ("winst"). */
	struct FSlot
	{
		const FWeaponSpec* Spec = nullptr;
		int32 Counter = 0, Burst = 0, State = 0, Next = 0, Prev = 0, Deploy = 0, Clip = 0;
		uint8 Phase = 0, Power = 0;
	};
	/** A linear position blend object (0x48b280 / 0x48b140): cur -= step per tick, snapping to `To` per axis. */
	struct FBlend
	{
		bool bActive = false;
		float Cur[3] = { 0, 0, 0 }, Step[3] = { 0, 0, 0 };
		int32 To[3] = { 0, 0, 0 };
	};

	FSlot Slots[NumSlots];
	FActionSpec Resolved[int32(EDFWeapon::Count)][int32(EAction::Count)];
	bool bResolved = false;

	int32 Cur = -1;       // soldier+0xdc
	int32 Pending = -1;   // [0x96c294]
	int32 Pools[int32(EAmmoClass::Count)] = {};
	int32 Clips[10] = {};
	int64 Now = 0;

	// sights / pistol blend, deploy blend, the shared model offset [0x96c4a4..ac] (16.16 m)
	bool bToggle = false;      // [0x96c3e0]
	bool bSightsWanted = false;   // [0x96b668]
	int32 SightsLatch = 1;     // [0x96b66c]
	int32 SightsSlot = -1;     // [0x96b660]
	FBlend Sights, Deploy;
	int32 DeploySlot = -1;     // [0x96c278]
	int32 Ofs[3] = { 0, 0, 0 };
	bool bFired = false;       // soldier flag 0x8000000
	int32 FiredCountdown = 0;  // [0x9dadf0]
	bool bPowerOn = false;     // [0x96c764] != 0
	int64 PowerT0 = 0;
	bool bRepost = false;      // a fire command that arrived while the auto weapon was in `fire` (re-posted by 0x4c8e50)

	// per call context
	IDFArsenalListener* Lis = nullptr;
	FDFArsenal::FTickInput In;

	struct FScope
	{
		FScope(FImpl& InI, IDFArsenalListener* L, const FDFArsenal::FTickInput* InTick = nullptr) : I(InI), OldL(InI.Lis), OldIn(InI.In)
		{
			I.Lis = L;
			if (InTick) I.In = *InTick;
		}
		~FScope() { I.Lis = OldL; I.In = OldIn; }
		FImpl& I;
		IDFArsenalListener* OldL;
		FDFArsenal::FTickInput OldIn;
	};

	// ------------------------------------------------------------ helpers
	void EnsureResolved()
	{
		if (bResolved) return;
		bResolved = true;
		for (const FWeaponSpec& S : DFWeapons::All())
		{
			for (int32 A = 0; A < int32(EAction::Count); ++A) Resolved[int32(S.Id)][A] = ResolveAction(S, EAction(A));
		}
	}
	const FActionSpec& Rec(const FSlot& S, int32 Action) const { return Resolved[int32(S.Spec->Id)][Action]; }

	int32 PoolOf(EAmmoClass C) const { return C == EAmmoClass::Unlimited ? -1 : Pools[int32(C)]; }                    // 0x4cd480
	int32 ClipOf(const FSlot& S) const { return S.Spec->ClipSlot ? Clips[S.Spec->ClipSlot] : S.Clip; }               // 0x4cd3b0
	void SetClip(FSlot& S, int32 V) { if (S.Spec->ClipSlot) Clips[S.Spec->ClipSlot] = V; else S.Clip = V; }
	int32 Total(const FSlot& S) const   // 0x4cd520: clip + pool
	{
		return PoolOf(S.Spec->AmmoClass) + (S.Spec->ClipSize != -1 ? ClipOf(S) : 0);
	}
	bool HasAmmo(const FSlot& S) const   // 0x4ca630
	{
		if (S.Spec->ClipSize != -1 && ClipOf(S) != 0) return true;
		return PoolOf(S.Spec->AmmoClass) != 0;
	}
	bool Selectable(const FSlot& S) const
	{
		return S.Spec && (S.Spec->Class == EClass::Primary || S.Spec->Class == EClass::Secondary || Total(S) != 0);
	}
	FSlot* CurSlot() { return Cur >= 0 && Slots[Cur].Spec ? &Slots[Cur] : nullptr; }
	const FSlot* CurSlot() const { return Cur >= 0 && Slots[Cur].Spec ? &Slots[Cur] : nullptr; }
	static int32 SlotIndex(const FWeaponSpec& S) { return S.Category * PerCat + S.Rank; }

	// ------------------------------------------------------------ blends (0x48b280, 0x48b140)
	static void BlendStart(FBlend& B, int32 Frames, const int32* From, const int32* To)
	{
		auto IsZero = [](const int32* V) { return V[0] == 0 && V[1] == 0 && V[2] == 0; };
		if (IsZero(To) || IsZero(From)) return;
		for (int32 A = 0; A < 3; ++A)
		{
			B.To[A] = To[A];
			B.Step[A] = float(double(From[A] - To[A]) / double(Frames));
			B.Cur[A] = float(From[A]);
		}
		B.bActive = true;
	}
	/** One tick; returns true if the output offset must be (re)written. */
	bool BlendStep(FBlend& B, const FWeaponSpec* Weapon)
	{
		if (!Weapon) { B.bActive = false; return false; }
		if (B.Step[0] == 0.f && B.Step[1] == 0.f && B.Step[2] == 0.f) { B.bActive = false; return false; }
		for (int32 A = 0; A < 3; ++A)
		{
			const double C = double(B.Cur[A]) - double(B.Step[A]);
			B.Cur[A] = float(C);
			if (FMath::Abs(C - double(B.To[A])) < FMath::Abs(double(B.Step[A])))
			{
				B.Step[A] = 0.f;
				B.Cur[A] = float(B.To[A]);
			}
		}
		for (int32 A = 0; A < 3; ++A) Ofs[A] = int32((double(B.Cur[A]) - double(Weapon->Pos[A])) * 256.0);   // _ftol truncates
		return true;
	}
	const FWeaponSpec* SpecAt(int32 Idx) const { return Idx >= 0 && Idx < NumSlots ? Slots[Idx].Spec : nullptr; }

	/** 0x48b090: the deploy blend. Lowering (bRaise false): the current weapon's pos -> L; raising: L -> the (new) current weapon's pos. */
	void DeployBlend(bool bRaise)
	{
		DeploySlot = Cur;
		const FSlot* S = CurSlot();
		if (!S) return;
		if (!bRaise) BlendStart(Deploy, DFWeapons::DeployTicks, S->Spec->Pos, DFWeapons::DeployLowered);
		else BlendStart(Deploy, DFWeapons::DeployTicks, DFWeapons::DeployLowered, S->Spec->Pos);
	}

	// ------------------------------------------------------------ generic start helper (0x4ca330 / 0x4ca570)
	void Generic(FSlot& S, EAction A)
	{
		if (S.Phase & 1) { S.Phase = 2; Lis->OnActionStart(S.Spec->Id, A); }
		else if (S.Phase & 0x40) { S.Phase = 2; Lis->OnActionStart(S.Spec->Id, A); }
	}

	// ------------------------------------------------------------ requests (0x4c8e50, 0x4c8f30, 0x4c8f60, 0x4c8fb0)
	/** 0x4c8e50: the fire request. Always stores the power byte. */
	bool RequestFire(FSlot& S, uint8 Power)
	{
		S.Power = Power;
		if (!(S.Spec->Flags & DFWeapons::F_Auto))
		{
			if (S.State == 0) { S.Next = 2; return true; }
			if (S.State == 1) { S.Next = 5; return true; }
			return false;
		}
		switch (S.State)
		{
		case 0: case 3: S.Next = 2; return true;
		case 1: S.Next = 5; return false;
		case 2: bRepost = true; return true;   // re-posts the command (0x45f9c0): processed again once the state left `fire`
		default: return false;
		}
	}
	void RequestReloadSlot(FSlot& S)   // 0x4c8f30
	{
		if (S.Phase & 0x80) return;
		if (S.Next == 0 || S.Next == 1) S.Next = 4;
	}
	bool RequestSwitchFrom(FSlot& S)   // 0x4c8f60
	{
		if (S.State == 7) return false;
		if (S.Phase == 4 || S.Phase == 0x40 || S.Phase == 0)
		{
			S.Counter = 0; S.Burst = 0; S.Deploy = 0; S.Next = 7; S.Prev = 0; S.State = 0;
			return true;
		}
		S.Next = 0;   // mid-action (a reload ...): silently dropped
		return true;
	}
	void RequestSwitchRank(FSlot& S)   // 0x4c8fb0
	{
		if (S.State == 8) return;
		if (S.Phase == 4 || S.Phase == 0x40 || S.Phase == 0) { S.Counter = 0; S.Next = 8; }
		else S.Next = 0;
	}

	// ------------------------------------------------------------ CanFire (0x4ca6a0)
	bool CanFire(FSlot& S)
	{
		const FWeaponSpec& W = *S.Spec;
		if (!(W.Flags & DFWeapons::F_OkWhileJumping) && In.bAirborne) return false;
		if (W.ClipSize != -1)
		{
			if (ClipOf(S) == 0) { S.Next = PoolOf(W.AmmoClass) == 0 ? 1 : 3; return false; }
		}
		else
		{
			const int32 Pool = PoolOf(W.AmmoClass);
			if (Pool >= 0 && Pool < kRoundsPerClipUnit) { S.Next = 1; return false; }
		}
		return true;
	}

	// ------------------------------------------------------------ reload ammo move (0x4c9e10)
	void ReloadMove(FSlot& S)
	{
		const FWeaponSpec& W = *S.Spec;
		S.Phase &= 0x7f;
		if (W.ClipSize == -1 || W.AmmoClass == EAmmoClass::Unlimited) return;
		int32& Pool = Pools[int32(W.AmmoClass)];
		const int32 Clip = ClipOf(S);
		if (Clip) Pool += Clip * kRoundsPerClipUnit;                         // 0x4cd6e0: the loaded rounds go back to the pool
		const int32 Take = FMath::Min(kRoundsPerClipUnit * W.ClipSize, Pool);
		Pool = FMath::Max(0, Pool - Take);                                    // 0x4cd620
		SetClip(S, Take / kRoundsPerClipUnit);
		Lis->OnReloadDone(W.Id);
	}

	// ------------------------------------------------------------ action handlers
	void HIdle(FSlot& S, bool bEmptyIdle)   // 0x4c96e0 / 0x4c97e0
	{
		const EAction Me = bEmptyIdle ? EAction::EmptyIdle : EAction::Idle;
		if (S.Counter == 0 && S.Spec && S.Phase != 4)
		{
			Lis->OnActionStart(S.Spec->Id, Me);
			S.Phase = 4;
			if (S.Spec->ClipSize != -1 && ClipOf(S) == 0)
			{
				const int32 Pool = PoolOf(S.Spec->AmmoClass);
				if (Pool > 0 && kAutoReloadOption)
				{
					if (!(S.Phase & 0x80) && (S.Next == 0 || S.Next == 1)) S.Next = 4;
				}
				else S.Next = 1;
			}
			return;
		}
		Generic(S, Me);
	}

	void HFire(FSlot& S)   // 0x4c98f0
	{
		const FActionSpec& R = Rec(S, 2);
		if (S.Phase == 1 && !CanFire(S))
		{
			S.Counter = 0;
			S.Phase = 4;
			return;
		}
		if (S.Counter != 0 || S.Phase == 4) { Generic(S, EAction::Fire); return; }
		// the round leaves: start event first (the EXE's helper runs after the spawn), then the shot
		Generic(S, EAction::Fire);
		const FWeaponSpec& W = *S.Spec;
		if (W.ClipSize != -1) { const int32 C = ClipOf(S); if (C > 0) SetClip(S, C - 1); }   // 0x4cd590
		if (W.Ammo.KillZone != EKillZone::Knife) bFired = true;                              // soldier flag 0x8000000 (0x48e951)
		Lis->OnFire(W.Id, S.Power);
		if (W.Flags & DFWeapons::F_Burst) { if (S.Burst == 0) S.Burst = 2; else --S.Burst; }
		S.Next = 3;
		S.Counter = R.DelayEnd;
		S.Phase = 4;
	}

	void HRecoil(FSlot& S)   // 0x4c9b20
	{
		const FActionSpec& R = Rec(S, 3);
		const FWeaponSpec& W = *S.Spec;
		const int32 Clip = W.ClipSize == -1 ? 1 : ClipOf(S);
		Generic(S, EAction::Recoil);
		const bool bTimed = S.Phase != 4 && (R.DelayStart != 0 || R.DelayEnd != 0);
		if (!bTimed)
		{
			if ((S.Counter == 0 || S.Counter == 1) && Clip != 0 && (W.Flags & DFWeapons::F_Auto) && S.Burst <= 0 && In.bFireHeld)
			{
				RequestFire(S, 0);   // the re-posted command 0x95 (0x4c9bea)
			}
			if (R.DelayStart != 0 || R.DelayEnd != 0) return;
		}
		if (S.Counter != 0) return;
		S.Phase = 4;
		if (W.ClipSize != -1 && Clip == 0)
		{
			const int32 Pool = PoolOf(W.AmmoClass);
			if (Pool >= kRoundsPerClipUnit * W.ClipSize && kAutoReloadOption) S.Next = 4;
			else S.Next = 1;
			return;
		}
		S.Next = S.Burst != 0 ? 2 : 0;
		S.Counter = R.DelayEnd;
	}

	void HReload(FSlot& S)   // 0x4c9d70
	{
		const FActionSpec& R = Rec(S, 4);
		const bool bFirst = !(S.Phase & 0x80) && (S.Phase & 1);
		Generic(S, EAction::Reload);
		if (bFirst && kReloadMovesAmmoAtStart) ReloadMove(S);
		if (S.Counter == 0 && S.Phase != 4)
		{
			if (!kReloadMovesAmmoAtStart) ReloadMove(S);
			S.Counter = R.DelayEnd;
			S.Next = 0;
			S.Phase = 4;
			S.Burst = 0;
		}
	}

	void HEmpty(FSlot& S)   // 0x4c9f90
	{
		const FActionSpec& R = Rec(S, 5);
		Generic(S, EAction::Empty);
		if (S.Counter == 0 && S.Phase != 4)
		{
			S.Counter = R.DelayEnd;
			S.Next = 1;
			S.Phase = 4;
		}
	}

	void HSwitchTo(FSlot& S)   // 0x4c9fe0
	{
		const FActionSpec& R = Rec(S, 6);
		Generic(S, EAction::SwitchTo);
		if (S.Counter == 0 && S.Phase != 4 && S.Phase != 0x40)
		{
			S.Deploy = -900;
			S.Counter = R.DelayEnd;
			S.Next = S.Prev;
			S.Phase = 4;
			return;
		}
		if (S.Deploy > 0) { S.Deploy = 0; S.Counter = 0; S.Phase = 4; }
		else { S.Deploy += 30; S.Counter = R.DelayEnd; }
	}

	void HSwitchFrom(FSlot& S)   // 0x4ca0c0
	{
		const FActionSpec& R = Rec(S, 7);
		Generic(S, EAction::SwitchFrom);
		if (S.Counter == 0 && S.Phase != 4 && S.Phase != 0x40)
		{
			S.Deploy = 0;
			S.Counter = R.DelayEnd;
			S.Next = S.Prev;
			S.Phase = 4;
			return;
		}
		if (S.Deploy < -900)
		{
			// commit: the pending weapon becomes the current one
			S.Deploy = 0; S.Counter = 0; S.Phase = 4;
			if (Pending >= 0 && Slots[Pending].Spec)
			{
				const EDFWeapon From = S.Spec->Id;
				Cur = Pending;
				Pending = -1;
				FSlot& N = Slots[Cur];
				if (N.State != 6) N.Next = (N.Phase == 4 || N.Phase == 0) ? 6 : 0;
				DeployBlend(true);
				S.Deploy = -901;
				Lis->OnCurrentChanged(From, N.Spec->Id);
			}
		}
		else { S.Deploy -= 30; S.Counter = R.DelayEnd; }
	}

	void HSwitchRank(FSlot& S)   // 0x4ca230
	{
		const FActionSpec& R = Rec(S, 8);
		if (S.Counter == 0 && S.Phase != 4)
		{
			if (Pending >= 0 && Slots[Pending].Spec)
			{
				const EDFWeapon From = S.Spec->Id;
				Cur = Pending;
				Pending = -1;
				S.Deploy = 0;
				if (!(Slots[Cur].Spec->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted))) bToggle = false;
				Lis->OnCurrentChanged(From, Slots[Cur].Spec->Id);
			}
			S.Next = 0;
			S.Counter = R.DelayEnd;
			S.Phase = 4;
			return;
		}
		Generic(S, EAction::SwitchRank);
	}

	void Run(FSlot& S, int32 State)
	{
		switch (State)
		{
		case 0: HIdle(S, false); break;
		case 1: HIdle(S, true); break;
		case 2: HFire(S); break;
		case 3: HRecoil(S); break;
		case 4: HReload(S); break;
		case 5: HEmpty(S); break;
		case 6: HSwitchTo(S); break;
		case 7: HSwitchFrom(S); break;
		case 8: HSwitchRank(S); break;
		default: break;
		}
	}

	void StartNew(FSlot& S, bool bRestart)
	{
		if (S.State != 6 && S.State != 7) S.Prev = S.State;
		S.State = S.Next;
		S.Next = 0;
		S.Counter = Rec(S, S.State).DelayStart;
		S.Phase = 1;
		Run(S, S.State);
		if (bRestart) S.Phase = 0x40;
	}

	/** The tick function 0x4c8ca0 of the current slot. */
	void SlotTick()
	{
		FSlot* P = CurSlot();
		if (!P) return;
		FSlot& S = *P;
		const int32 St = S.State;
		if (S.Counter > 0) { --S.Counter; Run(S, St); return; }
		if (St == S.Next) { Run(S, St); return; }
		if (S.Phase == 4 || S.Phase == 0) { StartNew(S, false); return; }
		if (S.Phase == 0x40) { StartNew(S, true); return; }
		Run(S, St);
	}

	// ------------------------------------------------------------ selection (0x489680, 0x487270)
	/** 0x487270: the switch entry. */
	void Select(int32 Idx)
	{
		FSlot* C = CurSlot();
		if (!C || Idx < 0 || !Slots[Idx].Spec) return;
		FSlot& T = Slots[Idx];
		Pending = Idx;
		if (C->Spec->Category == T.Spec->Category)
		{
			if (!Deploy.bActive) RequestSwitchRank(*C);
		}
		else if (RequestSwitchFrom(*C) && C->Next == 7)
		{
			DeployBlend(false);
		}
		Ofs[0] = Ofs[1] = Ofs[2] = 0;
		if (T.Spec->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted))
		{
			SightsSlot = Idx;
			if (bToggle)
			{
				bSightsWanted = true;
				BlendStart(Sights, 1, T.Spec->Pos, T.Spec->TPos);
				SightsLatch = 0;
			}
		}
		else SightsSlot = T.Spec->Category == 2 ? Idx : -1;
	}

	void SelectCategory(int32 Cat)
	{
		PowerOn(false);
		FSlot* C = CurSlot();
		if (!C) return;
		if (C->State == 4 && C->Spec->Category == Cat) return;                // reloading: the same category key is ignored
		SightsSlot = (C->Spec->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted)) ? Cur : -1;
		int32 Start;
		if (Cat == C->Spec->Category) Start = C->Spec->Rank;
		else
		{
			const FSlot& Cand = Slots[Cat * PerCat];
			if (Cand.Spec && (Cand.Spec->Class == EClass::Primary || Cand.Spec->Class == EClass::Secondary || PoolOf(Cand.Spec->AmmoClass) != 0))
			{
				Select(Cat * PerCat);
				return;
			}
			Start = 0;
		}
		int32 R = Start;
		for (;;)
		{
			R = (R + 1) % PerCat;
			const FSlot& S = Slots[Cat * PerCat + R];
			if (S.Spec && Selectable(S)) { Select(Cat * PerCat + R); return; }
			if (R == Start) break;
		}
		Lis->OnSelectError();
	}

	void Cycle(int32 Dir)   // 0x4880b0
	{
		FSlot* C = CurSlot();
		if (!C || Dir == 0) return;
		const int32 Step = Dir > 0 ? 1 : -1;
		const int32 Start = SlotIndex(*C->Spec);
		int32 I = Start;
		for (int32 N = 0; N <= NumSlots; ++N)
		{
			I += Step;
			if (I >= NumSlots) I = 0;
			else if (I < 0) I = NumSlots - 1;
			if (Slots[I].Spec)
			{
				if (I != Start) Select(I);
				return;
			}
			if (I == Start) return;
		}
	}

	void PowerOn(bool bOn) { bPowerOn = bOn; }

	// ------------------------------------------------------------ per-tick view update (0x487650, the blend half)
	void UpdateBlends()
	{
		const FSlot* C = CurSlot();
		if (C && C->Spec->Category == 2 && !Sights.bActive)
		{
			const FWeaponSpec& W = *C->Spec;
			if (bFired && SightsLatch == 1) { BlendStart(Sights, DFWeapons::SightsBlendTicks, W.Pos, W.TPos); SightsLatch = 0; }
			else if (!bFired && SightsLatch == 0) { BlendStart(Sights, DFWeapons::SightsBlendTicks, W.TPos, W.Pos); SightsLatch = 1; }
		}
		if (Sights.bActive)
		{
			BlendStep(Sights, SpecAt(SightsSlot));
			if (!Sights.bActive) bToggle = bSightsWanted;
		}
		if (Deploy.bActive) BlendStep(Deploy, SpecAt(DeploySlot));
	}
};


FDFArsenal::FDFArsenal() : Impl(MakeUnique<FImpl>()) {}
FDFArsenal::~FDFArsenal() = default;

void FDFArsenal::Reset(const TArray<EDFWeapon>& Loadout, EDFWeapon Initial)
{
	FImpl& I = *Impl;
	I = FImpl();
	I.EnsureResolved();
	// give the weapons (0x4c9160): the slot is category * 25 + rank; the ammo pools are the sums of startrounds (0x4c94b0)
	for (EDFWeapon W : Loadout)
	{
		const FWeaponSpec* S = DFWeapons::Find(W);
		if (!S) continue;
		const int32 Idx = FImpl::SlotIndex(*S);
		if (Idx < 0 || Idx >= NumSlots) continue;
		I.Slots[Idx] = FImpl::FSlot();
		I.Slots[Idx].Spec = S;
		if (S->AmmoClass != EAmmoClass::Unlimited && S->StartRounds > 0) I.Pools[int32(S->AmmoClass)] += S->StartRounds;
	}
	// every clip is filled from its pool (the port's spawn rule; WEAPONS.md 1)
	bool bFilled[10] = {};
	for (EDFWeapon W : Loadout)
	{
		const FWeaponSpec* S = DFWeapons::Find(W);
		if (!S || S->ClipSize == -1 || S->AmmoClass == EAmmoClass::Unlimited) continue;
		if (S->ClipSlot && bFilled[S->ClipSlot]) continue;
		if (S->ClipSlot) bFilled[S->ClipSlot] = true;
		FImpl::FSlot& Slot = I.Slots[FImpl::SlotIndex(*S)];
		int32& Pool = I.Pools[int32(S->AmmoClass)];
		const int32 Take = FMath::Min(S->ClipSize, Pool);
		Pool -= Take;
		I.SetClip(Slot, Take);
	}
	// the initial weapon is assigned directly (0x489570): no switch, no deploy
	if (const FWeaponSpec* S = DFWeapons::Find(Initial))
	{
		const int32 Idx = FImpl::SlotIndex(*S);
		if (Idx >= 0 && Idx < NumSlots && I.Slots[Idx].Spec)
		{
			I.Cur = Idx;
			I.SightsSlot = ((S->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted)) || S->Category == 2) ? Idx : -1;
		}
	}
}

void FDFArsenal::Tick(const FTickInput& In, IDFArsenalListener& L)
{
	FImpl& I = *Impl;
	FImpl::FScope Scope(I, &L, &In);
	++I.Now;
	// soldier flag 0x8000000 ("fired a round") is cleared for everyone every 744 ticks (0x4aabd0)
	if (I.FiredCountdown > 0) --I.FiredCountdown;
	if (I.FiredCountdown == 0) { I.bFired = false; I.FiredCountdown = DFWeapons::FiredFlagPeriod; }
	if (I.bRepost)
	{
		I.bRepost = false;
		if (FImpl::FSlot* C = I.CurSlot()) { if (C->State != 2) I.RequestFire(*C, C->Power); else I.bRepost = true; }
	}
	I.SlotTick();
	I.UpdateBlends();
}

void FDFArsenal::SelectCategory(int32 Cat, IDFArsenalListener& L)
{
	FImpl::FScope Scope(*Impl, &L);
	if (Cat < 0 || Cat >= 10) return;
	Impl->SelectCategory(Cat);
}

void FDFArsenal::Cycle(int32 Dir, IDFArsenalListener& L)
{
	FImpl::FScope Scope(*Impl, &L);
	Impl->Cycle(Dir);
}

void FDFArsenal::PressFire(IDFArsenalListener& L)
{
	FImpl::FScope Scope(*Impl, &L);
	if (FImpl::FSlot* C = Impl->CurSlot()) Impl->RequestFire(*C, 0);
}

void FDFArsenal::PressAltFire()
{
	FImpl& I = *Impl;
	I.bPowerOn = false;
	const FImpl::FSlot* C = I.CurSlot();
	if (!C || C->Spec->Class != EClass::Grenade) return;
	// 0x4c8ee0: can a fire request start the action? (non-auto weapons: idle only)
	const bool bIdle = (C->Spec->Flags & DFWeapons::F_Auto) ? (C->State == 0 || C->State == 2 || C->State == 3) : C->State == 0;
	if (bIdle && I.HasAmmo(*C)) { I.bPowerOn = true; I.PowerT0 = I.Now; }
}

void FDFArsenal::ReleaseAltFire(IDFArsenalListener& L)
{
	FImpl& I = *Impl;
	FImpl::FScope Scope(I, &L);
	FImpl::FSlot* C = I.CurSlot();
	if (!I.bPowerOn || !C || C->Spec->Class != EClass::Grenade) return;
	// 0x48a014: p = (now - t0) * 0.0107527 (float), clamped to [0.1, 1], power = trunc(p * 255)
	float P = float(I.Now - I.PowerT0) * 0.0107527f;
	if (P > 1.0f) P = 1.0f;
	else if (P < 0.1f) P = 0.1f;
	const uint8 Power = uint8(int32(P * 255.0f));
	I.bPowerOn = false;
	I.RequestFire(*C, Power);
}

void FDFArsenal::ToggleSights()
{
	FImpl& I = *Impl;
	if (I.bPowerOn) return;   // blocked while the grenade power timer runs (0x489df1)
	const FImpl::FSlot* C = I.CurSlot();
	if (!C || !(C->Spec->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted)) || I.Sights.bActive) return;   // 0x488cc0
	const FWeaponSpec& W = *C->Spec;
	if (I.bToggle)
	{
		FImpl::BlendStart(I.Sights, I.SightsLatch == 0 ? DFWeapons::SightsBlendTicks : 1, W.TPos, W.Pos);
		I.bSightsWanted = false;
		I.bToggle = false;
		I.SightsLatch = 1;
	}
	else
	{
		I.bSightsWanted = true;
		FImpl::BlendStart(I.Sights, DFWeapons::SightsBlendTicks, W.Pos, W.TPos);
		I.SightsLatch = 0;
	}
}

void FDFArsenal::RequestReload()
{
	FImpl& I = *Impl;
	FImpl::FSlot* C = I.CurSlot();
	if (!C || C->Spec->ClipSize == -1) return;
	if (I.ClipOf(*C) == C->Spec->ClipSize || I.PoolOf(C->Spec->AmmoClass) == 0) return;   // 0x48a5be
	I.RequestReloadSlot(*C);
}

EDFWeapon FDFArsenal::GetCurrent() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C ? C->Spec->Id : EDFWeapon::None;
}

const FWeaponSpec* FDFArsenal::GetCurrentSpec() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C ? C->Spec : nullptr;
}

bool FDFArsenal::Has(EDFWeapon W) const
{
	for (const FImpl::FSlot& S : Impl->Slots) { if (S.Spec && S.Spec->Id == W) return true; }
	return false;
}

EAction FDFArsenal::GetAction() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C ? EAction(FMath::Clamp(C->State, 0, int32(EAction::Count) - 1)) : EAction::Idle;
}

int32 FDFArsenal::GetClip() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return (C && C->Spec->ClipSize != -1) ? Impl->ClipOf(*C) : -1;
}

int32 FDFArsenal::GetReserve() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C ? Impl->PoolOf(C->Spec->AmmoClass) : -1;
}

bool FDFArsenal::IsDrawn() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	if (!C) return false;
	if (C->Spec->Flags & DFWeapons::F_NoClipsNoDraw)
	{
		const bool bEmpty = kNoDrawChecksPoolOnly ? Impl->PoolOf(C->Spec->AmmoClass) == 0 : Impl->Total(*C) == 0;
		if (bEmpty) return false;
	}
	return true;
}

bool FDFArsenal::IsReloading() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C && C->State == 4;
}

bool FDFArsenal::GetSightsToggle() const { return Impl->bToggle; }

bool FDFArsenal::IsSightsUp() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	return C && Impl->bToggle && (C->Spec->Flags & (DFWeapons::F_Scoped | DFWeapons::F_Sighted)) && C->State != 4;
}

FVector FDFArsenal::GetModelPosUnits(bool bAirborne) const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	if (!C) return FVector::ZeroVector;
	FVector P(C->Spec->Pos[0], C->Spec->Pos[1], C->Spec->Pos[2]);
	if (!bAirborne && C->State != 4) P += FVector(Impl->Ofs[0], Impl->Ofs[1], Impl->Ofs[2]) / 256.0;
	return P;
}

double FDFArsenal::GetPowerBar() const
{
	const FImpl::FSlot* C = Impl->CurSlot();
	if (!C || C->Spec->Class != EClass::Grenade || !Impl->bPowerOn || !Impl->HasAmmo(*C)) return -1.0;
	return FMath::Min(1.0, double(Impl->Now - Impl->PowerT0) / double(DFWeapons::GrenadePowerTicks));
}

int64 FDFArsenal::GetNow() const { return Impl->Now; }

FString FDFArsenal::Describe() const
{
	static const TCHAR* Names[] = { TEXT("idle"), TEXT("emptyidle"), TEXT("fire"), TEXT("recoil"), TEXT("reload"), TEXT("empty"), TEXT("switchto"), TEXT("switchfrom"), TEXT("switchrank") };
	const FImpl& I = *Impl;
	const FImpl::FSlot* C = I.CurSlot();
	if (!C) return TEXT("no weapon");
	auto N = [](int32 S) { return Names[FMath::Clamp(S, 0, 8)]; };
	return FString::Printf(TEXT("%s state=%s next=%s prev=%s phase=0x%02x cnt=%d deploy=%d clip=%d pool=%d pend=%d sights=%d/%d dblend=%d sblend=%d ofs=(%d,%d,%d) power=%d"),
		C->Spec->DefName, N(C->State), N(C->Next), N(C->Prev), int32(C->Phase), C->Counter, C->Deploy, I.ClipOf(*C), I.PoolOf(C->Spec->AmmoClass),
		I.Pending, I.bToggle ? 1 : 0, I.bSightsWanted ? 1 : 0, I.Deploy.bActive ? 1 : 0, I.Sights.bActive ? 1 : 0,
		I.Ofs[0], I.Ofs[1], I.Ofs[2], I.bPowerOn ? int32(I.Now - I.PowerT0) : -1);
}

// ====================================================================== grenades (WEAPONS.md 6; mover 0x4cf2c0, launcher 0x48e2c0)

FQuat FDFGrenade::GetQuat() const
{
	return DF::EulerYXZ(Pitch, Yaw, Roll);
}

namespace
{
	constexpr double GrenadeGravity = 167.0 / 65536.0;      // m / tick^2 (0x4cf2c0: vz += -167)
	constexpr double GrenadeRestSpeed = 0x800 / 65536.0;    // horizontal speeds below this count as 0
	constexpr double GrenadeSpin = 1.0 * DF::DEG;           // 0xB60B60 per tick
	constexpr double WallBackoff = 0x800 / 65536.0;         // 0.03 m

	void MoveGrenade(FDFGrenade& G, const ADFWorld* World, IDFGrenadeListener& L)
	{
		const FVector Prev = G.Pos;
		G.Pos.X += G.Vel.X;
		G.Pos.Z += G.Vel.Z;
		G.Vel.Y -= GrenadeGravity;
		G.Pos.Y += G.Vel.Y;
		G.Yaw += G.YawRate;
		G.Pitch += G.PitchRate;
		double Hs = FMath::Sqrt(G.Vel.X * G.Vel.X + G.Vel.Z * G.Vel.Z);
		if (Hs < GrenadeRestSpeed) Hs = 0.0;

		// walls and props: sweep the move, stop short of the contact and slide (the exact EXE response 0x472290 / 0x48f6c0 is not decoded)
		if (World)
		{
			const FVector D = G.Pos - Prev;
			const double Len = D.Size();
			if (Len > 1e-6)
			{
				const FVector Dir = D / Len;
				double T = 0; FVector Pt, N;
				if (World->CastRay(Prev, Dir, Len + WallBackoff, T, Pt, N) && N.Y < 0.7 && FVector::DotProduct(G.Vel, N) < 0.0)
				{
					G.Pos = Pt - Dir * WallBackoff;
					G.Vel -= N * FVector::DotProduct(G.Vel, N);
					++G.Bounces;
					if (G.Bounces <= 5) L.OnGrenadeBounce(G);
				}
			}
		}

		// terrain / floor
		double H = 0.0;
		bool bGround = true;
		if (World)
		{
			H = World->GroundAt(FVector(G.Pos.X, FMath::Max(G.Pos.Y, Prev.Y), G.Pos.Z), 0.05);
			bGround = H > -1000.0;
		}
		if (bGround && G.Pos.Y < H)
		{
			G.Pos.Y = H;
			G.YawRate *= 0.8; G.PitchRate *= 0.8;
			G.Vel.X *= 0.8; G.Vel.Z *= 0.8;
			if (Hs != 0.0)
			{
				G.Vel.Y = -0.2 * G.Vel.Y;
				++G.Bounces;
				G.YawRate += double(FMath::RandRange(-5, 4)) * DF::DEG;
				G.PitchRate += double(FMath::RandRange(-5, 4)) * DF::DEG;
				G.bAtRest = false;
				if (G.Bounces <= 5) L.OnGrenadeBounce(G);
			}
			else
			{
				G.YawRate = G.PitchRate = G.RollRate = 0.0;
				G.Roll = 90.0 * DF::DEG;   // lies flat (0x3fffffc0)
				G.Pitch = 0.0;
				G.Vel.Y = 0.0;
				G.bAtRest = true;
			}
		}
	}
}

int32 FDFGrenadeSim::Throw(EDFWeapon Type, const FVector& Eye, double ViewYaw, double ViewPitch, uint8 Power)
{
	const FWeaponSpec* S = DFWeapons::Find(Type);
	if (!S || S->Class != EClass::Grenade) return 0;
	FDFGrenade G;
	G.Id = NextId++;
	G.Type = Type;
	G.Pos = Eye;
	double Speed = S->Ammo.Velocity / 62.0;                       // m per tick (0x48e721)
	if (Power > 0 && Power < 255) Speed *= double(Power) / 256.0;  // speed * power >> 8
	G.Vel = DF::EulerYXZ(ViewPitch, ViewYaw, 0.0).RotateVector(FVector(0, 0, -1)) * Speed;
	G.Yaw = ViewYaw; G.Pitch = ViewPitch; G.Roll = 0.0;
	G.YawRate = GrenadeSpin; G.PitchRate = GrenadeSpin; G.RollRate = 0.0;
	G.Life = FMath::RoundToInt(S->Ammo.MaxAge * 62.0);
	Grenades.Add(G);
	return G.Id;
}

void FDFGrenadeSim::Tick(const ADFWorld* World, IDFGrenadeListener& L)
{
	Removed.Reset();
	for (int32 i = 0; i < Grenades.Num();)
	{
		const int32 Id = Grenades[i].Id;
		MoveGrenade(Grenades[i], World, L);
		FDFGrenade& G = Grenades[i];
		++G.Age;
		const FWeaponSpec* S = DFWeapons::Find(G.Type);
		if (S && S->Ammo.ArmAge > 0 && G.Age == FMath::RoundToInt(S->Ammo.ArmAge * 62.0)) L.OnGrenadeArm(G);
		if (G.Age >= G.Life)
		{
			L.OnGrenadeDetonate(G);
			Removed.Add(Id);
			Grenades.RemoveAt(i);
			continue;
		}
		++i;
	}
}
