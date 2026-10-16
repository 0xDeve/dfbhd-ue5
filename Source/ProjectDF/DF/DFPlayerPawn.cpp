// Port of DFBHD-WebLite src/main.js. Variable names follow main.js (P = player, S = game state); the simulation is in
// three space (DFMath.h) and only component transforms are converted to Unreal.
// The movement half of updatePlayer / simTick runs in UDFCharacterMovementComponent (custom mode DFMOVE_Sim); this file is the rest.
#include "DFPlayerPawn.h"
#include "DFCharacterMovementComponent.h"
#include "DFEffects.h"
#include "DFGameData.h"
#include "DFHUD.h"
#include "DFMath.h"
#include "DFWeaponData.h"
#include "DFWorld.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFPawn, Log, All);

using namespace DFData;

namespace
{
	const TCHAR* StanceName(EDFStance St)
	{
		return St == EDFStance::Prone ? TEXT("prone") : St == EDFStance::Crouch ? TEXT("crouch") : TEXT("stand");
	}
	constexpr double LEAN_STEP = LEAN::StepDeg * DF::DEG;
	constexpr double TICK = DFData::Tick;
	constexpr double FP_SCALE = 0.2;   // UCameraComponent::FirstPersonScale (must match the camera setup below)
	/** Angle (radians, wrapped) <-> int16 fraction of pi for the replicated pose. */
	int16 QuantAngle(double A) { return int16(FMath::Clamp(FMath::RoundToInt(DF::WrapPi(A) / DF::PI_D * 32767.0), -32767, 32767)); }
	double DequantAngle(int16 V) { return double(V) / 32767.0 * DF::PI_D; }
}

float ADFPlayerPawn::GetCapsuleRadiusCm()
{
	return static_cast<float>(PHYS::Radius * DF::M2CM);
}

float ADFPlayerPawn::GetCapsuleHalfHeightCm()
{
	return static_cast<float>(STANCE::Stand.Height * 0.5 * DF::M2CM);
}

ADFPlayerPawn::ADFPlayerPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDFCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	WeaponEvents.Pawn = this;
	bUseControllerRotationYaw = true;   // the actor yaw follows the view (replicated to the proxies)
	bUseControllerRotationPitch = bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
	DFMove = Cast<UDFCharacterMovementComponent>(GetCharacterMovement());

	// capsule = root. Collision is the web's cylinder-vs-AABB / map scene-query code (ADFWorld), so the capsule only exists as the
	// updated component and a query target: query only, object type Pawn, ignoring every channel (ADFWorld's map queries and the
	// hitscan use Visibility and must not hit it)
	const float HalfHeight = GetCapsuleHalfHeightCm();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(GetCapsuleRadiusCm(), HalfHeight);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetGenerateOverlapEvents(false);

	// CharacterMesh0: empty, invisible, no collision, no animation; at the feet, where the network smoothing moves it on proxies
	if (USkeletalMeshComponent* CharMesh = GetMesh())
	{
		CharMesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -HalfHeight), FRotator::ZeroRotator);
		CharMesh->SetVisibility(false);
		CharMesh->SetHiddenInGame(true);
		CharMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CharMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CharMesh->SetGenerateOverlapEvents(false);
		CharMesh->SetCastShadow(false);
		CharMesh->bNoSkeletonUpdate = true;
		CharMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		CharMesh->PrimaryComponentTick.bCanEverTick = false;
		CharMesh->PrimaryComponentTick.bStartWithTickEnabled = false;
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Capsule);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(static_cast<float>(VIEW::HFov));
	Camera->bEnableFirstPersonFieldOfView = true;
	Camera->FirstPersonFieldOfView = static_cast<float>(VIEW::VmHFov);
	Camera->bEnableFirstPersonScale = true;
	Camera->FirstPersonScale = static_cast<float>(FP_SCALE);

	VMPivot = CreateDefaultSubobject<USceneComponent>(TEXT("VMPivot"));
	VMPivot->SetupAttachment(Camera);
	VMRigRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VMRigRoot"));
	VMRigRoot->SetupAttachment(VMPivot);
	VMMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("VMMesh"));
	VMMesh->SetupAttachment(VMRigRoot);

	SoldierRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SoldierRoot"));
	SoldierRoot->SetupAttachment(Capsule);
	SoldierMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("SoldierMesh"));
	SoldierMesh->SetupAttachment(SoldierRoot);
	TPGun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TPGun"));
	TPGun->SetupAttachment(SoldierMesh);

	// assets (FpMeshAsset, SoldierMeshAsset, TPGun's static mesh, sounds) are assigned in the Blueprint

	// no collision on anything: the pawn's collision is the web's cylinder-vs-AABB code
	TArray<UPrimitiveComponent*> Prims = { VMMesh, SoldierMesh, TPGun };
	for (UPrimitiveComponent* C : Prims)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
	}
	VMMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	// frustum culling uses the unscaled bounds (the gun is drawn scaled 0.2 toward the camera, so small weapons whose real position is
	// below / behind the view were culled): make the bounds big enough to always pass
	VMMesh->SetBoundsScale(12.f);
	VMMesh->SetCastShadow(false);
	VMMesh->bCastDynamicShadow = false;
}

void ADFPlayerPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	DFMove = Cast<UDFCharacterMovementComponent>(GetCharacterMovement());
}

void ADFPlayerPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ADFPlayerPawn, RepPose, COND_SkipOwner);
}

void ADFPlayerPawn::LinkTickToController()
{
	if (AController* C = GetController())
	{
		AddTickPrerequisiteActor(C);   // the controller processes input (fires our handlers) first
		if (DFMove) DFMove->AddTickPrerequisiteActor(C);
	}
}

void ADFPlayerPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	LinkTickToController();
}

void ADFPlayerPawn::OnRep_Controller()
{
	Super::OnRep_Controller();
	LinkTickToController();
}

// ------------------------------------------------------------------ setup

void ADFPlayerPawn::ParseCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	auto Num = [Cmd](const TCHAR* Key, double& Out) -> bool
	{
		float V = 0.f;
		if (FParse::Value(Cmd, Key, V)) { Out = V; return true; }
		return false;
	};
	double V = 0;
	if (Num(TEXT("DFView="), V)) S.bThird = FMath::RoundToInt(V) == 3;
	bHasYaw = Num(TEXT("DFYaw="), InitYawDeg);
	Num(TEXT("DFPitch="), InitPitchDeg);
	FString Str;
	if (FParse::Value(Cmd, TEXT("DFStance="), Str, false))
	{
		if (Str == TEXT("crouch")) InitStance = EDFStance::Crouch;
		else if (Str == TEXT("prone")) InitStance = EDFStance::Prone;
	}
	if (Num(TEXT("DFLean="), LeanParam)) bHasLean = true;
	if (FParse::Value(Cmd, TEXT("DFPos="), Str, false))
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 2) { bHasPos = true; PosX = FCString::Atod(*Parts[0]); PosZ = FCString::Atod(*Parts[1]); }
	}
	bAutoWalk = FParse::Param(Cmd, TEXT("DFWalk"));
	bAutoFire = FParse::Param(Cmd, TEXT("DFFire"));
	bAutoReload = FParse::Param(Cmd, TEXT("DFReload"));
	bHarnessAds = FParse::Param(Cmd, TEXT("DFAds"));
	bDbg = FParse::Param(Cmd, TEXT("DFDbg"));
	if (FParse::Value(Cmd, TEXT("DFWeapon="), Str, false))
	{
		Str = Str.ToLower();
		if (Str == TEXT("knife")) InitialWeapon = EDFWeapon::Knife;
		else if (Str == TEXT("colt") || Str == TEXT("colt45")) InitialWeapon = EDFWeapon::Colt45;
		else if (Str == TEXT("car15") || Str == TEXT("car15auto")) InitialWeapon = EDFWeapon::Car15Auto;
		else if (Str == TEXT("car15semi") || Str == TEXT("semi")) InitialWeapon = EDFWeapon::Car15Semi;
		else if (Str == TEXT("flash") || Str == TEXT("flashbang")) InitialWeapon = EDFWeapon::Flashbang;
		else if (Str == TEXT("frag")) InitialWeapon = EDFWeapon::FragGrenade;
		else if (Str == TEXT("smoke")) InitialWeapon = EDFWeapon::SmokeGrenade;
		else UE_LOG(LogDFPawn, Warning, TEXT("-DFWeapon=%s unknown"), *Str);
	}
	if (FParse::Value(Cmd, TEXT("DFVm="), Str, false))   // -DFVm=x,y,z: fixed viewmodel position in weapon.def units (debug)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 3) { bVmPosOverride = true; VmPosOverride = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2])); }
	}
	if (FParse::Value(Cmd, TEXT("DFKeys="), Str, false))
	{
		TArray<FString> Items;
		Str.ParseIntoArray(Items, TEXT(","));
		for (const FString& It : Items)
		{
			FString Tm, Act;
			if (It.Split(TEXT(":"), &Tm, &Act)) ScriptKeys.Add({ FCString::Atod(*Tm), Act.TrimStartAndEnd().ToLower() });
		}
		ScriptKeys.StableSort([](const FScriptedKey& A, const FScriptedKey& B) { return A.T < B.T; });
	}
	Num(TEXT("DFJump="), JumpAt);
	if (Num(TEXT("DFRoll="), V)) AutoRoll = V > 0 ? 1 : V < 0 ? -1 : 0;
	if (FParse::Value(Cmd, TEXT("DFCam="), Str, false))
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 6)
		{
			bDebugCam = true;
			for (int32 i = 0; i < 6; ++i) DebugCam[i] = FCString::Atod(*Parts[i]);
		}
	}
	Num(TEXT("DFWarp="), WarpSeconds);
	if (FParse::Value(Cmd, TEXT("DFShot="), Str, false) && !Str.IsEmpty())
	{
		ShotPath = FPaths::ConvertRelativePathToFull(Str);
	}
	Num(TEXT("DFShotDelay="), ShotDelay);
	if (bAutoReload)   // -DFReload: fire a short burst, then reload (the clip is not full)
	{
		bAutoReload = false;
		ScriptKeys.Add({ 0.1, TEXT("lmbdown") }); ScriptKeys.Add({ 0.35, TEXT("lmbup") }); ScriptKeys.Add({ 0.6, TEXT("r") });
		ScriptKeys.StableSort([](const FScriptedKey& A, const FScriptedKey& B) { return A.T < B.T; });
	}
}

