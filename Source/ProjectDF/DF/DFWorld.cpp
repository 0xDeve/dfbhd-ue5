#include "DFWorld.h"
#include "DFMath.h"
#include "DFGameMode.h"
#include "DFData.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFWorld, Log, All);

namespace
{
	const FName BuiltTag(TEXT("DFBuilt"));

	constexpr uint32 LayoutSeed = 1993;

	// ---- lighting / exposure tuning (manual exposure, see report)
	constexpr float SunLux = 8.0f;           // directional light intensity (lux)
	constexpr float SkyLightScale = 1.0f;    // real-time captured sky light intensity
	constexpr float FogLuminance = 0.6f;     // scale on the horizon colour for the fog inscattering
	constexpr float ExposureBias = -0.4f;    // AEM_Manual exposure compensation (EV)

	/** Query-only collision that blocks ECC_Visibility (the pawn's scene queries and hitscan). */
	void MakeSolidMap(UStaticMeshComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionObjectType(ECC_WorldStatic);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	FVector ReadVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Field, A) && A->Num() >= 3)
		{
			return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}

	struct FPutOpts
	{
		TOptional<double> MaxZ, MinZ, Y;
	};
}

ADFWorld::ADFWorld()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

ADFWorld* ADFWorld::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADFWorld> It(World); It; ++It)
	{
		ADFWorld* W = *It;
		if (W && !W->IsTemplate())
		{
			if (!W->bBuilt && World->IsGameWorld())
			{
				W->Build();
			}
			return W;
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
	UClass* Class = GM && GM->WorldClass ? GM->WorldClass.Get() : ADFWorld::StaticClass();
	ADFWorld* W = World->SpawnActor<ADFWorld>(Class, FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (W && !W->bBuilt)
	{
		W->Build();
	}
	return W;
}

void ADFWorld::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		Build();
	}
}

void ADFWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UWorld* World = GetWorld();
	if (IsTemplate() || !World || World->IsGameWorld())
	{
		return;   // runtime: BeginPlay builds
	}
	Build();
}

void ADFWorld::Build()
{
	if (IsTemplate() || !GetWorld())
	{
		return;
	}
	const bool bEditorPreview = !GetWorld()->IsGameWorld();

	// ---- idempotent: destroy what a previous build (or a construction-script run) left behind
	{
		TArray<UActorComponent*> Old;
		GetComponents(Old);
		for (UActorComponent* C : Old)
		{
			if (C && C->ComponentHasTag(BuiltTag))
			{
				C->DestroyComponent();
			}
		}
		for (UActorComponent* C : BuiltComponents)
		{
			if (IsValid(C) && !C->IsBeingDestroyed())
			{
				C->DestroyComponent();
			}
		}
		BuiltComponents.Reset();
	}
	Boxes.Reset();
	GroundComps.Reset();
	GridOriginM = FVector2D::ZeroVector;
	bBuilt = true;
	bMap = false;
	LoadedMapKey.Reset();
	bPreview = bEditorPreview;
	BoundsMin = FVector2D(-PlayHalf, -PlayHalf);
	BoundsMax = FVector2D(PlayHalf, PlayHalf);
	Spawn = FDFSpawnPoint{ FVector(-1.0, 0.0, 4.0), 0.0 };
	SpawnList.Reset();
	SpawnBase = 0;

	if (!GetRootComponent())
	{
		return;
	}

	BuildEnvironment();

	const FString Key = ResolveMapKey();
	if (!Key.IsEmpty())
	{
		if (BuildMap(Key))
		{
			return;
		}
		UE_LOG(LogDFWorld, Warning, TEXT("Map '%s' failed to load; building the web compound instead"), *Key);
	}
	BuildCompound();
}

void ADFWorld::GetGroundComponents(TArray<UPrimitiveComponent*>& Out) const
{
	Out.Reset();
	for (const TObjectPtr<UPrimitiveComponent>& C : bMap ? TerrainComps : GroundComps)
	{
		if (IsValid(C))
		{
			Out.Add(C);
		}
	}
}

void ADFWorld::RegisterBuilt(UActorComponent* C, USceneComponent* AttachTo)
{
	C->ComponentTags.Add(BuiltTag);
	C->CreationMethod = bPreview ? EComponentCreationMethod::UserConstructionScript : EComponentCreationMethod::Instance;
	if (USceneComponent* SC = Cast<USceneComponent>(C))
	{
		SC->SetupAttachment(AttachTo);
	}
	BuiltComponents.Add(C);
}

