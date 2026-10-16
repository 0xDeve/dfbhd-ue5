// Editor-only helpers called from the asset setup Python script (unreal.DFEditorLibrary): Sound Cue graph building
// and Enhanced Input mapping context editing, which the Python API does not expose.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DFEditorLibrary.generated.h"

class USoundCue;
class USoundWave;
class USoundAttenuation;
class UInputMappingContext;
class UInputAction;
class UFont;
class UTexture2D;

UCLASS()
class PROJECTDFEDITOR_API UDFEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Rebuilds Cue as: WavePlayer per wave -> (Random when more than one wave) -> Modulator(pitch, volume ranges).
	 *  Idempotent (clears existing nodes first). Returns false on bad input. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static bool BuildSoundCue(USoundCue* Cue, const TArray<USoundWave*>& Waves, float PitchMin, float PitchMax, float VolumeMin, float VolumeMax);

	/** Rebuilds Cue from a sound set of sound_sets.json (SetJson = the set object {"layers":[{range,priority,variants:[{key,basePitch,pitchVar,volA,volB}]}]}):
	 *  Mixer (one input per layer; skipped for a single layer) <- per layer Random (one input per variant; skipped for a single variant, a null key = a
	 *  silent branch) <- per variant Modulator(pitch basePitch*(1-+pitchVar), volume volA/255*LayerGain) <- WavePlayer(<WaveFolder>/S_<key>).
	 *  Atten (optional) becomes the cue's attenuation. Idempotent. Returns false on bad input or a missing wave. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static bool BuildLayeredSoundCue(USoundCue* Cue, const FString& SetJson, const FString& WaveFolder, USoundAttenuation* Atten, float LayerGain);

	/** Readable dump of the node tree, e.g. Modulator(pitch 0.96-1.04, vol 0.90)[Random[WavePlayer(S_fire2), WavePlayer(S_fire3)]]. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static FString DescribeSoundCue(USoundCue* Cue);

	/** Appends Key -> Action to Context; optionally with a Negate and/or a Swizzle (YXZ) modifier. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static bool AddKeyMapping(UInputMappingContext* Context, UInputAction* Action, FKey Key, bool bNegate, bool bSwizzleYXZ);

	/** Loads the Font asset at AssetPath (package path, e.g. /Game/DF/Fonts/F_DF_Arial16b) or creates an empty one. Not saved. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static UFont* FindOrCreateFont(const FString& AssetPath);

	/** Turns Font into an offline bitmap font from a DFBHD .fnt export (Tools/DFExport/fnt.py JSON): Characters indexed by char code
	 *  (empty below 0x20), Textures = Pages (one page = the whole exported PNG, y in the JSON is the pixel row in it; several pages =
	 *  JSON y split by pageSize), Kerning = letterSpacing - 1 so the advance is width + spacing - 1 (Docs/FONTS.md). Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static bool BuildBitmapFont(UFont* Font, const TArray<UTexture2D*>& Pages, const FString& JsonPath);

	/** Readable dump for verification (Textures is not readable from Python): "Offline;tex=T_Font_Arial16b,...;kerning=-4;chars=256;maxh=16". */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static FString DescribeBitmapFont(UFont* Font);

	/** Removes every key mapping from Context. */
	UFUNCTION(BlueprintCallable, Category = "DF|Editor")
	static void ClearKeyMappings(UInputMappingContext* Context);
};