void ADFPlayerPawn::InitRigs()
{
	if (FpMeshAsset) VMMesh->SetSkinnedAssetAndUpdate(FpMeshAsset);
	else UE_LOG(LogDFPawn, Warning, TEXT("Property FpMeshAsset is not assigned"));
	if (SoldierMeshAsset) SoldierMesh->SetSkinnedAssetAndUpdate(SoldierMeshAsset);
	else UE_LOG(LogDFPawn, Warning, TEXT("Property SoldierMeshAsset is not assigned"));

	Vm = MakeUnique<FDFViewModel>();
	bVmOk = VMMesh && Vm->Init(VMMesh, VMRigRoot, VMPivot);
	if (!bVmOk) UE_LOG(LogDFPawn, Warning, TEXT("Viewmodel rig not available; first-person arms hidden"));
	Soldier = MakeUnique<FDFSoldier>();
	bSoldierOk = SoldierMesh && Soldier->Init(SoldierMesh, TPGun);
	if (!bSoldierOk) UE_LOG(LogDFPawn, Warning, TEXT("Soldier rig not available; third-person body hidden"));
}

/** The level's ADFWorld / ADFEffects (spawned from the game mode's classes if missing). On a client the game mode is only known
 *  through the GameState, which may not have arrived yet: both stay null and are retried every tick. */
void ADFPlayerPawn::ResolveSystems()
{
	UWorld* W = GetWorld();
	if (!World)
	{
		World = ADFWorld::Get(W);
		if (World && DFMove) DFMove->SetDFWorld(World);
	}
	if (!Effects)
	{
		Effects = ADFEffects::Get(W);
	}
}

void ADFPlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	ParseCommandLine();
	ResolveSystems();

	// the poseable meshes must tick after the pawn (the rig also pushes its pose itself)
	if (VMMesh) VMMesh->AddTickPrerequisiteActor(this);
	if (SoldierMesh) SoldierMesh->AddTickPrerequisiteActor(this);
	LinkTickToController();

	// ---- state P / S (main.js boot): the pawn starts where it was spawned (feet = capsule center - half height, yaw = -UE yaw)
	const FVector Feet = DF::FromUE(GetActorLocation() - FVector(0.0, 0.0, GetCapsuleHalfHeightCm()));
	const double SpawnYaw = 0.0 - FRotator::NormalizeAxis(GetActorRotation().Yaw) * DF::DEG;
	P.Yaw = SpawnYaw;
	P.Pitch = 0.0;
	if (DFMove) DFMove->InitSim(Feet, SpawnYaw);
	S.FovH = VIEW::HFov;
	S.bPlaying = true;
	TpDist = TUNE::TpBack;

	// Near plane: the first-person gun is drawn scaled 0.2 toward the camera, so the web's 0.05 m viewmodel near plane
	// becomes 1 cm; set as NearClipPlane=1 in Config/DefaultEngine.ini (a 5 cm plane clipped the gun's rear and optic).

	InitRigs();
	WeaponAssets.Reset();
	for (const UDFWeaponData* D : Weapons) { if (D) WeaponAssets.Add(D->Weapon, D); }
	// the local player's harness / warp / first pose happen in InitLocalPlayer (the first frame it is known to be locally controlled)
}

/** Once, at the local player's first frame (the controller is not known in BeginPlay): command-line overrides, then the warp or the first pose. */
void ADFPlayerPawn::InitLocalPlayer()
{
	bStarted = true;   // (also stops PrepareMoveInput from re-entering through the steps below)
	ResolveSystems();
	const bool bAuth = HasAuthority();
	FDFSimState& M = DFMove->P;

	if (bAuth && bHasYaw) P.Yaw = InitYawDeg * DF::DEG;
	P.Pitch = InitPitchDeg * DF::DEG;
	M.Yaw = P.Yaw;
	DFMove->Stance = InitStance;
	M.Lean = LeanParam * 16 * LEAN_STEP;
	if (bAuth && bHasPos)
	{
		M.Pos.X = PosX; M.Pos.Z = PosZ;
		if (World && World->IsMap()) M.Pos.Y = World->GroundAt(FVector(PosX, 2000.0, PosZ), PHYS::Radius * 0.7);   // top-most surface there
	}
	M.BodyYaw = M.LegR = M.LegL = M.LegTgtR = M.LegTgtL = M.Yaw; M.Tick = 0;
	M.BodyRoll = 0; M.ViewRoll = 0;
	DFMove->ResetRender();
	if (bAuth && bHasYaw) SetActorRotation(FRotator(0.0, 0.0 - P.Yaw / DF::DEG, 0.0));
	SyncControlRotation();

	PlayDFSound(SwitchSound);   // startPlay(): audio.play('switch')
	InitArsenal();

	// ?warp=<seconds>: simulate that much game time before the first frame (headless testing)
	auto Frame = [this](double Dt)
	{
		BuildMoveInput(Dt, true);   // the same sequence a frame has: move input, the movement component's DF move, the rest
		DFMove->StepDF(Dt);
		PostMoveStep(Dt);
	};
	if (WarpSeconds > 0)
	{
		for (int32 i = 0, n = FMath::RoundToInt(WarpSeconds * 60); i < n; ++i) Frame(1.0 / 60);
		if (bDbg) LogDbg();   // state right after the warp (the web logs the same)
	}
	else
	{
		Frame(0.0001);   // pose everything once so the first rendered frame is valid
	}
	PushHud();
}

/** ControlRotation = the DF view: UE yaw = -three yaw (deg), pitch = P.Pitch (deg). The server needs it for remote pawns; locally it follows P. */
void ADFPlayerPawn::SyncControlRotation()
{
	if (AController* C = GetController())
	{
		if (IsLocallyControlled())
		{
			C->SetControlRotation(FRotator(P.Pitch / DF::DEG, 0.0 - P.Yaw / DF::DEG, 0.0));
		}
	}
}

// ------------------------------------------------------------------ per frame

void ADFPlayerPawn::PlayDFSound(USoundBase* Sound) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

// ------------------------------------------------------------------ input (Enhanced Input)

void ADFPlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC) return;

	// levels (move, lean, fire held, jump held) are sampled every frame in PrepareMoveInput with GetBoundActionValue;
	// the edges apply immediately (UE standard) and are remembered for this frame's move where the move needs them
	for (const UInputAction* A : { MoveAction.Get(), LeanLeftAction.Get(), LeanRightAction.Get(), FireAction.Get(), JumpAction.Get() })
	{
		if (A) EIC->BindActionValue(A);
	}
	auto Edge = [this, EIC](const UInputAction* A, void (ADFPlayerPawn::*Handler)())
	{
		if (A) EIC->BindActionValueLambda(A, ETriggerEvent::Started, [this, Handler](const FInputActionValue&) { (this->*Handler)(); });
	};
	Edge(JumpAction, &ADFPlayerPawn::OnJumpStarted);
	Edge(FireAction, &ADFPlayerPawn::OnFireStarted);
	Edge(AltFireAction, &ADFPlayerPawn::OnAltFireStarted);   // before the sights: a throw timer blocks the sights toggle
	Edge(SightsAction, &ADFPlayerPawn::OnSightsStarted);
	if (AltFireAction) EIC->BindActionValueLambda(AltFireAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { OnAltFireCompleted(); });
	else if (SightsAction) EIC->BindActionValueLambda(SightsAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { OnAltFireCompleted(); });
	Edge(ReloadAction, &ADFPlayerPawn::OnReloadStarted);
	Edge(ProneAction, &ADFPlayerPawn::OnProneStarted);
	Edge(CrouchAction, &ADFPlayerPawn::OnCrouchStarted);
	Edge(StandAction, &ADFPlayerPawn::OnStandStarted);
	Edge(ToggleViewAction, &ADFPlayerPawn::OnToggleViewStarted);
	Edge(ToggleFpsAction, &ADFPlayerPawn::OnToggleFpsStarted);
	Edge(HudDetailAction, &ADFPlayerPawn::OnHudDetailStarted);
	Edge(MapZoomInAction, &ADFPlayerPawn::OnMapZoomInStarted);
	Edge(MapZoomOutAction, &ADFPlayerPawn::OnMapZoomOutStarted);
	for (int32 i = 0; i < SelectWeaponActions.Num(); ++i)
	{
		if (const UInputAction* A = SelectWeaponActions[i])
		{
			EIC->BindActionValueLambda(A, ETriggerEvent::Started, [this, i](const FInputActionValue&) { Arsenal.SelectCategory(i + 1, WeaponEvents); });
		}
	}
	if (CycleWeaponPrevAction) EIC->BindActionValueLambda(CycleWeaponPrevAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Arsenal.Cycle(-1, WeaponEvents); });
	if (CycleWeaponNextAction) EIC->BindActionValueLambda(CycleWeaponNextAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Arsenal.Cycle(+1, WeaponEvents); });
	if (LookAction) EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADFPlayerPawn::OnLook);
}

