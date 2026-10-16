// Copyright Epic Games, Inc. All Rights Reserved.

#include "DFEditorLibrary.h"

#include "Dom/JsonObject.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"

namespace
{
	/** Constructs a node under Cue; its child slots (and graph pins) are created through InsertChildNode. */
	template <typename T>
	T* MakeNode(USoundCue* Cue, int32 NumChildren, int32 Depth, int32 Row)
	{
		T* Node = Cue->ConstructSoundNode<T>(T::StaticClass(), false);
		for (int32 I = 0; I < NumChildren; ++I)
		{
			Node->InsertChildNode(Node->ChildNodes.Num());
		}
		if (Node->GraphNode)
		{
			Node->GraphNode->NodePosX = -250 * Depth - 100;
			Node->GraphNode->NodePosY = Row * 120;
		}
		return Node;
	}

	void Describe(const USoundNode* Node, FString& Out)
	{
		if (!Node)
		{
			Out += TEXT("None");
			return;
		}
		if (const USoundNodeWavePlayer* WP = Cast<USoundNodeWavePlayer>(Node))
		{
			const USoundWave* Wave = WP->GetSoundWave();
			Out += FString::Printf(TEXT("WavePlayer(%s)"), Wave ? *Wave->GetName() : TEXT("None"));
			return;
		}
		if (const USoundNodeModulator* Mod = Cast<USoundNodeModulator>(Node))
		{
			Out += FString::Printf(TEXT("Modulator(pitch %.2f-%.2f, vol "), Mod->PitchMin, Mod->PitchMax);
			Out += FMath::IsNearlyEqual(Mod->VolumeMin, Mod->VolumeMax)
				? FString::Printf(TEXT("%.2f)"), Mod->VolumeMin)
				: FString::Printf(TEXT("%.2f-%.2f)"), Mod->VolumeMin, Mod->VolumeMax);
		}
		else if (Cast<USoundNodeRandom>(Node))
		{
			Out += TEXT("Random");
		}
		else if (Cast<USoundNodeMixer>(Node))
		{
			Out += TEXT("Mixer");
		}
		else
		{
			Out += Node->GetClass()->GetName();
		}
		if (Node->ChildNodes.Num() > 0)
		{
			Out += TEXT("[");
			for (int32 I = 0; I < Node->ChildNodes.Num(); ++I)
			{
				if (I > 0) Out += TEXT(", ");
				Describe(Node->ChildNodes[I], Out);
			}
			Out += TEXT("]");
		}
	}
}

bool UDFEditorLibrary::BuildSoundCue(USoundCue* Cue, const TArray<USoundWave*>& Waves, float PitchMin, float PitchMax, float VolumeMin, float VolumeMax)
{
	if (!Cue || Waves.Num() == 0)
	{
		return false;
	}
	for (const USoundWave* W : Waves)
	{
		if (!W) return false;
	}

	// the commandlet does not necessarily load the editor module that registers the graph implementation
	FModuleManager::Get().LoadModuleChecked(TEXT("AudioEditor"));

	Cue->Modify();
	Cue->CreateGraph();   // no-op when the cue already has a graph

	// clear: remove the old graph nodes (the result node stays), then the sound nodes; the orphans are flagged
	// transient so a rebuild does not keep saving them inside the package
	for (USoundNode* Old : Cue->AllNodes)
	{
		if (!Old)
		{
			continue;
		}
		if (Old->GraphNode)
		{
			if (Cue->SoundCueGraph)
			{
				Cue->SoundCueGraph->RemoveNode(Old->GraphNode);
			}
			Old->GraphNode->SetFlags(RF_Transient);
		}
		Old->SetFlags(RF_Transient);
	}
	Cue->AllNodes.Reset();
	Cue->FirstNode = nullptr;

	const int32 Num = Waves.Num();
	USoundNodeModulator* Mod = MakeNode<USoundNodeModulator>(Cue, 1, 0, 0);
	Mod->PitchMin = PitchMin;
	Mod->PitchMax = PitchMax;
	Mod->VolumeMin = VolumeMin;
	Mod->VolumeMax = VolumeMax;

	USoundNode* WaveParent = Mod;
	int32 WaveDepth = 1;
	if (Num > 1)
	{
		USoundNodeRandom* Rand = MakeNode<USoundNodeRandom>(Cue, Num, 1, 0);
		Rand->bRandomizeWithoutReplacement = false;   // the web's pick is a plain uniform random
		Mod->ChildNodes[0] = Rand;
		WaveParent = Rand;
		WaveDepth = 2;
	}
	for (int32 I = 0; I < Num; ++I)
	{
		USoundNodeWavePlayer* WP = MakeNode<USoundNodeWavePlayer>(Cue, 0, WaveDepth, I);
		WP->SetSoundWave(Waves[I]);
		WaveParent->ChildNodes[Num > 1 ? I : 0] = WP;
	}
	Cue->FirstNode = Mod;

	Cue->LinkGraphNodesFromSoundNodes();   // editor graph from the sound nodes (also CacheAggregateValues)
	Cue->CacheAggregateValues();
	Cue->MarkPackageDirty();
	Cue->PostEditChange();
	return true;
}

