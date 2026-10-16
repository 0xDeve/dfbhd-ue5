// Port of DFBHD-WebLite src/rig.js: BAD clip playback, cross-fading rig, first-person viewmodel and third-person soldier.
#include "DFRig.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "DFData.h"
#include "DFGameData.h"
#include "DFMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFRig, Log, All);

namespace
{
	/** JS `%` (sign of the dividend). */
	FORCEINLINE double JsMod(double A, double B) { return FMath::Fmod(A, B); }

	constexpr int32 FpGunBone = 37;
}

// ====================================================================== rig

/** rig.js Rig: skeleton + clip mixer with one fading-out layer, pushing the pose into a poseable mesh. */
class FDFRig
{
public:
	struct FLayer
	{
		const FDFClipData* Clip = nullptr;
		double T = 0.0;
		bool bLoop = true;
		double Speed = 1.0;
	};

	bool Init(const FDFSkeletonData* InSkel, UPoseableMeshComponent* InMesh)
	{
		Skel = InSkel;
		Mesh = InMesh;
		if (!Skel || !Mesh || Skel->Num() == 0 || Skel->RestQuats.Num() != Skel->Num() || Skel->RestOffsets.Num() != Skel->Num())
		{
			return false;
		}
		if (!Mesh->GetSkinnedAsset())
		{
			UE_LOG(LogDFRig, Error, TEXT("Poseable mesh '%s' has no skinned asset"), *GetNameSafe(Mesh));
			return false;
		}
		N = Skel->Num();
		RestAbs = Skel->RestQuats;
		Abs = RestAbs;
		AbsB = RestAbs;
		Cur = FLayer();
		Prev = FLayer();
		FadeT = 0.0; FadeDur = 0.0;
		HideFrom = INDEX_NONE;
		for (int32 j = 0; j < 6; ++j) { RestRoot[j] = Skel->RestRoot[j]; Root[j] = RestRoot[j]; RootB[j] = 0.0; }
		LocalRot.SetNum(N);
		LocalPos.SetNum(N);
		ModelPos.SetNum(N);

		// our bone i -> mesh bone index
		MeshMap.SetNum(N);
		TArray<FString> Missing;
		for (int32 i = 0; i < N; ++i)
		{
			MeshMap[i] = Mesh->GetBoneIndex(Skel->Names[i]);
			if (MeshMap[i] == INDEX_NONE) Missing.Add(Skel->Names[i].ToString());
		}
		if (Missing.Num() > 0)
		{
			UE_LOG(LogDFRig, Error, TEXT("Mesh '%s' is missing %d rig bones: %s"), *GetNameSafe(Mesh->GetSkinnedAsset()),
				Missing.Num(), *FString::Join(Missing, TEXT(", ")));
		}

		// Sanity: FK of the rest pose must reproduce the pivots.
		if (Skel->Pivots.Num() == N)
		{
			double MaxErr = 0.0;
			TArray<FVector> P;
			P.SetNum(N);
			P[0] = Skel->Pivots[0];
			for (int32 i = 1; i < N; ++i)
			{
				const int32 Pa = Skel->Parents[i];
				P[i] = P[Pa] + RestAbs[Pa].RotateVector(Skel->RestOffsets[i]);
				MaxErr = FMath::Max(MaxErr, (P[i] - Skel->Pivots[i]).Size());
			}
			UE_LOG(LogDFRig, Log, TEXT("Skeleton '%s': %d bones, FK(rest) vs pivots max error %.6f m"),
				*GetNameSafe(Mesh->GetSkinnedAsset()), N, MaxErr);
			if (MaxErr > 1e-3) UE_LOG(LogDFRig, Warning, TEXT("Rest-pose FK does not reproduce pivots (%.6f m)"), MaxErr);
		}

		ApplyRest();
		return true;
	}

	int32 Num() const { return N; }
	double RootAt(int32 J) const { return Root[J]; }
	double RestRootAt(int32 J) const { return RestRoot[J]; }
	const FQuat& AbsAt(int32 I) const { return Abs[I]; }
	const FQuat& RestAbsAt(int32 I) const { return RestAbs[I]; }
	const FVector& PosAt(int32 I) const { return ModelPos[I]; }
	const FVector& PivotAt(int32 I) const { return Skel->Pivots[I]; }
	bool HasPivots() const { return Skel && Skel->Pivots.Num() == N; }
	const FDFClipData* CurClip() const { return Cur.Clip; }
	/** A non-looping current clip has played to its last key. */
	bool CurFinished() const { return Cur.Clip && !Cur.bLoop && Cur.T >= Cur.Clip->Duration; }
	/** Bones >= FirstBone are drawn with ~zero scale (INDEX_NONE = all visible). */
	void SetHideFrom(int32 FirstBone) { HideFrom = FirstBone; }
	TArray<FQuat>& AbsRef() { return Abs; }
	const FDFSkeletonData* GetSkel() const { return Skel; }
	bool bUseRootMotion = false;

