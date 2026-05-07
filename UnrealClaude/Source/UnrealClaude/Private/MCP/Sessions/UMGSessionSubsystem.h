// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UMGSessionSubsystem.generated.h"

/**
 * UEditorSubsystem that holds session-scoped UMG anchor state for the MCP layer.
 *
 * Two pieces of state:
 *  - CurrentTargetAssetPath: the active "current widget" — when a UMG MCP tool is
 *    invoked WITHOUT a `widget_blueprint_path`, this path is used as the fallback.
 *  - RecentlyEditedHistory:  LIFO stack of recently-touched widget blueprints,
 *    capped at HistoryCapacity. Maintained automatically by listening to
 *    AssetRegistry / package-dirtied events.
 *
 * This subsystem is purely an in-memory cache; nothing is persisted across editor
 * restarts. Token-saving helper for agents working a UMG loop without re-passing
 * the asset path on every call.
 *
 * Lives inside the UnrealClaude plugin namespace (no Paoge prefix) because it is
 * plugin-internal infrastructure, not project gameplay code.
 */
UCLASS()
class UUMGSessionSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Static accessor — returns nullptr only if the editor is shutting down. */
	static UUMGSessionSubsystem* Get();

	/**
	 * Set the current target widget blueprint asset path.
	 * @param InAssetPath  Asset path (e.g. "/Game/UI/WBP_PaogeCombatHUD") — may include _C or .WBP_X suffixes
	 * @return true if path is non-empty (no asset-existence check; caller validates)
	 */
	bool SetCurrentTarget(const FString& InAssetPath);

	/** Returns the current target asset path (empty string if unset). */
	const FString& GetCurrentTarget() const { return CurrentTargetAssetPath; }

	/** Returns the last edited widget blueprint path (empty if history is empty). */
	FString GetLastEdited() const;

	/** Returns up to MaxCount recently-edited paths in LIFO order (newest first). */
	TArray<FString> GetRecentlyEdited(int32 MaxCount = 5) const;

	/** Manually push an asset path into history (also called by event hooks). */
	void PushHistory(const FString& AssetPath);

	/** Maximum number of paths retained in history. */
	static constexpr int32 HistoryCapacity = 20;

	/**
	 * Apply UMG session fallback to a JSON parameter object IN PLACE.
	 * If `widget_blueprint_path` is missing or empty in Params AND the subsystem
	 * has a current target anchored, inject the anchor as that field. Otherwise
	 * leaves Params unchanged so the caller's validators return the canonical
	 * "missing required parameter" error.
	 *
	 * Safe to call when no subsystem exists (no-op).
	 *
	 * @param Params  Tool params JSON object to potentially augment
	 * @return true if a fallback was applied, false otherwise
	 */
	static bool ApplyWidgetBlueprintPathFallback(const TSharedRef<class FJsonObject>& Params);

private:
	/** Asset path currently anchored as the implicit target for UMG MCP tools. */
	UPROPERTY()
	FString CurrentTargetAssetPath;

	/** LIFO history of recently-edited widget blueprint paths. Capped at HistoryCapacity. */
	UPROPERTY()
	TArray<FString> RecentlyEditedHistory;

	/** Handle for AssetRegistry::OnAssetUpdated delegate. */
	FDelegateHandle AssetUpdatedHandle;

	/** Handler for AssetRegistry asset-updated event — pushes WidgetBlueprint paths into history. */
	void HandleAssetUpdated(const struct FAssetData& AssetData);
};
