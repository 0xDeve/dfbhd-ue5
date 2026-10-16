// Movement component of the DF player: runs the port of main.js updatePlayer (movement half) / simTick as the custom movement
// mode DFMOVE_Sim of a stock UCharacterMovementComponent, so the original numerics stay untouched while the character gets
// the engine's client prediction / server authority / correction networking.
// The simulation runs in the web project's three space (DFMath.h); only the capsule (the updated component) is in Unreal space.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "DFTypes.h"
#include "DFRig.h"
#include "DFCharacterMovementComponent.generated.h"

class ADFPlayerPawn;
class ADFWorld;

/** Custom movement mode of UDFCharacterMovementComponent: the whole DF simulation. */
enum EDFMoveMode : uint8
{
	DFMOVE_Sim = 1,
};

/** The DF input of one move: movement keys, lean, prone roll, jump, the stance the move starts in and the weapon in hand. 14 bits on the wire. */
struct FDFMoveInput
{
	int8 FIn = 0;       // W - S: -1, 0, 1
	int8 SIn = 0;       // D - A
	int8 LeanIn = 0;    // E - Q (zeroed while prone)
	int8 RollIn = 0;    // E - Q, prone roll request
	bool bJump = false;
	EDFStance Stance = EDFStance::Stand;
	uint8 Weapon = 0;   // EDFWeapon in the player's hand (3 bits): the soldier rig's run_anim / special_hold

	uint16 Pack() const;
	void Unpack(uint16 Bits);
	bool operator==(const FDFMoveInput& O) const
	{
		return FIn == O.FIn && SIn == O.SIn && LeanIn == O.LeanIn && RollIn == O.RollIn && bJump == O.bJump && Stance == O.Stance && Weapon == O.Weapon;
	}
};

/** main.js state P, movement half (Yaw is the view yaw the move uses; the pawn's mouse look owns it locally). Three space, meters / radians. */
struct FDFSimState
{
	FVector Pos = FVector::ZeroVector;        // feet; the capsule mirrors it
	double Vy = 0;
	FVector2D Vel = FVector2D::ZeroVector;    // (x, z)
	double Yaw = 0;
	bool bGround = true;
	double Eye = 0, Hip = 0, Height = 0;
	double BodyYaw = 0;
	double Lean = 0;
	int32 Roll = 0;                           // roll clip direction: -1, 0, +1
	double RollT = 0;
	double SimAcc = 0;
	int32 JumpCd = 0;
	double LegR = 0, LegL = 0, LegTgtR = 0, LegTgtL = 0;
	int32 Tick = 0;
	double BodyRoll = 0, ViewRoll = 0;
};

/** The part of the DF state the client cannot rebuild from location / velocity; the server sends it with a correction. */
struct FDFCorrectionState
{
	double Vy = 0;
	FVector2D Vel = FVector2D::ZeroVector;
	bool bGround = true;
	int32 JumpCd = 0;
	int32 Roll = 0;
	double RollT = 0;
	double SimAcc = 0;
	EDFStance Stance = EDFStance::Stand;
	double Height = 0;
};

// ------------------------------------------------------------------ network structs (client -> server move, saved move, server -> client correction)

/** The client's move RPC data, extended with the DF input. */
struct FDFNetworkMoveData : public FCharacterNetworkMoveData
{
	typedef FCharacterNetworkMoveData Super;
	FDFMoveInput Input;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FDFNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FDFNetworkMoveDataContainer()
	{
		NewMoveData = &MoveData[0];
		PendingMoveData = &MoveData[1];
		OldMoveData = &MoveData[2];
	}

private:
	FDFNetworkMoveData MoveData[3];
};

/** Server correction: the stock adjustment plus the DF state (only sent for corrections, not for good-move acks). */
struct FDFMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	typedef FCharacterMoveResponseDataContainer Super;
	FDFCorrectionState State;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
};

class FDFSavedMove : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;
	FDFMoveInput Input;

	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
	/** Never combined: a combined move reverts the character to the start of the older move, which would need the soldier rig reverted too. */
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override { return false; }
};

class FDFNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;
	FDFNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}
	virtual FSavedMovePtr AllocateNewMove() override;
};

// ------------------------------------------------------------------ the component

