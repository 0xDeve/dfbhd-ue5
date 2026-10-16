// Player weapons: the weapon.def / ammo.def records of the port's arsenal (knife, Colt .45, CAR-15 auto/semi, flashbang, frag,
// smoke) and a 1:1 port of DFBHD.EXE's per-weapon action state machine (tick 0x4c8ca0, handlers 0x4c96e0..0x4ca230): selection,
// switch (switchfrom / switchto / switchrank) and deploy, fire, recoil, reload, empty, the first-person model position blends
// (deploy 0x48b090/0x48b280, sights / pistol 0x487650) and the grenade throw power (0x489b00 / 0x48a014). Recovered rules:
// Docs/WEAPONS.md. Everything ticks at 62 Hz; positions are three space (DFMath.h) unless noted.
//
// PUBLIC API IS A CONTRACT used by ADFPlayerPawn. Implementers may add private members.
#pragma once

#include "CoreMinimal.h"
#include "DFWeaponData.h"

class ADFWorld;

namespace DFWeapons
{
	/** weapon.def `flags` bits (EXE name table 0x6425a8, weapon def +0x28). */
	enum EFlag : uint32
	{
		F_Scoped = 0x1, F_Sighted = 0x2, F_Underwater = 0x4, F_AutoLob = 0x8, F_NoClipsNoDraw = 0x10, F_Burst = 0x20,
		F_Auto = 0x100, F_Primary = 0x400, F_Secondary = 0x800, F_OkWhileJumping = 0x2000,
	};
	/** weapon.def `weapon_class` (def +0x288; parser 0x4cc191). */
	enum class EClass : uint8 { None = 0, Primary = 1, Secondary = 2, Grenade = 3 };
	/** Ammo pools (ammoclass; Unlimited = no ammoclass, startrounds -1). */
	enum class EAmmoClass : uint8 { Unlimited = 0, C556, C45, GrenadeFB, GrenadeHE, GrenadeSM, Count };
	/** Action ids (weapon.def action names, EXE name table 0x642560). */
	enum class EAction : uint8 { Idle = 0, EmptyIdle, Fire, Recoil, Reload, Empty, SwitchTo, SwitchFrom, SwitchRank, Count };
	/** Kill zone type of the round (ammo.def kztype; EXE table 0x61f940). */
	enum class EKillZone : uint8 { None = 0, Knife = 1, C4 = 5 };

	/** `delaystart` / `delayend` value `auto`. */
	inline constexpr int32 AutoDelay = -1;

	struct FActionSpec
	{
		int32 DelayStart = 0;   // ticks; AutoDelay = -1; omitted = 0
		int32 DelayEnd = 0;
	};

	struct FAmmoSpec
	{
		double Velocity = 0;       // m/s
		double MaxAge = 0;         // s (grenades: fuse / life)
		double ArmAge = 0;         // s (smoke: the cloud starts)
		int32 Recoil = 0;          // per shot: view recoil accumulator += Recoil << 20 (binary angle)
		double ImpactDamage = 0;
		EKillZone KillZone = EKillZone::None;
		double KzMinRadius = 0, KzMaxRadius = 0, KzDamage = 0;
		double KzHalfAngleDeg = 0; // knife: kz_pieslice 35 -> (35 >> 1) = 17 deg either side of the view yaw
		int32 KzPhysics = 0;       // 1 = blast push, 3 = flashbang
	};

	/** weapon.def `hudrndgfx a b c d n` (start dx, dy, step dx, dy, rounds per bar); HasClipGfx = `hudclipgfx` present. */
	struct FHudRnd
	{
		int32 StartDx = 0, StartDy = 0, StepDx = 0, StepDy = 0, PerBar = 1;
	};

	struct FWeaponSpec
	{
		EDFWeapon Id = EDFWeapon::None;
		const TCHAR* DefName = TEXT("");    // weapon.def name, e.g. WPN_CAR15AUTO
		const TCHAR* HudName = TEXT("");    // Gametext WepDes[DefName], e.g. "CAR15 - Auto"
		int32 Category = 0, Rank = 0;       // inventory slot = Category * 25 + Rank
		EClass Class = EClass::None;
		uint32 Flags = 0;
		EAmmoClass AmmoClass = EAmmoClass::Unlimited;
		int32 ClipSize = -1;                // -1 = no clip (knife)
		int32 StartRounds = -1;             // added to the ammo class pool at spawn (-1 = unlimited)
		int32 ClipSlot = 0;                 // weapons with the same non-zero clipslot share one clip (CAR-15 auto / semi)
		double ErrorHipDeg = 0, ErrorUpDeg = 0;
		int32 Pos[3] = { 0, 0, 0 };         // first-person model origin, eye frame (x forward, y left, z up), 1/256 m
		int32 TPos[3] = { 0, 0, 0 };        // sights / pistol firing pose (same frame); valid if bHasTPos
		bool bHasTPos = false;
		int32 SpecialHold = 0, AttackAnim = 0, RunAnim = 0;   // def +0x40 / +0x44 / +0x48
		FActionSpec Actions[int32(EAction::Count)];
		FAmmoSpec Ammo;
		bool bHasClipGfx = false;
		FHudRnd HudRnd;
		// first-person rig (dfdata.json keys); null = none (that action shows the idle clip)
		const TCHAR* FpSkeleton = nullptr;  // also the skeletal mesh's skeleton
		const TCHAR* FpModel = nullptr;     // weapon geometry model (userpoints, e.g. mflash01)
		const TCHAR* FpIdleClip = nullptr;
		const TCHAR* FpFireClip = nullptr;
		const TCHAR* FpReloadClip = nullptr;
		const TCHAR* FpEmptyIdleClip = nullptr;
		// third-person
		const TCHAR* TpModel = nullptr;     // dfdata model key (userpoints)
		const TCHAR* TpAttach = nullptr;    // dfdata tpAttach key
	};