void ADFPlayerPawn::OnLook(const FInputActionValue& Value)
{
	// raw per-frame mouse counts (Unreal: X + = right, Y + = UP); main.js movementX + = right, movementY + = down
	const FVector2D V = Value.Get<FVector2D>();
	if (ShotPath.IsEmpty() && (V.X != 0.0 || V.Y != 0.0) && DFMove)   // test runs (-DFShot) ignore the real mouse
	{
		const double k = TUNE::MouseSens * (S.FovH / VIEW::HFov);
		P.Yaw -= V.X * k;
		P.Pitch = DF::Clamp(P.Pitch + V.Y * k, -VIEW::PitchMax * DF::DEG,
			(DFMove->Stance == EDFStance::Prone ? VIEW::PitchMaxProne : VIEW::PitchMax) * DF::DEG);
		SyncControlRotation();
	}
}

void ADFPlayerPawn::OnFireStarted()
{
	bFireEdge = true;   // the trigger counts as held for this frame even if it is released again before the next
	if (bStarted) Arsenal.PressFire(WeaponEvents);
}

void ADFPlayerPawn::OnSightsStarted()
{
	if (!bStarted) return;
	Arsenal.PressAltFire();   // (idempotent) the RMB press first: it starts the grenade throw timer, which blocks the sights toggle
	Arsenal.ToggleSights();
}

void ADFPlayerPawn::OnAltFireStarted()
{
	if (bStarted) Arsenal.PressAltFire();
}

void ADFPlayerPawn::OnAltFireCompleted()
{
	if (bStarted) Arsenal.ReleaseAltFire(WeaponEvents);
}

void ADFPlayerPawn::OnReloadStarted()
{
	if (bStarted) Arsenal.RequestReload();
}

// original default bindings (DFBHD.EXE key table): Z = Prone, X = Crouch, C = Stand (toggles, 0x489cc0)
void ADFPlayerPawn::OnProneStarted()
{
	if (DFMove) DFMove->Stance = DFMove->Stance == EDFStance::Prone ? EDFStance::Stand : EDFStance::Prone;
}

void ADFPlayerPawn::OnCrouchStarted()
{
	if (DFMove) DFMove->Stance = DFMove->Stance == EDFStance::Crouch ? EDFStance::Stand : EDFStance::Crouch;
}

void ADFPlayerPawn::OnStandStarted()
{
	if (DFMove) DFMove->Stance = EDFStance::Stand;
}

void ADFPlayerPawn::OnToggleViewStarted()   // toggleView(): S.third = !S.third; audio.play('switch')
{
	S.bThird = !S.bThird;
	PlayDFSound(SwitchSound);
}

void ADFPlayerPawn::OnToggleFpsStarted()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ADFHUD* H = Cast<ADFHUD>(PC->GetHUD())) H->ToggleFps();
	}
}

void ADFPlayerPawn::OnHudDetailStarted()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ADFHUD* H = Cast<ADFHUD>(PC->GetHUD())) H->CycleDetail();
	}
}

void ADFPlayerPawn::OnMapZoomInStarted()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ADFHUD* H = Cast<ADFHUD>(PC->GetHUD())) H->ZoomMap(0.98f);
	}
}

void ADFPlayerPawn::OnMapZoomOutStarted()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ADFHUD* H = Cast<ADFHUD>(PC->GetHUD())) H->ZoomMap(1.02f);
	}
}

/** Start of the locally controlled pawn's frame: the held keys become this frame's DF move input. */
void ADFPlayerPawn::PrepareMoveInput(double RawDt)
{
	if (!bStarted)
	{
		InitLocalPlayer();   // (its first frame / warp builds its own move inputs)
	}
	BuildMoveInput(FMath::Min<double>(RawDt, 0.05), false);
}

void ADFPlayerPawn::BuildMoveInput(double Dt, bool bHarnessOnly)
{
	S.T += Dt;
	RunScriptedKeys();   // -DFKeys: scripted key presses at S.T

	FVector2D Move = FVector2D::ZeroVector;   // (D - A, W - S)
	bool bQ = false, bE = false, bFire = false, bJumpHeld = false;
	if (!bHarnessOnly)   // warp / first-pose frames have no input
	{
		if (const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			auto Level = [EIC](const UInputAction* A) { return A && EIC->GetBoundActionValue(A).Get<bool>(); };
			if (MoveAction) Move = EIC->GetBoundActionValue(MoveAction).Get<FVector2D>();
			bQ = Level(LeanLeftAction);
			bE = Level(LeanRightAction);
			bFire = Level(FireAction);
			bJumpHeld = Level(JumpAction);
		}
	}
	FDFMoveInput In;
	In.FIn = int8(FMath::Clamp(FMath::RoundToInt(Move.Y), -1, 1));   // W - S
	In.SIn = int8(FMath::Clamp(FMath::RoundToInt(Move.X), -1, 1));   // D - A
	if (bAutoWalk) In.FIn = 1;
	S.bAds = Arsenal.IsSightsUp();   // toggle flag (after the 15 tick blend), Sighted weapon, not reloading
	// lean (zeroed by the movement while prone, 0x47ed1f); prone + Q/E roll instead
	In.LeanIn = int8((bE ? 1 : 0) - (bQ ? 1 : 0));
	if (In.LeanIn == 0 && bHasLean) In.LeanIn = int8(FMath::Clamp(FMath::RoundToInt(LeanParam), -1, 1));
	In.RollIn = bE ? 1 : bQ ? -1 : 0;
	if (AutoRoll != 0 && S.T > 0.3) { In.RollIn = int8(AutoRoll); AutoRoll = 0; }   // -DFRoll=1|-1: one roll (testing)
	if (JumpAt >= 0 && S.T >= JumpAt) { JumpAt = -1.0; bJumpPending = true; }   // -DFJump=<t>: one Space press (testing)
	In.bJump = bJumpHeld || bJumpEdge || bJumpPending;
	bJumpEdge = false;
	S.bFireHeld = bFire || bFireEdge || bScriptFireHeld;
	bFireEdge = false;
	if (bAutoFire && S.T > 0.5)   // -DFFire: the trigger goes down for 1.0 s every 1.6 s
	{
		const bool bAutoHeld = FMath::Fmod(S.T - 0.5, 1.6) < 1.0;
		if (bAutoHeld && !bAutoHeldPrev) Arsenal.PressFire(WeaponEvents);
		bAutoHeldPrev = bAutoHeld;
		S.bFireHeld = S.bFireHeld || bAutoHeld;
	}
	In.Stance = DFMove->Stance;
	In.Weapon = uint8(Arsenal.GetCurrent());   // the server's rig needs the weapon's run_anim / special_hold
	DFMove->SetMoveInput(In);
	DFMove->DbgT = S.T;
}

void ADFPlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ResolveSystems();

	if (IsLocalDF())
	{
		if (!bStarted) return;
		// after the movement component's move: camera, visibility, viewmodel, weapon, effects, HUD, harness
		PostMoveStep(FMath::Min<double>(DeltaSeconds, 0.05));
		PushHud();
		HarnessTick(DeltaSeconds);
	}
	else
	{
		if (GetLocalRole() == ROLE_SimulatedProxy) TickProxy(DeltaSeconds);
		else if (HasAuthority()) TickRemoteOnServer(DeltaSeconds);
		if (bDbg) LogNetDbg(DeltaSeconds);
	}
}

// ------------------------------------------------------------------ weapon (FDFArsenal events, shots, stabs, grenades)

const UDFWeaponData* ADFPlayerPawn::DataFor(EDFWeapon W) const
{
	const UDFWeaponData* const* D = WeaponAssets.Find(W);
	return D ? *D : nullptr;
}

void ADFPlayerPawn::ApplyWeaponMeshes(EDFWeapon W)
{
	if (Effects) Effects->StopMuzzleFlash();
	const UDFWeaponData* D = DataFor(W);
	if (!D && W != EDFWeapon::None)
	{
		UE_LOG(LogDFPawn, Warning, TEXT("No UDFWeaponData for weapon %d in Weapons (BP_DFPlayerPawn): its meshes / sounds are missing"), int32(W));
	}
	if (D && W != EDFWeapon::None && (!D->FirstPersonMesh || !D->ThirdPersonMesh))
	{
		UE_LOG(LogDFPawn, Warning, TEXT("Weapon %d: %s is not assigned in %s"), int32(W), D->FirstPersonMesh ? TEXT("ThirdPersonMesh") : TEXT("FirstPersonMesh"), *D->GetName());
	}
	const bool bFpOk = !bVmOk || Vm->SetWeapon(W, D ? D->FirstPersonMesh.Get() : nullptr);
	if (!bFpOk && W != EDFWeapon::None) UE_LOG(LogDFPawn, Warning, TEXT("Viewmodel: SetWeapon(%d) failed (rig not built for this weapon)"), int32(W));
	if (bSoldierOk) Soldier->SetWeapon(W, D ? D->ThirdPersonMesh.Get() : nullptr);
}