	void ApplyRest()
	{
		Abs = RestAbs;
		for (int32 j = 0; j < 6; ++j) Root[j] = RestRoot[j];
		Apply(Skel->RestOffsets);
		Push();
	}

	void Play(const FDFClipData* Clip, bool bLoop, double Fade, double Speed, bool bRestart)
	{
		if (!Clip) return;
		if (Cur.Clip == Clip && !bRestart) { Cur.bLoop = bLoop; Cur.Speed = Speed; return; }
		Prev = Cur;
		Cur = FLayer();
		Cur.Clip = Clip; Cur.T = 0.0; Cur.bLoop = bLoop; Cur.Speed = Speed;
		FadeT = 0.0;
		FadeDur = Prev.Clip ? Fade : 0.0;
	}

	/** Advance both layers, sample and cross-fade into Abs/Root (rig.js update minus _apply). */
	void Advance(double Dt)
	{
		if (!Cur.Clip) return;
		Cur.T += Dt * Cur.Speed;
		if (Prev.Clip) Prev.T += Dt * Prev.Speed;
		SampleLayer(Cur, Abs, Root);
		if (Prev.Clip && FadeDur > 0.0)
		{
			FadeT += Dt;
			const double W = FMath::Min(1.0, FadeT / FadeDur);
			if (W < 1.0)
			{
				SampleLayer(Prev, AbsB, RootB);
				for (int32 i = 0; i < N; ++i) Abs[i] = DF::Slerp(AbsB[i], Abs[i], W);
				for (int32 j = 0; j < 6; ++j) Root[j] = RootB[j] * (1.0 - W) + Root[j] * W;
			}
			else
			{
				Prev = FLayer();
			}
		}
	}

	/** rig.js _apply: Abs -> parent-relative rotations / positions + model-space joint positions. */
	void Apply(const TArray<FVector>& ClipOffsets)
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (i == 0)
			{
				LocalRot[0] = Abs[0];
				LocalPos[0] = bUseRootMotion ? FVector(0.0, Root[3] - RestRoot[3], 0.0) : FVector::ZeroVector;
				ModelPos[0] = (HasPivots() ? Skel->Pivots[0] : FVector::ZeroVector) + LocalPos[0];
			}
			else
			{
				const int32 Pa = Skel->Parents[i];
				LocalRot[i] = Abs[Pa].Inverse() * Abs[i];
				LocalPos[i] = i < ClipOffsets.Num() ? ClipOffsets[i] : Skel->RestOffsets[i];
				ModelPos[i] = ModelPos[Pa] + Abs[Pa].RotateVector(LocalPos[i]);
			}
		}
	}

	/** Write the local transforms into the poseable mesh and refresh so attached children follow. */
	void Push()
	{
		if (!Mesh || !Mesh->GetSkinnedAsset()) return;
		const int32 NumMeshBones = Mesh->GetNumBones();
		if (Mesh->BoneSpaceTransforms.Num() != NumMeshBones)
		{
			Mesh->AllocateTransformData();
			if (Mesh->BoneSpaceTransforms.Num() != NumMeshBones) return;
		}
		for (int32 i = 0; i < N; ++i)
		{
			const int32 M = MeshMap[i];
			if (M == INDEX_NONE || M >= Mesh->BoneSpaceTransforms.Num()) continue;
			// the root's translation also carries the bind pivot of bone 0 (ModelPos[0])
			const FVector& Pos = (i == 0) ? ModelPos[0] : LocalPos[i];
			Mesh->BoneSpaceTransforms[M] = FTransform(DF::ToUE(LocalRot[i]), DF::ToUE(Pos),
				(HideFrom != INDEX_NONE && i >= HideFrom) ? FVector(1e-4) : FVector::OneVector);
		}
		Mesh->RefreshBoneTransforms();
	}

