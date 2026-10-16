#include "DFEffects.h"
#include "DFMath.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFMuzzleFlashTest, "DF.Effects.MuzzleFlash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDFMuzzleFlashTest::RunTest(const FString& Parameters)
{
	UClass* EffectsClass = LoadClass<ADFEffects>(nullptr, TEXT("/Game/DF/Blueprints/BP_DFEffects.BP_DFEffects_C"));
	if (!TestNotNull(TEXT("Effects Blueprint is available"), EffectsClass)) return false;

	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Effects test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ADFEffects* Effects = World->SpawnActor<ADFEffects>(EffectsClass);
	if (!TestNotNull(TEXT("Effects actor"), Effects)) return false;

	const FVector Muzzle(1.0, 1.7, -2.0);
	Effects->MuzzleFlash(Muzzle, 0.3, true);
	UMaterialBillboardComponent* Sprite = Effects->FindComponentByClass<UMaterialBillboardComponent>();
	UPointLightComponent* Light = Effects->FindComponentByClass<UPointLightComponent>();
	if (!TestNotNull(TEXT("Muzzle sprite"), Sprite) || !TestNotNull(TEXT("Muzzle light"), Light)) return false;
	TestTrue(TEXT("Sprite appears on firing"), Sprite->IsVisible());
	TestEqual(TEXT("Sprite uses the weapon's first-person projection"), Sprite->FirstPersonPrimitiveType, EFirstPersonPrimitiveType::FirstPerson);
	TestTrue(TEXT("Muzzle is passed unscaled to the renderer"), Sprite->GetComponentLocation().Equals(DF::ToUE(Muzzle)));
	if (!TestEqual(TEXT("One muzzle sprite element"), Sprite->Elements.Num(), 1)) return false;
	const float HalfSize = Sprite->Elements[0].BaseSizeX;
	TestTrue(TEXT("30 cm flash uses half extents, including size variation"), HalfSize >= 12.f && HalfSize <= 19.5f);

	const FVector MovedMuzzle = Muzzle + FVector(0.4, -0.1, 0.2);
	Effects->UpdateMuzzleFlash(MovedMuzzle, true, true);
	TestTrue(TEXT("Flash follows the animated barrel"), Sprite->GetComponentLocation().Equals(DF::ToUE(MovedMuzzle)));
	TestTrue(TEXT("Light follows the animated barrel"), Light->GetComponentLocation().Equals(DF::ToUE(MovedMuzzle)));
	Effects->UpdateFX(0.1f, FVector::ZeroVector);
	TestTrue(TEXT("New shot survives its first render even at low frame rates"), Sprite->IsVisible());
	Effects->UpdateFX(0.051f, FVector::ZeroVector);
	TestFalse(TEXT("Flash expires"), Sprite->IsVisible());
	TestFalse(TEXT("Muzzle light expires with flash"), Light->IsVisible());

	Effects->MuzzleFlash(Muzzle, 0.45, false);
	TestEqual(TEXT("Third-person flash uses world projection"), Sprite->FirstPersonPrimitiveType, EFirstPersonPrimitiveType::None);
	Effects->UpdateMuzzleFlash(Muzzle, true, true);
	TestFalse(TEXT("Changing views cancels the old flash"), Sprite->IsVisible());
	Effects->MuzzleFlash(Muzzle, 0.3, true);
	Effects->UpdateMuzzleFlash(Muzzle, true, false);
	TestFalse(TEXT("Hiding the weapon or raising sights cancels the flash"), Sprite->IsVisible());
	Effects->MuzzleFlash(Muzzle, 0.3, true);
	Effects->StopMuzzleFlash();
	TestFalse(TEXT("Weapon switch clears its previous flash"), Sprite->IsVisible());
	TestFalse(TEXT("Weapon switch clears its previous light"), Light->IsVisible());
	return true;
}

#endif