void ADFWorld::BuildEnvironment()
{
	USceneComponent* Root = GetRootComponent();
	auto Register = [this](UActorComponent* C, USceneComponent* AttachTo) { RegisterBuilt(C, AttachTo); };

	// ---------------------------------------------------------------- lights / sky / fog / post
	const FVector ToSun = DF::ToUEDir(FVector(0.45, 0.8, 0.35).GetSafeNormal());

	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this, TEXT("DFSun"));
	Sun->SetMobility(EComponentMobility::Movable);
	Register(Sun, Root);
	Sun->SetRelativeRotation(FRotationMatrix::MakeFromX(-ToSun).Rotator());
	Sun->SetLightColor(FLinearColor::FromSRGBColor(FColor(0xff, 0xf0, 0xd0)));
	Sun->SetIntensity(SunLux);
	Sun->SetCastShadows(true);
	Sun->SetDynamicShadowDistanceMovableLight(9000.f);
	Sun->SetDynamicShadowCascades(4);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);
	Sun->RegisterComponent();

	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(this, TEXT("DFSkyAtmosphere"));
	Atmo->SetMobility(EComponentMobility::Static);
	Register(Atmo, Root);
	Atmo->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
	Atmo->GroundAlbedo = FColor(0xc2, 0xa5, 0x6f);
	Atmo->RegisterComponent();

	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this, TEXT("DFSkyLight"));
	Sky->SetMobility(EComponentMobility::Movable);
	Register(Sky, Root);
	Sky->SourceType = SLS_CapturedScene;
	Sky->SetRealTimeCapture(true);
	Sky->SetIntensity(SkyLightScale);
	Sky->SetLowerHemisphereColor(FLinearColor(0.42f, 0.33f, 0.2f));
	Sky->RegisterComponent();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this, TEXT("DFFog"));
	Fog->SetMobility(EComponentMobility::Movable);
	Register(Fog, Root);
	// web: FogExp2 density 0.0125/m, colour 0xe3cfa6. UE: FogDensity/1000 per cm in base 2; 0.15 gives about the same haze scale.
	Fog->SetFogDensity(0.15f);
	Fog->SetFogHeightFalloff(0.02f);
	Fog->SetFogMaxOpacity(1.0f);
	Fog->SetStartDistance(0.f);
	Fog->SetFogInscatteringColor(FLinearColor::FromSRGBColor(FColor(0xe3, 0xcf, 0xa6)) * FogLuminance);
	Fog->SetSkyAtmosphereAmbientContributionColorScale(FLinearColor(0.3f, 0.3f, 0.3f));
	Fog->bEnableVolumetricFog = false;
	Fog->RegisterComponent();

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this, TEXT("DFPostProcess"));
	PP->SetMobility(EComponentMobility::Movable);
	Register(PP, Root);
	PP->bUnbound = true;
	PP->BlendWeight = 1.f;
	PP->Priority = 100.f;
	FPostProcessSettings& PS = PP->Settings;
	PS.bOverride_AutoExposureMethod = true;
	PS.AutoExposureMethod = AEM_Manual;
	PS.bOverride_AutoExposureBias = true;
	PS.AutoExposureBias = ExposureBias;
	// manual exposure must not also apply the physical camera (ISO 100, 1/60 s, f/4 ~ EV100 10 would turn an 8 lux sun black)
	PS.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PS.AutoExposureApplyPhysicalCameraExposure = false;
	PS.bOverride_MotionBlurAmount = true;
	PS.MotionBlurAmount = 0.f;
	PS.bOverride_VignetteIntensity = true;
	PS.VignetteIntensity = 0.f;
	PS.bOverride_BloomIntensity = true;
	PS.BloomIntensity = 0.25f;
	PP->RegisterComponent();

}