private:
	void SampleClip(const FDFClipData& C, double T, bool bLoop, TArray<FQuat>& Out, double* OutRoot) const
	{
		double F = T * C.Fps;
		const int32 Last = C.Keys - 1;
		if (bLoop && Last > 0) F = JsMod(JsMod(F, Last) + Last, Last);
		else F = FMath::Clamp(F, 0.0, (double)Last);
		const int32 K0 = (int32)FMath::FloorToDouble(F);
		const int32 K1 = FMath::Min(K0 + 1, Last);
		const double A = F - K0;
		const int32 Tracks = C.Tracks;
		const int32 Nb = FMath::Min(Tracks, N);
		const float* Q = C.Quats.GetData();
		if (C.Quats.Num() >= (K1 * Tracks + Tracks) * 4 && K0 >= 0)
		{
			for (int32 B = 0; B < Nb; ++B)
			{
				const float* Q0 = Q + (K0 * Tracks + B) * 4;
				const float* Q1 = Q + (K1 * Tracks + B) * 4;
				Out[B] = DF::Slerp(FQuat(Q0[0], Q0[1], Q0[2], Q0[3]), FQuat(Q1[0], Q1[1], Q1[2], Q1[3]), A);
			}
		}
		const int32 RK = FMath::Max(1, C.RootKeys);
		const int32 R0 = FMath::Min(K0, RK - 1) * 6, R1 = FMath::Min(K1, RK - 1) * 6;
		if (C.Root.Num() >= R0 + 6 && C.Root.Num() >= R1 + 6)
		{
			for (int32 j = 0; j < 6; ++j) OutRoot[j] = C.Root[R0 + j] * (1.0 - A) + C.Root[R1 + j] * A;
		}
	}

	void SampleLayer(const FLayer& L, TArray<FQuat>& OutAbs, double* OutRoot) const
	{
		const FDFClipData& C = *L.Clip;
		SampleClip(C, L.T, L.bLoop, OutAbs, OutRoot);
		// Untracked nodes keep their rest orientation relative to the parent.
		for (int32 i = C.Tracks; i < N; ++i)
		{
			const int32 Pa = Skel->Parents[i];
			OutAbs[i] = OutAbs[Pa] * (RestAbs[Pa].Inverse() * RestAbs[i]);
		}
	}

	const FDFSkeletonData* Skel = nullptr;
	UPoseableMeshComponent* Mesh = nullptr;
	int32 N = 0;
	TArray<FQuat> RestAbs, Abs, AbsB, LocalRot;
	TArray<FVector> LocalPos, ModelPos;
	TArray<int32> MeshMap;
	double RestRoot[6] = {}, Root[6] = {}, RootB[6] = {};
	FLayer Cur, Prev;
	double FadeT = 0.0, FadeDur = 0.0;
	int32 HideFrom = INDEX_NONE;
};

// ====================================================================== viewmodel

struct FDFViewModel::FImpl
{
	bool bOk = false;                 // a weapon is shown
	EDFWeapon Weapon = EDFWeapon::None;
	const DFWeapons::FWeaponSpec* Spec = nullptr;
	const FDFClipData* Idle = nullptr;
	const FDFClipData* Fire = nullptr;
	const FDFClipData* Reload = nullptr;
	const FDFClipData* Empty = nullptr;
	const FDFClipData* Hold = nullptr;   // clip played when fire / reload end (idle, or emptyidle after EmptyIdle)
	bool bReloading = false;
	bool bFiring = false;
	bool bGrenadeHidden = false;
	bool bHasMuzzle = false;
	double ReloadT = 0.0, ReloadEnd = 0.0;
	FVector MuzzleInBone = FVector::ZeroVector;   // muzzle in bone 37's rest frame
	FVector PosCam = FVector::ZeroVector;         // model origin, three camera space (m)
	FVector Kick = FVector::ZeroVector;
};

FDFViewModel::FDFViewModel() : Impl(MakeUnique<FImpl>()) {}
FDFViewModel::~FDFViewModel() = default;

bool FDFViewModel::Init(UPoseableMeshComponent* InMesh, USceneComponent* InRigRoot, USceneComponent* InPivot)
{
	Impl = MakeUnique<FImpl>();
	Rig.Reset();
	Mesh = InMesh; RigRoot = InRigRoot; Pivot = InPivot;
	const FDFData& Data = FDFData::Get();
	if (!Data.IsLoaded())
	{
		UE_LOG(LogDFRig, Error, TEXT("ViewModel: dfdata.json not loaded: %s"), *Data.GetError());
		return false;
	}
	if (!Mesh || !RigRoot)
	{
		UE_LOG(LogDFRig, Error, TEXT("ViewModel: missing mesh / rig root"));
		return false;
	}
	return true;
}

