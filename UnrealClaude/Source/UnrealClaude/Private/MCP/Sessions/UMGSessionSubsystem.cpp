// Copyright Natali Caggiano. All Rights Reserved.

#include "UMGSessionSubsystem.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"

void UUMGSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Step 1: Wire AssetRegistry update hook so saving a WidgetBlueprint pushes it
	// onto the recently-edited history automatically.
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetUpdatedHandle = AssetRegistryModule.Get().OnAssetUpdated().AddUObject(
		this, &UUMGSessionSubsystem::HandleAssetUpdated);

	UE_LOG(LogUnrealClaude, Log, TEXT("UUMGSessionSubsystem initialized (history cap=%d)"), HistoryCapacity);
}

void UUMGSessionSubsystem::Deinitialize()
{
	// Step 1: Unhook AssetRegistry delegate.
	if (AssetUpdatedHandle.IsValid())
	{
		FAssetRegistryModule* AssetRegistryModule =
			FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (AssetRegistryModule)
		{
			AssetRegistryModule->Get().OnAssetUpdated().Remove(AssetUpdatedHandle);
		}
		AssetUpdatedHandle.Reset();
	}

	Super::Deinitialize();
}

UUMGSessionSubsystem* UUMGSessionSubsystem::Get()
{
	if (!GEditor)
	{
		return nullptr;
	}
	return GEditor->GetEditorSubsystem<UUMGSessionSubsystem>();
}

bool UUMGSessionSubsystem::SetCurrentTarget(const FString& InAssetPath)
{
	if (InAssetPath.IsEmpty())
	{
		return false;
	}
	CurrentTargetAssetPath = InAssetPath;
	PushHistory(InAssetPath);
	UE_LOG(LogUnrealClaude, Log, TEXT("UMG session target set: %s"), *InAssetPath);
	return true;
}

FString UUMGSessionSubsystem::GetLastEdited() const
{
	return RecentlyEditedHistory.Num() > 0 ? RecentlyEditedHistory[0] : FString();
}

TArray<FString> UUMGSessionSubsystem::GetRecentlyEdited(int32 MaxCount) const
{
	if (MaxCount <= 0)
	{
		return TArray<FString>();
	}
	const int32 Count = FMath::Min(MaxCount, RecentlyEditedHistory.Num());
	TArray<FString> Result;
	Result.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Result.Add(RecentlyEditedHistory[i]);
	}
	return Result;
}

void UUMGSessionSubsystem::PushHistory(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return;
	}

	// Step 1: Remove existing copy (so re-edit moves it to the top).
	RecentlyEditedHistory.Remove(AssetPath);

	// Step 2: Insert at front.
	RecentlyEditedHistory.Insert(AssetPath, 0);

	// Step 3: Cap at HistoryCapacity.
	if (RecentlyEditedHistory.Num() > HistoryCapacity)
	{
		RecentlyEditedHistory.SetNum(HistoryCapacity, EAllowShrinking::No);
	}
}

// ===== Graph cursor =====

// Anchor the active graph and reset the cursor. Switching graphs invalidates both
// the program-counter node id (it lived in the old graph) and the visual position
// (a fresh canvas should start at origin).
//
// Cursor design portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
void UUMGSessionSubsystem::SetTargetGraph(const FString& InGraphName, bool bInIsFunctionGraph)
{
	const bool bGraphChanged =
		(InGraphName != CurrentGraphName) ||
		(bInIsFunctionGraph != bCurrentGraphIsFunction);

	CurrentGraphName = InGraphName;
	bCurrentGraphIsFunction = bInIsFunctionGraph;

	// Step 1: clear program-counter node id when graph changes — the old id refers
	// to a node in a different graph and would silently produce cross-graph wiring
	// errors if reused.
	if (bGraphChanged)
	{
		CurrentCursorNodeId.Reset();
		CurrentCursorPosition = FVector2D::ZeroVector;
	}

	UE_LOG(LogUnrealClaude, Verbose, TEXT("UMG session graph anchored: '%s' (function=%d, changed=%d)"),
		*InGraphName, bInIsFunctionGraph ? 1 : 0, bGraphChanged ? 1 : 0);
}

// Returns the cursor position the caller should use, then advances X by
// CursorAdvanceX so a sequence of position-less add_node calls lays out
// horizontally instead of stacking at origin.
FVector2D UUMGSessionSubsystem::GetAndAdvanceCursorPosition()
{
	const FVector2D Consumed = CurrentCursorPosition;
	CurrentCursorPosition.X += CursorAdvanceX;
	return Consumed;
}

bool UUMGSessionSubsystem::ApplyWidgetBlueprintPathFallback(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: If caller already provided a non-empty path, do nothing.
	FString Existing;
	if (Params->TryGetStringField(TEXT("widget_blueprint_path"), Existing) && !Existing.IsEmpty())
	{
		return false;
	}

	// Step 2: Look up the subsystem; bail if unavailable (no editor / shutting down).
	UUMGSessionSubsystem* Session = Get();
	if (!Session)
	{
		return false;
	}
	const FString& Anchor = Session->GetCurrentTarget();
	if (Anchor.IsEmpty())
	{
		return false;
	}

	// Step 3: Inject the anchor into Params so downstream validators succeed.
	Params->SetStringField(TEXT("widget_blueprint_path"), Anchor);
	return true;
}

void UUMGSessionSubsystem::HandleAssetUpdated(const FAssetData& AssetData)
{
	// Step 1: Filter to WidgetBlueprint asset class.
	static const FName WidgetBlueprintClassName = TEXT("WidgetBlueprint");
	if (AssetData.AssetClassPath.GetAssetName() != WidgetBlueprintClassName)
	{
		return;
	}

	// Step 2: Push the soft path into history.
	PushHistory(AssetData.GetSoftObjectPath().ToString());
}