/** Spawn (FDFArsenal::Reset): the loadout is the CAR-15 class list (knife, Colt .45, CAR-15 auto + semi, flashbang, frag, smoke). */
void ADFPlayerPawn::InitArsenal()
{
	Arsenal.Reset({ EDFWeapon::Knife, EDFWeapon::Colt45, EDFWeapon::Car15Auto, EDFWeapon::Car15Semi, EDFWeapon::Flashbang,
		EDFWeapon::FragGrenade, EDFWeapon::SmokeGrenade }, InitialWeapon);
	GrenadeSim = FDFGrenadeSim();
	GrenadeFx.Reset();
	ApplyWeaponMeshes(Arsenal.GetCurrent());
	if (bHarnessAds) Arsenal.ToggleSights();
	LogWpn(TEXT("spawn"), FString::Printf(TEXT("\"weapon\":%d,\"state\":\"%s\""), int32(Arsenal.GetCurrent()), *Arsenal.Describe()));
}

namespace
{
	const TCHAR* WeaponName(EDFWeapon W)
	{
		switch (W)
		{
		case EDFWeapon::Knife: return TEXT("Knife");
		case EDFWeapon::Colt45: return TEXT("Colt45");
		case EDFWeapon::Car15Auto: return TEXT("Car15Auto");
		case EDFWeapon::Car15Semi: return TEXT("Car15Semi");
		case EDFWeapon::Flashbang: return TEXT("Flashbang");
		case EDFWeapon::FragGrenade: return TEXT("FragGrenade");
		case EDFWeapon::SmokeGrenade: return TEXT("SmokeGrenade");
		default: return TEXT("None");
		}
	}
	const TCHAR* ActionName(DFWeapons::EAction A)
	{
		static const TCHAR* Names[] = { TEXT("idle"), TEXT("emptyidle"), TEXT("fire"), TEXT("recoil"), TEXT("reload"), TEXT("empty"), TEXT("switchto"), TEXT("switchfrom"), TEXT("switchrank") };
		return Names[FMath::Clamp(int32(A), 0, 8)];
	}
}

void ADFPlayerPawn::LogWpn(const TCHAR* Ev, const FString& Detail) const
{
	if (!bDbg) return;
	UE_LOG(LogDFPawn, Display, TEXT("DFWPN {\"tick\":%lld,\"t\":%.3f,\"ev\":\"%s\",%s}"), Arsenal.GetNow(), S.T, Ev, *Detail);
}

// ---- IDFArsenalListener

void ADFPlayerPawn::FWeaponEvents::OnActionStart(EDFWeapon W, DFWeapons::EAction A)
{
	ADFPlayerPawn& Me = *Pawn;
	Me.LogWpn(TEXT("start"), FString::Printf(TEXT("\"weapon\":\"%s\",\"action\":\"%s\""), WeaponName(W), ActionName(A)));
	const UDFWeaponData* D = Me.DataFor(W);
	const DFWeapons::FWeaponSpec* Sp = DFWeapons::Find(W);
	if (Me.bVmOk)
	{
		const double ReloadEnd = (A == DFWeapons::EAction::Reload && Sp) ? double(Sp->Actions[int32(A)].DelayStart) / SimHz : 0.0;
		Me.Vm->PlayAction(A, ReloadEnd);
	}
	switch (A)
	{
	case DFWeapons::EAction::SwitchFrom: if (D) Me.PlayDFSound(D->SwitchFromSound); break;
	case DFWeapons::EAction::SwitchTo: if (D) Me.PlayDFSound(D->SwitchToSound); break;
	case DFWeapons::EAction::Reload:
		if (D) Me.PlayDFSound(D->ReloadSound);
		Me.S.ReloadSfxTicks = (D && D->ReloadEndSound) ? SimHz : -1;   // CAR-15: the second reload sound 1.0 s in
		Me.S.BurstShots = 0;
		if (Me.DFMove) Me.DFMove->Pose.FireKickTicks = 80;               // 0x4c9e2f
		break;
	case DFWeapons::EAction::Empty: if (D) Me.PlayDFSound(D->DrySound); break;
	case DFWeapons::EAction::Fire:
		if (D && Sp && Sp->Class == DFWeapons::EClass::Grenade) Me.PlayDFSound(D->FireSound);   // GF_*_THROW is the fire action's soundset
		break;
	default: break;
	}
}

void ADFPlayerPawn::FWeaponEvents::OnFire(EDFWeapon W, uint8 Power)
{
	ADFPlayerPawn& Me = *Pawn;
	const DFWeapons::FWeaponSpec* Sp = DFWeapons::Find(W);
	if (!Sp) return;
	Me.LogWpn(TEXT("fire"), FString::Printf(TEXT("\"weapon\":\"%s\",\"power\":%d,\"clip\":%d"), WeaponName(W), int32(Power), Me.Arsenal.GetClip()));
	const UDFWeaponData* D = Me.DataFor(W);
	if (Me.DFMove)   // attack_anim: knife 1 = arm swing 40 ticks (0x4c99a9), throw 2 = upper-body kick 8 ticks (0x4c99cd)
	{
		if (Sp->AttackAnim == 1) Me.DFMove->Pose.ArmSwingTicks = 40;
		if (Sp->AttackAnim == 2) Me.DFMove->Pose.FireKickTicks = 8;
	}
	if (Sp->Ammo.KillZone == DFWeapons::EKillZone::Knife) Me.KnifeStab(*Sp, D);
	else if (Sp->Class == DFWeapons::EClass::Grenade) Me.ThrowGrenade(*Sp, D, Power);
	else Me.FireGun(*Sp, D);
}

void ADFPlayerPawn::FWeaponEvents::OnReloadDone(EDFWeapon W)
{
	Pawn->LogWpn(TEXT("reloaded"), FString::Printf(TEXT("\"weapon\":\"%s\",\"clip\":%d,\"reserve\":%d"), WeaponName(W), Pawn->Arsenal.GetClip(), Pawn->Arsenal.GetReserve()));
}

void ADFPlayerPawn::FWeaponEvents::OnCurrentChanged(EDFWeapon From, EDFWeapon To)
{
	ADFPlayerPawn& Me = *Pawn;
	Me.LogWpn(TEXT("changed"), FString::Printf(TEXT("\"from\":\"%s\",\"to\":\"%s\""), WeaponName(From), WeaponName(To)));
	Me.ApplyWeaponMeshes(To);
	Me.S.BurstShots = 0;
	Me.S.ReloadSfxTicks = -1;
}

void ADFPlayerPawn::FWeaponEvents::OnSelectError()
{
	Pawn->LogWpn(TEXT("selecterror"), TEXT("\"beep\":1"));
	Pawn->PlayDFSound(Pawn->SelectErrorSound);
}

// ---- shots

/** Spread half-angle (0x48e2c0): error_up with the sights up (not reloading) or prone, else error_hip. */
double ADFPlayerPawn::CurrentSpreadDeg() const
{
	const DFWeapons::FWeaponSpec* Sp = Arsenal.GetCurrentSpec();
	if (!Sp) return 0.0;
	const bool bSteady = (S.bAds && !S.bThird) || DFMove->Stance == EDFStance::Prone;
	return bSteady ? Sp->ErrorUpDeg : Sp->ErrorHipDeg;
}

/** Eye position (the lean pivot, 0x47e7ec) of the sim state. */
FVector ADFPlayerPawn::HeadPos() const
{
	return DFMove->LeanedHead(DFMove->P.Pos, DFMove->P.Lean);
}

/** Unscaled animated muzzle in three space; the flash renderer applies the weapon's first-person projection. */
FVector ADFPlayerPawn::GetMuzzle() const
{
	if (S.bThird)
	{
		return bSoldierOk ? DF::FromUE(Soldier->GetMuzzleWorldUE()) : HeadPos();
	}
	if (!bVmOk) return HeadPos();
	return DF::FromUE(Vm->GetMuzzleWorldUE());
}