bool FDFViewModel::SetWeapon(EDFWeapon W, USkeletalMesh* FpMesh)
{
	FImpl& S = *Impl;
	S.bOk = false;
	S.Weapon = EDFWeapon::None;
	S.Spec = nullptr;
	Rig.Reset();
	if (!Mesh) return false;
	const DFWeapons::FWeaponSpec* Spec = DFWeapons::Find(W);
	const FDFData& Data = FDFData::Get();
	if (!Spec || !FpMesh || !Data.IsLoaded() || !Spec->FpSkeleton)
	{
		Mesh->SetSkinnedAssetAndUpdate(nullptr);
		return false;
	}
	const FDFSkeletonData* Skel = Data.FindSkeleton(FName(Spec->FpSkeleton));
	auto Clip = [&](const TCHAR* Key) -> const FDFClipData* { return Key ? Data.FindClip(FName(Key)) : nullptr; };
	S.Idle = Clip(Spec->FpIdleClip);
	S.Fire = Clip(Spec->FpFireClip);
	S.Reload = Clip(Spec->FpReloadClip);
	S.Empty = Clip(Spec->FpEmptyIdleClip);
	if (!Skel || !S.Idle)
	{
		UE_LOG(LogDFRig, Error, TEXT("ViewModel: weapon %s: missing skeleton '%s' or idle clip '%s'"), Spec->DefName,
			Spec->FpSkeleton, Spec->FpIdleClip ? Spec->FpIdleClip : TEXT("(none)"));
		Mesh->SetSkinnedAssetAndUpdate(nullptr);
		return false;
	}
	Mesh->SetSkinnedAssetAndUpdate(FpMesh);
	Mesh->AllocateTransformData();
	Rig = MakeUnique<FDFRig>();
	if (!Rig->Init(Skel, Mesh))
	{
		Rig.Reset();
		UE_LOG(LogDFRig, Error, TEXT("ViewModel: rig init failed for %s"), Spec->DefName);
		return false;
	}
	Rig->Play(S.Idle, true, 0.0, 1.0, false);

	// muzzle in bone 37's rest frame (guns only)
	S.bHasMuzzle = false;
	if (Spec->FpModel && Skel->Num() > FpGunBone)
	{
		if (const FDFModelData* Model = Data.FindModel(FName(Spec->FpModel)))
		{
			if (const FVector* Mf = Model->FindUserpoint(TEXT("mflash01")))
			{
				S.MuzzleInBone = Skel->RestQuats[FpGunBone].Inverse().RotateVector(
					*Mf - (Skel->Pivots.Num() == Skel->Num() ? Skel->Pivots[FpGunBone] : FVector::ZeroVector));
				S.bHasMuzzle = true;
			}
		}
	}
	S.Weapon = W;
	S.Spec = Spec;
	S.Hold = S.Idle;
	S.bReloading = false;
	S.bFiring = false;
	S.bGrenadeHidden = false;
	S.Kick = FVector::ZeroVector;
	S.bOk = true;
	Mesh->SetVisibility(true);
	Update(0.0);
	return true;
}

EDFWeapon FDFViewModel::GetWeapon() const { return Impl->Weapon; }

bool FDFViewModel::IsReloading() const { return Impl->bReloading; }

void FDFViewModel::HideThrownGrenade()
{
	if (!Impl->bOk || !Rig) return;
	Impl->bGrenadeHidden = true;
	Rig->SetHideFrom(FpGunBone);
}

void FDFViewModel::PlayAction(DFWeapons::EAction Action, double EndAt)
{
	if (!Impl->bOk || !Rig) return;
	FImpl& S = *Impl;
	using EA = DFWeapons::EAction;
	switch (Action)
	{
	case EA::Fire:
		if (S.Fire)
		{
			S.bFiring = true;
			S.bReloading = false;
			Rig->Play(S.Fire, false, 0.02, 1.0, true);
		}
		break;
	case EA::Reload:
		if (S.Reload && !S.bReloading)
		{
			S.bReloading = true;
			S.bFiring = false;
			S.ReloadT = 0.0;
			S.ReloadEnd = EndAt > 0.0 ? FMath::Min(EndAt, S.Reload->Duration) : S.Reload->Duration;
			Rig->Play(S.Reload, false, 0.12, 1.0, true);
		}
		break;
	case EA::EmptyIdle:
		if (S.Empty)
		{
			S.Hold = S.Empty;
			if (!S.bFiring && !S.bReloading) Rig->Play(S.Empty, true, 0.1, 1.0, false);
		}
		break;
	case EA::Idle:
	case EA::SwitchTo:
	case EA::SwitchFrom:
	case EA::SwitchRank:
		S.Hold = S.Idle;
		if (!S.bFiring && !S.bReloading) Rig->Play(S.Idle, true, 0.1, 1.0, false);
		break;
	default:   // Recoil, Empty: keep the current hold (a running fire clip is not cut)
		break;
	}
}

void FDFViewModel::Fire()
{
	if (!Impl->bOk) return;
	const double A = DF::Rand() * DF::PI_D * 2.0;
	Impl->Kick = FVector(FMath::Cos(A) * DFData::CAR15::KickJitter, FMath::Sin(A) * DFData::CAR15::KickJitter,
		DFData::CAR15::KickBack);
}