void ADFWorld::BuildCompound()
{
	USceneComponent* Root = GetRootComponent();
	auto Register = [this](UActorComponent* C, USceneComponent* AttachTo) { RegisterBuilt(C, AttachTo); };

	// ---------------------------------------------------------------- ground + street
	TSet<FString> Warned;
	auto MakeMesh = [&](const FString& Key, const FName& Name, bool bShadows) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
		C->SetMobility(EComponentMobility::Static);
		Register(C, Root);
		const TObjectPtr<UStaticMesh>* Found = CompoundMeshes.Find(FName(*Key));
		if (Found && *Found)
		{
			C->SetStaticMesh(*Found);
		}
		else if (!Warned.Contains(Key))
		{
			Warned.Add(Key);
			UE_LOG(LogDFWorld, Warning, TEXT("CompoundMeshes has no mesh for key '%s'; prop is invisible (collider box is still built)"), *Key);
		}
		C->SetCastShadow(bShadows);
		return C;
	};
	auto MakeSolid = [](UStaticMeshComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionObjectType(ECC_WorldStatic);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	};

	UStaticMeshComponent* Ground = MakeMesh(TEXT("ground"), TEXT("DFGround"), false);
	MakeSolid(Ground);
	Ground->SetRelativeTransform(FTransform::Identity);
	Ground->RegisterComponent();
	GroundComps.Add(Ground);

	UStaticMeshComponent* Street = MakeMesh(TEXT("street"), TEXT("DFStreet"), false);
	Street->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Street->SetRelativeTransform(FTransform::Identity);   // quad already at three y = 0.01
	Street->RegisterComponent();
	GroundComps.Add(Street);

	// ---------------------------------------------------------------- props (world.js put / createProp)
	DF::FMulberry32 Rand(LayoutSeed);
	const FDFData& Data = FDFData::Get();
	int32 PropIndex = 0;

	auto Put = [&](const TCHAR* KeyStr, double X, double Z, int32 Rot, const FPutOpts& Opts = FPutOpts()) -> FDFBox
	{
		const FString Key(KeyStr);
		const FDFModelData* M = Data.FindModel(FName(*Key));
		if (!M)
		{
			UE_LOG(LogDFWorld, Warning, TEXT("No model data for '%s' (dfdata.json missing?) - prop skipped"), *Key);
			return FDFBox();
		}
		const FVector* GroundPt = M->FindUserpoint(FName(TEXT("ground")));
		const double GroundY = GroundPt ? GroundPt->Y : M->BBoxMin.Y;
		// createProp: local box = bbox shifted by -groundY; the mesh sits at (0, -groundY, 0) inside the group
		const FVector LMin(M->BBoxMin.X, M->BBoxMin.Y - GroundY, M->BBoxMin.Z);
		const FVector LMax(M->BBoxMax.X, M->BBoxMax.Y - GroundY, M->BBoxMax.Z);
		const FQuat Q(FVector(0, 1, 0), Rot * UE_DOUBLE_PI / 2);
		// rotated box relative to origin at (0,0,0)
		FVector WMin(TNumericLimits<double>::Max()), WMax(-TNumericLimits<double>::Max());
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector Corner((I & 1) ? LMax.X : LMin.X, (I & 2) ? LMax.Y : LMin.Y, (I & 4) ? LMax.Z : LMin.Z);
			const FVector R = Q.RotateVector(Corner);
			WMin = FVector(FMath::Min(WMin.X, R.X), FMath::Min(WMin.Y, R.Y), FMath::Min(WMin.Z, R.Z));
			WMax = FVector(FMath::Max(WMax.X, R.X), FMath::Max(WMax.Y, R.Y), FMath::Max(WMax.Z, R.Z));
		}
		const double Cx = (WMin.X + WMax.X) / 2, Cz = (WMin.Z + WMax.Z) / 2;
		double Pz = Z - Cz;
		if (Opts.MaxZ.IsSet()) Pz = Opts.MaxZ.GetValue() - WMax.Z;
		if (Opts.MinZ.IsSet()) Pz = Opts.MinZ.GetValue() - WMin.Z;
		const FVector Pos(X - Cx, Opts.Y.IsSet() ? Opts.Y.GetValue() : 0.0, Pz);

		UStaticMeshComponent* C = MakeMesh(Key, *FString::Printf(TEXT("DFProp_%03d_%s"), PropIndex++, *Key), true);
		MakeSolid(C);
		const FVector MeshPos = Pos + Q.RotateVector(FVector(0, -GroundY, 0));
		C->SetRelativeTransform(DF::ToUE(Q, MeshPos));
		C->RegisterComponent();

		const FDFBox Box(WMin + Pos, WMax + Pos);
		Boxes.Add(Box);
		return Box;
	};
	auto Free = [&](double X, double Z, double R) -> bool
	{
		for (const FDFBox& B : Boxes)
		{
			if (X > B.Min.X - R && X < B.Max.X + R && Z > B.Min.Z - R && Z < B.Max.Z + R)
			{
				return false;
			}
		}
		return FMath::Sqrt(FMath::Square(X - 0.0) + FMath::Square(Z - 6.0)) > 3.0;
	};
	auto MaxZ = [](double V) { FPutOpts O; O.MaxZ = V; return O; };
	auto MinZ = [](double V) { FPutOpts O; O.MinZ = V; return O; };
	auto YOpt = [](double V) { FPutOpts O; O.Y = V; return O; };

	const double N = -8, Sd = 8;   // street edges
	// main pair of buildings facing each other across the street
	Put(TEXT("mogbld1"), -10, 0, 0, MaxZ(N));
	Put(TEXT("mogbld2"), -6, 0, 2, MinZ(Sd));
	// street-side huts
	Put(TEXT("moghut1"), 14, 0, 0, MaxZ(N - 0.5));
	Put(TEXT("moghut2"), 36, 0, 0, MaxZ(N - 1));
	Put(TEXT("dhut01"), -38, 0, 0, MaxZ(N - 0.5));
	Put(TEXT("moghut2"), 24, 0, 2, MinZ(Sd + 1));
	Put(TEXT("moghut1"), -32, 0, 2, MinZ(Sd + 0.5));
	Put(TEXT("dhut01"), 48, 0, 2, MinZ(Sd + 0.5));
	// second row
	Put(TEXT("moghut1"), -22, -34, 1);
	Put(TEXT("dhut01"), 16, -42, 3);
	Put(TEXT("moghut2"), -58, -26, 0);
	Put(TEXT("mogbld1"), 66, -34, 1);
	Put(TEXT("mogbld2"), 56, 38, 3);
	Put(TEXT("moghut2"), -52, 34, 2);
	Put(TEXT("moghut1"), 0, 40, 0);
	Put(TEXT("dhut01"), -80, 4, 1);
	Put(TEXT("moghut1"), 86, 6, 3);
	// vehicles on the street
	Put(TEXT("ncar1"), 5, 1.2, 1);
	Put(TEXT("ncar1"), 46, -2.5, 1);
	// cover: crate stack, crate row, barrels, tires
	const FDFBox C2 = Put(TEXT("wcrate2"), 3, -6.2, 0);
	Put(TEXT("wcrate1"), 3.1, -6.2, 0, YOpt(C2.Max.Y));
	Put(TEXT("wcrate2"), 5.4, -6.4, 0);
	Put(TEXT("wcrate1"), 12, 6.4, 0); Put(TEXT("wcrate1"), 13, 6.6, 1); Put(TEXT("wcrate2"), 15.2, 6.5, 0);
	Put(TEXT("wcrate2"), -22, -6.6, 0); Put(TEXT("wcrate1"), -22, -6.6, 0, YOpt(2));
	Put(TEXT("tire01"), -14, 6.4, 0); Put(TEXT("tire01"), 27, -6.8, 0); Put(TEXT("tire01"), -45, 5, 0);
	for (int32 I = 0; I < 6; ++I)
	{
		const double Jitter = Rand() * 0.2;
		Put(TEXT("mbarrel1"), -20 + (I % 3) * 1.15 + Jitter, 6.3 + (I / 3) * 1.15, 0);
	}
	for (int32 I = 0; I < 4; ++I)
	{
		Put(TEXT("mbarrel1"), 22 + (I % 2) * 1.15, -6.4 - (I / 2) * 1.15, 0);
	}
	// seeded scatter of loose cover around the compound
	static const TCHAR* const Scatter[6] = { TEXT("mbarrel1"), TEXT("wcrate1"), TEXT("wcrate2"), TEXT("tire01"), TEXT("mbarrel1"), TEXT("wcrate1") };
	for (int32 Tries = 0, Cnt = 0; Tries < 400 && Cnt < 12; ++Tries)
	{
		const double X = (Rand() * 2 - 1) * 70;
		const double Z = (Rand() * 2 - 1) * 50;
		if (!Free(X, Z, 2.2)) continue;
		const int32 Rot = int32(Rand() * 4);
		Put(Scatter[Cnt % 6], X, Z, Rot);
		++Cnt;
	}

	UE_LOG(LogDFWorld, Log, TEXT("ADFWorld built: %d props/colliders (%s)"), Boxes.Num(), bPreview ? TEXT("editor preview") : TEXT("game"));
}