void ADFPlayerPawn::FireGun(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D)
{
	++S.Shots;
	// direction = view yaw/pitch + 1x recoil accumulator (taken before this shot's kick, 0x488450)
	// + spread r = err*cos(rand*pi/2), theta uniform: yaw += r sin(theta), pitch += r cos(theta)  (0x48e2c0)
	const double Err = CurrentSpreadDeg() * DF::DEG;
	const double R = Err * FMath::Cos(DF::Rand() * DF::PI_D / 2);
	const double Th = DF::Rand() * DF::PI_D * 2;
	const FQuat AimQ = DF::EulerYXZ(P.Pitch + S.Recoil + R * FMath::Cos(Th), P.Yaw + R * FMath::Sin(Th), 0.0);
	const FVector Dir = AimQ.RotateVector(FVector(0, 0, -1)).GetSafeNormal();
	const FVector Origin = HeadPos();                   // the round starts at the eye in either view (0x4884ae)
	const double Range = Sp.Ammo.Velocity * Sp.Ammo.MaxAge;
	double HitT = 0; FVector HitPoint = FVector::ZeroVector, HitNormal = FVector::UpVector;
	const bool bHit = World && World->CastRay(Origin, Dir, Range, HitT, HitPoint, HitNormal);
	S.Recoil += double(int64(Sp.Ammo.Recoil) << 20) * BAM * DF::DEG;   // ammo recoil << 20 (0x48e451)
	if (!S.bThird && bVmOk) Vm->Fire();                 // third person: the recoil pitch bends the upper body (0x484150)
	const FVector Muzzle = GetMuzzle();
	if (Effects)
	{
		if (S.bThird || !S.bAds)                        // none with the sights up
		{
			Effects->MuzzleFlash(Muzzle, S.bThird ? 0.27 : 0.18, !S.bThird);
		}
		Effects->Tracer(Origin, Dir, bHit ? HitT : TNumericLimits<double>::Max(), Sp.Ammo.Velocity, Sp.Ammo.MaxAge);   // every round is a tracer (0x48e357)
	}
	// the round flies at the ammo's velocity; show the impact when it gets there
	if (bHit) Impacts.Add({ HitT / Sp.Ammo.Velocity, HitPoint, HitNormal });
	// Auto and semi-auto repeat FireSound; only burst mode uses BurstSound for following rounds.
	const bool bBurstContinuation = DFWeapons::HasFlag(Sp, DFWeapons::F_Burst) && S.BurstShots > 0;
	USoundBase* ShotSound = D ? ((bBurstContinuation && D->BurstSound) ? D->BurstSound.Get() : D->FireSound.Get()) : nullptr;
	PlayDFSound(ShotSound);
	LogWpn(TEXT("shotsound"), FString::Printf(TEXT("\"round\":%d,\"sound\":\"%s\""), S.BurstShots, *GetNameSafe(ShotSound)));
	++S.BurstShots;
}

/** The knife kill zone (0x492d80): from the eye, targets with dist - radius <= 1.5 m in any direction, or <= 2.75 m within +-17 deg of the
 *  view yaw; clear line of sight. No health model yet: logs DFKZ and plays the flesh sound at the target. */
void ADFPlayerPawn::KnifeStab(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D)
{
	if (D) PlayDFSound(D->FireSound);   // GS_KNIFE: the swing
	UWorld* W = GetWorld();
	if (!W) return;
	const FVector Eye = HeadPos();
	for (TActorIterator<ADFPlayerPawn> It(W); It; ++It)
	{
		ADFPlayerPawn* O = *It;
		if (O == this) continue;
		const FVector Feet = DF::FromUE(O->GetActorLocation() - FVector(0, 0, GetCapsuleHalfHeightCm()));
		const FVector Dv = Feet - Eye;
		const double Dist = FMath::Max(0.0, Dv.Size() - PHYS::Radius);
		if (Dist > Sp.Ammo.KzMaxRadius) continue;
		const double Bearing = FMath::Atan2(-Dv.X, -Dv.Z);
		const bool bCone = FMath::Abs(DF::WrapPi(Bearing - P.Yaw)) <= Sp.Ammo.KzHalfAngleDeg * DF::DEG;
		if (Dist > Sp.Ammo.KzMinRadius && !bCone) continue;
		// line of sight: event + 0.25 m to the target + 0.25 m
		const FVector A = Eye + FVector(0, 0.25, 0), B = Feet + FVector(0, 0.25, 0);
		const FVector L = B - A;
		double T = 0; FVector Pt, N;
		if (World && L.Size() > 1e-4 && World->CastRay(A, L.GetSafeNormal(), L.Size(), T, Pt, N)) continue;
		UE_LOG(LogDFPawn, Display, TEXT("DFKZ {\"type\":\"knife\",\"target\":\"%s\",\"dist\":%.2f,\"cone\":%s,\"damage\":%.0f}"),
			*O->GetName(), Dist, bCone ? TEXT("true") : TEXT("false"), Sp.Ammo.KzDamage);
		if (D && D->ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, D->ImpactSound, DF::ToUE(Feet + FVector(0, 1.0, 0)));
	}
}

void ADFPlayerPawn::ThrowGrenade(const DFWeapons::FWeaponSpec& Sp, const UDFWeaponData* D, uint8 Power)
{
	const int32 Id = GrenadeSim.Throw(Sp.Id, HeadPos(), P.Yaw, P.Pitch, Power);   // from the eye, along the view, no inherited velocity
	if (Id == 0) return;
	const int32 Handle = Effects ? Effects->AddGrenade(Sp.Id, D ? D->ProjectileMesh.Get() : nullptr) : -1;
	GrenadeFx.Add(Id, Handle);
	if (bVmOk) Vm->HideThrownGrenade();
	LogWpn(TEXT("throw"), FString::Printf(TEXT("\"weapon\":\"%s\",\"id\":%d,\"power\":%d"), WeaponName(Sp.Id), Id, int32(Power)));
}

// ---- grenade events

void ADFPlayerPawn::FWeaponEvents::OnGrenadeBounce(const FDFGrenade& G)
{
	Pawn->LogWpn(TEXT("gbounce"), FString::Printf(TEXT("\"id\":%d,\"n\":%d"), G.Id, G.Bounces));
	if (const UDFWeaponData* D = Pawn->DataFor(G.Type))
	{
		if (D->ImpactSound) UGameplayStatics::PlaySoundAtLocation(Pawn, D->ImpactSound, DF::ToUE(G.Pos));
	}
}

void ADFPlayerPawn::FWeaponEvents::OnGrenadeArm(const FDFGrenade& G)
{
	Pawn->LogWpn(TEXT("garm"), FString::Printf(TEXT("\"id\":%d,\"weapon\":\"%s\""), G.Id, WeaponName(G.Type)));
	if (const UDFWeaponData* D = Pawn->DataFor(G.Type))
	{
		if (D->DetonateSound) UGameplayStatics::PlaySoundAtLocation(Pawn, D->DetonateSound, DF::ToUE(G.Pos));   // smoke: the pop
	}
}

void ADFPlayerPawn::FWeaponEvents::OnGrenadeDetonate(const FDFGrenade& G)
{
	ADFPlayerPawn& Me = *Pawn;
	Me.LogWpn(TEXT("gdetonate"), FString::Printf(TEXT("\"id\":%d,\"weapon\":\"%s\",\"pos\":[%.2f,%.2f,%.2f]"), G.Id, WeaponName(G.Type), G.Pos.X, G.Pos.Y, G.Pos.Z));
	if (G.Type != EDFWeapon::SmokeGrenade)   // the smoke grenade only ends here (its pop was the arm event)
	{
		if (Me.Effects) Me.Effects->GrenadeBurst(G.Type, G.Pos);
		if (const UDFWeaponData* D = Me.DataFor(G.Type))
		{
			if (D->DetonateSound) UGameplayStatics::PlaySoundAtLocation(&Me, D->DetonateSound, DF::ToUE(G.Pos));
		}
		Me.GrenadeKillZone(G);
	}
	if (const int32* H = Me.GrenadeFx.Find(G.Id))
	{
		if (Me.Effects && *H >= 0) Me.Effects->RemoveGrenade(*H);
		Me.GrenadeFx.Remove(G.Id);
	}
}

/** Kill zone of a detonation (0x493670): dist = max(0, |d| - r), line of sight blast + 0.25 m -> target + 0.25 m. The port has no health
 *  model: DFKZ lines with the damage; the flashbang acts on the local player (flash / deafness). */
void ADFPlayerPawn::GrenadeKillZone(const FDFGrenade& G)
{
	const DFWeapons::FWeaponSpec* Sp = DFWeapons::Find(G.Type);
	if (!Sp) return;
	const double RMax = Sp->Ammo.KzMaxRadius, RMin = Sp->Ammo.KzMinRadius;
	const FVector Blast = G.Pos;
	auto Los = [this](const FVector& A0, const FVector& B0)
	{
		const FVector A = A0 + FVector(0, 0.25, 0), B = B0 + FVector(0, 0.25, 0), L = B - A;
		double T = 0; FVector Pt, N;
		return !(World && L.Size() > 1e-4 && World->CastRay(A, L.GetSafeNormal(), L.Size(), T, Pt, N));
	};
	auto Handle = [&](const FString& Name, const FVector& Feet, bool bSelf)
	{
		const double Dist3 = (Feet - Blast).Size();
		if (Dist3 > PHYS::Radius + 2.0 * RMax) return;
		const double Dist = FMath::Max(0.0, Dist3 - PHYS::Radius);
		const bool bLos = Los(Blast, Feet);
		if (G.Type == EDFWeapon::Flashbang)
		{
			if (!bSelf) return;
			S.FlashLevel = FMath::Max(S.FlashLevel, 128);   // 0x4758c0: inside r + 2R, before the 20 m / LOS tests
			if (Dist <= RMax && bLos) AddFlash(Blast, Dist);
			UE_LOG(LogDFPawn, Display, TEXT("DFKZ {\"type\":\"flash\",\"target\":\"%s\",\"dist\":%.2f,\"los\":%s,\"flash\":%d}"), *Name, Dist, bLos ? TEXT("true") : TEXT("false"), S.FlashLevel);
			return;
		}
		if (Dist > RMax || !bLos) return;
		double Dmg = Sp->Ammo.KzDamage;
		if (Dist > RMin) Dmg *= 1.0 - (Dist - RMin) / (RMax - RMin);
		UE_LOG(LogDFPawn, Display, TEXT("DFKZ {\"type\":\"frag\",\"target\":\"%s\",\"dist\":%.2f,\"damage\":%.1f}"), *Name, Dist, Dmg);
	};
	Handle(TEXT("self"), DFMove->P.Pos, true);   // the thrower is not immune
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<ADFPlayerPawn> It(W); It; ++It)
		{
			if (*It == this) continue;
			Handle((*It)->GetName(), DF::FromUE((*It)->GetActorLocation() - FVector(0, 0, GetCapsuleHalfHeightCm())), false);
		}
	}
}