void FDFViewModel::SetModelPos(const FVector& PosUnits)
{
	// weapon.def units (1/256 m), eye frame (x forward, y left, z up) -> three camera space
	Impl->PosCam = FVector(-PosUnits.Y / 256.0, PosUnits.Z / 256.0, -PosUnits.X / 256.0);
}

void FDFViewModel::SetRecoilPitch(double Rad)
{
	if (!Pivot) return;
	Pivot->SetRelativeRotation(DF::ToUE(FQuat(FVector(1, 0, 0), Rad)));
}

void FDFViewModel::Update(double Dt)
{
	if (!Impl->bOk || !Rig) return;
	FImpl& S = *Impl;
	if (S.bReloading)
	{
		S.ReloadT += Dt;
		if (S.ReloadT >= S.ReloadEnd)
		{
			S.bReloading = false;
			Rig->Play(S.Hold, true, 0.2, 1.0, false);
		}
	}
	Rig->Advance(Dt);
	if (S.bFiring && Rig->CurFinished())
	{
		S.bFiring = false;
		if (S.bGrenadeHidden)
		{
			S.bGrenadeHidden = false;
			Rig->SetHideFrom(INDEX_NONE);
		}
		Rig->Play(S.Hold, true, 0.1, 1.0, false);
	}
	if (Rig->CurClip()) Rig->Apply(Rig->CurClip()->Offsets);
	Rig->Push();

	// kick returns 0.002 m per tick per axis (0x487829)
	const double Ret = DFData::CAR15::KickReturn * DFData::SimHz * Dt;
	for (int32 Ax = 0; Ax < 3; ++Ax)
	{
		const double V = S.Kick[Ax];
		const double Sign = V > 0.0 ? 1.0 : (V < 0.0 ? -1.0 : 0.0);
		S.Kick[Ax] = Sign * FMath::Max(0.0, FMath::Abs(V) - Ret);
	}
	if (RigRoot) RigRoot->SetRelativeLocation(DF::ToUE(S.PosCam + S.Kick));
}

FVector FDFViewModel::GetMuzzleWorldUE() const
{
	if (!Impl->bOk || !Rig.IsValid() || !Mesh) return RigRoot ? RigRoot->GetComponentLocation() : FVector::ZeroVector;
	if (!Impl->bHasMuzzle) return RigRoot ? RigRoot->GetComponentLocation() : Mesh->GetComponentLocation();
	const FVector Current = Rig->PosAt(FpGunBone) + Rig->AbsAt(FpGunBone).RotateVector(Impl->MuzzleInBone);
	return Mesh->GetComponentTransform().TransformPosition(DF::ToUE(Current));
}

// ====================================================================== soldier

namespace
{
	// G_ARMS = matrix [ebp-0x108] (bones 5, 9, 16: upper arms, right hand; carries the knife swing / fire kick),
	// G_ARMS2 = matrix [ebp-0x188] (bones 6, 10, 15; = G_ARMS except for special_hold weapons).
	enum EAimGroup { G_HIPS = 0, G_LOWER, G_UPPER, G_ARMS, G_HEAD, G_LEG_R, G_LEG_L, G_ARMS2, G_COUNT };
	// Bone -> aim group, DFBHD.EXE jump table 0x484dcc
	const int32 BoneGroup[20] = { G_HIPS, G_LOWER, G_UPPER, G_UPPER, G_UPPER, G_ARMS, G_ARMS2, G_LEG_R, G_LEG_L, G_ARMS,
		G_ARMS2, G_LEG_R, G_LEG_L, G_UPPER, G_HEAD, G_ARMS2, G_ARMS, G_LEG_R, G_LEG_L, G_HEAD };
	constexpr double Bam = 2.0 * 3.14159265358979323846 / 4294967296.0;   // binary angle unit -> rad
	const TCHAR* const DirNames[8] = { TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL") };
}

struct FDFSoldier::FImpl
{
	bool bOk = false;
	const FDFClipData* Idle = nullptr;
	const FDFClipData* Kneel = nullptr;
	const FDFClipData* Prone = nullptr;
	const FDFClipData* JumpStart = nullptr;
	const FDFClipData* JumpLoop = nullptr;
	const FDFClipData* RollL = nullptr;
	const FDFClipData* RollR = nullptr;
	const FDFClipData* RunFastK = nullptr;   // anim_run_2 (run_anim 1)
	const FDFClipData* RunFastW = nullptr;   // anim_run_3 (run_anim >= 2)
	const FDFClipData* Run[8] = {};
	const FDFClipData* Crouch[8] = {};
	const FDFClipData* Crawl[8] = {};
	double AirTime = 0.0;
	bool bRolling = false;
	double EyeHeight = 1.7;
	FVector MuzzleLocal = FVector::ZeroVector;
	const FDFSkeletonData* Skel = nullptr;
	FQuat G[G_COUNT];
	// +0x200 fire kick angle (rad) and its 62 Hz stepping (0x47d91c)
	double TickAccum = 0.0;
	double KickAngle = 0.0;
	int32 LastKickTicks = 0;
	int32 PendingKicks = 0;

