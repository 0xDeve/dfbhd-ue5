// Automation tests of the weapon system (FDFArsenal / FDFGrenadeSim), run headless:
//   UnrealEditor-Cmd.exe ProjectDF.uproject -ExecCmds="Automation RunTests DF.Weapons;Quit" -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
// Tick numbers: the input calls happen "at tick T" (between tick T and T + 1, i.e. GetNow() == T); the first Tick afterwards is T + 1.
// Expected values come from the ported EXE handlers (Docs/WEAPONS.md, Docs/RE/switch_sim.py); where they differ from WEAPONS.md the
// comment says why (the `auto` delays are the animation lengths: see kResolveAutoDelays in DFWeapons.cpp).
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "DFWeapons.h"
#include "DFMath.h"

using namespace DFWeapons;

namespace
{
	enum class EEv : uint8 { Start, Fire, ReloadDone, Changed, SelectError };

	struct FEv
	{
		int64 Tick = 0;
		EEv Kind = EEv::Start;
		EDFWeapon W = EDFWeapon::None;
		EDFWeapon To = EDFWeapon::None;
		EAction A = EAction::Idle;
		int32 Power = 0;
	};

	/** Recording listener plus the arsenal under test. */
	struct FHarness : IDFArsenalListener
	{
		FDFArsenal A;
		TArray<FEv> Ev;

		explicit FHarness(EDFWeapon Initial = EDFWeapon::Car15Auto)
		{
			A.Reset({ EDFWeapon::Knife, EDFWeapon::Colt45, EDFWeapon::Car15Auto, EDFWeapon::Car15Semi, EDFWeapon::Flashbang,
				EDFWeapon::FragGrenade, EDFWeapon::SmokeGrenade }, Initial);
		}

		virtual void OnActionStart(EDFWeapon W, EAction Act) override { FEv E; E.Tick = A.GetNow(); E.Kind = EEv::Start; E.W = W; E.A = Act; Ev.Add(E); }
		virtual void OnFire(EDFWeapon W, uint8 Power) override { FEv E; E.Tick = A.GetNow(); E.Kind = EEv::Fire; E.W = W; E.Power = Power; Ev.Add(E); }
		virtual void OnReloadDone(EDFWeapon W) override { FEv E; E.Tick = A.GetNow(); E.Kind = EEv::ReloadDone; E.W = W; Ev.Add(E); }
		virtual void OnCurrentChanged(EDFWeapon From, EDFWeapon To) override { FEv E; E.Tick = A.GetNow(); E.Kind = EEv::Changed; E.W = From; E.To = To; Ev.Add(E); }
		virtual void OnSelectError() override { FEv E; E.Tick = A.GetNow(); E.Kind = EEv::SelectError; Ev.Add(E); }

		void Tick(int32 N = 1, bool bHeld = false, bool bAir = false)
		{
			FDFArsenal::FTickInput In; In.bFireHeld = bHeld; In.bAirborne = bAir;
			for (int32 i = 0; i < N; ++i) A.Tick(In, *this);
		}
		int64 Now() const { return A.GetNow(); }

		/** Tick of the first event of Kind (and weapon / action when given) at or after FromTick, or -1. */
		int64 Find(EEv Kind, int64 FromTick = 0, EDFWeapon W = EDFWeapon::None, EAction Act = EAction::Count) const
		{
			for (const FEv& E : Ev)
			{
				if (E.Tick < FromTick || E.Kind != Kind) continue;
				if (W != EDFWeapon::None && E.W != W) continue;
				if (Kind == EEv::Start && Act != EAction::Count && E.A != Act) continue;
				return E.Tick;
			}
			return -1;
		}
		int32 Count(EEv Kind, int64 FromTick = 0) const
		{
			int32 N = 0;
			for (const FEv& E : Ev) { if (E.Kind == Kind && E.Tick >= FromTick) ++N; }
			return N;
		}
		TArray<int64> Ticks(EEv Kind, EDFWeapon W = EDFWeapon::None) const
		{
			TArray<int64> R;
			for (const FEv& E : Ev) { if (E.Kind == Kind && (W == EDFWeapon::None || E.W == W)) R.Add(E.Tick); }
			return R;
		}
		const FEv* FindEv(EEv Kind, int64 FromTick = 0) const
		{
			for (const FEv& E : Ev) { if (E.Kind == Kind && E.Tick >= FromTick) return &E; }
			return nullptr;
		}
	};
}

