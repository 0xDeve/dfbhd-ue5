// Player pawn: port of DFBHD-WebLite src/main.js (state P and S, weaponTick, shoot, camera, step).
// The movement half of updatePlayer / simTick lives in UDFCharacterMovementComponent (custom movement mode DFMOVE_Sim) so the
// character gets the engine's client prediction / server authority / corrections; this class keeps the view (mouse look), input,
// weapon, camera, viewmodel, HUD and the test harness, and animates the soldier rig that drives the movement.
// The simulation runs in the web project's three space (see DFMath.h); only component transforms are converted to Unreal.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DFTypes.h"
#include "DFRig.h"
#include "DFWeapons.h"
#include "DFPlayerPawn.generated.h"

class UCameraComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class USceneComponent;
class UInputAction;
class USoundBase;
class USkeletalMesh;
class USoundMix;
class USoundClass;
class UDFWeaponData;
struct FInputActionValue;
class ADFWorld;
class ADFEffects;
class ADFHUD;
class APlayerController;
class UDFCharacterMovementComponent;

/** What the server tells everybody else about a pawn's pose (replicated to everyone but the owner): the inputs of the soldier
 *  animation that cannot be derived from the replicated location / rotation. Angles are int16 fractions of pi. */
USTRUCT()
struct FDFRepPose
{
	GENERATED_BODY()

	UPROPERTY() uint8 Stance = 0;       // EDFStance
	UPROPERTY() int8 MoveX = 0;         // strafe input, + = right
	UPROPERTY() int8 MoveZ = 0;         // forward input as three z: -1 = forward
	UPROPERTY() uint8 bAirborne = 0;
	UPROPERTY() int16 Lean = 0;         // radians * 10000
	UPROPERTY() int8 Roll = 0;          // prone roll clip direction
	UPROPERTY() int16 BodyYaw = 0;      // three yaw of the hips
	UPROPERTY() int16 LegR = 0;         // leg yaw - body yaw
	UPROPERTY() int16 LegL = 0;
	UPROPERTY() int16 BodyRoll = 0;
	UPROPERTY() int16 ViewRoll = 0;
	UPROPERTY() uint8 Weapon = 0;       // EDFWeapon of the soldier's hand (third-person model, one-handed pose)
};