	/** Per-bone aim (matrix builder 0x484150). Swing = soldier +0x216 (knife), Kick = +0x200 (rad, <= 0), Special = weapon def +0x40. */
	void Aim(FDFRig& Rig, bool bProne, double D, double P, double K, double L, double LegR, double LegL, double R, double V,
		int32 Special, int32 Swing, double Kick)
	{
		using namespace DFData::AIM;
		auto Rot = [&](const double (&Row)[4], double Roll)
		{
			return DF::EulerYXZ(Row[1] * P + Row[2] * K, Row[0] * D, -(Row[3] * L + Roll));
		};
		G[G_HIPS] = FQuat(FVector(0, 0, 1), -R);
		G[G_ARMS] = Rot(UpperBody, R);
		G[G_HEAD] = Rot(Head, V);
		if (bProne) { G[G_LOWER] = G[G_HIPS]; G[G_UPPER] = G[G_HIPS]; }
		else { G[G_LOWER] = Rot(LowerSpine, R); G[G_UPPER] = G[G_ARMS]; }
		G[G_LEG_R] = DF::EulerYXZ(0.0, LegR, -R);
		G[G_LEG_L] = DF::EulerYXZ(0.0, LegL, -R);

		// Arm swing / fire kick (0x484555..0x484650): esi = kick angle (+0x200) minus, while the swing timer runs, the
		// |15 deg * sin(3x)| lunge with x = swing * pi/40 (constants 0x611840, 0x611430, 0x611838); a negative sum is doubled;
		// yaw offset = -20 deg * sin(x) (0x611830). If |esi| > 0x100 the arm matrix [ebp-0x108] becomes
		// yaw 3d/4 + yawOff, pitch P/2 + K + esi, roll lean + R (bones 5, 9, 16, and 6, 10, 15 unless special_hold).
		double Esi = Kick, YawOff = 0.0;
		if (Swing > 0)
		{
			const double X = Swing * (3.14159265358979323846 / 40.0);
			const double Lunge = FMath::Abs(double(int64(178956960.0 * FMath::Sin(3.0 * X)))) * Bam;
			Esi -= Lunge;
			YawOff = double(int64(-238609280.0 * FMath::Sin(X))) * Bam;
			if (YawOff > 0.0) YawOff *= 0.5;
			if (Esi < 0.0) Esi *= 2.0;
		}
		G[G_ARMS2] = G[G_ARMS];
		if (FMath::Abs(Esi) > 0x100 * Bam)
		{
			G[G_ARMS] = DF::EulerYXZ(0.5 * P + K + Esi, UpperBody[0] * D + YawOff, -(L + R));
			G[G_ARMS2] = G[G_ARMS];
		}
		// special_hold == 1 (0x4846dd): bones 6, 10, 15 take roll lean + R + 21.99 deg, pitch P/2 + K - 21.99 deg, yaw 3d/4.
		if (Special == 1)
		{
			constexpr double Hold = 0x0FA4FA40 * Bam;
			G[G_ARMS2] = DF::EulerYXZ(0.5 * P + K - Hold, UpperBody[0] * D, -(L + R + Hold));
		}

		TArray<FQuat>& Abs = Rig.AbsRef();
		for (int32 i = 0; i < Rig.Num(); ++i)
		{
			const int32 Grp = i < 20 ? BoneGroup[i] : (int32)G_HIPS;
			Abs[i] = G[Grp] * Abs[i];
		}
		if (Rig.CurClip()) Rig.Apply(Rig.CurClip()->Offsets);
	}
};

FDFSoldier::FDFSoldier() : Impl(MakeUnique<FImpl>()) {}
FDFSoldier::~FDFSoldier() = default;

bool FDFSoldier::Init(UPoseableMeshComponent* InMesh, UStaticMeshComponent* InGun)
{
	Impl->bOk = false;
	Mesh = InMesh; Gun = InGun;
	FImpl& S = *Impl;
	const FDFData& Data = FDFData::Get();
	if (!Data.IsLoaded())
	{
		UE_LOG(LogDFRig, Error, TEXT("Soldier: dfdata.json not loaded: %s"), *Data.GetError());
		return false;
	}
	const FDFSkeletonData* Skel = Data.FindSkeleton(TEXT("soldier"));
	if (!Mesh || !Skel)
	{
		UE_LOG(LogDFRig, Error, TEXT("Soldier: missing mesh or skeleton 'soldier'"));
		return false;
	}

	bool bAll = true;
	auto C = [&](const FString& Key) -> const FDFClipData*
	{
		const FDFClipData* Clip = Data.FindClip(FName(*Key));
		if (!Clip) { UE_LOG(LogDFRig, Error, TEXT("Soldier: clip '%s' missing"), *Key); bAll = false; }
		return Clip;
	};
	S.Idle = C(TEXT("sd_idle")); S.Kneel = C(TEXT("sd_kneel")); S.Prone = C(TEXT("sd_prone"));
	S.JumpStart = C(TEXT("sd_jumpS")); S.JumpLoop = C(TEXT("sd_jumpL"));
	S.RollL = C(TEXT("sd_rollL")); S.RollR = C(TEXT("sd_rollR"));
	for (int32 d = 0; d < 8; ++d)
	{
		S.Run[d] = C(FString(TEXT("sd_run")) + DirNames[d]);
		S.Crouch[d] = C(FString(TEXT("sd_cr")) + DirNames[d]);
	}
	// prone crawl: forward/back clips, diagonals and sides use the side crawls (DELTA01.ADM)
	const TCHAR* const CrawlKey[8] = { TEXT("F"), TEXT("R"), TEXT("R"), TEXT("R"), TEXT("B"), TEXT("L"), TEXT("L"), TEXT("L") };
	for (int32 d = 0; d < 8; ++d) S.Crawl[d] = C(FString(TEXT("sd_pr")) + CrawlKey[d]);
	S.RunFastK = Data.FindClip(TEXT("sd_runFastK"));
	S.RunFastW = Data.FindClip(TEXT("sd_runFastW"));
	if (!S.RunFastK || !S.RunFastW) UE_LOG(LogDFRig, Warning, TEXT("Soldier: clips sd_runFastK / sd_runFastW missing (re-run DFExport)"));
	if (!bAll) return false;

	Rig = MakeUnique<FDFRig>();
	Rig->bUseRootMotion = true;
	if (!Rig->Init(Skel, Mesh))
	{
		Rig.Reset();
		UE_LOG(LogDFRig, Error, TEXT("Soldier: rig init failed"));
		return false;
	}
	S.EyeHeight = Rig->RestRootAt(4) != 0.0 ? Rig->RestRootAt(4) : 1.7;
	Mesh->SetRelativeLocation(DF::ToUE(FVector(0.0, Rig->RestRootAt(3), 0.0)));

	S.Skel = Skel;
	Rig->Play(S.Idle, true, 0.0, 1.0, false);
	S.bOk = true;
	Update(0.0, FDFSoldierPose());

	return true;
}

double FDFSoldier::EyeHeight() const { return Impl->bOk ? Impl->EyeHeight : 1.7; }
double FDFSoldier::EyeNow() const { return Impl->bOk && Rig ? Rig->RootAt(4) : 1.7; }
double FDFSoldier::HipNow() const { return Impl->bOk && Rig ? Rig->RootAt(3) : 0.9; }

void FDFSoldier::RootVelocity(double& OutX, double& OutZ) const
{
	if (!Impl->bOk || !Rig) { OutX = 0.0; OutZ = 0.0; return; }
	OutX = Rig->RootAt(0) * 0.5 * DFData::SimHz;
	OutZ = Rig->RootAt(2) * 0.5 * DFData::SimHz;
}

void FDFSoldier::Update(double Dt, const FDFSoldierPose& Pose)
{
	if (!Impl->bOk || !Rig) return;
	FImpl& S = *Impl;
	const bool bProne = Pose.Stance == EDFStance::Prone;
	const bool bCrouch = Pose.Stance == EDFStance::Crouch;
	const double Mag = FMath::Min(1.0, FMath::Sqrt(Pose.MoveX * Pose.MoveX + Pose.MoveZ * Pose.MoveZ));
	const FDFClipData* const* Set = bProne ? S.Crawl : (bCrouch ? S.Crouch : S.Run);
	const bool bRunSet = !bProne && !bCrouch;
	const FDFClipData* Clip = nullptr;
	bool bLoop = true;
	S.AirTime = Pose.bAirborne ? S.AirTime + Dt : 0.0;
	bool bRestart = false;
	if (Pose.Roll != 0)
	{
		Clip = Pose.Roll < 0 ? S.RollL : S.RollR;
		bLoop = false;
		bRestart = !S.bRolling;   // every roll plays the clip from the start
	}
	else if (Pose.bAirborne && Pose.Stance == EDFStance::Stand)
	{
		Clip = S.AirTime < S.JumpStart->Duration ? S.JumpStart : S.JumpLoop;
		bLoop = Clip != S.JumpStart;
	}
	else if (Mag < 0.1)
	{
		Clip = bProne ? S.Prone : (bCrouch ? S.Kneel : S.Idle);
	}
	else
	{
		// 8 directions (forward = -Z): 0 = forward, +90deg = right
		const double A = FMath::Atan2(Pose.MoveX, -Pose.MoveZ);
		const int32 Idx = (int32)JsMod(JsMod(DF::JsRound(A / (DF::PI_D / 4.0)), 8.0) + 8.0, 8.0);
		Clip = Set[Idx];
		// run_anim (def +0x48): standing forward run uses anim_run_2 / anim_run_3 (0x47e3b3); root keys give 6.90 / 7.82 m/s
		if (bRunSet && Idx == 0 && Pose.RunAnim >= 1)
		{
			const FDFClipData* Fast = (Pose.RunAnim >= 2 && S.RunFastW) ? S.RunFastW : S.RunFastK;
			if (Fast) Clip = Fast;
		}
	}
	// crossfade 13 ticks for stand run / idles, 9 for crouch/prone walks, jump (DFBHD.EXE 0x413f8b)
	const bool bLong = (bRunSet && Clip != S.JumpStart && Clip != S.JumpLoop)
		|| Clip == S.Idle || Clip == S.Kneel || Clip == S.Prone;
	S.bRolling = Pose.Roll != 0;
	Rig->Play(Clip, bLoop, (bLong ? 13.0 : 9.0) / DFData::SimHz, 1.0, bRestart);
	Rig->Advance(Dt);

	// +0x200 fire kick (0x47d91c): per 62 Hz tick, while +0x215 > 0 it loses 0x03000000 (4.22 deg), then decays 1/8
	if (Pose.FireKickTicks < S.LastKickTicks) S.PendingKicks += S.LastKickTicks - Pose.FireKickTicks;
	S.LastKickTicks = Pose.FireKickTicks;
	S.TickAccum += Dt;
	const int32 Steps = FMath::Min(8, (int32)FMath::FloorToDouble(S.TickAccum * DFData::SimHz));
	S.TickAccum = FMath::Min(S.TickAccum - Steps / double(DFData::SimHz), 1.0 / DFData::SimHz);
	for (int32 t = 0; t < Steps; ++t)
	{
		if (S.PendingKicks > 0) { --S.PendingKicks; S.KickAngle -= 0x03000000 * Bam; }
		S.KickAngle -= (S.KickAngle + 4 * Bam) / 8.0;
	}
	// the body roll is not applied while a roll clip plays (0x48433d)
	S.Aim(*Rig, bProne, Pose.AimYaw, Pose.Pitch, Pose.Recoil, Pose.Lean, Pose.LegYawR, Pose.LegYawL,
		Pose.Roll != 0 ? 0.0 : Pose.BodyRoll, Pose.ViewRoll, Pose.SpecialHold, Pose.ArmSwingTicks, S.KickAngle);
	Rig->Push();
}

void FDFSoldier::SetWeapon(EDFWeapon W, UStaticMesh* TpMesh)
{
	FImpl& S = *Impl;
	S.MuzzleLocal = FVector::ZeroVector;
	if (!Gun) return;
	const DFWeapons::FWeaponSpec* Spec = DFWeapons::Find(W);
	if (!Spec || !TpMesh)
	{
		Gun->SetStaticMesh(nullptr);
		return;
	}
	Gun->SetStaticMesh(TpMesh);
	const FDFData& Data = FDFData::Get();
	const FDFGunAttach* Att = Spec->TpAttach ? Data.FindTpAttach(FName(Spec->TpAttach)) : nullptr;
	if (!Att) Att = &Data.GetTpGunAttach();
	if (Mesh && Mesh->GetSkinnedAsset())
	{
		const FName Bone = S.Skel && S.Skel->Names.IsValidIndex(Att->Bone) ? S.Skel->Names[Att->Bone] : Mesh->GetBoneName(Att->Bone);
		Gun->AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, Bone);
		Gun->SetRelativeTransform(FTransform(DF::ToUE(Att->Rotation), DF::ToUE(Att->Position)));
	}
	if (Spec->TpModel)
	{
		if (const FDFModelData* Model = Data.FindModel(FName(Spec->TpModel)))
		{
			if (const FVector* Mf = Model->FindUserpoint(TEXT("mflash01"))) S.MuzzleLocal = *Mf;
		}
	}
}

FVector FDFSoldier::GetMuzzleWorldUE() const
{
	if (!Impl->bOk || !Gun) return FVector::ZeroVector;
	return Gun->GetComponentTransform().TransformPosition(DF::ToUE(Impl->MuzzleLocal));
}
