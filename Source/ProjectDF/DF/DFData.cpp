#include "DFData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFData, Log, All);

namespace
{
	FVector ReadVec3(const TSharedPtr<FJsonValue>& V)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
	}

	FQuat ReadQuat(const TSharedPtr<FJsonValue>& V)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		return A.Num() >= 4 ? FQuat(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()).GetNormalized() : FQuat::Identity;
	}

	void ReadFloats(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, TArray<float>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(Field, A))
		{
			Out.SetNumUninitialized(A->Num());
			for (int32 i = 0; i < A->Num(); ++i) Out[i] = float((*A)[i]->AsNumber());
		}
	}

	void ReadVecArray(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, TArray<FVector>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(Field, A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A) Out.Add(ReadVec3(V));
		}
	}
}

const FDFData& FDFData::Get()
{
	static FDFData Instance;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		if (!Instance.Load(GetDataPath()))
		{
			UE_LOG(LogDFData, Error, TEXT("dfdata.json: %s"), *Instance.Error);
		}
	}
	return Instance;
}

FString FDFData::GetDataPath()
{
	return FPaths::ProjectContentDir() / TEXT("DF/Data/dfdata.json");
}

bool FDFData::Load(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Error = FString::Printf(TEXT("cannot read %s (run Tools/DFExport/export_unreal.py)"), *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Error = FString::Printf(TEXT("cannot parse %s"), *Path);
		return false;
	}

	// skeletons
	if (const TSharedPtr<FJsonObject>* Skels = nullptr; Root->TryGetObjectField(TEXT("skeletons"), Skels))
	{
		for (const auto& It : (*Skels)->Values)
		{
			const TSharedPtr<FJsonObject> S = It.Value->AsObject();
			FDFSkeletonData D;
			for (const TSharedPtr<FJsonValue>& V : S->GetArrayField(TEXT("names"))) D.Names.Add(FName(*V->AsString()));
			for (const TSharedPtr<FJsonValue>& V : S->GetArrayField(TEXT("parents"))) D.Parents.Add(int32(V->AsNumber()));
			for (const TSharedPtr<FJsonValue>& V : S->GetArrayField(TEXT("restQuats"))) D.RestQuats.Add(ReadQuat(V));
			ReadVecArray(S, TEXT("restOffsets"), D.RestOffsets);
			ReadVecArray(S, TEXT("pivots"), D.Pivots);
			const TArray<TSharedPtr<FJsonValue>>& RR = S->GetArrayField(TEXT("restRoot"));
			for (int32 i = 0; i < 6 && i < RR.Num(); ++i) D.RestRoot[i] = RR[i]->AsNumber();
			const int32 N = D.Parents.Num();
			if (D.Names.Num() != N || D.RestQuats.Num() != N || D.RestOffsets.Num() != N)
			{
				Error = FString::Printf(TEXT("skeleton %s: inconsistent array sizes"), *It.Key);
				return false;
			}
			Skeletons.Add(FName(*It.Key), MoveTemp(D));
		}
	}

	// clips
	if (const TSharedPtr<FJsonObject>* ClipsObj = nullptr; Root->TryGetObjectField(TEXT("clips"), ClipsObj))
	{
		for (const auto& It : (*ClipsObj)->Values)
		{
			const TSharedPtr<FJsonObject> C = It.Value->AsObject();
			FDFClipData D;
			D.Key = FName(*It.Key);
			D.Fps = int32(C->GetNumberField(TEXT("fps")));
			D.Keys = int32(C->GetNumberField(TEXT("keys")));
			D.Tracks = int32(C->GetNumberField(TEXT("tracks")));
			D.RootKeys = int32(C->GetNumberField(TEXT("rootKeys")));
			ReadFloats(C, TEXT("quats"), D.Quats);
			ReadFloats(C, TEXT("root"), D.Root);
			ReadVecArray(C, TEXT("offsets"), D.Offsets);
			D.Duration = double(FMath::Max(1, D.Keys - 1)) / double(FMath::Max(1, D.Fps));
			if (D.Quats.Num() != D.Keys * D.Tracks * 4 || D.Root.Num() != D.RootKeys * 6)
			{
				Error = FString::Printf(TEXT("clip %s: inconsistent array sizes"), *It.Key);
				return false;
			}
			Clips.Add(D.Key, MoveTemp(D));
		}
	}

	// models
	if (const TSharedPtr<FJsonObject>* ModelsObj = nullptr; Root->TryGetObjectField(TEXT("models"), ModelsObj))
	{
		for (const auto& It : (*ModelsObj)->Values)
		{
			const TSharedPtr<FJsonObject> M = It.Value->AsObject();
			FDFModelData D;
			if (const TSharedPtr<FJsonObject>* BBox = nullptr; M->TryGetObjectField(TEXT("bbox"), BBox))
			{
				D.BBoxMin = ReadVec3((*BBox)->TryGetField(TEXT("min")));
				D.BBoxMax = ReadVec3((*BBox)->TryGetField(TEXT("max")));
			}
			if (const TSharedPtr<FJsonObject>* Ups = nullptr; M->TryGetObjectField(TEXT("userpoints"), Ups))
			{
				for (const auto& U : (*Ups)->Values) D.Userpoints.Add(FName(*FString(*U.Key).ToLower()), ReadVec3(U.Value));
			}
			Models.Add(FName(*It.Key), MoveTemp(D));
		}
	}

	// third-person gun attachment
	if (const TSharedPtr<FJsonObject>* Att = nullptr; Root->TryGetObjectField(TEXT("tpGunAttach"), Att))
	{
		TpGunAttach.Bone = int32((*Att)->GetNumberField(TEXT("bone")));
		TpGunAttach.Position = ReadVec3((*Att)->TryGetField(TEXT("position")));
		TpGunAttach.Rotation = ReadQuat((*Att)->TryGetField(TEXT("quaternion")));
	}

	// per-weapon third-person attachments
	if (const TSharedPtr<FJsonObject>* Atts = nullptr; Root->TryGetObjectField(TEXT("tpAttach"), Atts))
	{
		for (const auto& It : (*Atts)->Values)
		{
			const TSharedPtr<FJsonObject>* A = nullptr;
			if (!It.Value->TryGetObject(A)) continue;
			FDFGunAttach G;
			G.Bone = int32((*A)->GetNumberField(TEXT("bone")));
			G.Position = ReadVec3((*A)->TryGetField(TEXT("position")));
			G.Rotation = ReadQuat((*A)->TryGetField(TEXT("quaternion")));
			TpAttach.Add(FName(*It.Key), G);
		}
	}

	bLoaded = Skeletons.Num() > 0 && Clips.Num() > 0;
	if (!bLoaded) Error = TEXT("no skeletons/clips in data file");
	UE_LOG(LogDFData, Log, TEXT("Loaded %s: %d skeletons, %d clips, %d models"), *Path, Skeletons.Num(), Clips.Num(), Models.Num());
	return bLoaded;
}
