// Game mode: DF pawn / controller / HUD; spawns the pawn at the world spawn without needing a PlayerStart.
// The Blueprint game mode overrides the classes (pawn, controller, HUD, world, effects).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DFGameMode.generated.h"

class ADFWorld;
class ADFEffects;

UCLASS()
class PROJECTDF_API ADFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADFGameMode();

	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }

	/** The game mode whose WorldClass / EffectsClass apply to a world: the GameState's default game mode (known on clients too),
	 *  else the authority game mode. bOutPending: a client whose GameState has not arrived yet (nothing may be spawned then). */
	static const ADFGameMode* FindForWorld(UWorld* World, bool& bOutPending);

	/** Spawned by ADFWorld::Get when the level has no ADFWorld. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF")
	TSubclassOf<ADFWorld> WorldClass;

	/** Spawned by ADFEffects::Get when the level has no ADFEffects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF")
	TSubclassOf<ADFEffects> EffectsClass;
};