// ------------------------------------------------------------------ switching

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSwitchTimeline, "DF.Weapons.SwitchCategoryTimeline", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSwitchTimeline::RunTest(const FString& Parameters)
{
	// SWITCH_RE.md 3a / switch_sim.py: request tick 0, SwitchFrom sound 1, commit 34, SwitchTo sound 35, first accepted fire 70
	FHarness H(EDFWeapon::Car15Auto);
	H.Tick(5);
	const int64 T0 = H.Now();
	H.A.SelectCategory(2, H);
	H.Tick(80);
	TestEqual(TEXT("SwitchFrom starts (old weapon's sound)"), H.Find(EEv::Start, T0, EDFWeapon::Car15Auto, EAction::SwitchFrom) - T0, int64(1));
	TestEqual(TEXT("commit"), H.Find(EEv::Changed, T0) - T0, int64(34));
	const FEv* Ch = H.FindEv(EEv::Changed, T0);
	TestTrue(TEXT("commit Car15Auto -> Colt45"), Ch && Ch->W == EDFWeapon::Car15Auto && Ch->To == EDFWeapon::Colt45);
	TestEqual(TEXT("SwitchTo starts (new weapon's sound)"), H.Find(EEv::Start, T0, EDFWeapon::Colt45, EAction::SwitchTo) - T0, int64(35));
	TestEqual(TEXT("current weapon"), H.A.GetCurrent(), EDFWeapon::Colt45);

	// first tick a fire request is accepted: press after tick p, the shot happens at p + 1
	int64 FirstFire = -1;
	for (int32 P = 40; P <= 80 && FirstFire < 0; ++P)
	{
		FHarness G(EDFWeapon::Car15Auto);
		G.Tick(5);
		const int64 S0 = G.Now();
		G.A.SelectCategory(2, G);
		G.Tick(P);
		G.A.PressFire(G);
		G.Tick(5);
		const int64 F = G.Find(EEv::Fire, S0);
		if (F >= 0) FirstFire = F - S0;
	}
	TestEqual(TEXT("first accepted fire (tick after the request)"), FirstFire, int64(70));

	// the old weapon cannot fire while it goes down (state switchfrom): press during the lowering is lost
	{
		FHarness G(EDFWeapon::Car15Auto);
		G.Tick(5);
		G.A.SelectCategory(2, G);
		G.Tick(10);
		G.A.PressFire(G);
		G.Tick(100);
		TestEqual(TEXT("no shot from a press during the lowering"), G.Count(EEv::Fire), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSwitchRank, "DF.Weapons.SwitchRankAutoSemi", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSwitchRank::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Car15Auto);
	H.Tick(5);
	const int64 T0 = H.Now();
	H.A.SelectCategory(3, H);   // same category: auto -> semi
	H.Tick(10);
	TestEqual(TEXT("commit tick (switchrank counter 1)"), H.Find(EEv::Changed, T0) - T0, int64(2));
	const FEv* Ch = H.FindEv(EEv::Changed, T0);
	TestTrue(TEXT("auto -> semi"), Ch && Ch->W == EDFWeapon::Car15Auto && Ch->To == EDFWeapon::Car15Semi);
	TestEqual(TEXT("no SwitchTo / SwitchFrom sounds"), H.Count(EEv::Start, T0) - H.Count(EEv::Start, T0), 0);
	bool bSwitchSound = false;
	for (const FEv& E : H.Ev) { if (E.Kind == EEv::Start && (E.A == EAction::SwitchTo || E.A == EAction::SwitchFrom)) bSwitchSound = true; }
	TestFalse(TEXT("switchrank plays no switch sound"), bSwitchSound);
	TestEqual(TEXT("shares the clip"), H.A.GetClip(), 30);

	int64 FirstFire = -1;
	for (int32 P = 0; P <= 10 && FirstFire < 0; ++P)
	{
		FHarness G(EDFWeapon::Car15Auto);
		G.Tick(5);
		const int64 S0 = G.Now();
		G.A.SelectCategory(3, G);
		G.Tick(P);
		G.A.PressFire(G);
		G.Tick(3);
		const int64 F = G.Find(EEv::Fire, S0);
		if (F >= 0) FirstFire = F - S0;
	}
	TestTrue(TEXT("the other fire mode fires ~4 ticks after the key"), FirstFire > 0 && FirstFire <= 4);

	// and back: semi -> auto
	H.A.SelectCategory(3, H);
	H.Tick(10);
	TestEqual(TEXT("semi -> auto"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSwitchDropped, "DF.Weapons.SwitchDroppedDuringReload", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSwitchDropped::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Car15Auto);
	H.Tick(5);
	// fire a few rounds, then reload
	H.A.PressFire(H);
	H.Tick(40, true);
	H.Tick(20, false);
	TestTrue(TEXT("rounds were fired"), H.A.GetClip() < 30);
	H.A.RequestReload();
	H.Tick(10);
	TestTrue(TEXT("reloading"), H.A.IsReloading());
	const int64 T0 = H.Now();
	H.A.SelectCategory(2, H);       // a category change during the reload: the request is silently dropped
	H.Tick(60);
	TestEqual(TEXT("no switchfrom started"), H.Find(EEv::Start, T0, EDFWeapon::Car15Auto, EAction::SwitchFrom), int64(-1));
	TestEqual(TEXT("still the CAR-15"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
	H.A.SelectCategory(3, H);       // same category while reloading: ignored (0x489680)
	H.Tick(5);
	TestEqual(TEXT("no switchrank during the reload"), H.Count(EEv::Changed, T0), 0);

	// a press after the reload goes through (the pending switch was dropped, not queued)
	H.Tick(200);
	TestEqual(TEXT("clip full"), H.A.GetClip(), 30);
	H.A.SelectCategory(2, H);
	H.Tick(40);
	TestEqual(TEXT("switch works once idle"), H.A.GetCurrent(), EDFWeapon::Colt45);
	return true;
}

// ------------------------------------------------------------------ guns

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsAutoCadence, "DF.Weapons.Car15AutoCadence", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsAutoCadence::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Car15Auto);
	H.Tick(5);
	H.A.PressFire(H);
	H.Tick(120, true);
	const TArray<int64> F = H.Ticks(EEv::Fire);
	TestTrue(TEXT("fires while held"), F.Num() >= 10);
	// fire (anim cr15_1D: delayend auto = 3 ticks) -> recoil (delayend 2) -> the fire request is re-posted while the key is held:
	// one round every 7 ticks. (WEAPONS.md assumed `auto` = no wait = 4 ticks; the weapon loader makes it the clip length.)
	bool bSame = true;
	for (int32 i = 2; i < F.Num(); ++i) { if (F[i] - F[i - 1] != 7) bSame = false; }
	TestTrue(TEXT("constant 7 tick cadence"), bSame);
	if (F.Num() > 2) AddInfo(FString::Printf(TEXT("CAR-15 interval %lld ticks"), F[2] - F[1]));
	// released: stops
	const int32 Before = H.Count(EEv::Fire);
	H.Tick(40, false);
	TestEqual(TEXT("no shot after release"), H.Count(EEv::Fire), Before);
	TestEqual(TEXT("clip counted"), H.A.GetClip(), 30 - Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSemiPress, "DF.Weapons.SemiAndColtOnePerPress", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSemiPress::RunTest(const FString& Parameters)
{
	for (EDFWeapon W : { EDFWeapon::Colt45, EDFWeapon::Car15Semi })
	{
		FHarness H(W);
		H.Tick(5);
		H.A.PressFire(H);
		H.Tick(100, true);   // the key stays down: still one round
		TestEqual(*FString::Printf(TEXT("%d: one round per press"), int32(W)), H.Count(EEv::Fire), 1);

		// presses every tick: the next round is accepted as soon as the weapon is idle again
		FHarness G(W);
		G.Tick(5);
		for (int32 i = 0; i < 80; ++i) { G.A.PressFire(G); G.Tick(1, true); }
		const TArray<int64> F = G.Ticks(EEv::Fire);
		TestTrue(TEXT("several rounds"), F.Num() >= 5);
		int64 MinGap = MAX_int64;
		for (int32 i = 1; i < F.Num(); ++i) MinGap = FMath::Min(MinGap, F[i] - F[i - 1]);
		AddInfo(FString::Printf(TEXT("weapon %d: min interval between rounds %lld ticks"), int32(W), MinGap));
		// Colt: fire (colt_1f, 2 frames: delayend auto = 5 - 0) -> recoil (0) -> idle: 8 ticks. Semi CAR-15: fire 1+3, recoil 1+2, idle 1: 8 ticks
		TestTrue(TEXT("min interval >= 3 ticks (WEAPONS.md lower bound)"), MinGap >= 3);
		TestEqual(TEXT("min interval (anim length based)"), MinGap, int64(8));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsColtReload, "DF.Weapons.ColtReload", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsColtReload::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Colt45);
	H.Tick(5);
	TestEqual(TEXT("clip"), H.A.GetClip(), 7);
	TestEqual(TEXT("pool"), H.A.GetReserve(), 21);
	for (int32 i = 0; i < 3; ++i) { H.A.PressFire(H); H.Tick(12); }
	TestEqual(TEXT("4 rounds left"), H.A.GetClip(), 4);
	const int64 T0 = H.Now();
	H.A.RequestReload();
	H.Tick(1);
	TestEqual(TEXT("reload action starts"), H.Find(EEv::Start, T0, EDFWeapon::Colt45, EAction::Reload) - T0, int64(1));
	// the ammo moves with the network message at the start (0x4c9d70 -> 0x423e40 -> 0x4c9e10)
	TestEqual(TEXT("ammo moved"), H.Find(EEv::ReloadDone, T0) - T0, int64(1));
	TestEqual(TEXT("clip 7"), H.A.GetClip(), 7);
	TestEqual(TEXT("pool 18"), H.A.GetReserve(), 18);

	// a switch request during the first delaystart (90) ticks is dropped
	H.Tick(20);
	H.A.SelectCategory(1, H);
	H.Tick(5);
	TestEqual(TEXT("switch dropped during the reload"), H.A.GetCurrent(), EDFWeapon::Colt45);

	// the reload action lasts delaystart 90 + delayend (colt_1r 61 frames = 128 ticks - 90) 38 ticks: idle at +129
	int64 FirstFire = -1;
	for (int32 P = 100; P <= 140 && FirstFire < 0; ++P)
	{
		FHarness G(EDFWeapon::Colt45);
		G.Tick(5);
		for (int32 i = 0; i < 3; ++i) { G.A.PressFire(G); G.Tick(12); }
		const int64 S0 = G.Now();
		G.A.RequestReload();
		G.Tick(P);
		G.A.PressFire(G);
		G.Tick(3);
		const int64 F = G.Find(EEv::Fire, S0);
		if (F >= 0) FirstFire = F - S0;
	}
	TestEqual(TEXT("first shot after a reload (ticks after the key)"), FirstFire, int64(131));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsAmmo, "DF.Weapons.AmmoAutoReloadAndEmpty", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsAmmo::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Car15Auto);
	H.Tick(5);
	TestEqual(TEXT("clip 30"), H.A.GetClip(), 30);
	TestEqual(TEXT("pool 270"), H.A.GetReserve(), 270);
	// A held trigger only re-posts the fire command at the end of a recoil (0x4c9bea); after the auto reload the player has to click
	// again, so this "player" clicks on every tick while the button is down and the gun is loaded.
	auto Hold = [&H](int32 N)
	{
		// (clicking on an empty gun every tick would keep overriding the reload request: the original does the same)
		for (int32 i = 0; i < N; ++i) { if (H.A.GetClip() > 0 || H.A.GetReserve() == 0) H.A.PressFire(H); H.Tick(1, true); }
	};
	Hold(400);
	TestTrue(TEXT("auto reload happened"), H.Count(EEv::ReloadDone) >= 1);
	TestTrue(TEXT("keeps firing"), H.Count(EEv::Fire) > 30);
	{
		// a held trigger alone does not resume firing after the reload
		FHarness G(EDFWeapon::Car15Auto);
		G.Tick(5);
		G.A.PressFire(G);
		G.Tick(400, true);
		TestEqual(TEXT("held trigger: the clip (30) is shot, the auto reload runs, no further round"), G.Count(EEv::Fire), 30);
		TestEqual(TEXT("reloaded"), G.A.GetClip(), 30);
	}

	// the rest: hold the trigger until everything is gone
	Hold(6000);
	H.Tick(30, false);
	TestEqual(TEXT("300 rounds fired in total"), H.Count(EEv::Fire), 300);
	TestEqual(TEXT("clip 0"), H.A.GetClip(), 0);
	TestEqual(TEXT("pool 0"), H.A.GetReserve(), 0);
	TestEqual(TEXT("emptyidle"), H.A.GetAction(), EAction::EmptyIdle);
	const int32 Shots = H.Count(EEv::Fire);
	const int64 T0 = H.Now();
	H.A.PressFire(H);
	H.Tick(10);
	TestEqual(TEXT("no round from an empty gun"), H.Count(EEv::Fire), Shots);
	TestTrue(TEXT("dry click (empty action)"), H.Find(EEv::Start, T0, EDFWeapon::Car15Auto, EAction::Empty) > 0);
	TestEqual(TEXT("back to emptyidle"), H.A.GetAction() == EAction::EmptyIdle || H.A.GetAction() == EAction::Empty, true);
	// the semi variant shares the empty clip and pool
	H.Tick(10);
	H.A.SelectCategory(3, H);
	H.Tick(10);
	// the semi variant (class none) needs ammo to be selected; the auto (class primary) is selectable empty: the cycle ends on itself
	TestEqual(TEXT("empty: key 3 does not switch to the semi"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
	return true;
}

// ------------------------------------------------------------------ knife

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsKnife, "DF.Weapons.KnifeFiresFirstTick", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsKnife::RunTest(const FString& Parameters)
{
	FHarness H(EDFWeapon::Knife);
	H.Tick(5);
	const int64 T0 = H.Now();
	H.A.PressFire(H);
	H.Tick(1);
	TestEqual(TEXT("the stab resolves on the first tick"), H.Find(EEv::Fire, T0) - T0, int64(1));
	TestEqual(TEXT("clip -1"), H.A.GetClip(), -1);
	TestEqual(TEXT("unlimited pool"), H.A.GetReserve(), -1);
	H.Tick(100, true);
	TestEqual(TEXT("one stab per press"), H.Count(EEv::Fire), 1);
	// re-stab possible after fire (m9k_1f 15 frames = 32 ticks) + recoil (3) + idle
	int64 Second = -1;
	for (int32 P = 5; P <= 80 && Second < 0; ++P)
	{
		FHarness G(EDFWeapon::Knife);
		G.Tick(5);
		const int64 S0 = G.Now();
		G.A.PressFire(G);
		G.Tick(P);
		G.A.PressFire(G);
		G.Tick(3);
		const TArray<int64> F = G.Ticks(EEv::Fire);
		if (F.Num() >= 2) Second = F[1] - S0;
	}
	AddInfo(FString::Printf(TEXT("second stab %lld ticks after the first press"), Second));
	TestTrue(TEXT("second stab accepted"), Second > 3);
	return true;
}

// ------------------------------------------------------------------ selection

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSelection, "DF.Weapons.SelectionRules", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSelection::RunTest(const FString& Parameters)
{
	// keys with no weapon: error beep
	{
		FHarness H;
		H.Tick(5);
		for (int32 Key : { 7, 8, 9 })
		{
			const int32 Before = H.Count(EEv::SelectError);
			H.A.SelectCategory(Key, H);
			TestEqual(*FString::Printf(TEXT("key %d beeps"), Key), H.Count(EEv::SelectError), Before + 1);
		}
		H.Tick(60);
		TestEqual(TEXT("nothing changed"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
	}
	// wheel: neighbouring slot with a weapon (no ammo test): colt (50) is before car15 auto (75); next from car15 semi (76) is flashbang (100)
	{
		FHarness H;
		H.Tick(5);
		H.A.Cycle(-1, H);
		H.Tick(80);
		TestEqual(TEXT("wheel up from the CAR-15 auto -> Colt"), H.A.GetCurrent(), EDFWeapon::Colt45);
		H.A.Cycle(+1, H);
		H.Tick(80);
		TestEqual(TEXT("wheel down from the Colt -> CAR-15 auto"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
		H.A.Cycle(+1, H);
		H.Tick(10);
		TestEqual(TEXT("wheel down from the auto -> semi (same category: switchrank)"), H.A.GetCurrent(), EDFWeapon::Car15Semi);
		H.Tick(80);
		H.A.Cycle(+1, H);
		H.Tick(80);
		TestEqual(TEXT("wheel down from the semi -> flashbang"), H.A.GetCurrent(), EDFWeapon::Flashbang);
	}
	// category keys pick rank 0 of that category
	{
		FHarness H;
		H.Tick(5);
		H.A.SelectCategory(5, H);
		H.Tick(80);
		TestEqual(TEXT("5 -> frag"), H.A.GetCurrent(), EDFWeapon::FragGrenade);
		H.A.SelectCategory(1, H);
		H.Tick(80);
		TestEqual(TEXT("1 -> knife"), H.A.GetCurrent(), EDFWeapon::Knife);
		// the request is refused while the new weapon's switchto has not passed its first branch ... but a quick second key is accepted later
		H.A.SelectCategory(3, H);
		H.Tick(80);
		TestEqual(TEXT("3 -> CAR-15 auto (rank 0)"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
	}
	// no ammo: a grenade key with an empty stack beeps
	{
		FHarness H(EDFWeapon::Flashbang);
		H.Tick(5);
		for (int32 N = 0; N < 5; ++N)
		{
			H.A.PressFire(H);
			H.Tick(150);
		}
		TestEqual(TEXT("5 flashbangs thrown"), H.Count(EEv::Fire), 5);
		TestEqual(TEXT("nothing left"), H.A.GetReserve(), 0);
		TestEqual(TEXT("clip empty"), H.A.GetClip(), 0);
		TestFalse(TEXT("an empty grenade is not drawn"), H.A.IsDrawn());
		TestEqual(TEXT("emptyidle"), H.A.GetAction(), EAction::EmptyIdle);
		H.A.PressFire(H);
		H.Tick(10);
		TestTrue(TEXT("dry click"), H.Find(EEv::Start, 0, EDFWeapon::Flashbang, EAction::Empty) > 0);
		H.A.SelectCategory(1, H);   // no automatic switch: the player goes to the knife by hand
		H.Tick(80);
		TestEqual(TEXT("knife"), H.A.GetCurrent(), EDFWeapon::Knife);
		const int32 Err = H.Count(EEv::SelectError);
		H.A.SelectCategory(4, H);
		H.Tick(10);
		TestEqual(TEXT("key 4 with no flashbang left beeps"), H.Count(EEv::SelectError), Err + 1);
		TestEqual(TEXT("stays on the knife"), H.A.GetCurrent(), EDFWeapon::Knife);
	}
	return true;
}

// ------------------------------------------------------------------ grenades

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsGrenadeThrow, "DF.Weapons.GrenadeThrowTiming", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsGrenadeThrow::RunTest(const FString& Parameters)
{
	struct FCase { EDFWeapon W; int32 Delay; };
	for (const FCase& C : { FCase{ EDFWeapon::Flashbang, 70 }, FCase{ EDFWeapon::FragGrenade, 80 }, FCase{ EDFWeapon::SmokeGrenade, 65 } })
	{
		FHarness H(C.W);
		H.Tick(5);
		const int64 T0 = H.Now();
		H.A.PressFire(H);
		H.Tick(C.Delay + 5);
		const int64 Start = H.Find(EEv::Start, T0, C.W, EAction::Fire);
		const int64 Throw = H.Find(EEv::Fire, T0);
		TestEqual(TEXT("throw action starts the tick after the click"), Start - T0, int64(1));
		TestEqual(*FString::Printf(TEXT("throw leaves delaystart (%d) ticks after the action started"), C.Delay), Throw - Start, int64(C.Delay));
		const FEv* E = H.FindEv(EEv::Fire, T0);
		TestTrue(TEXT("LMB: power 0 (full speed)"), E && E->Power == 0);
		TestEqual(TEXT("the grenade leaves the clip"), H.A.GetClip(), 0);
	}
	// the next one: ready again after the fire clip (flashbang: 109 ticks) + recoil + instant reload
	{
		FHarness H(EDFWeapon::Flashbang);
		H.Tick(5);
		int64 Second = -1;
		for (int32 P = 80; P <= 140 && Second < 0; ++P)
		{
			FHarness G(EDFWeapon::Flashbang);
			G.Tick(5);
			const int64 S0 = G.Now();
			G.A.PressFire(G);
			G.Tick(P);
			G.A.PressFire(G);
			G.Tick(80);
			const TArray<int64> F = G.Ticks(EEv::Fire);
			if (F.Num() >= 2) Second = F[1] - S0;
		}
		AddInfo(FString::Printf(TEXT("flashbang: second throw leaves %lld ticks after the first click"), Second));
		// fire 1 + 70 + delayend (flsh_1f 52 frames = 109 ticks - 70) 39, recoil 1+3, reload (instant) 1+3, idle: ready ~ tick 116, +71 to leave
		TestTrue(TEXT("second throw about 3 s later"), Second > 170 && Second < 200);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsGrenadePower, "DF.Weapons.GrenadeThrowPower", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsGrenadePower::RunTest(const FString& Parameters)
{
	auto Throw = [](int32 HoldTicks, double* OutBar) -> int32
	{
		FHarness H(EDFWeapon::FragGrenade);
		H.Tick(5);
		H.A.PressAltFire();
		H.Tick(HoldTicks);
		if (OutBar) *OutBar = H.A.GetPowerBar();
		H.A.ReleaseAltFire(H);
		H.Tick(100);
		const FEv* E = H.FindEv(EEv::Fire);
		return E ? E->Power : -1;
	};
	double Bar = 0;
	// p = ticks * 0.0107527 (float), power = trunc(p * 255): 46 ticks -> 126
	TestEqual(TEXT("46 ticks -> 126"), Throw(46, &Bar), 126);
	TestTrue(TEXT("power bar ~ 46/93"), FMath::IsNearlyEqual(Bar, 46.0 / 93.0, 0.01));
	TestEqual(TEXT("tap -> clamped to 10% (25)"), Throw(2, nullptr), 25);
	TestEqual(TEXT("93 ticks -> full (255)"), Throw(93, nullptr), 255);
	TestEqual(TEXT("long hold -> 255"), Throw(300, nullptr), 255);

	// the throw still leaves exactly delaystart ticks after the release
	{
		FHarness H(EDFWeapon::FragGrenade);
		H.Tick(5);
		H.A.PressAltFire();
		H.Tick(30);
		const int64 R0 = H.Now();
		H.A.ReleaseAltFire(H);
		H.Tick(100);
		TestEqual(TEXT("leaves 81 ticks after the release (1 + delaystart 80)"), H.Find(EEv::Fire, R0) - R0, int64(81));
	}
	// RMB while not idle starts no timer; the sights toggle is blocked while the timer runs (Sighted weapons only: use the CAR-15 -> no timer)
	{
		FHarness H(EDFWeapon::Car15Auto);
		H.Tick(5);
		H.A.PressAltFire();
		TestTrue(TEXT("no power bar on a gun"), H.A.GetPowerBar() < 0.0);
		H.A.ToggleSights();
		H.Tick(20);
		TestTrue(TEXT("sights toggle works on the CAR-15"), H.A.IsSightsUp());
	}
	{
		FHarness H(EDFWeapon::FragGrenade);
		H.Tick(5);
		H.A.PressAltFire();
		TestTrue(TEXT("power bar running"), H.A.GetPowerBar() >= 0.0);
		H.A.ToggleSights();   // a grenade is not Sighted anyway
		H.Tick(3);
		TestFalse(TEXT("no sights on a grenade"), H.A.IsSightsUp());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsSights, "DF.Weapons.SightsAndModelPosition", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsSights::RunTest(const FString& Parameters)
{
	const FWeaponSpec* Car = Find(EDFWeapon::Car15Auto);
	const FWeaponSpec* Colt = Find(EDFWeapon::Colt45);
	const FVector CarPos(Car->Pos[0], Car->Pos[1], Car->Pos[2]), CarT(Car->TPos[0], Car->TPos[1], Car->TPos[2]);
	const FVector ColtPos(Colt->Pos[0], Colt->Pos[1], Colt->Pos[2]), ColtT(Colt->TPos[0], Colt->TPos[1], Colt->TPos[2]);
	{
		FHarness H;
		H.Tick(5);
		TestTrue(TEXT("hip pose"), H.A.GetModelPosUnits(false).Equals(CarPos, 0.01));
		H.A.ToggleSights();
		H.Tick(8);
		const FVector Mid = H.A.GetModelPosUnits(false);
		TestTrue(TEXT("sights blend half way"), (Mid - CarPos).Size() > 5.0 && (Mid - CarT).Size() > 5.0);
		TestFalse(TEXT("toggle flag only after the 15 tick blend"), H.A.IsSightsUp());
		H.Tick(10);
		TestTrue(TEXT("sights up"), H.A.IsSightsUp());
		TestTrue(TEXT("tpos"), H.A.GetModelPosUnits(false).Equals(CarT, 0.5));
		TestTrue(TEXT("airborne: no blend offset"), H.A.GetModelPosUnits(true).Equals(CarPos, 0.01));

		// persists over a category switch and comes back up with the weapon
		H.A.SelectCategory(2, H);
		H.Tick(80);
		TestEqual(TEXT("Colt"), H.A.GetCurrent(), EDFWeapon::Colt45);
		TestTrue(TEXT("toggle survives (not Sighted: not up)"), H.A.GetSightsToggle() && !H.A.IsSightsUp());
		H.A.SelectCategory(3, H);
		H.Tick(80);
		TestEqual(TEXT("CAR-15 again"), H.A.GetCurrent(), EDFWeapon::Car15Auto);
		TestTrue(TEXT("sights up again"), H.A.IsSightsUp());

		// off: the flag clears at once, the model returns over 15 ticks
		H.A.ToggleSights();
		TestFalse(TEXT("toggle off immediately"), H.A.IsSightsUp());
		H.Tick(20);
		TestTrue(TEXT("hip pose again"), H.A.GetModelPosUnits(false).Equals(CarPos, 0.5));
		// a reload drops the sights (the toggle stays)
		H.A.ToggleSights();
		H.Tick(20);
		TestTrue(TEXT("up"), H.A.IsSightsUp());
		H.A.PressFire(H);
		H.Tick(3, true);
		H.Tick(20, false);
		H.A.RequestReload();
		H.Tick(3);
		TestTrue(TEXT("reloading"), H.A.IsReloading());
		TestFalse(TEXT("sights down while reloading"), H.A.IsSightsUp());
		TestTrue(TEXT("toggle kept"), H.A.GetSightsToggle());
	}
	// switch: lowered pose in the middle, then raised
	{
		FHarness H;
		H.Tick(5);
		H.A.SelectCategory(2, H);
		H.Tick(33);
		const FVector Low(DeployLowered[0], DeployLowered[1], DeployLowered[2]);
		TestTrue(TEXT("lowered pose just before the commit"), H.A.GetModelPosUnits(false).Equals(Low, 1.5));
		H.Tick(36);
		TestTrue(TEXT("raised: the Colt's pos"), H.A.GetModelPosUnits(false).Equals(ColtPos, 1.5));
	}
	// Colt firing pose (tpos) after the first shot, back after 744 ticks
	{
		FHarness H(EDFWeapon::Colt45);
		H.Tick(5);
		H.A.PressFire(H);
		H.Tick(30);
		TestTrue(TEXT("Colt in its firing pose"), H.A.GetModelPosUnits(false).Equals(ColtT, 0.5));
		H.Tick(760);
		TestTrue(TEXT("Colt back to pos"), H.A.GetModelPosUnits(false).Equals(ColtPos, 0.5));
	}
	return true;
}

// ------------------------------------------------------------------ grenade flight

namespace
{
	struct FGrenadeEvents : IDFGrenadeListener
	{
		TArray<int64> Bounce, Arm, Det;
		int64 Tick = 0;
		FDFGrenade LastDet;
		virtual void OnGrenadeBounce(const FDFGrenade& G) override { Bounce.Add(Tick); }
		virtual void OnGrenadeArm(const FDFGrenade& G) override { Arm.Add(Tick); }
		virtual void OnGrenadeDetonate(const FDFGrenade& G) override { Det.Add(Tick); LastDet = G; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFWeaponsGrenadeSim, "DF.Weapons.GrenadeFlight", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FDFWeaponsGrenadeSim::RunTest(const FString& Parameters)
{
	// launch: 30 m/s = 0.4839 m/tick along the view; power scales by power / 256
	{
		FDFGrenadeSim Sim;
		const int32 Id = Sim.Throw(EDFWeapon::FragGrenade, FVector(0, 1.6, 0), 0.0, 0.0, 0);
		TestTrue(TEXT("id"), Id > 0);
		TestEqual(TEXT("one grenade"), Sim.GetGrenades().Num(), 1);
		const FDFGrenade& G = Sim.GetGrenades()[0];
		TestTrue(TEXT("forward = -Z at full speed"), FMath::IsNearlyEqual(G.Vel.Z, -30.0 / 62.0, 1e-6) && FMath::IsNearlyZero(G.Vel.X, 1e-9));
		TestEqual(TEXT("life 248"), G.Life, 248);
		FDFGrenadeSim S2;
		S2.Throw(EDFWeapon::FragGrenade, FVector(0, 1.6, 0), 0.0, 0.0, 128);
		TestTrue(TEXT("power 128 = half speed"), FMath::IsNearlyEqual(S2.GetGrenades()[0].Vel.Size(), 30.0 / 62.0 * 0.5, 1e-6));
		FDFGrenadeSim S3;
		S3.Throw(EDFWeapon::FragGrenade, FVector(0, 1.6, 0), 0.0, 0.0, 255);
		TestTrue(TEXT("power 255 = full speed"), FMath::IsNearlyEqual(S3.GetGrenades()[0].Vel.Size(), 30.0 / 62.0, 1e-6));
	}
	// fuse: exactly 248 ticks (frag, flash), flat ground at y = 0
	for (EDFWeapon W : { EDFWeapon::FragGrenade, EDFWeapon::Flashbang })
	{
		FDFGrenadeSim Sim;
		FGrenadeEvents Ev;
		Sim.Throw(W, FVector(0, 1.6, 0), 0.3, 0.2, 0);
		for (int32 T = 1; T <= 300; ++T)
		{
			Ev.Tick = T;
			Sim.Tick(nullptr, Ev);
			if (T == 247) TestEqual(TEXT("not yet at 247"), Ev.Det.Num(), 0);
		}
		TestEqual(TEXT("detonates once"), Ev.Det.Num(), 1);
		if (Ev.Det.Num()) TestEqual(TEXT("fuse 248 ticks"), Ev.Det[0], int64(248));
		TestEqual(TEXT("removed"), Sim.GetGrenades().Num(), 0);
		TestTrue(TEXT("bounced a few times, sounds only for the first 5"), Ev.Bounce.Num() >= 1 && Ev.Bounce.Num() <= 5);
		TestEqual(TEXT("no arm event"), Ev.Arm.Num(), 0);
		// it came to rest on the ground, lying flat
		TestTrue(TEXT("at rest on the ground"), Ev.LastDet.bAtRest && FMath::IsNearlyEqual(Ev.LastDet.Pos.Y, 0.0, 1e-9));
		TestTrue(TEXT("flat roll"), FMath::IsNearlyEqual(Ev.LastDet.Roll, 90.0 * DF::DEG, 1e-6) && FMath::IsNearlyZero(Ev.LastDet.Pitch));
		// 30 m/s thrown 11 deg up from 1.6 m
		const double Dist = FVector2D(Ev.LastDet.Pos.X, Ev.LastDet.Pos.Z).Size();
		AddInfo(FString::Printf(TEXT("throw distance %.2f m"), Dist));
		TestTrue(TEXT("plausible range"), Dist > 20.0 && Dist < 80.0);
	}
	// smoke: arm event at tick 310, removed at 1860
	{
		FDFGrenadeSim Sim;
		FGrenadeEvents Ev;
		Sim.Throw(EDFWeapon::SmokeGrenade, FVector(0, 1.6, 0), 0.0, 0.0, 0);
		for (int32 T = 1; T <= 1900; ++T)
		{
			Ev.Tick = T;
			Sim.Tick(nullptr, Ev);
			if (T == 1859) TestEqual(TEXT("smoke still there at 1859"), Sim.GetGrenades().Num(), 1);
		}
		TestEqual(TEXT("one arm event"), Ev.Arm.Num(), 1);
		if (Ev.Arm.Num()) TestEqual(TEXT("arm at 310"), Ev.Arm[0], int64(310));
		TestEqual(TEXT("one end event"), Ev.Det.Num(), 1);
		if (Ev.Det.Num()) TestEqual(TEXT("removed at 30 s"), Ev.Det[0], int64(1860));
		TestEqual(TEXT("ids removed reported"), Sim.GetRemovedLastTick().Num(), 0);
	}
	return true;
}
