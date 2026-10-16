// Port of DFBHD-WebLite src/main.js updatePlayer (movement half) / simTick / turnLeg / leanedHead, run as the custom movement mode
// DFMOVE_Sim of the stock character movement component. Variable names follow main.js (P = player); the simulation is in three
// space (DFMath.h) and only the capsule is in Unreal space. The movement code is moved verbatim from the former ADFPlayerPawn.
#include "DFCharacterMovementComponent.h"
#include "DFGameData.h"
#include "DFMath.h"
#include "DFPlayerPawn.h"
#include "DFWorld.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFMove, Log, All);

using namespace DFData;

namespace
{
	const FStanceData& StanceData(EDFStance St)
	{
		return St == EDFStance::Prone ? STANCE::Prone : St == EDFStance::Crouch ? STANCE::Crouch : STANCE::Stand;
	}
	constexpr double LEAN_STEP = LEAN::StepDeg * DF::DEG;
	constexpr double LEG_TURN_MAX = BODY::LegTurnMaxDeg * DF::DEG;
	constexpr double LEG_MAX_OFF = BODY::LegMaxOffDeg * DF::DEG;
	constexpr double LEG_STEP = BODY::StepDeg * DF::DEG;
	constexpr double LEG_SETTLE = BODY::SettleDeg * DF::DEG;
	constexpr double BODY_SPIN = ROLL::BodySpinDeg * DF::DEG;
	constexpr double VIEW_SPIN = ROLL::ViewSpinDeg * DF::DEG;
	constexpr double TICK = DFData::Tick;
	constexpr double RADIUS = PHYS::Radius;
}

// ------------------------------------------------------------------ move input / network structs

uint16 FDFMoveInput::Pack() const
{
	auto E = [](int8 V) { return uint16(FMath::Clamp<int32>(V, -1, 1) + 1); };
	return uint16(E(FIn) | (E(SIn) << 2) | (E(LeanIn) << 4) | (E(RollIn) << 6) | ((bJump ? 1 : 0) << 8) | (uint16(Stance) << 9) | (uint16(Weapon & 7) << 11));
}

void FDFMoveInput::Unpack(uint16 Bits)
{
	auto D = [Bits](int32 Shift) { return int8(FMath::Clamp<int32>(int32((Bits >> Shift) & 3) - 1, -1, 1)); };
	FIn = D(0); SIn = D(2); LeanIn = D(4); RollIn = D(6);
	bJump = ((Bits >> 8) & 1) != 0;
	Stance = EDFStance(FMath::Min<int32>((Bits >> 9) & 3, 2));
	Weapon = uint8((Bits >> 11) & 7);
}

void FDFNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);
	Input = static_cast<const FDFSavedMove&>(ClientMove).Input;
}

bool FDFNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
	{
		return false;
	}
	uint16 Bits = Input.Pack();
	Ar.SerializeBits(&Bits, 14);
	if (Ar.IsLoading())
	{
		Input.Unpack(Bits);
	}
	return !Ar.IsError();
}

void FDFMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	if (IsCorrection())
	{
		if (const UDFCharacterMovementComponent* DFMove = Cast<UDFCharacterMovementComponent>(&CharacterMovement))
		{
			State = DFMove->GetPendingCorrectionState();
		}
	}
}

bool FDFMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}
	if (IsCorrection())
	{
		float Vy = float(State.Vy), VelX = float(State.Vel.X), VelY = float(State.Vel.Y), RollT = float(State.RollT), SimAcc = float(State.SimAcc), Height = float(State.Height);
		uint8 Bits = uint8((State.bGround ? 1 : 0) | ((FMath::Clamp(State.Roll, -1, 1) + 1) << 1) | (uint8(State.Stance) << 3));
		uint8 JumpCd = uint8(FMath::Clamp(State.JumpCd, 0, 255));
		Ar << Vy << VelX << VelY << RollT << SimAcc << Height << Bits << JumpCd;
		if (Ar.IsLoading())
		{
			State.Vy = Vy; State.Vel = FVector2D(VelX, VelY); State.RollT = RollT; State.SimAcc = SimAcc; State.Height = Height;
			State.bGround = (Bits & 1) != 0;
			State.Roll = int32((Bits >> 1) & 3) - 1;
			State.Stance = EDFStance(FMath::Min<int32>((Bits >> 3) & 3, 2));
			State.JumpCd = JumpCd;
		}
	}
	return !Ar.IsError();
}