bool ADFWorld::CastRay(const FVector& Origin, const FVector& Dir, double MaxDist, double& OutT, FVector& OutPoint, FVector& OutNormal) const
{
	bool bHit = false;
	double BestT = MaxDist;
	FVector BestN(0, 1, 0);

	// flat ground at three y = 0 (main.js castRay), robust even when the ground mesh has no collision (compound only)
	if (!bMap && Dir.Y < -1e-5 && Origin.Y > 0)
	{
		const double T = -Origin.Y / Dir.Y;
		if (T < MaxDist)
		{
			bHit = true;
			BestT = T;
		}
	}

	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFCastRay), /*bTraceComplex=*/true);
		const FVector Start = DF::ToUE(Origin);
		const FVector End = DF::ToUE(Origin + Dir * MaxDist);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			const double T = double(Hit.Distance) / DF::M2CM;
			if (!bHit || T < BestT)
			{
				bHit = true;
				BestT = T;
				BestN = DF::FromUEDir(FVector(Hit.ImpactNormal)).GetSafeNormal();
			}
		}
	}
	if (!bHit)
	{
		return false;
	}
	if (BestN.Dot(Dir) > 0)
	{
		BestN = -BestN;
	}
	OutT = BestT;
	OutPoint = Origin + Dir * BestT;
	OutNormal = BestN;
	return true;
}

// ------------------------------------------------------------------ map (Docs/MAPS.md section 6)

FString ADFWorld::ResolveMapKey() const
{
	auto Exists = [](const FString& K)
	{
		return !K.IsEmpty() && FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("DF/Data/Maps") / (K + TEXT(".json")));
	};
	FString Key = MapKey;
	if (Key.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("DFMap="), Key);
	}
	if (!Key.IsEmpty())
	{
		if (!Exists(Key))
		{
			UE_LOG(LogDFWorld, Warning, TEXT("Map JSON for '%s' not found in Content/DF/Data/Maps"), *Key);
			return FString();
		}
		return Key;
	}
	FString Level = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (GetWorld())
	{
		Level.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	}
	if (Level.RemoveFromStart(TEXT("DF_")) && Exists(Level))
	{
		return Level;
	}
	return FString();
}

