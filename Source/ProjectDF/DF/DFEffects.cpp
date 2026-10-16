#include "DFEffects.h"
#include "DFMath.h"
#include "DFGameData.h"
#include "DFGameMode.h"

#include "Components/DecalComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFFx, Log, All);

namespace
{
	constexpr int32 NumTracers = 32;
	constexpr int32 NumDust = 24;
	constexpr int32 MaxDecals = 100;
	constexpr double DecalSizeM = 0.14;

	double Clamp01(double V) { return DF::Clamp01(V); }

	/** Warns once (per flag) that an asset property is not assigned; that effect is then disabled. */
	bool CheckAsset(const UObject* Obj, const TCHAR* Property, bool& bWarned)
	{
		if (!Obj && !bWarned)
		{
			bWarned = true;
			UE_LOG(LogDFFx, Warning, TEXT("Property %s is not assigned; that effect is disabled"), Property);
		}
		return Obj != nullptr;
	}
}

ADFEffects::ADFEffects()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

ADFEffects* ADFEffects::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADFEffects> It(World); It; ++It)
	{
		if (*It && !(*It)->IsTemplate())
		{
			return *It;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// the game mode is only on the server; clients read the classes from the GameState's default game mode (not there yet: wait)
	bool bPending = false;
	const ADFGameMode* GM = ADFGameMode::FindForWorld(World, bPending);
	if (bPending)
	{
		return nullptr;
	}
	UClass* Class = GM && GM->EffectsClass ? GM->EffectsClass.Get() : ADFEffects::StaticClass();
	return World->SpawnActor<ADFEffects>(Class, FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

void ADFEffects::BeginPlay()
{
	Super::BeginPlay();
	EnsureInit();
}

void ADFEffects::EnsureInit()
{
	if (bInit || IsTemplate() || !GetWorld())
	{
		return;
	}
	bInit = true;
	USceneComponent* Root = GetRootComponent();

	// ---- tracers
	CheckAsset(TracerMesh, TEXT("TracerMesh"), bWarnMesh);
	Tracers.SetNum(NumTracers);
	for (int32 I = 0; I < NumTracers; ++I)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Tracer%02d"), I));
		C->SetupAttachment(Root);
		C->SetMobility(EComponentMobility::Movable);
		if (TracerMesh)
		{
			C->SetStaticMesh(TracerMesh);
		}
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bReceivesDecals = false;
		C->SetVisibility(false);
		C->RegisterComponent();
		Tracers[I].Comp = C;
	}

	// ---- muzzle flash
	if (CheckAsset(FlashMaterial, TEXT("FlashMaterial"), bWarnMat))
	{
		FlashMid = UMaterialInstanceDynamic::Create(FlashMaterial, this);
		FlashComp = NewObject<UMaterialBillboardComponent>(this, TEXT("MuzzleFlash"));
		FlashComp->SetupAttachment(Root);
		FlashComp->SetMobility(EComponentMobility::Movable);
		FlashComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FlashComp->SetCastShadow(false);
		FlashComp->AddElement(FlashMid, nullptr, false, 15.f, 15.f, nullptr);
		// First-person geometry is projected toward the camera; keep its unscaled bounds from culling the sprite.
		FlashComp->SetBoundsScale(12.f);
		FlashComp->SetVisibility(false);
		FlashComp->RegisterComponent();
	}
	FlashLight = NewObject<UPointLightComponent>(this, TEXT("MuzzleLight"));
	FlashLight->SetupAttachment(Root);
	FlashLight->SetMobility(EComponentMobility::Movable);
	FlashLight->SetLightColor(FLinearColor::FromSRGBColor(FColor(0xff, 0xaa, 0x55)));
	FlashLight->SetIntensityUnits(ELightUnits::Candelas);
	FlashLight->SetIntensity(60.f);
	FlashLight->SetAttenuationRadius(900.f);   // 9 m
	FlashLight->SetCastShadows(false);
	FlashLight->SetVisibility(false);
	FlashLight->RegisterComponent();

	// ---- dust
	Dust.SetNum(NumDust);
	if (CheckAsset(DustMaterial, TEXT("DustMaterial"), bWarnMat))
	{
		for (int32 I = 0; I < NumDust; ++I)
		{
			FDust& D = Dust[I];
			D.Mid = UMaterialInstanceDynamic::Create(DustMaterial, this);
			D.Comp = NewObject<UMaterialBillboardComponent>(this, *FString::Printf(TEXT("Dust%02d"), I));
			D.Comp->SetupAttachment(Root);
			D.Comp->SetMobility(EComponentMobility::Movable);
			D.Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			D.Comp->SetCastShadow(false);
			D.Comp->AddElement(D.Mid, nullptr, false, 15.f, 15.f, nullptr);
			D.Comp->SetVisibility(false);
			D.Comp->RegisterComponent();
		}
	}

	CheckAsset(DecalMaterial, TEXT("DecalMaterial"), bWarnMat);
}

void ADFEffects::MuzzleFlash(const FVector& Pos, double Size, bool bFirstPerson)
{
	EnsureInit();
	bFlashFirstPerson = bFirstPerson;
	const FVector P = DF::ToUE(Pos);
	if (FlashComp && FlashMid)
	{
		FlashComp->SetFirstPersonPrimitiveType(bFirstPerson ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None);
		FlashComp->SetWorldLocation(P);
		FlashMid->SetScalarParameterValue(TEXT("Rotation"), FMath::FRand());   // random angle (turns)
		// Material billboards take half extents; the source sprite size is its full width/height.
		const float SizeCm = float(Size * (0.8 + FMath::FRand() * 0.5) * DF::M2CM * 0.5);
		if (FlashComp->Elements.Num() > 0)
		{
			FlashComp->Elements[0].BaseSizeX = SizeCm;
			FlashComp->Elements[0].BaseSizeY = SizeCm;
			FlashComp->MarkRenderStateDirty();
		}
		FlashComp->SetVisibility(true);
	}
	if (FlashLight)
	{
		FlashLight->SetWorldLocation(P);
		FlashLight->SetVisibility(true);
	}
	FlashT = 0.05;
	bFlashJustSpawned = true;
}

void ADFEffects::UpdateMuzzleFlash(const FVector& Pos, bool bFirstPerson, bool bVisible)
{
	if (FlashT <= 0.0) return;
	if (!bVisible || bFirstPerson != bFlashFirstPerson)
	{
		StopMuzzleFlash();
		return;
	}
	const FVector P = DF::ToUE(Pos);
	if (FlashComp) FlashComp->SetWorldLocation(P);
	if (FlashLight) FlashLight->SetWorldLocation(P);
}

void ADFEffects::StopMuzzleFlash()
{
	FlashT = 0.0;
	bFlashJustSpawned = false;
	if (FlashComp) FlashComp->SetVisibility(false);
	if (FlashLight) FlashLight->SetVisibility(false);
}

void ADFEffects::Tracer(const FVector& Origin, const FVector& Dir, double Dist, double Speed, double MaxAge)
{
	EnsureInit();
	if (Tracers.Num() == 0)
	{
		return;
	}
	FTracer& T = Tracers[TracerI++ % Tracers.Num()];
	T.Origin = Origin;
	T.Dir = Dir;
	T.Speed = Speed;
	T.Dist = FMath::Min(Dist, Speed * MaxAge);
	T.Age = 0.0;
	T.Rot = FRotationMatrix::MakeFromX(DF::ToUEDir(Dir)).ToQuat();   // model -Z (three) = UE +X along the flight
	T.bLive = true;
	if (T.Comp)
	{
		T.Comp->SetVisibility(false);   // placed on the next update
	}
}

void ADFEffects::Impact(const FVector& Point, const FVector& Normal)
{
	EnsureInit();

	// ---- decal oriented to the hit normal (projects along its local +X; into the surface)
	if (DecalMaterial)
	{
		FDecal* D;
		if (Decals.Num() < MaxDecals)
		{
			D = &Decals.AddDefaulted_GetRef();
			D->Mid = UMaterialInstanceDynamic::Create(DecalMaterial, this);
			D->Comp = NewObject<UDecalComponent>(this, *FString::Printf(TEXT("Decal%03d"), Decals.Num()));
			D->Comp->SetupAttachment(GetRootComponent());
			D->Comp->SetMobility(EComponentMobility::Movable);
			D->Comp->SetDecalMaterial(D->Mid);
			D->Comp->SetFadeScreenSize(0.f);
			D->Comp->RegisterComponent();
		}
		else
		{
			D = &Decals[DecalNext];       // recycle oldest first
			DecalNext = (DecalNext + 1) % Decals.Num();
		}
		const double Scale = 0.8 + FMath::FRand() * 0.6;
		const FVector N = DF::ToUEDir(Normal).GetSafeNormal();
		const FQuat Q = FRotationMatrix::MakeFromX(-N).ToQuat() * FQuat(FVector::XAxisVector, FMath::FRand() * UE_TWO_PI);
		const double HalfCm = DecalSizeM * Scale * 0.5 * DF::M2CM;
		D->Comp->SetWorldLocationAndRotation(DF::ToUE(Point) + N * 1.2, Q);
		D->Comp->DecalSize = FVector(6.0, HalfCm, HalfCm);
		D->Comp->MarkRenderStateDirty();
		D->Comp->SetVisibility(true);
		if (D->Mid)
		{
			D->Mid->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		}
		D->Age = 0.0;
	}

	// ---- dust puffs
	for (int32 I = 0; I < 2 && Dust.Num() > 0; ++I)
	{
		FDust& S = Dust[DustI++ % Dust.Num()];
		if (!S.Comp)
		{
			continue;
		}
		S.Pos = Point + Normal * 0.08;
		S.Vel = FVector((FMath::FRand() - 0.5) * 0.6, 0.4 + FMath::FRand() * 0.5, (FMath::FRand() - 0.5) * 0.6) + Normal * 0.8;
		S.T = 0.0;
		S.Life = 0.5 + FMath::FRand() * 0.3;
		S.bLive = true;
		S.Comp->SetWorldLocation(DF::ToUE(S.Pos));
		S.Comp->SetVisibility(true);
		if (S.Comp->Elements.Num() > 0)
		{
			S.Comp->Elements[0].BaseSizeX = S.Comp->Elements[0].BaseSizeY = float(0.15 * DF::M2CM);
			S.Comp->MarkRenderStateDirty();
		}
		if (S.Mid)
		{
			S.Mid->SetScalarParameterValue(TEXT("Opacity"), 0.9f);
		}
	}
}

void ADFEffects::UpdateFX(float Dt, const FVector& CamPos)
{
	EnsureInit();

	if (FlashT > 0.0)
	{
		// A shot is emitted before UpdateFX. Its first frame must render even when Dt exceeds its lifetime.
		if (!bFlashJustSpawned) FlashT -= Dt;
		bFlashJustSpawned = false;
		if (FlashT <= 0.0)
		{
			StopMuzzleFlash();
		}
	}

	// ---- tracers (trcr render function 0x494290 + Tracer1R node controller 0x4fd98c)
	for (FTracer& T : Tracers)
	{
		if (!T.bLive)
		{
			continue;
		}
		T.Age += Dt;
		const double PerTick = T.Speed / DFData::SimHz;
		const double Flown = T.Age * T.Speed;
		if (Flown >= T.Dist)
		{
			T.bLive = false;
			if (T.Comp) T.Comp->SetVisibility(false);
			continue;
		}
		const FVector Pos = T.Origin + T.Dir * Flown;
		const double S = Clamp01((FMath::Min(PerTick, Flown) - DFData::TRACER::StretchMin) / (DFData::TRACER::StretchMax - DFData::TRACER::StretchMin));
		const double W = Clamp01((FVector::Dist(Pos, CamPos) - DFData::TRACER::WidthNear) / DFData::TRACER::WidthRange);
		const double Len = DF::Lerp(DFData::TRACER::LengthScale[0], DFData::TRACER::LengthScale[1], S);
		const double Wid = DF::Lerp(DFData::TRACER::WidthScale[0], DFData::TRACER::WidthScale[1], W);
		if (T.Comp)
		{
			T.Comp->SetWorldTransform(FTransform(T.Rot, DF::ToUE(Pos), DF::ToUEScale(FVector(Wid, Wid, Len))));
			T.Comp->SetVisibility(true);
		}
	}

	UpdateParts(Dt);

	// ---- dust
	for (FDust& S : Dust)
	{
		if (!S.bLive)
		{
			continue;
		}
		S.T += Dt;
		if (S.T >= S.Life)
		{
			S.bLive = false;
			S.Comp->SetVisibility(false);
			continue;
		}
		const double K = S.T / S.Life;
		S.Pos += S.Vel * Dt;
		S.Vel *= (1.0 - Dt * 2.5);
		S.Comp->SetWorldLocation(DF::ToUE(S.Pos));
		if (S.Comp->Elements.Num() > 0)
		{
			const float SizeCm = float((0.15 + K * 0.7) * DF::M2CM);
			S.Comp->Elements[0].BaseSizeX = SizeCm;
			S.Comp->Elements[0].BaseSizeY = SizeCm;
			S.Comp->MarkRenderStateDirty();
		}
		if (S.Mid)
		{
			S.Mid->SetScalarParameterValue(TEXT("Opacity"), float((1.0 - K) * 0.9));
		}
	}

	// ---- decals: fade 25 s .. 30 s
	for (FDecal& D : Decals)
	{
		if (!D.Comp || !D.Comp->IsVisible())
		{
			continue;
		}
		D.Age += Dt;
		if (D.Age > 25.0)
		{
			const double Op = FMath::Max(0.0, 1.0 - (D.Age - 25.0) / 5.0);
			if (D.Mid) D.Mid->SetScalarParameterValue(TEXT("Opacity"), float(Op));
			if (Op <= 0.01) D.Comp->SetVisibility(false);
		}
	}
}

int32 ADFEffects::CountLiveTracers() const
{
	int32 N = 0;
	for (const FTracer& T : Tracers)
	{
		N += T.bLive ? 1 : 0;
	}
	return N;
}

// ================================================================== grenade effects (Prjctls.ptl / DF_Exp.ptl)

namespace
{
	constexpr int32 PoolMax[6] = { 130, 24, 24, 10, 6, 64 };   // smoke, dirt, steam, flash, shockwave, spark
	constexpr double SmokeStart = 5.0, SmokeEnd = 25.0;        // Smoke_Base: emit_delay 5 s, emit_dur 20 s
	const FVector WindVel(0.35, 0.0, -0.15);                    // three space: a little drift
	double Rnd() { return FMath::FRand(); }
	double RndS() { return FMath::FRand() * 2.0 - 1.0; }
}

FVector ADFEffects::RandUnit()
{
	for (;;)
	{
		const FVector V(RndS(), RndS(), RndS());
		const double L2 = V.SizeSquared();
		if (L2 > 1e-4 && L2 <= 1.0)
		{
			return V / FMath::Sqrt(L2);
		}
	}
}

UMaterialInterface* ADFEffects::MaterialFor(EPKind Kind) const
{
	switch (Kind)
	{
	case PK_Smoke: return SmokeMaterial;
	case PK_Dirt:  return DirtMaterial;
	case PK_Steam: return SteamMaterial;
	case PK_Flash: return BlastFlashMaterial;
	case PK_Shock: return ShockwaveMaterial;
	default:       return SparkMaterial;
	}
}

ADFEffects::FPart* ADFEffects::SpawnPart(EPKind Kind, const FVector& Pos, const FVector& Vel, double Life, float Size0M, float Size1M, float Op0,
	int32 Fade, const FLinearColor& Tint, float Intensity, float RotSpd, double Accel, double Damp, const FVector& Wind)
{
	EnsureInit();
	UMaterialInterface* Mat = MaterialFor(Kind);
	if (!Mat)
	{
		static bool bWarned[PK_Count] = {};
		if (!bWarned[Kind])
		{
			bWarned[Kind] = true;
			UE_LOG(LogDFFx, Warning, TEXT("Grenade effect material %d is not assigned; that particle kind is disabled"), int32(Kind));
		}
		return nullptr;
	}
	TArray<FPart>& Pool = Parts[Kind];
	FPart* P = nullptr;
	for (FPart& C : Pool)
	{
		if (!C.bLive)
		{
			P = &C;
			break;
		}
	}
	if (!P)
	{
		if (Pool.Num() < PoolMax[Kind])
		{
			P = &Pool.AddDefaulted_GetRef();
			P->Mid = UMaterialInstanceDynamic::Create(Mat, this);
			P->Comp = NewObject<UMaterialBillboardComponent>(this, *FString::Printf(TEXT("Fx%d_%03d"), int32(Kind), Pool.Num()));
			P->Comp->SetupAttachment(GetRootComponent());
			P->Comp->SetMobility(EComponentMobility::Movable);
			P->Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			P->Comp->SetCastShadow(false);
			P->Comp->AddElement(P->Mid, nullptr, false, 100.f, 100.f, nullptr);
			P->Comp->SetVisibility(false);
			P->Comp->RegisterComponent();
		}
		else
		{
			P = &Pool[PartNext[Kind]++ % Pool.Num()];   // recycle the oldest
		}
	}
	P->Pos = Pos; P->Vel = Vel; P->Wind = Wind;
	P->T = 0.0; P->Life = FMath::Max(Life, 0.01);
	P->Accel = Accel; P->Damp = Damp;
	P->Size0 = Size0M; P->Size1 = Size1M; P->Op0 = Op0; P->Fade = Fade; P->Tint = Tint; P->Intensity = Intensity;
	P->Rot0 = float(Rnd()); P->RotSpd = RotSpd;
	P->LastSizeCm = -1.f;
	P->bLive = true;
	P->Comp->SetWorldLocation(DF::ToUE(Pos));
	P->Comp->SetVisibility(true);
	if (P->Mid)
	{
		P->Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		P->Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		P->Mid->SetScalarParameterValue(TEXT("Rotation"), P->Rot0);
		P->Mid->SetScalarParameterValue(TEXT("Opacity"), Op0);
	}
	return P;
}

void ADFEffects::EmitSmoke(const FVector& Pos)
{
	// start velocity 1.5 +- 0.5 m/s in a 90 degree cone around up
	const double CosMax = FMath::Cos(DF::DEG * 45.0);
	const double Cz = CosMax + (1.0 - CosMax) * Rnd();
	const double Sz = FMath::Sqrt(1.0 - Cz * Cz), Ph = Rnd() * DF::PI_D * 2.0;
	const FVector Dir(Sz * FMath::Cos(Ph), Cz, Sz * FMath::Sin(Ph));   // three: +Y up
	const double Speed = 1.5 + RndS() * 0.5;
	const double Scale = 3.0 + RndS();   // x growth 2 % -> 85 % of the life
	const float Wm = 1.8f;               // sprite width per unit scale
	const float G = 0.75f + 0.25f * float(Rnd());   // grey 192..255
	SpawnPart(PK_Smoke, Pos + RandUnit() * 0.2, Dir * Speed, 5.0 + RndS(), float(Scale * Wm * 0.02), float(Scale * Wm * 0.85), 0.94f, 1,
		FLinearColor(G, G, G), 1.f, float((30.0 + RndS() * 30.0) / 360.0), 0.5, 0.6, WindVel);
}

void ADFEffects::EmitSteam(const FVector& Pos)
{
	SpawnPart(PK_Steam, Pos, FVector(RndS() * 0.1, 0.1, RndS() * 0.1), 0.25 + RndS() * 0.05, 0.5f, 0.8f, 0.6f, 0,
		FLinearColor(0.9f, 0.9f, 0.9f), 1.f, 0.f, 0.4, 0.0);
}

void ADFEffects::EmitSpark(const FVector& Pos, double Speed, double Life, float SizeM, double Accel)
{
	SpawnPart(PK_Spark, Pos, RandUnit() * (Speed * (0.5 + Rnd() * 0.5)), Life * (0.7 + Rnd() * 0.3), SizeM, SizeM * 0.6f, 1.f, 0,
		FLinearColor(1.f, 0.85f, 0.55f), 2.f, 0.f, Accel, 0.0);
}

void ADFEffects::PopLight(const FVector& Pos, double Dur, float Peak, const FLinearColor& Col)
{
	EnsureInit();
	FBurstLight* L = nullptr;
	for (FBurstLight& C : BurstLights)
	{
		if (C.T <= 0.0)
		{
			L = &C;
			break;
		}
	}
	if (!L)
	{
		if (BurstLights.Num() >= 4)
		{
			return;
		}
		L = &BurstLights.AddDefaulted_GetRef();
		L->Comp = NewObject<UPointLightComponent>(this, *FString::Printf(TEXT("BurstLight%d"), BurstLights.Num()));
		L->Comp->SetupAttachment(GetRootComponent());
		L->Comp->SetMobility(EComponentMobility::Movable);
		L->Comp->SetIntensityUnits(ELightUnits::Candelas);
		L->Comp->SetCastShadows(false);
		L->Comp->SetVisibility(false);
		L->Comp->RegisterComponent();
	}
	L->T = L->Dur = Dur;
	L->Peak = Peak;
	L->Comp->SetLightColor(Col);
	L->Comp->SetAttenuationRadius(3000.f);   // 30 m
	L->Comp->SetIntensity(Peak);
	L->Comp->SetWorldLocation(DF::ToUE(Pos) + FVector(0, 0, 50));
	L->Comp->SetVisibility(true);
}

void ADFEffects::UpdateParts(float Dt)
{
	for (int32 K = 0; K < PK_Count; ++K)
	{
		for (FPart& P : Parts[K])
		{
			if (!P.bLive)
			{
				continue;
			}
			P.T += Dt;
			if (P.T >= P.Life)
			{
				P.bLive = false;
				P.Comp->SetVisibility(false);
				continue;
			}
			const double Kf = P.T / P.Life;
			P.Pos += (P.Vel + P.Wind) * Dt;
			P.Vel.Y += P.Accel * Dt;
			P.Vel *= FMath::Max(0.0, 1.0 - P.Damp * Dt);
			P.Comp->SetWorldLocation(DF::ToUE(P.Pos));
			const float SizeCm = float(DF::Lerp(P.Size0, P.Size1, Kf) * DF::M2CM);
			if (P.Comp->Elements.Num() > 0 && (P.LastSizeCm < 0.f || FMath::Abs(SizeCm - P.LastSizeCm) > 0.03f * P.LastSizeCm))
			{
				P.Comp->Elements[0].BaseSizeX = P.Comp->Elements[0].BaseSizeY = SizeCm;
				P.Comp->MarkRenderStateDirty();
				P.LastSizeCm = SizeCm;
			}
			if (P.Mid)
			{
				const double Fade = P.Fade == 1 ? (Kf < 0.25 ? 1.0 : 1.0 - (Kf - 0.25) / 0.75) : 1.0 - Kf;
				P.Mid->SetScalarParameterValue(TEXT("Opacity"), float(P.Op0 * Fade));
				if (P.RotSpd != 0.f)
				{
					P.Mid->SetScalarParameterValue(TEXT("Rotation"), float(FMath::Frac(P.Rot0 + P.RotSpd * P.T)));
				}
			}
		}
	}
	for (FBurstLight& L : BurstLights)
	{
		if (L.T <= 0.0)
		{
			continue;
		}
		L.T -= Dt;
		if (L.T <= 0.0)
		{
			L.Comp->SetVisibility(false);
		}
		else
		{
			L.Comp->SetIntensity(float(L.Peak * L.T / L.Dur));
		}
	}
}

int32 ADFEffects::AddGrenade(EDFWeapon Type, UStaticMesh* Mesh)
{
	EnsureInit();
	FGrenade* G = nullptr;
	for (FGrenade& C : Grenades)
	{
		if (!C.bUsed)
		{
			G = &C;
			break;
		}
	}
	if (!G)
	{
		G = &Grenades.AddDefaulted_GetRef();
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Grenade%02d"), Grenades.Num()));
		C->SetupAttachment(GetRootComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bReceivesDecals = false;
		C->SetVisibility(false);
		C->RegisterComponent();
		G->Comp = C;
	}
	G->bUsed = true;
	G->Id = NextGrenadeId++;
	G->Type = Type;
	G->LastAge = -1.0;
	G->SteamAcc = G->SmokeAcc = G->SparkAcc = 0.0;
	G->bPopped = false;
	G->Pos = FVector::ZeroVector;
	G->Comp->SetStaticMesh(Mesh);
	G->Comp->SetVisibility(Mesh != nullptr);
	return G->Id;
}

void ADFEffects::UpdateGrenade(int32 Handle, const FVector& Pos, const FQuat& Rot, double AgeSec)
{
	FGrenade* G = nullptr;
	for (FGrenade& C : Grenades)
	{
		if (C.bUsed && C.Id == Handle)
		{
			G = &C;
			break;
		}
	}
	if (!G)
	{
		return;
	}
	G->Pos = Pos;
	G->Comp->SetWorldTransform(DF::ToUE(Rot, Pos));
	const double Last = G->LastAge < 0.0 ? FMath::Max(0.0, AgeSec - 1e-3) : G->LastAge;
	G->LastAge = AgeSec;
	const double Dt = FMath::Max(0.0, AgeSec - Last);
	if (G->Type == EDFWeapon::Flashbang || G->Type == EDFWeapon::SmokeGrenade)
	{
		G->SteamAcc += Dt * 40.0;   // FlashBang_SmokeToss: 40/s for the whole life
		for (int32 N = 0; G->SteamAcc >= 1.0 && N < 8; ++N, G->SteamAcc -= 1.0)
		{
			EmitSteam(Pos);
		}
		G->SteamAcc = FMath::Min(G->SteamAcc, 1.0);
	}
	if (G->Type != EDFWeapon::SmokeGrenade)
	{
		return;
	}
	if (!G->bPopped && AgeSec >= SmokeStart)
	{
		G->bPopped = true;   // Smokegrnd_Flash: 0.1 s additive pop, 7 m
		SpawnPart(PK_Flash, Pos, FVector::ZeroVector, 0.1, 7.f, 7.f, 1.f, 0, FLinearColor::White, 2.f);
	}
	const double A0 = FMath::Max(Last, SmokeStart), A1 = FMath::Min(AgeSec, SmokeEnd);
	if (A1 > A0)
	{
		const double Span = A1 - A0;
		G->SmokeAcc += Span * 20.0;   // Smoke_Base
		for (int32 N = 0; G->SmokeAcc >= 1.0 && N < 8; ++N, G->SmokeAcc -= 1.0)
		{
			EmitSmoke(Pos);
		}
		G->SmokeAcc = FMath::Min(G->SmokeAcc, 1.0);
		G->SparkAcc += Span * 100.0;   // SmokeGren_Sparks: 100/s, life 0.1 s
		for (int32 N = 0; G->SparkAcc >= 1.0 && N < 8; ++N, G->SparkAcc -= 1.0)
		{
			EmitSpark(Pos, 2.5, 0.1, 0.5f, 0.0);
		}
		G->SparkAcc = FMath::Min(G->SparkAcc, 1.0);
	}
}

void ADFEffects::RemoveGrenade(int32 Handle)
{
	for (FGrenade& C : Grenades)
	{
		if (C.bUsed && C.Id == Handle)
		{
			C.bUsed = false;
			C.Comp->SetVisibility(false);
			return;
		}
	}
}

void ADFEffects::GrenadeBurst(EDFWeapon Type, const FVector& Pos)
{
	EnsureInit();
	if (Type == EDFWeapon::FragGrenade)
	{
		// Effect_FragGrndDirt
		SpawnPart(PK_Flash, Pos, FVector::ZeroVector, 0.1, 10.f, 10.f, 1.f, 0, FLinearColor::White, 2.5f);
		SpawnPart(PK_Flash, Pos + FVector(0, 0.3, 0), FVector::ZeroVector, 0.1, 7.f, 7.f, 1.f, 0, FLinearColor(1.f, 0.85f, 0.6f), 2.5f);
		SpawnPart(PK_Shock, Pos + FVector(0, 0.2, 0), FVector::ZeroVector, 0.23, 10.f, 15.f, 1.f, 0, FLinearColor::White, 1.5f);
		for (int32 I = 0; I < 3; ++I)
		{
			const float G = 0.45f + float(Rnd()) * 0.15f;
			SpawnPart(PK_Smoke, Pos + RandUnit() * 0.8, RandUnit() * 2.0 + FVector(0, 1.5, 0), 2.0, 2.5f, 5.f, 0.85f, 0,
				FLinearColor(G, G, G), 1.f, float(RndS() * 0.1), 0.5, 1.0);
		}
		for (int32 I = 0; I < 10; ++I)
		{
			SpawnPart(PK_Dirt, Pos + RandUnit() * 0.5, RandUnit() * 8.0 + FVector(0, 4.0, 0), 1.0, 1.0f, 3.0f, 0.9f, 0,
				FLinearColor(0.55f, 0.45f, 0.35f), 1.f, float(RndS() * 0.2), -6.0, 1.5);
		}
		for (int32 I = 0; I < 25; ++I)
		{
			EmitSpark(Pos, 18.0, 0.5, 0.5f, -12.0);
		}
		PopLight(Pos, 0.15, 40000.f, FLinearColor(1.f, 0.8f, 0.5f));
	}
	else if (Type == EDFWeapon::Flashbang)
	{
		// Effect_FlashBang
		for (int32 I = 0; I < 3; ++I)
		{
			SpawnPart(PK_Flash, Pos + RandUnit() * 0.3, FVector::ZeroVector, 0.2, 10.f, 10.f, 1.f, 0, FLinearColor::White, 2.5f);
		}
		for (int32 I = 0; I < 12; ++I)
		{
			EmitSpark(Pos, 15.0, 0.6, 0.4f, -10.0);
		}
		SpawnPart(PK_Smoke, Pos, FVector(0, 0.5, 0), 2.5, 1.0f, 2.0f, 0.5f, 0, FLinearColor(0.9f, 0.9f, 0.9f), 1.f, 0.05f, 0.3, 0.8);
		PopLight(Pos, 0.3, 120000.f, FLinearColor::White);
	}
}

int32 ADFEffects::CountLiveSmoke() const
{
	int32 N = 0;
	for (const FPart& P : Parts[PK_Smoke])
	{
		N += P.bLive ? 1 : 0;
	}
	return N;
}
