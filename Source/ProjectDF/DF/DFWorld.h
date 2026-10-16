// World: sky/lights/fog/post plus either the web remake's hand-built compound (port of DFBHD-WebLite src/world.js)
// or a map exported from the game's mission files (Docs/MAPS.md, e.g. Panic Attack). Positions in three space (DFMath.h).
//
// PUBLIC API IS A CONTRACT used by ADFPlayerPawn. The collision queries below replace the pawn's former direct loops
// over GetBoxes(): in compound mode they run the web's cylinder-vs-AABB code unchanged (main.js pushOut / groundAt /
// head probe / chase-camera clip); in map mode they are Unreal scene queries (ECC_Visibility, complex collision)
// against the map's terrain tiles and props.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFTypes.h"
#include "DFWorld.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** A team start / player start of a map (three space). */
struct FDFSpawnPoint
{
	FVector Pos = FVector::ZeroVector;
	double Yaw = 0.0;   // three yaw, radians
};

UCLASS()
class PROJECTDF_API ADFWorld : public AActor
{
	GENERATED_BODY()

public:
	ADFWorld();

	static constexpr double MapHalf = 128.0;   // compound: terrain half extent (m)
	static constexpr double PlayHalf = 120.0;  // compound: walkable bounds (m)
	static constexpr double StepUp = 0.5;      // main.js pushOut: obstacles whose top is below feet + 0.5 m are stepped onto
	static constexpr double GroundProbe = 0.55;  // main.js groundAt: surfaces up to feet + 0.55 m count as ground

	/** Map to build (Content/DF/Data/Maps/<MapKey>.json). If empty: -DFMap=<Key> on the command line, else the level
	 *  name without its "DF_" prefix when that JSON exists, else the web compound. */
	UPROPERTY(EditAnywhere, Category = "DF")
	FString MapKey;

	/** Compound meshes by the web's key (ground, street, mogbld1, mogbld2, moghut1, moghut2, dhut01, wcrate1, wcrate2,
	 *  mbarrel1, ncar1, tire01). Assigned in the Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Compound")
	TMap<FName, TObjectPtr<UStaticMesh>> CompoundMeshes;

	/** Finds the ADFWorld in the level, or spawns one (and builds it). */
	static ADFWorld* Get(UWorld* World);

	/** True when a game map (not the compound) was built. */
	bool IsMap() const { return bMap; }
	/** Key of the built map ("" for the compound). */
	const FString& GetLoadedMapKey() const { return LoadedMapKey; }

	/** Compound prop colliders (world.js world.boxes), axis aligned. Empty in map mode. */
	const TArray<FDFBox>& GetBoxes() const { return Boxes; }

	/** Player spawn. Compound: world.js world.spawn (-1, 0, 4), yaw 0. Map: the team start picked by -DFTeam=red|blue
	 *  (default red) and -DFSpawn=<index> (default 0, wrapped); falls back to the player start, then the bounds centre. */
	FVector GetSpawn() const;
	/** Three yaw (radians) of the spawn (0 in the compound). */
	double GetSpawnYaw() const;
	/** Spawn of the PlayerIndex-th player (0 = GetSpawn()). Map: the next team starts after the -DFSpawn one, wrapped; compound (or
	 *  a map with a single start): 1.5 m further along three +X per extra player. */
	void GetSpawnFor(int32 PlayerIndex, FVector& OutPos, double& OutYaw) const;

	// ---------------------------------------------------------------- collision queries (three space, meters)

	/** main.js pushOut: pushes a vertical cylinder of Radius whose solid band is [Pos.Y + StepUp, Pos.Y + Height]
	 *  horizontally (X/Z only) out of solid geometry. Map mode: capsule overlap + MTD against the props/terrain, band
	 *  at least 2*Radius tall, only steep contacts (horizontal part of the MTD normal >= 0.5) push. */
	void PushOut(FVector& Pos, double Radius, double Height) const;

	/** main.js groundAt: height of the highest surface at or below Pos.Y + GroundProbe under a disc of FootRadius
	 *  around (Pos.X, Pos.Z). Compound: boxes, else the flat ground 0. Map: downward sphere sweep; -1e4 if nothing. */
	double GroundAt(const FVector& Pos, double FootRadius) const;

	/** True if a probe of radius R at Pt touches solid geometry (lean head probe). Compound: box test as before. */
	bool SphereBlocked(const FVector& Pt, double R) const;

	/** Chase camera: distance from Origin along unit Dir until a ball of radius Pad touches something, MaxDist if clear.
	 *  Compound: boxes expanded by Pad (ignored if Origin is inside) and the plane y = Pad, like main.js. */
	double ClipCamera(const FVector& Origin, const FVector& Dir, double MaxDist, double Pad) const;

	/** Keeps Pos.X / Pos.Z inside the walkable area (compound: +-PlayHalf, map: the JSON bounds). */
	void ClampToBounds(FVector& Pos) const;

	/** Hitscan against props + ground (main.js castRay). Origin/Dir/OutPoint/OutNormal in three space, Dir unit length.
	 *  OutNormal faces the ray (n.dot(dir) <= 0). Returns false on a miss within MaxDist. */
	bool CastRay(const FVector& Origin, const FVector& Dir, double MaxDist, double& OutT, FVector& OutPoint, FVector& OutNormal) const;

	/** Components showing the ground (compound: ground + street quads; map: the terrain tiles), for the HUD spin map capture. */
	void GetGroundComponents(TArray<UPrimitiveComponent*>& Out) const;

	/** Origin (engine x, y metres; x = -three.z, y = -three.x) of the 50 m map-coordinate grid: 50 * trunc(Map Centerpoint marker / 50); (0, 0) without one (HUD.md 10.5). */
	FVector2D GetGridOriginM() const { return GridOriginM; }

	/** (Re)builds the environment and the compound or the map. Idempotent. */
	void Build();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void BuildEnvironment();
	void BuildCompound();
	bool BuildMap(const FString& Key);
	FString ResolveMapKey() const;
	void RegisterBuilt(UActorComponent* C, USceneComponent* AttachTo);

	TArray<FDFBox> Boxes;
	FVector2D GridOriginM = FVector2D::ZeroVector;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> GroundComps;   // compound ground + street
	bool bMap = false;
	FString LoadedMapKey;
	FVector2D BoundsMin = FVector2D(-PlayHalf, -PlayHalf);   // three (X, Z)
	FVector2D BoundsMax = FVector2D(PlayHalf, PlayHalf);
	FDFSpawnPoint Spawn{ FVector(-1.0, 0.0, 4.0), 0.0 };
	TArray<FDFSpawnPoint> SpawnList;   // the team's (or the player) start list a map spawn was picked from
	int32 SpawnBase = 0;               // index of Spawn in SpawnList

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> BuiltComponents;
	bool bBuilt = false;
	/** Exact terrain height at a three-space (X, Z) (DFBHD.EXE 0x5409a0, Docs/MAPS.md 4.3); false if the map has no heightfield. */
	bool TerrainHeight(double X, double Z, double& OutY) const;
	double HfSample(int32 Xi, int32 Yi) const;

	TArray<uint16> HfData;                 // heightfield .bin (HfSize x HfSize, row-major)
	int32 HfSize = 0, HfSector = 512, HfOrigin[2] = { 0, 0 };
	int32 HfSectors[16][16] = {};
	double HfUnit = 1.0 / 256.0;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> TerrainComps;   // ignored by the props ground sweep when a heightfield exists
	bool bPreview = false;   // built by the editor construction script (not a game world)
};
