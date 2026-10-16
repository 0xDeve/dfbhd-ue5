// Player controller: game-only input; adds the Enhanced Input mapping contexts (assigned in the Blueprint). The pawn
// binds the input actions itself.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DFPlayerController.generated.h"

class UInputMappingContext;

UCLASS()
class PROJECTDF_API ADFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADFPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Mapping contexts added to the local player's Enhanced Input subsystem. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Input")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;
};