	/** The spec of a weapon (EDFWeapon::None / Count: nullptr). */
	PROJECTDF_API const FWeaponSpec* Find(EDFWeapon W);
	/** All specs of the port, in EDFWeapon order (Knife .. SmokeGrenade). */
	PROJECTDF_API TConstArrayView<FWeaponSpec> All();

	inline bool HasFlag(const FWeaponSpec& S, uint32 F) { return (S.Flags & F) != 0; }

	/** First-person "lowered" pose of a switch: (-100, -20, -300) pos units (0x48b090 literal). */
	inline constexpr int32 DeployLowered[3] = { -100, -20, -300 };
	inline constexpr int32 DeployTicks = 31;        // 0x48b090: lowering / raising blend frames
	inline constexpr int32 SightsBlendTicks = 15;   // 0x48b280 frames for pos <-> tpos (sights, pistol firing pose)
	inline constexpr int32 FiredFlagPeriod = 744;   // 0x4aabd0: soldier flag 0x8000000 cleared for everyone every 0x2e8 ticks
	inline constexpr int32 GrenadePowerTicks = 93;  // 0x48a014: power = clamp(held / 93, 0.1, 1) * 255
}

/** Events raised by FDFArsenal::Tick and the input calls (implemented by the pawn). */
class IDFArsenalListener
{
public:
	virtual ~IDFArsenalListener() = default;
	/** An action of the current weapon started (phase 1, the tick its handler first runs): first-person clip and the
	 *  action's `soundset` (switchfrom / switchto / reload / empty / grenade throw). */
	virtual void OnActionStart(EDFWeapon W, DFWeapons::EAction A) = 0;
	/** The fire handler executed this tick (countdown reached 0): shot / stab / throw, plus the fire action's `soundsetend`.
	 *  Power = the throw power byte [w+0x5b] (0 or 255 = full speed). The clip was already decremented. */
	virtual void OnFire(EDFWeapon W, uint8 Power) = 0;
	/** The reload handler moved ammo (end of delaystart). */
	virtual void OnReloadDone(EDFWeapon W) = 0;
	/** The current weapon changed (category switch commit, switchrank commit, spawn). From may be None. */
	virtual void OnCurrentChanged(EDFWeapon From, EDFWeapon To) = 0;
	/** A selection key found nothing usable (error beep [0xa02030]). */
	virtual void OnSelectError() = 0;
};

/** The local player's weapons: 10 x 25 inventory slots, ammo pools, the action state machine of the current slot and the
 *  first-person model position blends. Deterministic, no UObject. */
class PROJECTDF_API FDFArsenal
{
public:
	FDFArsenal();
	~FDFArsenal();

	/** Spawn: give the weapons (each slot set up, ammo class pools = sum of StartRounds, every clip filled from its pool),
	 *  select Initial directly (no switch, no deploy; 0x489570) and reset the timers. */
	void Reset(const TArray<EDFWeapon>& Loadout, EDFWeapon Initial);

	struct FTickInput
	{
		bool bFireHeld = false;    // primary fire key level (auto weapons re-post the fire request while held)
		bool bAirborne = false;    // soldier flag 0x2000: CanFire refuses weapons without OkWhileJumping; model offset not applied
	};
	/** One 62 Hz tick: the current slot's action machine (0x4c8ca0), the model position blends (0x487650), timers. */
	void Tick(const FTickInput& In, IDFArsenalListener& L);

