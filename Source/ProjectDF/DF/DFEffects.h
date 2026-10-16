// Visual effects: impact decals, dust puffs, muzzle flash, tracers. Port of DFBHD-WebLite src/fx.js.
// All positions/directions in three space (see DFMath.h); converted to Unreal internally.
//
// PUBLIC API IS A CONTRACT used by ADFPlayerPawn.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFWeaponData.h"
#include "DFEffects.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDecalComponent;
class UPointLightComponent;

UCLASS()
class PROJECTDF_API ADFEffects : public AActor
{
	GENERATED_BODY()

public:
	ADFEffects();

	/** Finds the ADFEffects in the level, or spawns one. */
	static ADFEffects* Get(UWorld* World);

	/** Muzzle flash for 0.05 s at the unscaled muzzle (three space), full sprite size in meters.
	 *  First-person sprites use the same camera projection and depth scale as the weapon. */
	void MuzzleFlash(const FVector& Pos, double Size, bool bFirstPerson = false);
	/** Follow the animated muzzle each frame; cancel when the weapon is hidden or the view changes. */
	void UpdateMuzzleFlash(const FVector& Pos, bool bFirstPerson, bool bVisible);
	/** Cancel any flash left over from the previous weapon. */
	void StopMuzzleFlash();

	/** A round's tracer: flies from Origin along unit Dir at Speed (m/s, ammo velocity) until it has covered Dist (the impact)
	 *  or MaxAge (s, ammo max_age) runs out (fx.js tracer). Dist may be +infinity (TNumericLimits<double>::Max()). */
	void Tracer(const FVector& Origin, const FVector& Dir, double Dist, double Speed = 990.0, double MaxAge = 1.5);

	// ---- grenades (DFWeapons.h FDFGrenadeSim moves them; these are their visuals). Positions / rotations in three space.
	/** Show a thrown grenade's model (the weapon data asset's ProjectileMesh) and start its in-flight emitters by Type:
	 *  Flashbang = Effect_FlashBangToss (white steam trail); SmokeGrenade = Effect_SmokeToss (the same trail, then at 5 s
	 *  the pop flash + sparks + the Smoke_Base cloud emitting until 25 s); FragGrenade = none. Returns a handle. */
	int32 AddGrenade(EDFWeapon Type, UStaticMesh* Mesh);
	/** Per frame: model transform (three-space position, three-space rotation) and the grenade's age in seconds (drives the
	 *  emitters: they follow the grenade, also while it lies on the ground). */
	void UpdateGrenade(int32 Handle, const FVector& Pos, const FQuat& Rot, double AgeSec);
	/** Remove the model and stop its emitters (puffs already emitted live on). */
	void RemoveGrenade(int32 Handle);
	/** Detonation visuals at Pos: FragGrenade = Effect_FragGrndDirt (flashes, shockwave, smoke, sparks, debris + a light),
	 *  Flashbang = Effect_FlashBang (flash, sparks, smoke + a bright light). */
	void GrenadeBurst(EDFWeapon Type, const FVector& Pos);
	/** Number of live smoke puffs (debug). */
	int32 CountLiveSmoke() const;

	/** Bullet hole decal + two dust puffs at Point, oriented to Normal (fx.js impact). */
	void Impact(const FVector& Point, const FVector& Normal);

	/** Advance all effects; CamPos (three space) drives the tracer width (fx.js update). Call once per frame. */
	void UpdateFX(float Dt, const FVector& CamPos);

	/** Number of tracers currently in flight (debug). */
	int32 CountLiveTracers() const;