void FDFSavedMove::Clear()
{
	Super::Clear();
	Input = FDFMoveInput();
}

void FDFSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UDFCharacterMovementComponent* DFMove = Cast<UDFCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		Input = DFMove->GetMoveInput();
		Input.Stance = DFMove->Stance;
	}
}

void FDFSavedMove::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UDFCharacterMovementComponent* DFMove = Cast<UDFCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		DFMove->SetMoveInput(Input);
		DFMove->SetReplayYaw(SavedControlRotation.Yaw);
	}
}

bool FDFSavedMove::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	if (Super::IsImportantMove(LastAckedMove))
	{
		return true;
	}
	const FDFSavedMove* Last = static_cast<const FDFSavedMove*>(LastAckedMove.Get());
	return Last && !(Input == Last->Input);
}

FSavedMovePtr FDFNetworkPredictionData_Client::AllocateNewMove()
{
	return MakeShared<FDFSavedMove>();
}

// ------------------------------------------------------------------ component

UDFCharacterMovementComponent::UDFCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetNetworkMoveDataContainer(DFMoveDataContainer);
	SetMoveResponseDataContainer(DFResponseContainer);

	// the whole DF simulation is one custom mode; nothing of the stock walking / falling / nav code may run
	MovementMode = MOVE_Custom;
	CustomMovementMode = DFMOVE_Sim;
	bEnablePhysicsInteraction = false;
	bUseRVOAvoidance = false;
	bRunPhysicsWithNoController = false;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	bAllowPhysicsRotationDuringAnimRootMotion = false;
	bShrinkProxyCapsule = false;
	bProjectNavMeshWalking = false;
	bCanWalkOffLedges = false;
	NavAgentProps.bCanCrouch = false;
}

void UDFCharacterMovementComponent::OnRegister()
{
	Super::OnRegister();
	DFPawn = Cast<ADFPlayerPawn>(GetOwner());
}

void UDFCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// the locally controlled pawn builds this frame's DF move input (harness clock, held keys) before the stock tick turns it into a move
	if (DFPawn && HasValidData() && CharacterOwner->IsLocallyControlled())
	{
		DFPawn->PrepareMoveInput(DeltaTime);
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UDFCharacterMovementComponent::SetDefaultMovementMode()
{
	SetMovementMode(MOVE_Custom, DFMOVE_Sim);
}

void UDFCharacterMovementComponent::SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode)
{
	// the stock code asks for walking / falling / nav walking in many places; the DF sim handles all of those itself
	if (NewMovementMode == MOVE_None)
	{
		Super::SetMovementMode(MOVE_None, 0);
	}
	else
	{
		Super::SetMovementMode(MOVE_Custom, DFMOVE_Sim);
	}
}

void UDFCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode != DFMOVE_Sim)
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	StepDF(FMath::Min<double>(DeltaTime, 0.05));   // main.js step(): dt = min(dt, 0.05)
}

void UDFCharacterMovementComponent::SimulateMovement(float DeltaTime)
{
	// Simulated proxies do not run the DF sim: the server's capsule position and velocity arrive through ReplicatedMovement (the
	// engine smooths the Mesh), the pose through the pawn's replicated FDFRepPose. In between, extrapolate horizontally.
	if (!HasValidData() || UpdatedComponent->IsSimulatingPhysics())
	{
		return;
	}
	bNetworkUpdateReceived = false;
	bNetworkMovementModeChanged = false;
	bNetworkGravityDirectionChanged = false;
	bJustTeleported = false;
	bForceNextFloorCheck = false;
	if (!Velocity.IsNearlyZero())
	{
		UpdatedComponent->AddWorldOffset(FVector(Velocity.X, Velocity.Y, 0.0) * DeltaTime, false, nullptr, ETeleportType::None);
	}
	UpdateComponentVelocity();
}

void UDFCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	// server (and client replay): the move's DF input
	if (const FDFNetworkMoveData* MoveData = static_cast<const FDFNetworkMoveData*>(GetCurrentNetworkMoveData()))
	{
		Input = MoveData->Input;
		Stance = Input.Stance;
	}
}

FNetworkPredictionData_Client* UDFCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UDFCharacterMovementComponent* MutableThis = const_cast<UDFCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FDFNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

float UDFCharacterMovementComponent::GetCapsuleHalfHeight() const
{
	return CharacterOwner ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : float(STANCE::Stand.Height * 50.0);
}

// ------------------------------------------------------------------ network: corrections

FDFCorrectionState UDFCharacterMovementComponent::CaptureState() const
{
	FDFCorrectionState S;
	S.Vy = P.Vy; S.Vel = P.Vel; S.bGround = P.bGround; S.JumpCd = P.JumpCd; S.Roll = P.Roll; S.RollT = P.RollT;
	S.SimAcc = P.SimAcc; S.Stance = Stance; S.Height = P.Height;
	return S;
}

void UDFCharacterMovementComponent::ApplyCorrectionState(const FDFCorrectionState& S)
{
	P.Vy = S.Vy; P.Vel = S.Vel; P.bGround = S.bGround; P.JumpCd = S.JumpCd; P.Roll = S.Roll; P.RollT = S.RollT;
	P.SimAcc = S.SimAcc; Stance = S.Stance; P.Height = S.Height;
}

void UDFCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);
	// the correction is sent at the end of the frame: remember the DF state that belongs to the location the adjustment captured now
	if (const FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character())
	{
		if (ServerData->PendingAdjustment.TimeStamp == ClientTimeStamp && !ServerData->PendingAdjustment.bAckGoodMove)
		{
			PendingCorrectionState = CaptureState();
		}
	}
}

void UDFCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	// restore the DF state before the saved moves are replayed (the location itself is applied by the stock code right after this)
	++NumCorrections;
	const FVector Err = NewLocation - UpdatedComponent->GetComponentLocation();
	UE_LOG(LogDFMove, Display, TEXT("DFCORR #%d t=%.3f err=%.2f cm (%.1f, %.1f, %.1f)"), NumCorrections, TimeStamp, Err.Size(), Err.X, Err.Y, Err.Z);
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
	ApplyCorrectionState(static_cast<const FDFMoveResponseDataContainer&>(GetMoveResponseDataContainer()).State);
}

bool UDFCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// The server corrects location / velocity / DF state, but not the cosmetic state (lean, legs, body yaw, roll angles, tick counter):
	// replaying the saved moves would integrate it a second time, so keep what the client has. The live move input and stance too.
	const FDFSimState Saved = P;
	const FDFMoveInput SavedInput = Input;
	const EDFStance SavedStance = Stance;
	const FVector2D SavedGroundVel = GroundVel;
	const FDFSoldierPose SavedPose = Pose;
	const double SavedSpeed01 = Speed01;
	const FVector SavedPrevPos = PrevPos, SavedRenderPos = RenderPos;
	const double SavedPrevLean = PrevLean, SavedPrevBodyYaw = PrevBodyYaw, SavedRenderLean = RenderLean, SavedRenderBodyYaw = RenderBodyYaw;

	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();

	P.Yaw = Saved.Yaw; P.Eye = Saved.Eye; P.Hip = Saved.Hip;
	P.Lean = Saved.Lean; P.BodyYaw = Saved.BodyYaw; P.Tick = Saved.Tick;
	P.LegR = Saved.LegR; P.LegL = Saved.LegL; P.LegTgtR = Saved.LegTgtR; P.LegTgtL = Saved.LegTgtL;
	P.BodyRoll = Saved.BodyRoll; P.ViewRoll = Saved.ViewRoll;
	Input = SavedInput; Stance = SavedStance; GroundVel = SavedGroundVel; Pose = SavedPose; Speed01 = SavedSpeed01;
	PrevPos = SavedPrevPos; RenderPos = SavedRenderPos;
	PrevLean = SavedPrevLean; PrevBodyYaw = SavedPrevBodyYaw; RenderLean = SavedRenderLean; RenderBodyYaw = SavedRenderBodyYaw;
	return bResult;
}