/** The local player is flashed: white-out 255 and hearing loss 40960 (own / friendly grenade), decaying 264 per step facing the blast, 352 with the back turned. */
void ADFPlayerPawn::AddFlash(const FVector& BlastPos, double Dist)
{
	S.FlashLevel = 255;
	const FVector Dv = DFMove->P.Pos - BlastPos;
	const double Bearing = FMath::Atan2(-Dv.X, -Dv.Z);
	const bool bBackTurned = FMath::Abs(DF::WrapPi(Bearing - P.Yaw)) < 90.0 * DF::DEG;
	S.HearingLoss = 40960;
	S.HearingStep = bBackTurned ? 352 : 264;
	if (FlashSoundMix && FlashSoundClass && !S.bMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(this, FlashSoundMix);
		S.bMixPushed = true;
	}
}

// ---- per frame / per tick

void ADFPlayerPawn::UpdateWeapon(double Dt)
{
	for (int32 i = Impacts.Num() - 1; i >= 0; --i)
	{
		FImpact& Im = Impacts[i];
		Im.T -= Dt;
		if (Im.T <= 0)
		{
			if (Effects) Effects->Impact(Im.Point, Im.Normal);
			Impacts.RemoveAt(i);
		}
	}
	S.WAcc += Dt;
	while (S.WAcc >= TICK) { S.WAcc -= TICK; WeaponTick(); }
	SyncControlRotation();   // the recoil climb changed the view pitch
	// thrown grenades: model + emitters follow the sim (interpolated between ticks)
	if (Effects)
	{
		const double Frac = S.WAcc / TICK;
		for (const FDFGrenade& G : GrenadeSim.GetGrenades())
		{
			if (const int32* H = GrenadeFx.Find(G.Id))
			{
				if (*H >= 0) Effects->UpdateGrenade(*H, G.bAtRest ? G.Pos : G.Pos + G.Vel * Frac, G.GetQuat(), (double(G.Age) + Frac) * TICK);
			}
		}
	}
}

void ADFPlayerPawn::WeaponTick()
{
	// acc -= (acc + 4) >> 3 per tick (0x47d8bd); what it loses climbs into the view pitch for good (0x47d8cb).
	// The original does not clamp here; clamping to the mouse limits keeps the view from flipping. [DEVIATION]
	const double Lost = S.Recoil * CAR15::RecoilDecay;
	S.Recoil -= Lost;
	P.Pitch = DF::Clamp(P.Pitch + Lost * CAR15::RecoilClimb, -VIEW::PitchMax * DF::DEG,
		(DFMove->Stance == EDFStance::Prone ? VIEW::PitchMaxProne : VIEW::PitchMax) * DF::DEG);

	// soldier attack timers (0x47d8f8) and the flashbang white-out (0x4877d7)
	FDFSoldierPose& Pose = DFMove->Pose;
	if (Pose.ArmSwingTicks > 0) --Pose.ArmSwingTicks;
	if (Pose.FireKickTicks > 0) --Pose.FireKickTicks;
	S.FlashLevel = S.FlashLevel > 3 ? S.FlashLevel - 4 : 0;
	if (S.HearingLoss > 0)
	{
		S.HearingLoss = FMath::Max(0, S.HearingLoss - S.HearingStep);   // one mixer step per tick [INFERRED]
		if (S.bMixPushed && FlashSoundMix && FlashSoundClass)
		{
			const float Vol = float(FMath::Clamp(1.0 - double(S.HearingLoss) / 32768.0, 0.0, 1.0));
			UGameplayStatics::SetSoundMixClassOverride(this, FlashSoundMix, FlashSoundClass, Vol, 1.f, 0.f);
			if (S.HearingLoss == 0)
			{
				UGameplayStatics::ClearSoundMixClassOverride(this, FlashSoundMix, FlashSoundClass, 0.f);
				UGameplayStatics::PopSoundMixModifier(this, FlashSoundMix);
				S.bMixPushed = false;
			}
		}
	}
	if (S.ReloadSfxTicks >= 0)
	{
		if (!Arsenal.IsReloading()) S.ReloadSfxTicks = -1;
		else if (--S.ReloadSfxTicks == 0)
		{
			S.ReloadSfxTicks = -1;
			if (const UDFWeaponData* D = DataFor(Arsenal.GetCurrent())) PlayDFSound(D->ReloadEndSound);
		}
	}
	if (!S.bFireHeld) S.BurstShots = 0;

	FDFArsenal::FTickInput In;
	In.bFireHeld = S.bFireHeld;
	In.bAirborne = !DFMove->P.bGround;
	Arsenal.Tick(In, WeaponEvents);
	GrenadeSim.Tick(World, WeaponEvents);
}

// ---- harness: -DFKeys=<t>:<action>,...

void ADFPlayerPawn::RunScriptedKeys()
{
	while (ScriptNext < ScriptKeys.Num() && ScriptKeys[ScriptNext].T <= S.T)
	{
		DoScriptedKey(ScriptKeys[ScriptNext].Action);
		++ScriptNext;
	}
}

void ADFPlayerPawn::DoScriptedKey(const FString& Action)
{
	LogWpn(TEXT("key"), FString::Printf(TEXT("\"action\":\"%s\""), *Action));
	if (Action.Len() == 1 && FChar::IsDigit(Action[0]) && Action[0] != '0') Arsenal.SelectCategory(Action[0] - '0', WeaponEvents);
	else if (Action == TEXT("wheelup")) Arsenal.Cycle(-1, WeaponEvents);
	else if (Action == TEXT("wheeldown")) Arsenal.Cycle(+1, WeaponEvents);
	else if (Action == TEXT("lmb")) { Arsenal.PressFire(WeaponEvents); bFireEdge = true; }
	else if (Action == TEXT("lmbdown")) { Arsenal.PressFire(WeaponEvents); bScriptFireHeld = true; }
	else if (Action == TEXT("lmbup")) bScriptFireHeld = false;
	else if (Action == TEXT("rmbdown")) { Arsenal.PressAltFire(); Arsenal.ToggleSights(); }
	else if (Action == TEXT("rmbup")) Arsenal.ReleaseAltFire(WeaponEvents);
	else if (Action == TEXT("r")) Arsenal.RequestReload();
	else UE_LOG(LogDFPawn, Warning, TEXT("-DFKeys: unknown action '%s'"), *Action);
}

// ------------------------------------------------------------------ camera

void ADFPlayerPawn::UpdateCamera(double Dt)
{
	S.AdsT = DF::Approach(S.AdsT, S.bAds ? 1 : 0, Dt * SimHz / CAR15::AdsBlendTicks);   // 15-tick pos <-> tpos blend

	// render state (interpolated between the previous and the current sim tick) comes from the movement component
	const FDFSimState& M = DFMove->P;
	const FVector RenderPos = DFMove->RenderPos;
	const double RenderLean = DFMove->RenderLean;

	// first-person camera: pitch + recoil (0x43b6b4), roll = lean/4 + view roll (0x43b6bd); the chase camera does not roll
	const double CamRoll = S.bThird ? 0.0 : -(RenderLean * LEAN::CameraRoll + M.ViewRoll);
	FQuat CamQ = DF::EulerYXZ(P.Pitch + CAR15::RecoilCamera * S.Recoil, P.Yaw, CamRoll);
	if (bVmOk) Vm->SetRecoilPitch((CAR15::RecoilWeaponModel - CAR15::RecoilCamera) * S.Recoil);   // weapon drawn at pitch + 2*recoil (0x4892f0)
	const FVector Hp = DFMove->LeanedHead(RenderPos, RenderLean);
	// FOV: 80 deg horizontal, x0.8 with a Sighted weapon's sights up (0x4b73c7); eased 1/32 per tick (0x4f5e9f)
	const double FovTarget = VIEW::HFov * (S.bAds && !S.bThird ? CAR15::SightsFovScale : 1.0);
	S.FovH += (FovTarget - S.FovH) * (1 - FMath::Pow(1 - VIEW::FovEase, Dt * SimHz));
	Camera->SetFieldOfView(static_cast<float>(S.FovH));

	if (!S.bThird)
	{
		CamPos = Hp;
	}
	else
	{
		const FVector TpOff = CamQ.RotateVector(FVector(TUNE::TpRight * (1 - S.AdsT * 0.4), TUNE::TpUp, TUNE::TpBack));
		const double Full = TpOff.Size();
		const FVector TpSeg = TpOff / Full;
		double Lim = World ? World->ClipCamera(Hp, TpSeg, Full, 0.25) : Full;
		Lim = FMath::Max(Lim, 0.3);
		TpDist = Lim < TpDist ? Lim : TpDist + (Lim - TpDist) * FMath::Min(1.0, Dt * 8);
		CamPos = Hp + TpSeg * TpDist;
	}
	if (bDebugCam)   // -DFCam=dx,dy,dz,lx,ly,lz: fixed camera relative to the player (testing)
	{
		CamPos = FVector(M.Pos.X + DebugCam[0], M.Pos.Y + DebugCam[1], M.Pos.Z + DebugCam[2]);
		const FVector Target(M.Pos.X + DebugCam[3], M.Pos.Y + DebugCam[4], M.Pos.Z + DebugCam[5]);
		const FVector F = (Target - CamPos).GetSafeNormal();
		// camera.lookAt with up = +Y: yaw about +Y (forward = (-sin y, 0, -cos y)), then pitch about +X
		CamQ = DF::EulerYXZ(FMath::Asin(DF::Clamp(F.Y, -1.0, 1.0)), FMath::Atan2(-F.X, -F.Z), 0.0);
	}

	Camera->SetWorldLocationAndRotation(DF::ToUE(CamPos), DF::ToUE(CamQ.GetNormalized()));
}