bool ADFWorld::BuildMap(const FString& Key)
{
	FString Text;
	const FString JsonPath = FPaths::ProjectContentDir() / TEXT("DF/Data/Maps") / (Key + TEXT(".json"));
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		UE_LOG(LogDFWorld, Warning, TEXT("Cannot read %s"), *JsonPath);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogDFWorld, Warning, TEXT("%s is not valid JSON"), *JsonPath);
		return false;
	}
	FString AssetDir;
	if (!Root->TryGetStringField(TEXT("assetDir"), AssetDir) || AssetDir.IsEmpty())
	{
		UE_LOG(LogDFWorld, Warning, TEXT("%s has no assetDir"), *JsonPath);
		return false;
	}
	USceneComponent* SceneRoot = GetRootComponent();

	// ---- meshes (each distinct name loaded once; a missing mesh is warned about once and skipped)
	TMap<FString, UStaticMesh*> Meshes;
	TSet<FString> Missing;
	auto FindMesh = [&](const FString& Name) -> UStaticMesh*
	{
		if (UStaticMesh** Found = Meshes.Find(Name))
		{
			return *Found;
		}
		const FString Path = FString::Printf(TEXT("%s/Meshes/%s.%s"), *AssetDir, *Name, *Name);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!M)
		{
			Missing.Add(Name);
			UE_LOG(LogDFWorld, Warning, TEXT("Map mesh %s not found; skipped"), *Path);
		}
		Meshes.Add(Name, M);
		return M;
	};
	int32 Count = 0, Solid = 0;
	bool bPlacingTerrain = false;
	auto Place = [&](const FString& Name, const FTransform& Xf, bool bCollide, bool bShadow)
	{
		UStaticMesh* M = FindMesh(Name);
		if (!M)
		{
			return;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("DFMap_%05d"), Count));
		C->SetMobility(EComponentMobility::Static);
		RegisterBuilt(C, SceneRoot);
		C->SetStaticMesh(M);
		C->SetCastShadow(bShadow);
		if (bCollide)
		{
			MakeSolidMap(C);
			++Solid;
		}
		else
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		C->SetRelativeTransform(Xf);
		C->RegisterComponent();
		if (bPlacingTerrain)
		{
			TerrainComps.Add(C);
		}
		++Count;
	};

	// 50 m map-grid origin (HUD.md 10.5): the "Map Centerpoint" marker (item 2043; the JSON item ids carry a 100000 prefix). Engine axes:
	// x = -three.z, y = -three.x
	GridOriginM = FVector2D::ZeroVector;
	{
		const TArray<TSharedPtr<FJsonValue>>* Markers = nullptr;
		if (Root->TryGetArrayField(TEXT("markers"), Markers))
		{
			for (const TSharedPtr<FJsonValue>& V : *Markers)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
				if (O.IsValid() && int32(O->GetNumberField(TEXT("item"))) == 102043 && O->TryGetArrayField(TEXT("pos"), Pos) && Pos->Num() >= 3)
				{
					const double Ex = -(*Pos)[2]->AsNumber(), Ey = -(*Pos)[0]->AsNumber();
					GridOriginM = FVector2D(50.0 * double(int64(Ex / 50.0)), 50.0 * double(int64(Ey / 50.0)));
					break;
				}
			}
		}
	}
	TerrainComps.Reset();
	HfData.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("terrain"), Arr))
	{
		bPlacingTerrain = true;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FString Name;
			if (O.IsValid() && O->TryGetStringField(TEXT("mesh"), Name))
			{
				Place(Name, FTransform::Identity, true, false);
			}
		}
	}
	bPlacingTerrain = false;
	const int32 TerrainCount = Count;
	if (Root->TryGetArrayField(TEXT("objects"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FString Name;
			if (!O.IsValid() || !O->TryGetStringField(TEXT("mesh"), Name))
			{
				continue;
			}
			bool bCollide = false;
			O->TryGetBoolField(TEXT("collide"), bCollide);
			double Yaw = 0.0;
			O->TryGetNumberField(TEXT("yaw"), Yaw);
			Place(Name, DF::ToUE(FQuat(FVector(0, 1, 0), Yaw), ReadVec3(O, TEXT("pos"))), bCollide, true);
		}
	}

	// ---- heightfield: the game's exact terrain function (optional)
	const TSharedPtr<FJsonObject>* Hf = nullptr;
	if (Root->TryGetObjectField(TEXT("heightfield"), Hf))
	{
		FString File;
		double Size = 0, Sector = 512, Unit = 1.0 / 256.0;
		(*Hf)->TryGetStringField(TEXT("file"), File);
		(*Hf)->TryGetNumberField(TEXT("size"), Size);
		(*Hf)->TryGetNumberField(TEXT("sectorSize"), Sector);
		(*Hf)->TryGetNumberField(TEXT("unit"), Unit);
		const TArray<TSharedPtr<FJsonValue>>* Org = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Sec = nullptr;
		TArray<uint8> Bytes;
		const int32 N = int32(Size);
		if (!File.IsEmpty() && N > 0 && (*Hf)->TryGetArrayField(TEXT("origin"), Org) && Org->Num() >= 2 && (*Hf)->TryGetArrayField(TEXT("sectors"), Sec) && Sec->Num() >= 16
			&& FFileHelper::LoadFileToArray(Bytes, *(FPaths::GetPath(JsonPath) / File)) && Bytes.Num() >= N * N * 2)
		{
			HfSize = N; HfSector = int32(Sector); HfUnit = Unit;
			HfOrigin[0] = int32((*Org)[0]->AsNumber()); HfOrigin[1] = int32((*Org)[1]->AsNumber());
			for (int32 Sy = 0; Sy < 16; ++Sy)
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = (*Sec)[Sy]->AsArray();
				for (int32 Sx = 0; Sx < 16; ++Sx)
				{
					HfSectors[Sy][Sx] = Sx < Row.Num() ? int32(Row[Sx]->AsNumber()) : 0;
				}
			}
			HfData.SetNumUninitialized(N * N);
			for (int32 I = 0; I < N * N; ++I)
			{
				HfData[I] = uint16(Bytes[I * 2] | (Bytes[I * 2 + 1] << 8));
			}
		}
		else
		{
			UE_LOG(LogDFWorld, Warning, TEXT("Map '%s': heightfield block present but unusable; using the terrain mesh for ground height"), *Key);
		}
	}

	// ---- bounds
	const TSharedPtr<FJsonObject>* Bounds = nullptr;
	if (Root->TryGetObjectField(TEXT("bounds"), Bounds))
	{
		const TArray<TSharedPtr<FJsonValue>>* Mn = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Mx = nullptr;
		if ((*Bounds)->TryGetArrayField(TEXT("min"), Mn) && (*Bounds)->TryGetArrayField(TEXT("max"), Mx) && Mn->Num() >= 2 && Mx->Num() >= 2)
		{
			BoundsMin = FVector2D((*Mn)[0]->AsNumber(), (*Mn)[1]->AsNumber());
			BoundsMax = FVector2D((*Mx)[0]->AsNumber(), (*Mx)[1]->AsNumber());
		}
	}

	bMap = true;
	LoadedMapKey = Key;

	// ---- spawn: team start (-DFTeam=red|blue, -DFSpawn=<i>), else the player start, else the bounds centre on the ground
	FString Team = TEXT("red");
	FParse::Value(FCommandLine::Get(), TEXT("DFTeam="), Team);
	const TCHAR* TeamList = Team.ToLower() == TEXT("blue") ? TEXT("blue") : TEXT("red");
	int32 SpawnIndex = 0;
	FParse::Value(FCommandLine::Get(), TEXT("DFSpawn="), SpawnIndex);
	bool bSpawnFound = false;
	const TSharedPtr<FJsonObject>* Spawns = nullptr;
	if (Root->TryGetObjectField(TEXT("spawns"), Spawns))
	{
		const TCHAR* Lists[2] = { TeamList, TEXT("player") };
		for (const TCHAR* List : Lists)
		{
			const TArray<TSharedPtr<FJsonValue>>* S = nullptr;
			if ((*Spawns)->TryGetArrayField(List, S) && S->Num() > 0)
			{
				const int32 N = S->Num();
				const TSharedPtr<FJsonObject> O = (*S)[((SpawnIndex % N) + N) % N]->AsObject();
				if (O.IsValid())
				{
					Spawn.Pos = ReadVec3(O, TEXT("pos"));
					O->TryGetNumberField(TEXT("yaw"), Spawn.Yaw);
					SpawnList.Reset();
					for (const TSharedPtr<FJsonValue>& V : *S)
					{
						FDFSpawnPoint Pt;
						if (const TSharedPtr<FJsonObject> E = V->AsObject())
						{
							Pt.Pos = ReadVec3(E, TEXT("pos"));
							E->TryGetNumberField(TEXT("yaw"), Pt.Yaw);
						}
						SpawnList.Add(Pt);
					}
					SpawnBase = ((SpawnIndex % N) + N) % N;
					bSpawnFound = true;
					break;
				}
			}
		}
	}
	if (!bSpawnFound)
	{
		Spawn.Pos = FVector((BoundsMin.X + BoundsMax.X) * 0.5, 0.0, (BoundsMin.Y + BoundsMax.Y) * 0.5);
		Spawn.Yaw = 0.0;
		const double G = GroundAt(FVector(Spawn.Pos.X, 2000.0, Spawn.Pos.Z), 0.0);
		Spawn.Pos.Y = G > -1e3 ? G : 0.0;
	}

	// ---- fog: the web's 1000 m (FogExp2) would be soup at UE scale; keep ~500 m readable and flat in height [DEVIATION]
	if (UExponentialHeightFogComponent* Fog = FindComponentByClass<UExponentialHeightFogComponent>())
	{
		Fog->SetFogDensity(0.02f);
		Fog->SetFogHeightFalloff(0.001f);
		Fog->SetRelativeLocation(FVector(0.0, 0.0, DF::M2CM * Spawn.Pos.Y));
	}

	UE_LOG(LogDFWorld, Log, TEXT("ADFWorld map '%s': %d components (%d terrain tiles, %d objects, %d with collision), %d missing meshes, spawn (%.2f, %.2f, %.2f) yaw %.2f (%s)"),
		*Key, Count, TerrainCount, Count - TerrainCount, Solid, Missing.Num(), Spawn.Pos.X, Spawn.Pos.Y, Spawn.Pos.Z, Spawn.Yaw,
		bPreview ? TEXT("editor preview") : TEXT("game"));
	return true;
}

