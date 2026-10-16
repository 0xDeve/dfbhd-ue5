// Game data extracted from Delta Force: Black Hawk Down (Team Sabre) by Tools/DFExport/export_unreal.py:
// skeletons, BAD animation clips, model bounding boxes / userpoints and the third-person gun attachment.
// Loaded once from Content/DF/Data/dfdata.json. Everything here is in the web project's three space
// (meters, +Y up, forward -Z; see DFMath.h), exactly as DFBHD-WebLite's assets.json.
#pragma once

#include "CoreMinimal.h"

/** A skeleton (assets.json skeletons[key]). Index i is the 3DI node index; Names[i] is the bone name in the
 *  imported UE skeletal mesh. RestQuats are ABSOLUTE bone orientations of the *_RST bind pose; RestOffsets[i]
 *  is bone i's origin in its parent's frame; Pivots are the bind-pose bone positions. */
struct FDFSkeletonData
{
	TArray<FName> Names;
	TArray<int32> Parents;          // Parents[0] == 0 (root)
	TArray<FQuat> RestQuats;
	TArray<FVector> RestOffsets;
	TArray<FVector> Pivots;
	double RestRoot[6] = { 0, 0, 0, 0, 0, 0 };   // root key of the rest clip: t0..t5 (t3 hip height, t4 eye height)
	int32 Num() const { return Parents.Num(); }
};

/** One .BAD clip (assets.json clips[key]). Quats are absolute bone orientations, key-major:
 *  Quats[(k * Tracks + b) * 4 + {0,1,2,3}] = (x, y, z, w). Root keys are 6 floats each:
 *  t0 sideways velocity (+ = right after export), t1 vertical, t2 forward (export: - = forward), t3 hip height, t4 eye height, t5. */
struct FDFClipData
{
	FName Key;
	int32 Fps = 30;
	int32 Keys = 1;
	int32 Tracks = 0;
	int32 RootKeys = 1;
	TArray<float> Quats;
	TArray<float> Root;
	TArray<FVector> Offsets;        // per bone record of the clip (may be fewer than the skeleton's bones)
	double Duration = 0.0;          // max(1, Keys - 1) / Fps
};

/** Per-model data (assets.json models[key] minus the geometry). */
struct FDFModelData
{
	FVector BBoxMin = FVector::ZeroVector;
	FVector BBoxMax = FVector::ZeroVector;
	TMap<FName, FVector> Userpoints;   // lower-case names, e.g. "mflash01", "ground"
	const FVector* FindUserpoint(FName Name) const { return Userpoints.Find(Name); }
};

/** Transform of the third-person rifle relative to the soldier's right-hand bone (port of rig.js Soldier._attachGun,
 *  precomputed by the exporter from the idle pose). */
struct FDFGunAttach
{
	int32 Bone = 16;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
};

class PROJECTDF_API FDFData
{
public:
	/** Loads Content/DF/Data/dfdata.json on first use. Check IsLoaded(). */
	static const FDFData& Get();

	bool IsLoaded() const { return bLoaded; }
	const FString& GetError() const { return Error; }

	const FDFSkeletonData* FindSkeleton(FName Key) const { return Skeletons.Find(Key); }
	const FDFClipData* FindClip(FName Key) const { return Clips.Find(Key); }
	const FDFModelData* FindModel(FName Key) const { return Models.Find(Key); }
	const FDFGunAttach& GetTpGunAttach() const { return TpGunAttach; }
	/** Third-person attachment of a weapon model to the right-hand bone (dfdata tpAttach[key]: car15, colt, knife, frag, flash, smoke); null if absent. */
	const FDFGunAttach* FindTpAttach(FName Key) const { return TpAttach.Find(Key); }

	static FString GetDataPath();

private:
	bool Load(const FString& Path);

	bool bLoaded = false;
	FString Error;
	TMap<FName, FDFSkeletonData> Skeletons;
	TMap<FName, FDFClipData> Clips;
	TMap<FName, FDFModelData> Models;
	FDFGunAttach TpGunAttach;
	TMap<FName, FDFGunAttach> TpAttach;
};
