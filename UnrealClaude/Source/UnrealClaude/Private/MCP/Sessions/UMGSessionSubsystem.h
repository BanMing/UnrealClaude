// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UMGSessionSubsystem.generated.h"

/**
 * UEditorSubsystem that holds session-scoped MCP anchor state.
 *
 * Despite the UMG-prefixed name (kept for backward source-compat), this subsystem
 * tracks two orthogonal session concerns:
 *
 *  1. UMG anchor (widget blueprint path):
 *     - CurrentTargetAssetPath: the active "current widget" — when a UMG MCP tool is
 *       invoked WITHOUT a `widget_blueprint_path`, this path is used as the fallback.
 *     - RecentlyEditedHistory:  LIFO stack of recently-touched widget blueprints,
 *       capped at HistoryCapacity. Maintained automatically by listening to
 *       AssetRegistry / package-dirtied events.
 *
 *  2. Graph cursor (Blueprint node-creation auto-layout):
 *     - CurrentGraphName / bCurrentGraphIsFunction: which graph the cursor lives in.
 *     - CurrentCursorNodeId:    "program counter" — the last node added; consumed by
 *                               connection helpers that want to chain off the previous
 *                               node without forcing the caller to thread node IDs.
 *     - CurrentCursorPosition:  visual cursor in graph coordinates. When add_node is
 *                               called without explicit pos_x/pos_y, the tool reads
 *                               and advances this position so a sequence of node-add
 *                               calls produces a left-to-right ribbon instead of
 *                               stacking every node at (0,0).
 *
 * This subsystem is purely an in-memory cache; nothing is persisted across editor
 * restarts. Token-saving helper for agents working a tight loop without re-passing
 * the asset path / graph name / cursor position on every call.
 *
 * Lives inside the UnrealClaude plugin namespace (no Paoge prefix) because it is
 * plugin-internal infrastructure, not project gameplay code.
 *
 * Cursor design portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq —
 * UmgAttentionSubsystem. https://github.com/winyunq/UnrealMotionGraphicsMCP
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

	// ===== Graph cursor (auto-layout + program counter) =====

	/**
	 * Set the current target graph for cursor tracking. Switching graphs clears
	 * the cursor (both the program-counter node id and the visual position) so
	 * the next add_node call against the new graph starts at origin.
	 *
	 * @param InGraphName        Graph display name (empty for default Event Graph)
	 * @param bInIsFunctionGraph True if this names a function graph; false for ubergraph
	 */
	void SetTargetGraph(const FString& InGraphName, bool bInIsFunctionGraph);

	/** Returns the currently anchored graph name (empty = default Event Graph). */
	const FString& GetTargetGraphName() const { return CurrentGraphName; }

	/** Returns whether the anchored graph is a function graph (vs ubergraph). */
	bool IsTargetGraphFunction() const { return bCurrentGraphIsFunction; }

	/** Set the program-counter node id (typically called right after add_node). */
	void SetCursorNode(const FString& InNodeId) { CurrentCursorNodeId = InNodeId; }

	/** Returns the program-counter node id (empty if no node has been added yet). */
	const FString& GetCursorNode() const { return CurrentCursorNodeId; }

	/** Reset cursor position to origin. */
	void ResetCursorPosition() { CurrentCursorPosition = FVector2D::ZeroVector; }

	/**
	 * Read the current cursor position then advance X by CursorAdvanceX so the next
	 * call lands to the right of the slot just consumed. Mirrors UmgMcp's
	 * GetAndAdvanceCursorPosition, which produces a horizontal ribbon of nodes
	 * when callers omit explicit positions.
	 *
	 * @return The position the caller should use BEFORE the advance.
	 */
	FVector2D GetAndAdvanceCursorPosition();

	/** Horizontal step applied to the cursor after each consume. */
	static constexpr float CursorAdvanceX = 250.0f;

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

	/** Currently anchored graph for cursor tracking (empty = default Event Graph). */
	UPROPERTY()
	FString CurrentGraphName;

	/** Whether CurrentGraphName refers to a function graph (vs the ubergraph). */
	UPROPERTY()
	bool bCurrentGraphIsFunction = false;

	/** Program-counter: id of the most recently added node in the anchored graph. */
	UPROPERTY()
	FString CurrentCursorNodeId;

	/** Visual cursor position in graph-space coordinates (unitless graph units). */
	FVector2D CurrentCursorPosition = FVector2D::ZeroVector;

	/** Handle for AssetRegistry::OnAssetUpdated delegate. */
	FDelegateHandle AssetUpdatedHandle;

	/** Handler for AssetRegistry asset-updated event — pushes WidgetBlueprint paths into history. */
	void HandleAssetUpdated(const struct FAssetData& AssetData);
};