// ------------------------------------------------------------------ per frame, after the movement (main.js step() without updatePlayer)

void ADFPlayerPawn::SetBodyVisibility(bool bWantSoldier, bool bWantVm, bool bWantTpGun)
{
	// compare against the components themselves (a cached flag missed the first update when it already matched)
	if (SoldierMesh->GetVisibleFlag() != bWantSoldier)
	{
		SoldierMesh->SetVisibility(bWantSoldier, true);
	}
	const bool bGun = bWantSoldier && bWantTpGun;
	if (TPGun->GetVisibleFlag() != bGun) TPGun->SetVisibility(bGun, true);
	if (VMMesh->GetVisibleFlag() != bWantVm)
	{
		VMMesh->SetVisibility(bWantVm, true);
	}
}

void ADFPlayerPawn::PostMoveStep(double Dt)
{
	if (DFMove->ConsumeJumped()) bJumpPending = false;   // the harness jump happened
	UpdateCamera(S.bPlaying ? Dt : 0.0001);

	// visibility by view mode
	const bool bFirst = !S.bThird;
	const bool bDrawn = Arsenal.IsDrawn();   // NoClipsNoDraw weapons with an empty pool (the last grenade gone) are not drawn
	SetBodyVisibility(bSoldierOk && !bFirst, bVmOk && bFirst && !IsScopeOn() && bDrawn, bDrawn);
	if (bVmOk) Vm->SetModelPos(bVmPosOverride ? VmPosOverride : Arsenal.GetModelPosUnits(!DFMove->P.bGround));
	if (bVmOk) Vm->Update(S.bPlaying ? Dt : 0.0);

	if (S.bPlaying) UpdateWeapon(Dt);
	if (Effects)
	{
		Effects->UpdateMuzzleFlash(GetMuzzle(), bFirst, bDrawn && (bFirst ? bVmOk && !S.bAds : bSoldierOk));
		Effects->UpdateFX(static_cast<float>(Dt), CamPos);
	}
}

/** End of a DF move (called by the movement component, not for client replays): the soldier hips face the body yaw; spine, arms and
 *  head bend toward the view (0x484150). The rig is advanced per move so the server reproduces the client's root velocity. */
void ADFPlayerPawn::OnDFStepped(double Dt)
{
	if (!bSoldierOk) return;
	const FDFSimState& M = DFMove->P;
	const bool bLocal = IsLocallyControlled();
	SoldierRoot->SetWorldLocationAndRotation(DF::ToUE(DFMove->RenderPos), DF::ToUE(FQuat(FVector(0, 1, 0), DFMove->RenderBodyYaw)));
	FDFSoldierPose& Pose = DFMove->Pose;
	Pose.AimYaw = DF::WrapPi(M.Yaw - DFMove->RenderBodyYaw);
	Pose.Pitch = bLocal ? P.Pitch : FRotator::NormalizeAxis(GetBaseAimRotation().Pitch) * DF::DEG;
	Pose.Recoil = bLocal ? S.Recoil : 0.0;
	Pose.LegYawR = DF::WrapPi(M.LegR - DFMove->RenderBodyYaw); Pose.LegYawL = DF::WrapPi(M.LegL - DFMove->RenderBodyYaw);
	Pose.BodyRoll = M.BodyRoll; Pose.ViewRoll = M.ViewRoll;
	Soldier->Update(S.bPlaying ? Dt : 0.0, Pose);
	if (HasAuthority()) FillRepPose();
}

void ADFPlayerPawn::PushHud()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
	int32 SX = 0, SY = 0;
	PC->GetViewportSize(SX, SY);
	if (SX > 0 && SY > 0) CamAspect = double(SX) / double(SY);
	ADFHUD* H = Cast<ADFHUD>(PC->GetHUD());
	if (!H) return;
	FDFHudState& St = H->State;
	const DFWeapons::FWeaponSpec* Sp = Arsenal.GetCurrentSpec();
	const UDFWeaponData* WD = Sp ? DataFor(Sp->Id) : nullptr;
	St.bScope = IsScopeOn();
	St.HeadingDeg = -P.Yaw / DF::DEG;
	St.bThirdPerson = S.bThird;
	St.Stance = int32(DFMove->Stance);
	St.Health01 = 1.0;   // no damage in the port yet: the stance figure stays green
	St.WeaponId = Sp ? int32(Sp->Id) : 0;
	St.WeaponName = Sp ? FString(Sp->HudName) : FString();
	St.WeaponIcon = WD ? WD->HudIcon.Get() : nullptr;
	if (Sp)
	{
		const int32 Clip = FMath::Max(0, Arsenal.GetClip()), Pool = Arsenal.GetReserve();
		St.ClipFadeKey = Sp->ClipSlot * 16 + int32(Sp->AmmoClass);
		if (Sp->ClipSize == -1) { St.AmmoMode = 0; St.Mag = 0; St.Reserve = 0; }                       // knife: no ammo text
		else if (Sp->ClipSize == 1) { St.AmmoMode = 2; St.Mag = Clip; St.Reserve = Pool + Clip; }      // grenades: one number, the loaded round included
		else { St.AmmoMode = 1; St.Mag = Clip; St.Reserve = Pool; }
		St.bClipGfx = Sp->bHasClipGfx;
		St.HudRndStartDx = Sp->HudRnd.StartDx; St.HudRndStartDy = Sp->HudRnd.StartDy;
		St.HudRndStepDx = Sp->HudRnd.StepDx; St.HudRndStepDy = Sp->HudRnd.StepDy; St.HudRndPerBar = Sp->HudRnd.PerBar;
	}
	else { St.AmmoMode = 0; St.ClipFadeKey = 0; St.bClipGfx = false; }
	St.bCrosshair = Sp != nullptr && !IsScopeOn();
	St.PowerBar = Arsenal.GetPowerBar();
	St.FlashAlpha = double(S.FlashLevel) / 255.0;
	St.TimeSec = S.T;
	St.PosUE = GetActorLocation();
}

// ------------------------------------------------------------------ other players' pawns

/** Server: the pose inputs everybody else needs to animate this soldier (the simulation itself does not run on the proxies). */
void ADFPlayerPawn::FillRepPose()
{
	const FDFSimState& M = DFMove->P;
	const FDFSoldierPose& Pose = DFMove->Pose;
	FDFRepPose R;
	R.Stance = uint8(DFMove->Stance);
	R.MoveX = int8(FMath::Clamp(FMath::RoundToInt(Pose.MoveX), -1, 1));
	R.MoveZ = int8(FMath::Clamp(FMath::RoundToInt(Pose.MoveZ), -1, 1));
	R.bAirborne = Pose.bAirborne ? 1 : 0;
	R.Lean = int16(FMath::Clamp(FMath::RoundToInt(M.Lean * 10000.0), -32767, 32767));
	R.Roll = int8(M.Roll);
	R.BodyYaw = QuantAngle(DFMove->RenderBodyYaw);
	R.LegR = QuantAngle(Pose.LegYawR);
	R.LegL = QuantAngle(Pose.LegYawL);
	R.BodyRoll = QuantAngle(M.BodyRoll);
	R.ViewRoll = QuantAngle(M.ViewRoll);
	R.Weapon = DFMove->GetMoveInput().Weapon;
	RepPose = R;
}