FVector ADFWorld::GetSpawn() const
{
	return Spawn.Pos;
}

double ADFWorld::GetSpawnYaw() const
{
	return Spawn.Yaw;
}

void ADFWorld::GetSpawnFor(int32 PlayerIndex, FVector& OutPos, double& OutYaw) const
{
	if (bMap && SpawnList.Num() > 1)
	{
		const FDFSpawnPoint& Pt = SpawnList[(SpawnBase + FMath::Max(PlayerIndex, 0)) % SpawnList.Num()];
		OutPos = Pt.Pos;
		OutYaw = Pt.Yaw;
		return;
	}
	OutPos = Spawn.Pos;
	OutYaw = Spawn.Yaw;
	OutPos.X += 1.5 * FMath::Max(PlayerIndex, 0);
}

// ------------------------------------------------------------------ collision queries

void ADFWorld::PushOut(FVector& Pos, double Radius, double Height) const
{
	if (!bMap)
	{
		// main.js pushOut (compound)
		const double Top = Pos.Y + Height, Feet = Pos.Y + StepUp;   // can step up 0.5 m
		for (int32 It = 0; It < 2; ++It)
		{
			for (const FDFBox& B : Boxes)
			{
				if (B.Max.Y <= Feet || B.Min.Y >= Top) continue;
				if (Pos.X < B.Min.X - Radius || Pos.X > B.Max.X + Radius || Pos.Z < B.Min.Z - Radius || Pos.Z > B.Max.Z + Radius) continue;
				const double Cx = DF::Clamp(Pos.X, B.Min.X, B.Max.X), Cz = DF::Clamp(Pos.Z, B.Min.Z, B.Max.Z);
				const double Dx = Pos.X - Cx, Dz = Pos.Z - Cz;
				const double D2 = Dx * Dx + Dz * Dz;
				if (D2 >= Radius * Radius) continue;
				if (D2 > 1e-8)
				{
					const double D = FMath::Sqrt(D2), K = (Radius - D) / D;
					Pos.X += Dx * K; Pos.Z += Dz * K;
				}
				else   // center inside the box: exit through the nearest face
				{
					const double L = Pos.X - B.Min.X, R = B.Max.X - Pos.X, N = Pos.Z - B.Min.Z, F = B.Max.Z - Pos.Z;
					const double M = FMath::Min(FMath::Min(L, R), FMath::Min(N, F));
					if (M == L) Pos.X = B.Min.X - Radius;
					else if (M == R) Pos.X = B.Max.X + Radius;
					else if (M == N) Pos.Z = B.Min.Z - Radius;
					else Pos.Z = B.Max.Z + Radius;
				}
			}
		}
		return;
	}

	// map: capsule of Radius, bottom at feet + StepUp, at least two radii tall, MTD against what it overlaps
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Bottom = StepUp, Top = FMath::Max(Height, StepUp + 2.0 * Radius);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(float(Radius * DF::M2CM), float((Top - Bottom) * 0.5 * DF::M2CM));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFPushOut), /*bTraceComplex=*/true);
	for (int32 It = 0; It < 3; ++It)
	{
		TArray<FOverlapResult> Overlaps;
		const FVector Centre = DF::ToUE(FVector(Pos.X, Pos.Y + (Bottom + Top) * 0.5, Pos.Z));
		if (!World->OverlapMultiByChannel(Overlaps, Centre, FQuat::Identity, ECC_Visibility, Shape, Params))
		{
			break;
		}
		bool bMoved = false;
		for (const FOverlapResult& O : Overlaps)
		{
			UPrimitiveComponent* Comp = O.GetComponent();
			if (!Comp)
			{
				continue;
			}
			FMTDResult Mtd;
			const FVector At = DF::ToUE(FVector(Pos.X, Pos.Y + (Bottom + Top) * 0.5, Pos.Z));
			if (!Comp->ComputePenetration(Mtd, Shape, At, FQuat::Identity) || Mtd.Distance <= 0.f)
			{
				continue;
			}
			const FVector N = DF::FromUEDir(FVector(Mtd.Direction));   // three space
			const double H = FMath::Sqrt(N.X * N.X + N.Z * N.Z);
			if (H < 0.5)
			{
				continue;   // floor / ceiling like: GroundAt handles it
			}
			const double Move = FMath::Min(double(Mtd.Distance) / DF::M2CM / H, 0.5);
			Pos.X += N.X / H * Move;
			Pos.Z += N.Z / H * Move;
			bMoved = true;
		}
		if (!bMoved)
		{
			break;
		}
	}
}