	// ---- input (call in the order the original processes them; each may raise listener events immediately)
	/** Keys 1..9 (commands 0xc9..0xd1): select category Cat (0x489680). */
	void SelectCategory(int32 Cat, IDFArsenalListener& L);
	/** Mouse wheel / [ ] (command 0xd3): Dir = -1 previous, +1 next slot holding a weapon (0x4880b0). */
	void Cycle(int32 Dir, IDFArsenalListener& L);
	/** Primary fire pressed (command 0x95): fire request with power 0. */
	void PressFire(IDFArsenalListener& L);
	/** Secondary mouse button down (raw event 8, 0x489b00): starts the grenade throw-power timer for an idle grenade with ammo. */
	void PressAltFire();
	/** Secondary mouse button released (command 0x96, 0x48a014): grenade throw with power clamp(held/93, 0.1, 1). */
	void ReleaseAltFire(IDFArsenalListener& L);
	/** Toggle Scope (command 6, 0x488cc0): only Sighted/Scoped weapons, blocked while the throw-power timer runs. Call after PressAltFire. */
	void ToggleSights();
	/** Reload key (command 0xd2): needs reserve > 0 and a clip that is not full. */
	void RequestReload();

	// ---- state for the pawn / HUD / rigs
	EDFWeapon GetCurrent() const;
	const DFWeapons::FWeaponSpec* GetCurrentSpec() const;
	bool Has(EDFWeapon W) const;
	DFWeapons::EAction GetAction() const;
	/** Rounds in the current weapon's clip (-1 for no clip). */
	int32 GetClip() const;
	/** Pool of the current weapon's ammo class (-1 = unlimited). */
	int32 GetReserve() const;
	/** NoClipsNoDraw weapons with nothing left (clip + pool == 0) are not drawn (0x488920). */
	bool IsDrawn() const;
	bool IsReloading() const;
	/** Sights toggle state [0x96c3e0] (kept across category switches; cleared by a switchrank to a non-Sighted weapon). */
	bool GetSightsToggle() const;
	/** Toggle on, current weapon Sighted (or Scoped) and not reloading: FOV x0.8, scope overlay, error_up. */
	bool IsSightsUp() const;
	/** First-person model origin in the eye frame (x forward, y left, z up), 1/256 m: weapon pos + the shared blend offset
	 *  [0x96c4a4] when not airborne and not reloading (0x4892f0). Kick / sway are the viewmodel's. */
	FVector GetModelPosUnits(bool bAirborne) const;
	/** Grenade power bar fraction 0..1 (HUD 0x50a190), or < 0 when hidden (not a grenade, no ammo, timer not running). */
	double GetPowerBar() const;
	/** Ticks since Reset (the original's [0x9f374c] tick counter role). */
	int64 GetNow() const;
	/** Short state dump for -DFDbg logs: current weapon, action, phase, counter, next, deploy counter, blends. */
	FString Describe() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

/** A thrown grenade (three space; m, m per 62 Hz tick, radians). */
struct FDFGrenade
{
	int32 Id = 0;
	EDFWeapon Type = EDFWeapon::None;
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;   // m per tick
	double Yaw = 0, Pitch = 0, Roll = 0; // model orientation (three: yaw about +Y, then pitch, then roll)
	double YawRate = 0, PitchRate = 0, RollRate = 0;   // rad per tick (spin)
	int32 Age = 0;                       // ticks since the throw
	int32 Life = 0;                      // max_age * 62
	int32 Bounces = 0;
	bool bAtRest = false;
	/** Model orientation as a three-space quaternion. */
	FQuat GetQuat() const;
};

/** Grenade events (implemented by the pawn): sounds, effects, kill zones. */
class IDFGrenadeListener
{
public:
	virtual ~IDFGrenadeListener() = default;
	virtual void OnGrenadeBounce(const FDFGrenade& G) = 0;      // ground / wall contact while moving (first 5 bounces)
	virtual void OnGrenadeArm(const FDFGrenade& G) = 0;         // Age == arm_age * 62 (smoke: pop + cloud starts)
	virtual void OnGrenadeDetonate(const FDFGrenade& G) = 0;    // Age == max_age * 62: frag / flash burst + kill zone; smoke: removed
};

/** The grenade mover ("useownmove" item move function `nade`, 0x4cf2c0) for the local player's thrown grenades. */
class PROJECTDF_API FDFGrenadeSim
{
public:
	/** Spawn at the eye, direction = view yaw / pitch (no added angle, no spread, thrower velocity not added),
	 *  speed = Velocity * (Power / 256 when 0 < Power < 255, else 1) (0x48e2c0). Returns the grenade id. */
	int32 Throw(EDFWeapon Type, const FVector& Eye, double ViewYaw, double ViewPitch, uint8 Power);
	/** One 62 Hz tick of every grenade (gravity, terrain / object collision, bounce, spin, fuse); raises events; removes the
	 *  detonated ones after OnGrenadeDetonate. World may be null (flat ground at y = 0). */
	void Tick(const ADFWorld* World, IDFGrenadeListener& L);
	const TArray<FDFGrenade>& GetGrenades() const { return Grenades; }
	/** Ids removed during the last Tick (for the effects to drop their models). */
	const TArray<int32>& GetRemovedLastTick() const { return Removed; }

private:
	TArray<FDFGrenade> Grenades;
	TArray<int32> Removed;
	int32 NextId = 1;
};
