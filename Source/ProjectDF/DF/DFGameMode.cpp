#include "DFGameMode.h"
#include "DFPlayerPawn.h"
#include "DFPlayerController.h"
#include "DFHUD.h"
#include "DFWorld.h"
#include "DFEffects.h"
#include "DFMath.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"

ADFGameMode::ADFGameMode()
{
	DefaultPawnClass = ADFPlayerPawn::StaticClass();
	PlayerControllerClass = ADFPlayerController::StaticClass();
	HUDClass = ADFHUD::StaticClass();
	WorldClass = ADFWorld::StaticClass();
	EffectsClass = ADFEffects::StaticClass();
}

const ADFGameMode* ADFGameMode::FindForWorld(UWorld* World, bool& bOutPending)
{
	bOutPending = false;
	if (!World)
	{
		return nullptr;
	}
	const ADFGameMode* GM = nullptr;
	if (const AGameStateBase* GS = World->GetGameState())
	{
		GM = Cast<ADFGameMode>(GS->GetDefaultGameMode());
	}
	if (!GM)
	{
		GM = World->GetAuthGameMode<ADFGameMode>();
	}
	bOutPending = !GM && World->GetNetMode() == NM_Client && !World->GetGameState();
	return GM;
}

void ADFGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}
	// world.js world.spawn (-1, 0, 4) in the compound, a team start in a map; no PlayerStart needed.
	// The first player gets exactly that; the 2nd+ the next start / 1.5 m further along +X (ADFWorld::GetSpawnFor).
	const ADFWorld* DFWorld = ADFWorld::Get(GetWorld());
	int32 PlayerIndex = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->Get() == NewPlayer)
		{
			break;
		}
		++PlayerIndex;
	}
	FVector Spawn(-1.0, 0.0, 4.0);
	double Yaw = 0.0;
	if (DFWorld)
	{
		DFWorld->GetSpawnFor(PlayerIndex, Spawn, Yaw);
	}
	else
	{
		Spawn.X += 1.5 * PlayerIndex;
	}
	// the actor is the capsule: its center is half a body above the feet
	FTransform SpawnTransform = DF::ToUE(FQuat(FVector(0, 1, 0), Yaw), Spawn);
	SpawnTransform.AddToTranslation(FVector(0.0, 0.0, ADFPlayerPawn::GetCapsuleHalfHeightCm()));
	RestartPlayerAtTransform(NewPlayer, SpawnTransform);
}