UCLASS()
class PROJECTDF_API UDFCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UDFCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	// ---- engine hooks
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void SetDefaultMovementMode() override;
	virtual void SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void SimulateMovement(float DeltaTime) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	/** The server does not simulate a remote client on its own while moves are late: the DF sim also advances the soldier rig, which the client's saved moves would not match. */
	virtual bool ForcePositionUpdate(float DeltaTime) override { return false; }
	using UCharacterMovementComponent::ServerMoveHandleClientError;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	// ---- simulation state (main.js state P, movement half) and the stance
	FDFSimState P;
	EDFStance Stance = EDFStance::Stand;
	FVector2D GroundVel = FVector2D::ZeroVector;   // gWorld
	double Speed01 = 0;
	FDFSoldierPose Pose;                           // movement fields (the pawn fills in the aim fields)

	// render interpolation between the previous and the current sim tick
	FVector PrevPos = FVector::ZeroVector;
	double PrevLean = 0, PrevBodyYaw = 0;
	FVector RenderPos = FVector::ZeroVector;
	double RenderLean = 0, RenderBodyYaw = 0;

	/** The DF input of the move being built / simulated (set by the pawn every frame, by the server from the move RPC). */
	void SetMoveInput(const FDFMoveInput& In) { Input = In; }
	const FDFMoveInput& GetMoveInput() const { return Input; }
	/** Control yaw (UE degrees) of the saved move being replayed; the stock PrepMoveFor does not restore the control rotation. */
	void SetReplayYaw(double YawDeg) { ReplayYawDeg = YawDeg; }
	/** Time the pawn's test harness clock shows (jump log only). */
	double DbgT = 0.0;
	/** Sum of the simulated move times (server-side debug clock). */
	double SimTime = 0.0;

	/** Boot / harness: the pawn's initial state (feet position and view yaw); legs and body face the view. */
	void InitSim(const FVector& FeetPos, double YawRad);
	/** After the pawn changed P directly (harness position / yaw / lean): refresh interpolation state and move the capsule. */
	void ResetRender();
	/** Capsule center = ToUE(P.Pos) + half height. */
	void ApplySimToCapsule();

	/** One DF move of Dt seconds (what PhysCustom does): sim, render interpolation, capsule, Velocity, then the pawn places and animates the soldier. */
	void StepDF(double Dt);

	/** Eye position of the lean pivot (DFBHD.EXE 0x47e7ec) for a given feet position and lean. */
	FVector LeanedHead(const FVector& Pos, double Lean) const;

	/** True once since the last call if the sim started a jump (the pawn then drops its pending harness jump). */
	bool ConsumeJumped() { const bool b = bJumped; bJumped = false; return b; }

	void SetDFWorld(ADFWorld* InWorld) { DFWorld = InWorld; }

	/** Capsule half height, cm. */
	float GetCapsuleHalfHeight() const;

	// ---- network
	FDFCorrectionState CaptureState() const;
	void ApplyCorrectionState(const FDFCorrectionState& State);
	const FDFCorrectionState& GetPendingCorrectionState() const { return PendingCorrectionState; }
	int32 NumCorrections = 0;

protected:
	using UCharacterMovementComponent::OnClientCorrectionReceived;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

private:
	double TurnLeg(double Leg, double Target) const;
	void UpdatePlayer(double Dt);
	void SimTickDF(double FIn, double SIn, double LeanIn);
	/** The view yaw this move uses (see StepDF). */
	double ResolveYaw() const;
	void SyncFromCapsule();

	UPROPERTY(Transient) TObjectPtr<ADFWorld> DFWorld;
	ADFPlayerPawn* DFPawn = nullptr;
	FDFMoveInput Input;
	double ReplayYawDeg = 0.0;
	FVector LastCapsuleLoc = FVector::ZeroVector;   // where the sim last put the capsule (anything else moved it: correction, teleport)
	bool bJumped = false;
	double JumpT0 = -1.0, JumpY0 = 0, JumpApex = 0;   // jump log (-DFDbg)
	FDFCorrectionState PendingCorrectionState;

	FDFNetworkMoveDataContainer DFMoveDataContainer;
	FDFMoveResponseDataContainer DFResponseContainer;
};
