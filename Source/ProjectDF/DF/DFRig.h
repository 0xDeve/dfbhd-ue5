// Skeletons, BAD clip playback and the two animated rigs: port of DFBHD-WebLite src/rig.js.
//
// All rig math runs in the web project's three space (meters, +Y up, forward -Z) exactly like rig.js; only the final
// pose is converted to Unreal (DFMath.h ToUE) and written into a UPoseableMeshComponent as parent-relative
// (bone-space) transforms. The imported skeletal meshes (SK_fp, SK_soldier) have bind pose = the *_RST clip, bone
// names = FDFSkeletonData::Names, so writing local = (inv(absParent) * abs, offset) reproduces rig.js skinning.
//
// PUBLIC API IS A CONTRACT used by ADFPlayerPawn: keep the public members of FDFViewModel / FDFSoldier as declared.
// Implementers may add private members and internal helper classes (e.g. FDFClip, FDFRig) freely.
#pragma once

#include "CoreMinimal.h"
#include "DFTypes.h"
#include "DFWeapons.h"

class UPoseableMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USkeletalMesh;
class FDFRig;

/** First-person weapon + arms (rig.js ViewModel), one weapon at a time. Each weapon has its own skeleton / skeletal mesh
 *  (SK_fp, SK_fp_colt, ...; bones 0..36 = the shared arms, 37+ = the weapon) and clips (DFWeapons::FWeaponSpec Fp* keys). */
class PROJECTDF_API FDFViewModel
{
public:
	FDFViewModel();
	~FDFViewModel();

	/**
	 * Component layout (created and attached by the pawn):
	 *   Camera -> Pivot (identity at the eye; SetRecoilPitch rotates it) -> RigRoot (relative location set here:
	 *   model position + kick, eye frame) -> Mesh (poseable, identity).
	 * No weapon is shown until SetWeapon. Returns false if the components or dfdata.json are missing.
	 */
	bool Init(UPoseableMeshComponent* InMesh, USceneComponent* InRigRoot, USceneComponent* InPivot);

	/** Show weapon W: sets FpMesh on the poseable mesh, re-initialises the rig on the weapon's skeleton and plays its idle
	 *  clip (no blend). FpMesh null or W None: nothing shown, returns false. */
	bool SetWeapon(EDFWeapon W, USkeletalMesh* FpMesh);
	EDFWeapon GetWeapon() const;