bool UDFEditorLibrary::BuildLayeredSoundCue(USoundCue* Cue, const FString& SetJson, const FString& WaveFolder, USoundAttenuation* Atten, float LayerGain)
{
	if (!Cue)
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SetJson), Root) || !Root.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
	if (!Root->TryGetArrayField(TEXT("layers"), Layers) || Layers->Num() == 0)
	{
		return false;
	}
	FModuleManager::Get().LoadModuleChecked(TEXT("AudioEditor"));
	Cue->Modify();
	Cue->CreateGraph();
	for (USoundNode* Old : Cue->AllNodes)
	{
		if (!Old)
		{
			continue;
		}
		if (Old->GraphNode)
		{
			if (Cue->SoundCueGraph)
			{
				Cue->SoundCueGraph->RemoveNode(Old->GraphNode);
			}
			Old->GraphNode->SetFlags(RF_Transient);
		}
		Old->SetFlags(RF_Transient);
	}
	Cue->AllNodes.Reset();
	Cue->FirstNode = nullptr;

	const int32 NumLayers = Layers->Num();
	int32 Row = 0;
	// one layer -> its sub tree; returns null on error
	auto BuildLayer = [&](const TSharedPtr<FJsonObject>& Layer, int32 Depth, bool& bOk) -> USoundNode*
	{
		const TArray<TSharedPtr<FJsonValue>>* Vars = nullptr;
		if (!Layer->TryGetArrayField(TEXT("variants"), Vars) || Vars->Num() == 0)
		{
			bOk = false;
			return nullptr;
		}
		const int32 NumVars = Vars->Num();
		USoundNodeRandom* Rand = nullptr;
		if (NumVars > 1)
		{
			Rand = MakeNode<USoundNodeRandom>(Cue, NumVars, Depth, Row);
			Rand->bRandomizeWithoutReplacement = false;
		}
		USoundNode* Single = nullptr;
		for (int32 V = 0; V < NumVars; ++V)
		{
			const TSharedPtr<FJsonObject> Var = (*Vars)[V]->AsObject();
			FString Key;
			USoundNode* Branch = nullptr;   // a null key = silence: the branch stays empty
			if (Var.IsValid() && Var->TryGetStringField(TEXT("key"), Key) && !Key.IsEmpty())
			{
				const FString Path = FString::Printf(TEXT("%s/S_%s.S_%s"), *WaveFolder, *Key, *Key);
				USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (!Wave)
				{
					bOk = false;
					return nullptr;
				}
				const double Pitch = Var->HasField(TEXT("basePitch")) ? Var->GetNumberField(TEXT("basePitch")) : 1.0;
				const double PVar = Var->HasField(TEXT("pitchVar")) ? Var->GetNumberField(TEXT("pitchVar")) : 0.0;
				const double VolA = Var->HasField(TEXT("volA")) ? Var->GetNumberField(TEXT("volA")) : 255.0;
				USoundNodeModulator* Mod = MakeNode<USoundNodeModulator>(Cue, 1, Depth + 1, Row);
				Mod->PitchMin = FMath::Clamp(float(Pitch * (1.0 - PVar)), 0.25f, 4.f);   // a huge pitchVar (1.0) would give pitch 0
				Mod->PitchMax = FMath::Clamp(float(Pitch * (1.0 + PVar)), 0.25f, 4.f);
				Mod->VolumeMin = Mod->VolumeMax = float(VolA / 255.0) * LayerGain;
				USoundNodeWavePlayer* WP = MakeNode<USoundNodeWavePlayer>(Cue, 0, Depth + 2, Row);
				WP->SetSoundWave(Wave);
				Mod->ChildNodes[0] = WP;
				Branch = Mod;
			}
			++Row;
			if (Rand)
			{
				Rand->ChildNodes[V] = Branch;
			}
			else
			{
				Single = Branch;
			}
		}
		return Rand ? static_cast<USoundNode*>(Rand) : Single;
	};

	USoundNodeMixer* Mixer = NumLayers > 1 ? MakeNode<USoundNodeMixer>(Cue, NumLayers, 0, 0) : nullptr;
	bool bOk = true;
	for (int32 L = 0; L < NumLayers; ++L)
	{
		const TSharedPtr<FJsonObject> Layer = (*Layers)[L]->AsObject();
		if (!Layer.IsValid())
		{
			return false;
		}
		USoundNode* N = BuildLayer(Layer, Mixer ? 1 : 0, bOk);
		if (!bOk)
		{
			return false;
		}
		if (Mixer)
		{
			Mixer->ChildNodes[L] = N;
		}
		else
		{
			Cue->FirstNode = N;
		}
	}
	if (Mixer)
	{
		Cue->FirstNode = Mixer;
	}
	if (Atten)
	{
		Cue->AttenuationSettings = Atten;
	}
	Cue->LinkGraphNodesFromSoundNodes();
	Cue->CacheAggregateValues();
	Cue->MarkPackageDirty();
	Cue->PostEditChange();
	return Cue->FirstNode != nullptr;
}