	/** Assets, assigned in the Blueprint. A missing one disables that effect (warning in the log). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UStaticMesh> TracerMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> FlashMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> DustMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> DecalMaterial;
	/** Grenade effects (ptl textures): smoke puffs (thinpuf6, alpha), dirt puffs (dirtpuf2, alpha), steam trail (CSteamAD,
	 *  alpha), blast flash (mbFlash4, additive), shockwave (BmBlast1, additive), sparks (spark7, additive). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> SmokeMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> DirtMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> SteamMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> BlastFlashMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> ShockwaveMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Effects") TObjectPtr<UMaterialInterface> SparkMaterial;

protected:
	virtual void BeginPlay() override;

private:
	struct FTracer
	{
		UStaticMeshComponent* Comp = nullptr;
		FVector Origin = FVector::ZeroVector;   // three space
		FVector Dir = FVector(0, 0, -1);
		FQuat Rot = FQuat::Identity;            // UE rotation
		double Age = 0.0, Dist = 0.0;
		double Speed = 990.0;
		bool bLive = false;
	};
	/** Particle pool kinds: one material each (see the *Material properties). */
	enum EPKind : uint8 { PK_Smoke, PK_Dirt, PK_Steam, PK_Flash, PK_Shock, PK_Spark, PK_Count };
	/** One billboard particle (grenade effects). Positions / velocities in three space (m, m/s). */
	struct FPart
	{
		UMaterialBillboardComponent* Comp = nullptr;
		UMaterialInstanceDynamic* Mid = nullptr;
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector, Wind = FVector::ZeroVector;
		double T = 0.0, Life = 1.0, Accel = 0.0, Damp = 0.0;   // Accel: three +Y m/s^2
		float Size0 = 1.f, Size1 = 1.f, Op0 = 1.f, Rot0 = 0.f, RotSpd = 0.f, Intensity = 1.f, LastSizeCm = -1.f;
		int32 Fade = 0;   // 0 linear out, 1 smoke table (flat for the first quarter, then out)
		FLinearColor Tint = FLinearColor::White;
		bool bLive = false;
	};
	struct FGrenade
	{
		int32 Id = 0;
		EDFWeapon Type = EDFWeapon::None;
		UStaticMeshComponent* Comp = nullptr;
		FVector Pos = FVector::ZeroVector;
		double LastAge = -1.0, SteamAcc = 0.0, SmokeAcc = 0.0, SparkAcc = 0.0;
		bool bPopped = false, bUsed = false;
	};
	struct FBurstLight
	{
		UPointLightComponent* Comp = nullptr;
		double T = 0.0, Dur = 0.0;
		float Peak = 0.f;
	};
	struct FDust
	{
		UMaterialBillboardComponent* Comp = nullptr;
		UMaterialInstanceDynamic* Mid = nullptr;
		FVector Pos = FVector::ZeroVector;      // three space
		FVector Vel = FVector::ZeroVector;
		double T = 0.0, Life = 0.0;
		bool bLive = false;
	};
	struct FDecal
	{
		UDecalComponent* Comp = nullptr;
		UMaterialInstanceDynamic* Mid = nullptr;
		double Age = 0.0;
	};

	void EnsureInit();
	UMaterialInterface* MaterialFor(EPKind Kind) const;
	FPart* SpawnPart(EPKind Kind, const FVector& Pos, const FVector& Vel, double Life, float Size0M, float Size1M, float Op0, int32 Fade,
		const FLinearColor& Tint = FLinearColor::White, float Intensity = 1.f, float RotSpd = 0.f, double Accel = 0.0, double Damp = 0.0,
		const FVector& Wind = FVector::ZeroVector);
	void EmitSmoke(const FVector& Pos);
	void EmitSteam(const FVector& Pos);
	void EmitSpark(const FVector& Pos, double Speed, double Life, float SizeM, double Accel);
	void PopLight(const FVector& Pos, double Dur, float Peak, const FLinearColor& Col);
	void UpdateParts(float Dt);
	static FVector RandUnit();

	bool bInit = false;
	bool bWarnMesh = false, bWarnMat = false;

	TArray<FTracer> Tracers;
	int32 TracerI = 0;
	TArray<FDust> Dust;
	int32 DustI = 0;
	TArray<FDecal> Decals;
	int32 DecalNext = 0;

	TArray<FPart> Parts[PK_Count];
	int32 PartNext[PK_Count] = { 0, 0, 0, 0, 0, 0 };
	TArray<FGrenade> Grenades;
	int32 NextGrenadeId = 1;
	TArray<FBurstLight> BurstLights;

	UPROPERTY(Transient) TObjectPtr<UMaterialBillboardComponent> FlashComp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashMid;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FlashLight;
	double FlashT = 0.0;
	bool bFlashFirstPerson = false;
	bool bFlashJustSpawned = false;
};