double ADFWorld::HfSample(int32 Xi, int32 Yi) const
{
	auto FloorDiv = [](int32 A, int32 B) { return FMath::FloorToInt(double(A) / double(B)); };
	const int32 Sx = FMath::Clamp(FloorDiv(Xi, HfSector) - HfOrigin[0], 0, 15);
	const int32 Sy = FMath::Clamp(FloorDiv(Yi, HfSector) - HfOrigin[1], 0, 15);
	const int32 T = HfSectors[Sy][Sx] - 1;
	if (T < 0)
	{
		return 0.0;
	}
	const int32 Mx = ((Xi % HfSector) + HfSector) % HfSector, My = ((Yi % HfSector) + HfSector) % HfSector;
	const int32 Row = My + ((T & 1) ? HfSector : 0), Col = Mx + ((T & 2) ? HfSector : 0);
	return double(HfData[Row * HfSize + Col]) * HfUnit;
}

bool ADFWorld::TerrainHeight(double X, double Z, double& OutY) const
{
	if (HfData.Num() == 0)
	{
		return false;
	}
	// engine x = -Z, terrain Y = -engine y = X (three space)
	const double Ex = -Z, Ey = X;
	const double Fx = FMath::Floor(Ex), Fy = FMath::Floor(Ey);
	const double Ax = Ex - Fx, Ay = Ey - Fy;
	const int32 Xi = int32(Fx), Yi = int32(Fy);
	const double H00 = HfSample(Xi, Yi), H10 = HfSample(Xi + 1, Yi), H01 = HfSample(Xi, Yi + 1), H11 = HfSample(Xi + 1, Yi + 1);
	OutY = FMath::Lerp(FMath::Lerp(H00, H10, Ax), FMath::Lerp(H01, H11, Ax), Ay);
	return true;
}