FString UDFEditorLibrary::DescribeSoundCue(USoundCue* Cue)
{
	if (!Cue)
	{
		return TEXT("(null)");
	}
	FString Out;
	Describe(Cue->FirstNode, Out);
	return Out;
}

bool UDFEditorLibrary::AddKeyMapping(UInputMappingContext* Context, UInputAction* Action, FKey Key, bool bNegate, bool bSwizzleYXZ)
{
	if (!Context || !Action || !Key.IsValid())
	{
		return false;
	}
	Context->Modify();
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
	if (bSwizzleYXZ)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}
	if (bNegate)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
	Context->MarkPackageDirty();
	return true;
}

void UDFEditorLibrary::ClearKeyMappings(UInputMappingContext* Context)
{
	if (!Context)
	{
		return;
	}
	Context->Modify();
	Context->UnmapAll();
	Context->MarkPackageDirty();
}

UFont* UDFEditorLibrary::FindOrCreateFont(const FString& AssetPath)
{
	const FString Name = FPackageName::GetShortName(AssetPath);
	if (UFont* Existing = LoadObject<UFont>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Existing;
	}
	UPackage* Pkg = CreatePackage(*AssetPath);
	if (!Pkg)
	{
		return nullptr;
	}
	UFont* Font = NewObject<UFont>(Pkg, *Name, RF_Public | RF_Standalone | RF_Transactional);
	if (Font)
	{
		FAssetRegistryModule::AssetCreated(Font);
		Pkg->MarkPackageDirty();
	}
	return Font;
}

bool UDFEditorLibrary::BuildBitmapFont(UFont* Font, const TArray<UTexture2D*>& Pages, const FString& JsonPath)
{
	if (!Font || Pages.Num() == 0)
	{
		return false;
	}
	for (const UTexture2D* T : Pages)
	{
		if (!T)
		{
			return false;
		}
	}
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Glyphs = nullptr;
	if (!Root->TryGetObjectField(TEXT("glyphs"), Glyphs))
	{
		return false;
	}
	const int32 LetterSpacing = int32(Root->GetNumberField(TEXT("letterSpacing")));
	const int32 LineHeight = int32(Root->GetNumberField(TEXT("lineHeight")));
	const int32 PageSize = Root->HasField(TEXT("pageSize")) ? int32(Root->GetNumberField(TEXT("pageSize"))) : 256;

	Font->Modify();
	Font->FontCacheType = EFontCacheType::Offline;
	Font->IsRemapped = 0;
	Font->CharRemap.Reset();
	Font->Textures.Reset();
	for (UTexture2D* T : Pages)
	{
		Font->Textures.Add(T);
	}
	Font->Characters.Reset();
	Font->Characters.AddZeroed(256);   // indexed by char code; 0x00..0x1F stay empty
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Glyphs)->Values)
	{
		const int32 Code = FCString::Atoi(*Pair.Key);
		const TSharedPtr<FJsonObject> G = Pair.Value->AsObject();
		if (!G.IsValid() || Code < 0x20 || Code > 0xFF)
		{
			continue;
		}
		const int32 Y = int32(G->GetNumberField(TEXT("y")));
		FFontCharacter& C = Font->Characters[Code];
		C.StartU = int32(G->GetNumberField(TEXT("x")));
		C.TextureIndex = Pages.Num() > 1 ? uint8(Y / PageSize) : 0;
		C.StartV = Pages.Num() > 1 ? Y % PageSize : Y;
		C.USize = int32(G->GetNumberField(TEXT("w")));
		C.VSize = int32(G->GetNumberField(TEXT("h")));
		C.VerticalOffset = 0;
	}
	// UFont's offline advance is USize + Kerning (CanvasItem.cpp), the game's is width + letterSpacing - 1 (Docs/FONTS.md)
	Font->Kerning = LetterSpacing - 1;
	Font->EmScale = 1.f;
	Font->ScalingFactor = 1.f;
	Font->Ascent = float(LineHeight);
	Font->Descent = 0.f;
	Font->Leading = 0.f;
	Font->LegacyFontSize = FMath::Max(1, LineHeight);
	Font->CacheCharacterCountAndMaxCharHeight();
	Font->MarkPackageDirty();
	return true;
}

FString UDFEditorLibrary::DescribeBitmapFont(UFont* Font)
{
	if (!Font)
	{
		return TEXT("(null)");
	}
	TArray<FString> Tex;
	for (const UTexture2D* T : Font->Textures)
	{
		Tex.Add(T ? T->GetName() : TEXT("None"));
	}
	return FString::Printf(TEXT("%s;tex=%s;kerning=%d;chars=%d;maxh=%.0f"),
		Font->FontCacheType == EFontCacheType::Offline ? TEXT("Offline") : TEXT("Runtime"), *FString::Join(Tex, TEXT(",")),
		Font->Kerning, Font->Characters.Num(), double(Font->GetMaxCharHeight()));
}