	/** An action of the current weapon started (FDFArsenal OnActionStart), cr15_1st.adm-style mapping:
	 *  Fire -> the fire clip from its start (if the weapon has one) and back to idle at its end (a following recoil / idle
	 *  action does not cut it); Reload -> the reload clip, back to the idle hold after ReloadEndAt seconds (the reload
	 *  delaystart; 0.2 s fade, rig.js reload(endAt)); EmptyIdle -> the emptyidle clip if any; every other action keeps
	 *  the idle hold (the original maps switchto / switchfrom / switchrank / recoil / empty to the idle clip). */
	void PlayAction(DFWeapons::EAction Action, double ReloadEndAt = 0.0);
	bool IsReloading() const;
	/** Grenades: the round left the hand (OnFire): hide the grenade nodes (bones 37+) until the fire clip has ended. */
	void HideThrownGrenade();
	/** Per shot of a gun: pushed back 0.031 m plus a random 0.0078 m offset across the barrel (0x4c9d04). */
	void Fire();
	/** Model origin in the eye frame (x forward, y left, z up) in weapon.def units (1/256 m): FDFArsenal::GetModelPosUnits
	 *  (pos, the deploy / sights / pistol blends). The kick is added here. */
	void SetModelPos(const FVector& PosUnits);
	/** Extra pitch of the weapon model over the camera, radians (+ = up), about the eye (0x4892f0 vs 0x43b6b4). */
	void SetRecoilPitch(double Rad);
	/** Advance clips and kick return; pushes the pose to the mesh. */
	void Update(double Dt);
	/** Muzzle (userpoint mflash01 of the weapon model, carried by bone 37), Unreal world space (cm); the rig root if the
	 *  weapon has no mflash01 (knife, grenades). */
	FVector GetMuzzleWorldUE() const;

private:
	TUniquePtr<FDFRig> Rig;
	UPoseableMeshComponent* Mesh = nullptr;
	USceneComponent* RigRoot = nullptr;
	USceneComponent* Pivot = nullptr;
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

/** Pose inputs for the third-person soldier (rig.js Soldier.update options). Angles in radians. */
struct FDFSoldierPose
{
	double MoveX = 0;            // strafe input, + = right
	double MoveZ = 0;            // forward input as three z: -1 = forward
	EDFStance Stance = EDFStance::Stand;
	bool bAirborne = false;
	double Lean = 0;             // + = right
	int32 Roll = 0;              // prone roll clip direction: -1 left, +1 right, 0 none
	double AimYaw = 0;           // view yaw - body yaw
	double Pitch = 0;            // view pitch, + = up
	double Recoil = 0;           // weapon recoil accumulator
	double LegYawR = 0;          // right leg yaw - body yaw
	double LegYawL = 0;          // left leg yaw - body yaw
	double BodyRoll = 0;         // soldier +0x1c
	double ViewRoll = 0;         // soldier +0x198
	int32 SpecialHold = 0;       // current weapon.def special_hold (def +0x40): 1 = one-handed arm pose (0x4846dd)
	int32 RunAnim = 0;           // current weapon.def run_anim (def +0x48): 1 = forward run uses anim_run_2 RunFastK (0x47e3b3)
	int32 ArmSwingTicks = 0;     // soldier +0x216: 40 on a knife attack (attack_anim 1), -1 per tick (0x4c99a9, 0x47d8f8)
	int32 FireKickTicks = 0;     // soldier +0x215: 8 on a throw (attack_anim 2), 80 at a reload start, -1 per tick (0x4c99cd, 0x4c9e2f)
};

/** Third-person Delta soldier (rig.js Soldier). Skeleton "soldier", clips sd_*. */
class PROJECTDF_API FDFSoldier
{
public:
	FDFSoldier();
	~FDFSoldier();

	/**
	 * Mesh = SK_soldier poseable mesh attached to the pawn's soldier root (the root's world transform = feet position,
	 * yaw = body yaw, set by the pawn). Init sets the mesh's relative location to the rest hip height (rig.js hips group)
	 * and attaches Gun (SM_tp_gun) to the right-hand bone with the precomputed FDFData::GetTpGunAttach transform.
	 */
	bool Init(UPoseableMeshComponent* InMesh, UStaticMeshComponent* InGun);

	/** Third-person weapon: Gun shows TpMesh attached to the right-hand bone with dfdata tpAttach[spec.TpAttach] (the CAR-15
	 *  keeps tpGunAttach); muzzle = the model's mflash01. TpMesh null or W None: the gun component is emptied. */
	void SetWeapon(EDFWeapon W, UStaticMesh* TpMesh);

	/** Rest eye height (restRoot t4), meters. */
	double EyeHeight() const;
	/** Eye height above the feet in the current blended animation (root key t4). */
	double EyeNow() const;
	/** Hip height above the feet in the current blended animation (root key t3). */
	double HipNow() const;
	/** Ground velocity the current animation drives, m/s in the body frame (three: +x right, -z forward):
	 *  0.5 * (t0, t2) m per 62 Hz tick (DFBHD.EXE 0x413f30). */
	void RootVelocity(double& OutX, double& OutZ) const;
	/** Clip selection + cross-fade + per-bone aim (0x484150); pushes the pose to the mesh. */
	void Update(double Dt, const FDFSoldierPose& Pose);
	/** Muzzle of the third-person weapon (its model's userpoint mflash01, else the model origin), Unreal world space (cm). */
	FVector GetMuzzleWorldUE() const;

private:
	TUniquePtr<FDFRig> Rig;
	UPoseableMeshComponent* Mesh = nullptr;
	UStaticMeshComponent* Gun = nullptr;
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