double ADFWorld::GroundAt(const FVector& Pos, double FootRadius) const
{
	const double Lim = Pos.Y + GroundProbe;
	if (!bMap)
	{
		// main.js groundAt (compound)
		double G = 0;
		for (const FDFBox& B : Boxes)
		{
			if (B.Max.Y > G && B.Max.Y <= Lim && Pos.X > B.Min.X - FootRadius && Pos.X < B.Max.X + FootRadius && Pos.Z > B.Min.Z - FootRadius && Pos.Z < B.Max.Z + FootRadius)
			{
				G = B.Max.Y;
			}
		}
		return G;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return -1e4;
	}
	constexpr double Reach = 3000.0;   // m below the probe
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFGroundAt), /*bTraceComplex=*/true);
	// terrain: the game's point query (always counts, no probe limit); the scene queries then only see props
	double Terrain = -1e4;
	const bool bHasTerrain = TerrainHeight(Pos.X, Pos.Z, Terrain);
	if (bHasTerrain)
	{
		TArray<UPrimitiveComponent*> Ignored;
		for (UPrimitiveComponent* C : TerrainComps)
		{
			Ignored.Add(C);
		}
		Params.AddIgnoredComponents(Ignored);
	}
	FHitResult Hit;
	if (FootRadius > 1e-3)
	{
		const FVector Start = DF::ToUE(FVector(Pos.X, Lim + FootRadius, Pos.Z));
		const FVector End = DF::ToUE(FVector(Pos.X, Lim - Reach, Pos.Z));
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(float(FootRadius * DF::M2CM)), Params)
			&& !Hit.bStartPenetrating)
		{
			return FMath::Max(Terrain, double(Hit.ImpactPoint.Z) / DF::M2CM);
		}
	}
	// start-penetrating sweep (inside a wall next to the feet) or zero radius: centre line
	if (World->LineTraceSingleByChannel(Hit, DF::ToUE(FVector(Pos.X, Lim, Pos.Z)), DF::ToUE(FVector(Pos.X, Lim - Reach, Pos.Z)), ECC_Visibility, Params))
	{
		return FMath::Max(Terrain, double(Hit.ImpactPoint.Z) / DF::M2CM);
	}
	return Terrain;
}

bool ADFWorld::SphereBlocked(const FVector& Pt, double R) const
{
	if (!bMap)
	{
		for (const FDFBox& B : Boxes)
		{
			if (Pt.X > B.Min.X - R && Pt.X < B.Max.X + R && Pt.Y > B.Min.Y - R && Pt.Y < B.Max.Y + R &&
				Pt.Z > B.Min.Z - R && Pt.Z < B.Max.Z + R) return true;
		}
		return false;
	}
	const UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFSphereBlocked), /*bTraceComplex=*/true);
	return World && World->OverlapAnyTestByChannel(DF::ToUE(Pt), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(float(R * DF::M2CM)), Params);
}

double ADFWorld::ClipCamera(const FVector& Origin, const FVector& Dir, double MaxDist, double Pad) const
{
	if (!bMap)
	{
		double Lim = MaxDist;
		for (const FDFBox& B : Boxes)
		{
			const FDFBox Ex = B.ExpandBy(Pad);
			double T = 0;
			if (Ex.IntersectRay(Origin, Dir, T) && T < Lim && !Ex.Contains(Origin)) Lim = T;
		}
		if (Dir.Y < -1e-4) Lim = FMath::Min(Lim, (Origin.Y - Pad) / -Dir.Y);
		return Lim;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return MaxDist;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFClipCamera), /*bTraceComplex=*/true);
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, DF::ToUE(Origin), DF::ToUE(Origin + Dir * MaxDist), FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(float(Pad * DF::M2CM)), Params))
	{
		return Hit.bStartPenetrating ? 0.0 : FMath::Min(MaxDist, double(Hit.Distance) / DF::M2CM);
	}
	return MaxDist;
}

void ADFWorld::ClampToBounds(FVector& Pos) const
{
	Pos.X = DF::Clamp(Pos.X, BoundsMin.X, BoundsMax.X);
	Pos.Z = DF::Clamp(Pos.Z, BoundsMin.Y, BoundsMax.Y);
}