UCLASS()
class PROJECTDF_API ADFPlayerPawn : public ACharacter
{
	GENERATED_BODY()

public:
	ADFPlayerPawn(const FObjectInitializer& ObjectInitializer);

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- components (the root is ACharacter's capsule; CharacterMesh0 is an empty invisible mesh at the feet that receives the network smoothing)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<USceneComponent> VMPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<USceneComponent> VMRigRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<UPoseableMeshComponent> VMMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<USceneComponent> SoldierRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<UPoseableMeshComponent> SoldierMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF|Components") TObjectPtr<UStaticMeshComponent> TPGun;

	// Assets (assigned in the Blueprint), applied to the poseable components in BeginPlay (setting a skinned asset on the CDO trips an engine ensure).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Meshes") TObjectPtr<USkeletalMesh> FpMeshAsset;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Meshes") TObjectPtr<USkeletalMesh> SoldierMeshAsset;

	// Weapons: one data asset per weapon (meshes, HUD icon, Sound Cues; DFWeaponData.h). The gameplay values are DFWeapons.h.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapons") TArray<TObjectPtr<UDFWeaponData>> Weapons;
	/** The view-toggle sound (main.js toggleView / startPlay: audio 'switch'). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> SwitchSound;
	/** Selection error beep ([0xa02030]: a category key with nothing usable); optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> SelectErrorSound;
	/** Flashbang deafness: a sound mix whose class override (FlashSoundClass, i.e. SCL_DF) is pushed with the decaying volume. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundMix> FlashSoundMix;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundClass> FlashSoundClass;

	// Enhanced Input actions (mapped by the controller's mapping contexts).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> MoveAction;        // Axis2D: (D - A, W - S)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> LookAction;        // Axis2D: raw mouse counts, + X right, + Y up
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> LeanLeftAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> LeanRightAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> FireAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> SightsAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> ReloadAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> ProneAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> StandAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> ToggleViewAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> ToggleFpsAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> MapZoomInAction;    // '=': spin map zoom x0.98
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> MapZoomOutAction;   // '-': x1.02
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> HudDetailAction;   // F6: HUD declutter level
	/** Weapon category keys 1..9 (commands 0xc9..0xd1: knife, sidearm, primary, flashbang, frag, smoke, accessory, detonator,
	 *  medpack); element i = key i + 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TArray<TObjectPtr<UInputAction>> SelectWeaponActions;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> CycleWeaponPrevAction;   // mouse wheel up, '['
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> CycleWeaponNextAction;   // mouse wheel down, ']'
	/** Fire Weapon 2 (RMB, command 0x96 + raw event 8): press starts the grenade throw power, release throws. RMB is also SightsAction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input") TObjectPtr<UInputAction> AltFireAction;

	// ---- used by the movement component
	/** Capsule dimensions (cm): radius = PHYS::Radius, half height = half the standing collision height. */
	static float GetCapsuleRadiusCm();
	static float GetCapsuleHalfHeightCm();
	UDFCharacterMovementComponent* GetDFMove() const { return DFMove; }
	/** Start of the locally controlled pawn's frame (called by the movement component before the stock tick): harness clock and the DF move input. */
	void PrepareMoveInput(double RawDt);
	/** End of every DF move that is not a client replay: places the soldier at the sim position and advances its animation (the rig drives ground speed). */
	void OnDFStepped(double Dt);
	/** The rig the movement reads ground speed / eye / hip from (null if it did not load). */
	FDFSoldier* GetSoldierRig() const { return bSoldierOk ? Soldier.Get() : nullptr; }
	bool IsDbg() const { return bDbg; }
	/** The view yaw the local sim uses (mouse look). */
	double GetViewYaw() const { return P.Yaw; }
	void LogJump(double Apex, double Air) const;

private:
	/** The view (mouse look) of the local player, three space radians. The rest of main.js state P lives in the movement component. */
	struct FPlayerState
	{
		double Yaw = 0, Pitch = 0;
	};
	// ---- main.js state S (game)
	struct FGameState
	{
		bool bPlaying = true;
		bool bThird = false, bAds = false;        // bAds = the arsenal's sights-up flag (IsSightsUp)
		bool bFireHeld = false;
		int32 Shots = 0;
		double Recoil = 0, WAcc = 0;
		double AdsT = 0;
		int32 BurstShots = 0;                     // rounds fired in the current trigger pull (BurstSound only in burst mode)
		int32 ReloadSfxTicks = -1;                // CAR-15: ReloadEndSound 1.0 s into the reload
		int32 FlashLevel = 0;                     // [0x96c428]: flashbang white-out 0..255, -4 per tick
		int32 HearingLoss = 0, HearingStep = 0;   // [0xb635c0] amplitude removed from the mix (0x8000 = silent), decays by Step per tick
		bool bMixPushed = false;
		double T = 0, FovH = 80;
	};
	struct FImpact { double T; FVector Point; FVector Normal; };

	// ---- setup
	void ParseCommandLine();
	void InitRigs();
	void ResolveSystems();
	void LinkTickToController();
	void InitLocalPlayer();
	void SyncControlRotation();
	void PlayDFSound(USoundBase* Sound) const;

	// ---- input (Enhanced Input events apply immediately; levels are sampled in PrepareMoveInput)
	void OnLook(const FInputActionValue& Value);
	void OnJumpStarted() { bJumpEdge = true; }
	void OnFireStarted();
	void OnSightsStarted();
	void OnAltFireStarted();
	void OnAltFireCompleted();
	void OnReloadStarted();
	void OnProneStarted();
	void OnCrouchStarted();
	void OnStandStarted();
	void OnToggleViewStarted();
	void OnToggleFpsStarted();
	void OnHudDetailStarted();
	void OnMapZoomInStarted();
	void OnMapZoomOutStarted();
	void BuildMoveInput(double Dt, bool bHarnessOnly);

	// ---- weapon (the action state machine is FDFArsenal; this class is its listener and draws the consequences)
	/** The arsenal's and the grenade simulation's events, forwarded to the pawn (a pawn cannot derive from non-UObject interfaces). */
	struct FWeaponEvents : public IDFArsenalListener, public IDFGrenadeListener
	{
		ADFPlayerPawn* Pawn = nullptr;
		virtual void OnActionStart(EDFWeapon W, DFWeapons::EAction A) override;
		virtual void OnFire(EDFWeapon W, uint8 Power) override;
		virtual void OnReloadDone(EDFWeapon W) override;
		virtual void OnCurrentChanged(EDFWeapon From, EDFWeapon To) override;
		virtual void OnSelectError() override;
		virtual void OnGrenadeBounce(const FDFGrenade& G) override;
		virtual void OnGrenadeArm(const FDFGrenade& G) override;
		virtual void OnGrenadeDetonate(const FDFGrenade& G) override;
	};
	struct FScriptedKey { double T = 0; FString Action; };

	const UDFWeaponData* DataFor(EDFWeapon W) const;
	void ApplyWeaponMeshes(EDFWeapon W);
	void InitArsenal();
	double CurrentSpreadDeg() const;
	FVector GetMuzzle() const;
	void FireGun(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D);
	void KnifeStab(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D);
	void ThrowGrenade(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D, uint8 Power);
	void GrenadeKillZone(const FDFGrenade& G);
	void AddFlash(const FVector& BlastPos, double Dist);
	void UpdateWeapon(double Dt);
	void WeaponTick();
	void RunScriptedKeys();
	void DoScriptedKey(const FString& Action);
	void LogWpn(const TCHAR* Ev, const FString& Detail) const;

	// ---- camera / per frame
	FVector HeadPos() const;
	void UpdateCamera(double Dt);
	void PostMoveStep(double Dt);
	void SetBodyVisibility(bool bWantSoldier, bool bWantVm, bool bWantTpGun);
	void PushHud();
	/** Scope overlay: the sights are up (the arsenal's toggle flag, set when the 15 tick pos -> tpos blend has finished). */
	bool IsScopeOn() const { return !S.bThird && S.bAds; }
	bool IsLocalDF() const { return IsLocallyControlled(); }

	// ---- other players' pawns
	void FillRepPose();
	void TickProxy(double Dt);
	void TickRemoteOnServer(double Dt);
	void LogNetDbg(double RawDt);

	// ---- test harness
	void HarnessTick(double RawDt);
	void LogDbg();

	FPlayerState P;
	FGameState S;
	TArray<FImpact> Impacts;

	FVector CamPos = FVector::ZeroVector;      // three space, the position the camera was last set to
	double CamAspect = 16.0 / 9.0;
	double TpDist = 2.8;

	// pending edges for the next move input
	bool bJumpEdge = false, bFireEdge = false;

	// command-line harness (main.js query params)
	bool bAutoFire = false, bAutoWalk = false, bAutoReload = false, bHasLean = false, bDbg = false;
	bool bAutoHeldPrev = false;
	EDFWeapon InitialWeapon = EDFWeapon::Car15Auto;   // -DFWeapon=<knife|colt|car15|car15semi|flash|frag|smoke>
	bool bVmPosOverride = false;
	FVector VmPosOverride = FVector::ZeroVector;
	bool bHarnessAds = false;                          // -DFAds: toggle the sights once at the start
	TArray<FScriptedKey> ScriptKeys;                   // -DFKeys=<t>:<action>,...
	int32 ScriptNext = 0;
	bool bScriptFireHeld = false;
	double LeanParam = 0.0;
	EDFStance InitStance = EDFStance::Stand;
	int32 AutoRoll = 0;
	double JumpAt = -1.0;                      // -DFJump=<t>: press Space once at S.T >= t (UE-only harness)
	bool bJumpPending = false;
	bool bDebugCam = false;
	double DebugCam[6] = { 0, 0, 0, 0, 0, 0 };
	double WarpSeconds = 0.0;
	double InitYawDeg = 0, InitPitchDeg = 0;
	bool bHasYaw = false;                      // -DFYaw given (else the spawn transform's yaw)
	bool bHasPos = false;
	double PosX = 0, PosZ = 0;
	FString ShotPath;
	double ShotDelay = 3.0;
	double ShotClock = 0.0;
	double ShotRequestedAt = -1.0;
	double ShotDoneAt = -1.0;
	double DbgClock = 0.0;
	double NetDbgClock = 0.0;

	// replicated pose of this pawn for everybody but its owner, and the proxy's smoothed copy of it
	UPROPERTY(Replicated) FDFRepPose RepPose;
	bool bProxyPoseInit = false;
	double ProxyBodyYaw = 0, ProxyLean = 0, ProxyLegR = 0, ProxyLegL = 0, ProxyBodyRoll = 0, ProxyViewRoll = 0;

	// systems
	UPROPERTY(Transient) TObjectPtr<ADFWorld> World;
	UPROPERTY(Transient) TObjectPtr<ADFEffects> Effects;
	UDFCharacterMovementComponent* DFMove = nullptr;
	FDFArsenal Arsenal;
	FDFGrenadeSim GrenadeSim;
	FWeaponEvents WeaponEvents;
	TMap<int32, int32> GrenadeFx;                // sim grenade id -> ADFEffects handle
	TMap<EDFWeapon, const UDFWeaponData*> WeaponAssets;
	EDFWeapon ProxyWeapon = EDFWeapon::None;     // the weapon the soldier rig of a remote pawn holds
	TUniquePtr<FDFViewModel> Vm;
	TUniquePtr<FDFSoldier> Soldier;
	bool bVmOk = false, bSoldierOk = false;
	bool bStarted = false;
};