/** Simulated proxy: no sim, no camera / HUD / weapon. The soldier stands at the smoothed feet (CharacterMesh0) and plays the replicated pose. */
void ADFPlayerPawn::TickProxy(double Dt)
{
	if (!bSoldierOk) return;
	SetBodyVisibility(true, false, true);
	if (EDFWeapon(RepPose.Weapon) != ProxyWeapon)   // the soldier's hand
	{
		ProxyWeapon = EDFWeapon(RepPose.Weapon);
		const UDFWeaponData* D = DataFor(ProxyWeapon);
		Soldier->SetWeapon(ProxyWeapon, D ? D->ThirdPersonMesh.Get() : nullptr);
	}

	const double TBody = DequantAngle(RepPose.BodyYaw), TLegR = DequantAngle(RepPose.LegR), TLegL = DequantAngle(RepPose.LegL);
	const double TLean = double(RepPose.Lean) / 10000.0, TBodyRoll = DequantAngle(RepPose.BodyRoll), TViewRoll = DequantAngle(RepPose.ViewRoll);
	if (!bProxyPoseInit)
	{
		bProxyPoseInit = true;
		ProxyBodyYaw = TBody; ProxyLegR = TLegR; ProxyLegL = TLegL; ProxyLean = TLean; ProxyBodyRoll = TBodyRoll; ProxyViewRoll = TViewRoll;
	}
	else
	{
		const double A = 1.0 - FMath::Exp(-Dt * 20.0);   // the pose arrives at the net update rate: ease toward it
		ProxyBodyYaw += DF::WrapPi(TBody - ProxyBodyYaw) * A;
		ProxyLegR += (TLegR - ProxyLegR) * A; ProxyLegL += (TLegL - ProxyLegL) * A;
		ProxyLean += (TLean - ProxyLean) * A;
		ProxyBodyRoll += DF::WrapPi(TBodyRoll - ProxyBodyRoll) * A; ProxyViewRoll += DF::WrapPi(TViewRoll - ProxyViewRoll) * A;
	}

	SoldierRoot->SetWorldLocationAndRotation(GetMesh()->GetComponentLocation(), DF::ToUE(FQuat(FVector(0, 1, 0), ProxyBodyYaw)));
	FDFSoldierPose Pose;
	Pose.MoveX = RepPose.MoveX; Pose.MoveZ = RepPose.MoveZ;
	Pose.Stance = EDFStance(FMath::Min<int32>(RepPose.Stance, 2));
	Pose.bAirborne = RepPose.bAirborne != 0;
	Pose.Lean = ProxyLean; Pose.Roll = RepPose.Roll;
	const double ViewYaw = 0.0 - FRotator::NormalizeAxis(GetActorRotation().Yaw) * DF::DEG;   // the actor yaw follows the view
	Pose.AimYaw = DF::WrapPi(ViewYaw - ProxyBodyYaw);
	Pose.Pitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch) * DF::DEG;
	Pose.Recoil = 0.0;
	Pose.LegYawR = ProxyLegR; Pose.LegYawL = ProxyLegL;
	Pose.BodyRoll = ProxyBodyRoll; Pose.ViewRoll = ProxyViewRoll;
	if (const DFWeapons::FWeaponSpec* Wp = DFWeapons::Find(ProxyWeapon)) { Pose.SpecialHold = Wp->SpecialHold; Pose.RunAnim = Wp->RunAnim; }
	Soldier->Update(Dt, Pose);
}

/** The server's (or listen host's) view of a remote client's pawn: the rig advances per move; here it follows the smoothed feet. */
void ADFPlayerPawn::TickRemoteOnServer(double Dt)
{
	if (!bSoldierOk || !DFMove) return;
	SetBodyVisibility(true, false, true);
	if (EDFWeapon(DFMove->GetMoveInput().Weapon) != ProxyWeapon)   // the weapon of the client's last move
	{
		ProxyWeapon = EDFWeapon(DFMove->GetMoveInput().Weapon);
		const UDFWeaponData* D = DataFor(ProxyWeapon);
		Soldier->SetWeapon(ProxyWeapon, D ? D->ThirdPersonMesh.Get() : nullptr);
	}
	SoldierRoot->SetWorldLocationAndRotation(GetMesh()->GetComponentLocation(), DF::ToUE(FQuat(FVector(0, 1, 0), DFMove->RenderBodyYaw)));
}

void ADFPlayerPawn::LogNetDbg(double RawDt)
{
	NetDbgClock += RawDt;
	if (NetDbgClock < 1.0) return;
	NetDbgClock -= 1.0;
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FVector Feet = DF::FromUE(GetMesh()->GetComponentLocation());
		UE_LOG(LogDFPawn, Display, TEXT("DBGPROXY {\"name\":\"%s\",\"t\":%.2f,\"stance\":\"%s\",\"airborne\":%s,\"pos\":[%.3f,%.3f,%.3f],\"bodyYaw\":%.1f,\"moveX\":%d,\"moveZ\":%d}"),
			*GetName(), GetWorld()->GetTimeSeconds(), StanceName(EDFStance(FMath::Min<int32>(RepPose.Stance, 2))), RepPose.bAirborne ? TEXT("true") : TEXT("false"),
			Feet.X, Feet.Y, Feet.Z, DequantAngle(RepPose.BodyYaw) / DF::DEG, RepPose.MoveX, RepPose.MoveZ);
	}
	else if (HasAuthority() && DFMove)
	{
		const FDFSimState& M = DFMove->P;
		UE_LOG(LogDFPawn, Display, TEXT("DBGNET {\"name\":\"%s\",\"t\":%.2f,\"stance\":\"%s\",\"ground\":%s,\"pos\":[%.3f,%.3f,%.3f],\"yaw\":%.2f}"),
			*GetName(), DFMove->SimTime, StanceName(DFMove->Stance), M.bGround ? TEXT("true") : TEXT("false"), M.Pos.X, M.Pos.Y, M.Pos.Z, M.Yaw / DF::DEG);
	}
}

// ------------------------------------------------------------------ test harness

void ADFPlayerPawn::LogJump(double Apex, double Air) const
{
	UE_LOG(LogDFPawn, Display, TEXT("JUMP apex=%.3f air=%.3f"), Apex, Air);
}

void ADFPlayerPawn::LogDbg()
{
	const FDFSimState& M = DFMove->P;
	const double EyeNow = bSoldierOk ? Soldier->EyeNow() : 0.0;
	UE_LOG(LogDFPawn, Display,
		TEXT("DBG {\"t\":%.2f,\"stance\":\"%s\",\"eye\":%.3f,\"hip\":%.3f,\"sEye\":%.3f,\"cy\":%.3f,\"ground\":%s,\"pitch\":%.2f,\"recoil\":%.2f,\"shots\":%d,\"viewRoll\":%.1f,\"bodyRoll\":%.1f,\"tracers\":%d,")
		TEXT("\"pos\":[%.3f,%.3f,%.3f],\"yaw\":%.2f,\"lean\":%.2f,\"mag\":%d,\"reserve\":%d,\"reloading\":%s,\"fov\":%.2f,\"adsT\":%.2f,\"third\":%s,")
		TEXT("\"weapon\":\"%s\",\"action\":\"%s\",\"grenades\":%d,\"flash\":%d,\"drawn\":%s,\"vmVisible\":%s,\"sights\":%s}"),
		S.T, StanceName(DFMove->Stance), M.Eye, M.Hip, EyeNow, CamPos.Y, M.bGround ? TEXT("true") : TEXT("false"),
		P.Pitch / DF::DEG, S.Recoil / DF::DEG, S.Shots, M.ViewRoll / DF::DEG, M.BodyRoll / DF::DEG,
		Effects ? Effects->CountLiveTracers() : 0,
		M.Pos.X, M.Pos.Y, M.Pos.Z, P.Yaw / DF::DEG, M.Lean / DF::DEG, Arsenal.GetClip(), Arsenal.GetReserve(),
		Arsenal.IsReloading() ? TEXT("true") : TEXT("false"), S.FovH, S.AdsT, S.bThird ? TEXT("true") : TEXT("false"),
		WeaponName(Arsenal.GetCurrent()), ActionName(Arsenal.GetAction()), GrenadeSim.GetGrenades().Num(), S.FlashLevel,
		Arsenal.IsDrawn() ? TEXT("true") : TEXT("false"), (VMMesh && VMMesh->GetVisibleFlag()) ? TEXT("true") : TEXT("false"),
		Arsenal.IsSightsUp() ? TEXT("true") : TEXT("false"));
}

void ADFPlayerPawn::HarnessTick(double RawDt)
{
	if (bDbg)
	{
		DbgClock += RawDt;
		if (DbgClock >= 1.0) { DbgClock -= 1.0; LogDbg(); }
	}
	if (!ShotPath.IsEmpty())
	{
		ShotClock += RawDt;
		if (ShotRequestedAt < 0)
		{
			if (ShotClock >= ShotDelay)
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShotPath), true);
				IFileManager::Get().Delete(*ShotPath, false, true, true);
				FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
				ShotRequestedAt = ShotClock;
				UE_LOG(LogDFPawn, Display, TEXT("Screenshot requested: %s"), *ShotPath);
			}
		}
		else
		{
			if (ShotDoneAt < 0 && IFileManager::Get().FileExists(*ShotPath)) ShotDoneAt = ShotClock;
			if ((ShotDoneAt >= 0 && ShotClock - ShotDoneAt > 0.5) || ShotClock - ShotRequestedAt > 5.0)
			{
				UE_LOG(LogDFPawn, Display, TEXT("Screenshot %s, exiting"), ShotDoneAt >= 0 ? TEXT("written") : TEXT("timed out"));
				FPlatformMisc::RequestExit(false);
			}
		}
	}
}