// ------------------------------------------------------------------ one move

void UDFCharacterMovementComponent::InitSim(const FVector& FeetPos, double YawRad)
{
	P = FDFSimState();
	P.Pos = FeetPos;
	P.Yaw = YawRad;
	P.bGround = true;
	P.Eye = STANCE::Stand.Eye + EyeAboveTrack;
	P.Hip = STANCE::Stand.Hip;
	P.Height = STANCE::Stand.Height;
	P.BodyYaw = P.LegR = P.LegL = P.LegTgtR = P.LegTgtL = P.Yaw;
	P.Tick = 0;
	ResetRender();
	if (UpdatedComponent)
	{
		LastCapsuleLoc = UpdatedComponent->GetComponentLocation();
	}
}

void UDFCharacterMovementComponent::ResetRender()
{
	PrevPos = RenderPos = P.Pos;
	PrevLean = RenderLean = P.Lean;
	PrevBodyYaw = RenderBodyYaw = P.BodyYaw;
	ApplySimToCapsule();
}

void UDFCharacterMovementComponent::ApplySimToCapsule()
{
	if (!UpdatedComponent)
	{
		return;
	}
	// no sweep: collision is the DF code's job (ADFWorld queries)
	UpdatedComponent->SetWorldLocation(DF::ToUE(P.Pos) + FVector(0.0, 0.0, GetCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
	LastCapsuleLoc = UpdatedComponent->GetComponentLocation();
}

void UDFCharacterMovementComponent::SyncFromCapsule()
{
	// something else moved the capsule since the last move (server correction, teleport): the capsule is the truth then
	const FVector Cur = UpdatedComponent->GetComponentLocation();
	if (Cur != LastCapsuleLoc)
	{
		P.Pos = DF::FromUE(Cur - FVector(0.0, 0.0, GetCapsuleHalfHeight()));
		LastCapsuleLoc = Cur;
	}
}

double UDFCharacterMovementComponent::ResolveYaw() const
{
	// the locally controlled pawn drives the sim with its view yaw (unchanged code path); everywhere else (server move of a remote
	// client, replay of a saved move, forced update) it is the move's control rotation: three yaw = -UE yaw, unwrapped next to the
	// previous value so the leg / body math stays continuous
	if (DFPawn && CharacterOwner->IsLocallyControlled() && !CharacterOwner->bClientUpdating)
	{
		return DFPawn->GetViewYaw();
	}
	double YawDeg = CharacterOwner->GetControlRotation().Yaw;
	if (CharacterOwner->bClientUpdating)
	{
		YawDeg = ReplayYawDeg;   // the replayed saved move's own yaw (set in FDFSavedMove::PrepMoveFor)
	}
	else if (const FCharacterNetworkMoveData* MoveData = GetCurrentNetworkMoveData())
	{
		YawDeg = MoveData->ControlRotation.Yaw;
	}
	double Y = 0.0 - YawDeg * DF::DEG;
	Y += 2.0 * DF::PI_D * DF::JsRound((P.Yaw - Y) / (2.0 * DF::PI_D));
	return Y;
}

void UDFCharacterMovementComponent::StepDF(double Dt)
{
	if (!DFPawn || !UpdatedComponent)
	{
		return;
	}
	const bool bReplay = CharacterOwner && CharacterOwner->bClientUpdating;
	SyncFromCapsule();
	P.Yaw = ResolveYaw();
	if (!bReplay)
	{
		SimTime += Dt;
	}

	UpdatePlayer(Dt);

	if (!bReplay)
	{
		// render state: interpolate between the previous and the current sim tick (UE renders faster than 62 Hz)
		const double Alpha = DF::Clamp01(P.SimAcc / TICK);
		RenderPos = FMath::Lerp(PrevPos, P.Pos, Alpha);
		RenderLean = DF::Lerp(PrevLean, P.Lean, Alpha);
		RenderBodyYaw = PrevBodyYaw + DF::WrapPi(P.BodyYaw - PrevBodyYaw) * Alpha;
	}

	ApplySimToCapsule();
	// ReplicatedMovement / extrapolation: UE cm/s from the ground velocity of the animation plus the residual / air velocity
	Velocity = FVector(-(GroundVel.Y + P.Vel.Y), GroundVel.X + P.Vel.X, P.Vy) * DF::M2CM;

	// the soldier rig drives ground speed and eye / hip, so it advances per move (not again while the client replays saved moves)
	if (!bReplay)
	{
		DFPawn->OnDFStepped(Dt);
	}
}

// ------------------------------------------------------------------ player simulation: fixed 62 Hz tick (DFBHD.EXE 0x47ca90)

FVector UDFCharacterMovementComponent::LeanedHead(const FVector& Pos, double InLean) const
{
	const double H = P.Eye - P.Hip;
	const double Lat = LEAN::EyeLateral * H * FMath::Sin(InLean);
	const double Rx = FMath::Cos(P.Yaw), Rz = -FMath::Sin(P.Yaw);
	return FVector(Pos.X + Rx * Lat, Pos.Y + P.Hip + H * FMath::Cos(InLean), Pos.Z + Rz * Lat);
}

void UDFCharacterMovementComponent::UpdatePlayer(double Dt)
{
	double FIn = Input.FIn;   // W - S
	double SIn = Input.SIn;   // D - A
	// no lean while prone (0x47ed1f); there Q/E roll instead
	double LeanIn = 0;
	if (Stance != EDFStance::Prone)
	{
		LeanIn = Input.LeanIn;
	}
	// camera/hip heights come straight from the playing clip's root keys (crossfaded like the original);
	// camera = feet + t4 + 0.125 (0x43b5e0). There is no separate head bob: it is the clip's eye curve.
	const FDFSoldier* Soldier = DFPawn->GetSoldierRig();
	const double EyeNow = Soldier ? Soldier->EyeNow() : 0.0;
	const double HipNow = Soldier ? Soldier->HipNow() : 0.0;
	P.Eye = (EyeNow != 0.0 ? EyeNow : StanceData(Stance).Eye) + EyeAboveTrack;
	P.Hip = HipNow != 0.0 ? HipNow : StanceData(Stance).Hip;
	P.Height = DF::Approach(P.Height, StanceData(Stance).Height, Dt * 4);   // collision height [TUNED]
	// prone + Q/E: play the roll clip to its end (0x47e4ac); the clip's root keys carry the sideways motion
	const int32 RollIn = Input.RollIn;
	if (Stance == EDFStance::Prone && !P.Roll && RollIn) { P.Roll = RollIn; P.RollT = 0; }
	if (P.Roll)
	{
		P.RollT += Dt;
		if (P.RollT >= ROLL::Duration || Stance != EDFStance::Prone) P.Roll = 0; else { FIn = 0; SIn = 0; }
	}
	P.SimAcc += Dt;
	while (P.SimAcc >= TICK) { P.SimAcc -= TICK; SimTickDF(FIn, SIn, LeanIn); }
	const double Hs = GroundVel.Size() + P.Vel.Size();
	Speed01 = DF::Clamp(Hs / PHYS::RunSpeed, 0, 1);
	Pose.MoveX = SIn; Pose.MoveZ = -FIn; Pose.Stance = Stance; Pose.bAirborne = !P.bGround;
	Pose.Lean = P.Lean; Pose.Roll = P.Roll;
	// the weapon in hand (the move input carries it, so the server's rig plays RunFastK for the knife and the speeds match)
	const DFWeapons::FWeaponSpec* Wp = DFWeapons::Find(EDFWeapon(Input.Weapon));
	Pose.SpecialHold = Wp ? Wp->SpecialHold : 0;
	Pose.RunAnim = Wp ? Wp->RunAnim : 0;
}

/** One leg's yaw step toward its target, kept within 67.5 deg of the view (0x47cde4..0x47ce4d). */
double UDFCharacterMovementComponent::TurnLeg(double Leg, double Target) const
{
	Leg += DF::Clamp(DF::WrapPi(Target - Leg) * BODY::LegTurn, -LEG_TURN_MAX, LEG_TURN_MAX);
	const double Off = DF::WrapPi(Leg - P.Yaw);
	return Off > LEG_MAX_OFF ? P.Yaw + LEG_MAX_OFF : Off < -LEG_MAX_OFF ? P.Yaw - LEG_MAX_OFF : Leg;
}

void UDFCharacterMovementComponent::SimTickDF(double FIn, double SIn, double LeanIn)
{
	const ADFWorld* World = DFWorld;
	const bool bReplay = CharacterOwner && CharacterOwner->bClientUpdating;
	const bool bDbg = DFPawn->IsDbg() && !bReplay && CharacterOwner->IsLocallyControlled();

	// remember the state before this tick for render interpolation (does not touch the simulation)
	PrevPos = P.Pos; PrevLean = P.Lean; PrevBodyYaw = P.BodyYaw;

	// lean: decay 1/16, then +-4.22 deg while held (0x47d8e1, 0x47ed3a)
	P.Lean -= P.Lean * LEAN::Decay;
	if (LeanIn != 0) P.Lean += LeanIn * LEAN_STEP;
	for (int32 K = 0; K < 16 && FMath::Abs(P.Lean) > 1e-3 && World && World->SphereBlocked(LeanedHead(P.Pos, P.Lean), TUNE::HeadR); ++K) P.Lean *= 0.8;   // [DEVIATION]
	// body yaw (0x47cd60): moving, both legs chase the view; standing still, a leg steps when it is > 30 deg
	// off, or on its every-64-ticks check when > 5 deg off. The hips face midway between the legs.
	P.Tick++;
	if (!P.bGround || P.Roll || FIn != 0 || SIn != 0) P.LegTgtR = P.LegTgtL = P.Yaw;
	else
	{
		const double OffR = FMath::Abs(DF::WrapPi(P.Yaw - P.LegR)), OffL = FMath::Abs(DF::WrapPi(P.Yaw - P.LegL));
		if ((OffR > LEG_STEP || P.Tick % BODY::SettleTicks == 0) && OffR > LEG_SETTLE) P.LegTgtR = P.Yaw;
		if ((OffL > LEG_STEP || (P.Tick + BODY::SettleTicks / 2) % BODY::SettleTicks == 0) && OffL > LEG_SETTLE) P.LegTgtL = P.Yaw;
	}
	P.LegR = TurnLeg(P.LegR, P.LegTgtR); P.LegL = TurnLeg(P.LegL, P.LegTgtL);
	P.BodyYaw = P.LegL + DF::WrapPi(P.LegR - P.LegL) / 2;
	// roll angles (see ROLL): view roll eases 1/32 toward 0 in anim_idle_prone, else toward the body roll (0x47d941)
	const bool bProne = Stance == EDFStance::Prone;
	const bool bIdleProne = bProne && !P.Roll && FIn == 0 && SIn == 0;
	P.ViewRoll -= (bIdleProne ? P.ViewRoll : DF::WrapPi(P.ViewRoll - P.BodyRoll)) * ROLL::ViewEase;
	// body roll: prone it follows the (flat) ground every other tick, held during a roll; else decays (0x47df28)
	if (!bProne) P.BodyRoll -= P.BodyRoll * ROLL::BodyDecay;
	else if (!P.Roll && P.Tick % 2 == 0) P.BodyRoll -= P.BodyRoll * ROLL::BodyEaseProne;
	if (P.Roll) { P.BodyRoll += P.Roll * BODY_SPIN; P.ViewRoll += P.Roll * VIEW_SPIN; }   // 0x47e12e
	P.BodyRoll = DF::WrapPi(P.BodyRoll); P.ViewRoll = DF::WrapPi(P.ViewRoll);
	const double Sy = FMath::Sin(P.Yaw), Cy = FMath::Cos(P.Yaw);
	// the animation's root velocity, rotated by yaw (body frame: +x right, -z forward)
	double VAnimX = 0, VAnimZ = 0;
	if (const FDFSoldier* Soldier = DFPawn->GetSoldierRig()) Soldier->RootVelocity(VAnimX, VAnimZ);
	GroundVel.Set(Cy * VAnimX + Sy * VAnimZ, -Sy * VAnimX + Cy * VAnimZ);
	if (P.bGround)
	{
		P.Pos.X += GroundVel.X * TICK; P.Pos.Z += GroundVel.Y * TICK;
		P.Vel *= 7.0 / 8;                                                // residual velocity (v*7+4)>>3
		if (Input.bJump && P.JumpCd == 0 && Stance != EDFStance::Prone)   // jump (0x47eda0 / 0x47ee2a)
		{
			if (!bReplay) bJumped = true;
			JumpT0 = DbgT; JumpY0 = P.Pos.Y; JumpApex = 0;
			P.Vy = PHYS::JumpVelocity;
			P.Vel += GroundVel * 0.75;
			P.bGround = false; P.JumpCd = PHYS::JumpCooldownTicks;
			if (Stance == EDFStance::Crouch) Stance = EDFStance::Stand;
		}
	}
	else
	{
		P.Vy = FMath::Max(P.Vy - PHYS::Gravity * TICK, -PHYS::TerminalVelocity);   // -208 per tick, clamp -0x4000
		if (FIn != 0 || SIn != 0)                                                 // air control: 64 units per tick along the move dir
		{
			const double L = FMath::Sqrt(FIn * FIn + SIn * SIn), Ax = SIn / L, Az = -FIn / L;
			P.Vel.X += (Cy * Ax + Sy * Az) * PHYS::AirThrust * TICK;
			P.Vel.Y += (-Sy * Ax + Cy * Az) * PHYS::AirThrust * TICK;
		}
		P.Vel *= 63.0 / 64;
	}
	P.Pos.X += P.Vel.X * TICK; P.Pos.Z += P.Vel.Y * TICK;
	P.Pos.Y += P.Vy * TICK;
	if (World)
	{
		World->ClampToBounds(P.Pos);
		World->PushOut(P.Pos, RADIUS, P.Height);
	}
	else
	{
		P.Pos.X = DF::Clamp(P.Pos.X, -ADFWorld::PlayHalf, ADFWorld::PlayHalf);
		P.Pos.Z = DF::Clamp(P.Pos.Z, -ADFWorld::PlayHalf, ADFWorld::PlayHalf);
	}
	const double G = World ? World->GroundAt(P.Pos, RADIUS * 0.7) : 0.0, H = P.Pos.Y - G;
	if (P.bGround)
	{
		if (H > PHYS::StepDown) { P.bGround = false; P.Vel += GroundVel * 0.75; }   // walked off a ledge
		else P.Pos.Y = G;                                                        // drops under 0.94 m are walked
	}
	else if (H <= 0 && P.Vy <= 0)
	{
		P.Pos.Y = G; P.Vy = 0; P.bGround = true;
		if (bDbg && JumpT0 >= 0) { DFPawn->LogJump(JumpApex, DbgT - JumpT0); JumpT0 = -1; }
	}
	if (!P.bGround) JumpApex = FMath::Max(JumpApex, P.Pos.Y - JumpY0);
	if (P.JumpCd > 0) P.JumpCd--;
}
