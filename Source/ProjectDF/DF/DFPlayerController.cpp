#include "DFPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"

ADFPlayerController::ADFPlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	PrimaryActorTick.bCanEverTick = true;
}

void ADFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// the web's pointer-lock play mode: game-only input, hidden cursor
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

void ADFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* Context : DefaultMappingContexts)
			{
				if (Context)
				{
					Subsystem->AddMappingContext(Context, 0);
				}
			}
		}
	}
}
