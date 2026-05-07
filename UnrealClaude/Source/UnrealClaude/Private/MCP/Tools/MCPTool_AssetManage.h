// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"
#include "AssetRegistry/AssetData.h"

/**
 * MCP Tool: asset_manage — write/CRUD operations on Unreal project assets.
 *
 * For pure read/search work, prefer asset_search instead.
 * This tool handles: search, find, list_folder, open_in_editor, save,
 * save_all_dirty, duplicate, move, and delete.
 *
 * IMPORTANT: The "delete" operation requires confirm_delete:true as a literal
 * boolean parameter — it will refuse to delete without explicit confirmation.
 * If the asset has referencers and force is not set, delete also refuses and
 * returns the referencing paths so the caller can decide.
 *
 * Dispatch is via the required "operation" string parameter.
 */
class FMCPTool_AssetManage : public FMCPToolBase
{
public:
	/**
	 * Returns tool metadata: name, description, parameter schema, and annotations.
	 * Registered as "asset_manage" with Modifying annotations (has destructive ops).
	 */
	virtual FMCPToolInfo GetInfo() const override;

	/**
	 * Entry point for all asset_manage calls.
	 * Reads the required "operation" parameter and dispatches to the appropriate
	 * ExecuteXxx() private method.
	 *
	 * @param Params  - JSON object containing at minimum {"operation": "<op>"} plus
	 *                  op-specific parameters.
	 * @return FMCPToolResult with success/error status and structured data.
	 */
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// ===== Operation Handlers =====

	/**
	 * Search assets by name, class, and/or path prefix.
	 * Params: query (required), class_filter?, path_filter? (default /Game/), max_count? (default 50).
	 * Returns: {assets:[...], count, total}
	 */
	FMCPToolResult ExecuteSearch(const TSharedRef<FJsonObject>& Params);

	/**
	 * Look up a single asset by its exact Content Browser path.
	 * Uses IAssetRegistry::GetAssetByObjectPath(FSoftObjectPath).
	 * Params: asset_path (required).
	 * Returns: {exists, path, name, class, package_path}
	 */
	FMCPToolResult ExecuteFind(const TSharedRef<FJsonObject>& Params);

	/**
	 * List all assets in a given folder, optionally recursive and filtered by class.
	 * Uses FARFilter with bRecursivePaths set from the recursive parameter.
	 * Params: folder_path (required), recursive? (default false), class_filter?.
	 * Returns: {assets:[...], count, total}
	 */
	FMCPToolResult ExecuteListFolder(const TSharedRef<FJsonObject>& Params);

	/**
	 * Open an asset in its native Unreal editor (Blueprint editor, material editor, etc.).
	 * Loads the asset with LoadObject then calls UAssetEditorSubsystem::OpenEditorForAsset.
	 * Params: asset_path (required).
	 * Returns: {ok:true, asset_path}
	 */
	FMCPToolResult ExecuteOpenInEditor(const TSharedRef<FJsonObject>& Params);

	/**
	 * Save a single asset to disk by its Content Browser path.
	 * Calls UEditorAssetLibrary::SaveAsset(AssetPath, bOnlyIfDirty=false).
	 * Params: asset_path (required).
	 * Returns: {ok:true, asset_path}
	 */
	FMCPToolResult ExecuteSave(const TSharedRef<FJsonObject>& Params);

	/**
	 * Save all currently dirty packages (both map and content packages).
	 * Uses UEditorLoadingAndSavingUtils::SaveDirtyPackages.
	 * Params: confirm? (default false — currently informational, always proceeds).
	 * Returns: {ok:true}
	 */
	FMCPToolResult ExecuteSaveAllDirty(const TSharedRef<FJsonObject>& Params);

	/**
	 * Duplicate an asset to a new path, creating an independent copy.
	 * Calls UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath).
	 * Params: source_path (required), dest_path (required).
	 * Returns: {ok:true, source_path, dest_path}
	 */
	FMCPToolResult ExecuteDuplicate(const TSharedRef<FJsonObject>& Params);

	/**
	 * Move/rename an asset, preserving all references automatically.
	 * Calls UEditorAssetLibrary::RenameAsset(SourcePath, DestPath) — UE's rename
	 * semantics include moving across folders and fixup of redirectors.
	 * Params: source_path (required), dest_path (required).
	 * Returns: {ok:true, source_path, dest_path}
	 */
	FMCPToolResult ExecuteMove(const TSharedRef<FJsonObject>& Params);

	/**
	 * Delete an asset from the project.
	 * Requires confirm_delete:true (literal boolean). If force is false (default),
	 * first queries IAssetRegistry for referencers and refuses if any exist,
	 * returning the referencing paths so the caller can decide whether to retry
	 * with force:true.
	 * Params: asset_path (required), confirm_delete:true (required), force? (default false).
	 * Returns: {ok:true, asset_path} on success, or Error with referencer paths if blocked.
	 */
	FMCPToolResult ExecuteDelete(const TSharedRef<FJsonObject>& Params);

	// ===== Shared Helpers =====

	/**
	 * Serialize a single FAssetData into a JSON object.
	 * Matches the pattern used in MCPTool_AssetSearch for consistency.
	 * Fields: path, name, class, package_path.
	 *
	 * @param AssetData - The asset registry entry to serialize.
	 * @return Shared JSON object with the four standard fields.
	 */
	TSharedPtr<FJsonObject> AssetDataToJson(const FAssetData& AssetData) const;

	/**
	 * Build a JSON array from a TArray of FAssetData.
	 * Calls AssetDataToJson() on each entry.
	 *
	 * @param Assets - Array of asset registry entries.
	 * @return JSON array of asset objects.
	 */
	TArray<TSharedPtr<FJsonValue>> AssetArrayToJson(const TArray<FAssetData>& Assets) const;
};
