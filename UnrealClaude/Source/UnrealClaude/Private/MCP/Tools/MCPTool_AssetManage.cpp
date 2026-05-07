// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#include "MCPTool_AssetManage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"           // duplicate/rename/delete/save
#include "FileHelpers.h"                   // UEditorLoadingAndSavingUtils
#include "Subsystems/AssetEditorSubsystem.h"  // OpenEditorForAsset
#include "Editor.h"                         // GEditor

// ============================================================
// GetInfo
// ============================================================

FMCPToolInfo FMCPTool_AssetManage::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("asset_manage");
	Info.Description = TEXT(
		"Write/CRUD operations for Unreal project assets. "
		"For pure read/search, prefer asset_search instead.\n\n"
		"Operations (set via required 'operation' parameter):\n"
		"  search         — find assets by name substring, class, and/or path prefix\n"
		"  find           — look up a single asset by exact Content Browser path\n"
		"  list_folder    — list assets in a folder (optionally recursive)\n"
		"  open_in_editor — open an asset in its native Unreal editor\n"
		"  save           — save a single asset to disk\n"
		"  save_all_dirty — save all currently dirty packages\n"
		"  duplicate      — copy an asset to a new path\n"
		"  move           — move/rename an asset, preserving references\n"
		"  delete         — delete an asset (requires confirm_delete:true; blocked if referenced unless force:true)\n\n"
		"SAFETY: 'delete' will not proceed without confirm_delete set to the literal boolean true. "
		"If the asset has referencers, delete also refuses and returns their paths unless force:true is passed."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"),      TEXT("string"),  TEXT("Operation to perform: search | find | list_folder | open_in_editor | save | save_all_dirty | duplicate | move | delete"), true),
		FMCPToolParameter(TEXT("query"),          TEXT("string"),  TEXT("(search) Name substring to match against asset names, case-insensitive"), false),
		FMCPToolParameter(TEXT("asset_path"),     TEXT("string"),  TEXT("(find/open_in_editor/save/delete) Exact Content Browser path to the asset, e.g. /Game/Characters/BP_Player"), false),
		FMCPToolParameter(TEXT("folder_path"),    TEXT("string"),  TEXT("(list_folder) Content Browser folder to list, e.g. /Game/Characters/"), false),
		FMCPToolParameter(TEXT("source_path"),    TEXT("string"),  TEXT("(duplicate/move) Source asset Content Browser path"), false),
		FMCPToolParameter(TEXT("dest_path"),      TEXT("string"),  TEXT("(duplicate/move) Destination asset Content Browser path"), false),
		FMCPToolParameter(TEXT("class_filter"),   TEXT("string"),  TEXT("(search/list_folder) Asset class to filter by, e.g. Blueprint, StaticMesh, Texture2D"), false),
		FMCPToolParameter(TEXT("path_filter"),    TEXT("string"),  TEXT("(search) Path prefix to search within. Default: /Game/"), false, TEXT("/Game/")),
		FMCPToolParameter(TEXT("max_count"),      TEXT("number"),  TEXT("(search) Maximum number of results to return. Default: 50"), false, TEXT("50")),
		FMCPToolParameter(TEXT("recursive"),      TEXT("boolean"), TEXT("(list_folder) Whether to recurse into sub-folders. Default: false"), false, TEXT("false")),
		FMCPToolParameter(TEXT("confirm_delete"), TEXT("boolean"), TEXT("(delete) Must be literal true to allow deletion. Safety gate — omitting or false causes an error."), false),
		FMCPToolParameter(TEXT("force"),          TEXT("boolean"), TEXT("(delete) If true, delete even when referencers exist. Default: false"), false, TEXT("false")),
		FMCPToolParameter(TEXT("confirm"),        TEXT("boolean"), TEXT("(save_all_dirty) Informational confirmation flag. Default: false"), false, TEXT("false")),
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

// ============================================================
// Execute — dispatch on "operation"
// ============================================================

FMCPToolResult FMCPTool_AssetManage::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Read the required "operation" parameter.
	FString Operation;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Dispatch to the matching handler.
	if (Operation == TEXT("search"))          return ExecuteSearch(Params);
	if (Operation == TEXT("find"))            return ExecuteFind(Params);
	if (Operation == TEXT("list_folder"))     return ExecuteListFolder(Params);
	if (Operation == TEXT("open_in_editor"))  return ExecuteOpenInEditor(Params);
	if (Operation == TEXT("save"))            return ExecuteSave(Params);
	if (Operation == TEXT("save_all_dirty"))  return ExecuteSaveAllDirty(Params);
	if (Operation == TEXT("duplicate"))       return ExecuteDuplicate(Params);
	if (Operation == TEXT("move"))            return ExecuteMove(Params);
	if (Operation == TEXT("delete"))          return ExecuteDelete(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Expected: search | find | list_folder | open_in_editor | save | save_all_dirty | duplicate | move | delete"),
		*Operation));
}

// ============================================================
// ExecuteSearch
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteSearch(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse parameters.
	FString Query;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("query"), Query, ParamError))
	{
		return ParamError.GetValue();
	}
	const FString ClassFilter = ExtractOptionalString(Params, TEXT("class_filter"));
	const FString PathFilter  = ExtractOptionalString(Params, TEXT("path_filter"), TEXT("/Game/"));
	const int32   MaxCount    = FMath::Clamp(ExtractOptionalNumber<int32>(Params, TEXT("max_count"), 50), 1, 1000);

	// Step 2: Build FARFilter — recursive by default for a search operation.
	FARFilter Filter;
	Filter.bRecursivePaths   = true;
	Filter.bRecursiveClasses = true;
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
	}
	if (!ClassFilter.IsEmpty())
	{
		// Resolve short names against common script packages, mirroring MCPTool_AssetSearch.
		FString ClassPath = ClassFilter;
		if (!ClassPath.StartsWith(TEXT("/")))
		{
			UClass* Found = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter));
			if (!Found) { Found = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *ClassFilter)); }
			if (!Found) { Found = FindObject<UClass>(nullptr, *ClassFilter); }
			if (Found)  { ClassPath = Found->GetClassPathName().ToString(); }
			else        { ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter); }
		}
		Filter.ClassPaths.Add(FTopLevelAssetPath(ClassPath));
	}

	// Step 3: Query the asset registry.
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AllAssets;
	Module.Get().GetAssets(Filter, AllAssets);

	// Step 4: Post-filter by name substring (case-insensitive).
	TArray<FAssetData> Matched;
	for (const FAssetData& Asset : AllAssets)
	{
		if (Asset.AssetName.ToString().Contains(Query, ESearchCase::IgnoreCase))
		{
			Matched.Add(Asset);
		}
	}

	// Step 5: Cap results at max_count.
	const int32 Total = Matched.Num();
	const int32 Count = FMath::Min(Total, MaxCount);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (int32 i = 0; i < Count; ++i)
	{
		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetDataToJson(Matched[i])));
	}
	ResultData->SetArrayField(TEXT("assets"), AssetsArray);
	ResultData->SetNumberField(TEXT("count"),  Count);
	ResultData->SetNumberField(TEXT("total"),  Total);

	const FString Message = (Total == 0)
		? FString::Printf(TEXT("No assets found matching query '%s'"), *Query)
		: FString::Printf(TEXT("Found %d asset%s matching '%s'%s"),
			Count, Count == 1 ? TEXT("") : TEXT("s"), *Query,
			Count < Total ? *FString::Printf(TEXT(" (%d total, showing first %d)"), Total, Count) : TEXT(""));

	return FMCPToolResult::Success(Message, ResultData);
}

// ============================================================
// ExecuteFind
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteFind(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse required asset_path.
	FString AssetPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Query the asset registry by object path.
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const FAssetData AssetData = Module.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));

	// Step 3: Build result.
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	const bool bExists = AssetData.IsValid();
	ResultData->SetBoolField(TEXT("exists"), bExists);
	if (bExists)
	{
		ResultData->SetStringField(TEXT("path"),         AssetData.GetObjectPathString());
		ResultData->SetStringField(TEXT("name"),         AssetData.AssetName.ToString());
		ResultData->SetStringField(TEXT("class"),        AssetData.AssetClassPath.GetAssetName().ToString());
		ResultData->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	}

	const FString Message = bExists
		? FString::Printf(TEXT("Found asset: %s"), *AssetPath)
		: FString::Printf(TEXT("No asset found at path: %s"), *AssetPath);

	return FMCPToolResult::Success(Message, ResultData);
}

// ============================================================
// ExecuteListFolder
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteListFolder(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse parameters.
	FString FolderPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, ParamError))
	{
		return ParamError.GetValue();
	}
	const bool    bRecursive  = ExtractOptionalBool(Params, TEXT("recursive"), false);
	const FString ClassFilter = ExtractOptionalString(Params, TEXT("class_filter"));

	// Step 2: Build FARFilter.
	FARFilter Filter;
	Filter.bRecursivePaths   = bRecursive;
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(*FolderPath));
	if (!ClassFilter.IsEmpty())
	{
		FString ClassPath = ClassFilter;
		if (!ClassPath.StartsWith(TEXT("/")))
		{
			UClass* Found = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter));
			if (!Found) { Found = FindObject<UClass>(nullptr, *ClassFilter); }
			if (Found)  { ClassPath = Found->GetClassPathName().ToString(); }
			else        { ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter); }
		}
		Filter.ClassPaths.Add(FTopLevelAssetPath(ClassPath));
	}

	// Step 3: Query and build result.
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	Module.Get().GetAssets(Filter, Assets);

	const int32 Total = Assets.Num();
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("assets"), AssetArrayToJson(Assets));
	ResultData->SetNumberField(TEXT("count"),  Total);
	ResultData->SetNumberField(TEXT("total"),  Total);

	const FString Message = FString::Printf(
		TEXT("Listed %d asset%s in '%s'%s"),
		Total, Total == 1 ? TEXT("") : TEXT("s"), *FolderPath,
		bRecursive ? TEXT(" (recursive)") : TEXT(""));

	return FMCPToolResult::Success(Message, ResultData);
}

// ============================================================
// ExecuteOpenInEditor
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteOpenInEditor(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse asset_path.
	FString AssetPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Guard — GEditor must be available (editor-only operation).
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("GEditor is not available. This operation requires the Unreal Editor."));
	}

	// Step 3: Load the UObject for the asset.
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath));
	}

	// Step 4: Open via UAssetEditorSubsystem.
	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return FMCPToolResult::Error(TEXT("UAssetEditorSubsystem is not available."));
	}
	EditorSubsystem->OpenEditorForAsset(Asset);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"),        true);
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Opened editor for asset: %s"), *AssetPath),
		ResultData);
}

// ============================================================
// ExecuteSave
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteSave(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse asset_path.
	FString AssetPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Save — bOnlyIfDirty=false ensures a forced save.
	const bool bOk = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfDirty=*/false);
	if (!bOk)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"),          true);
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Saved asset: %s"), *AssetPath),
		ResultData);
}

// ============================================================
// ExecuteSaveAllDirty
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteSaveAllDirty(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Save all dirty map and content packages.
	// bSaveMapPackages=true, bSaveContentPackages=true.
	UEditorLoadingAndSavingUtils::SaveDirtyPackages(/*bSaveMapPackages=*/true, /*bSaveContentPackages=*/true);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"), true);

	return FMCPToolResult::Success(TEXT("Saved all dirty packages."), ResultData);
}

// ============================================================
// ExecuteDuplicate
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteDuplicate(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse both path parameters.
	FString SourcePath, DestPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("source_path"), SourcePath, ParamError)) return ParamError.GetValue();
	if (!ExtractRequiredString(Params, TEXT("dest_path"),   DestPath,   ParamError)) return ParamError.GetValue();

	// Step 2: Duplicate via EditorAssetLibrary.
	UObject* NewAsset = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	if (!NewAsset)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to duplicate '%s' to '%s'"), *SourcePath, *DestPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"),          true);
	ResultData->SetStringField(TEXT("source_path"), SourcePath);
	ResultData->SetStringField(TEXT("dest_path"),   DestPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Duplicated '%s' to '%s'"), *SourcePath, *DestPath),
		ResultData);
}

// ============================================================
// ExecuteMove
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteMove(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse both path parameters.
	FString SourcePath, DestPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("source_path"), SourcePath, ParamError)) return ParamError.GetValue();
	if (!ExtractRequiredString(Params, TEXT("dest_path"),   DestPath,   ParamError)) return ParamError.GetValue();

	// Step 2: Rename (move) via EditorAssetLibrary.
	// RenameAsset covers both rename and cross-folder moves, and automatically
	// creates redirectors to preserve existing references.
	const bool bOk = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);
	if (!bOk)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to move '%s' to '%s'"), *SourcePath, *DestPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"),          true);
	ResultData->SetStringField(TEXT("source_path"), SourcePath);
	ResultData->SetStringField(TEXT("dest_path"),   DestPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Moved '%s' to '%s'"), *SourcePath, *DestPath),
		ResultData);
}

// ============================================================
// ExecuteDelete
// ============================================================

FMCPToolResult FMCPTool_AssetManage::ExecuteDelete(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Parse asset_path.
	FString AssetPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Require explicit confirm_delete:true — never delete silently.
	// The parameter must be present AND set to the literal boolean true.
	bool bConfirm = false;
	if (!Params->TryGetBoolField(TEXT("confirm_delete"), bConfirm) || !bConfirm)
	{
		return FMCPToolResult::Error(TEXT(
			"'confirm_delete' must be set to literal boolean true to delete an asset. "
			"This is a safety gate — set confirm_delete:true to proceed."));
	}

	// Step 3: Unless force:true, check for referencers and refuse if any exist.
	const bool bForce = ExtractOptionalBool(Params, TEXT("force"), false);
	if (!bForce)
	{
		// Derive the package name from the asset path (strip the asset name after the last dot).
		// Content Browser paths like /Game/Folder/AssetName map to package /Game/Folder/AssetName.
		FName PackageName(*AssetPath);
		// If the path contains a dot, use only the left portion as the package name.
		FString PackageStr = AssetPath;
		int32 DotIdx;
		if (PackageStr.FindLastChar(TEXT('.'), DotIdx))
		{
			PackageStr = PackageStr.Left(DotIdx);
		}
		PackageName = FName(*PackageStr);

		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FName> ReferencerNames;
		Module.Get().GetReferencers(PackageName, ReferencerNames);

		if (ReferencerNames.Num() > 0)
		{
			// Build a human-readable list of referencing packages.
			FString RefList;
			for (const FName& Ref : ReferencerNames)
			{
				RefList += TEXT("\n  ") + Ref.ToString();
			}
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Cannot delete '%s' — it is referenced by %d package(s):%s\n\n"
				     "To delete anyway, pass force:true."),
				*AssetPath, ReferencerNames.Num(), *RefList));
		}
	}

	// Step 4: Delete the asset.
	const bool bOk = UEditorAssetLibrary::DeleteAsset(AssetPath);
	if (!bOk)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to delete asset: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("ok"),          true);
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted asset: %s"), *AssetPath),
		ResultData);
}

// ============================================================
// Shared helpers
// ============================================================

TSharedPtr<FJsonObject> FMCPTool_AssetManage::AssetDataToJson(const FAssetData& AssetData) const
{
	// Mirror the field names used in MCPTool_AssetSearch for consistency.
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("path"),         AssetData.GetObjectPathString());
	Json->SetStringField(TEXT("name"),         AssetData.AssetName.ToString());
	Json->SetStringField(TEXT("class"),        AssetData.AssetClassPath.GetAssetName().ToString());
	Json->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	return Json;
}

TArray<TSharedPtr<FJsonValue>> FMCPTool_AssetManage::AssetArrayToJson(const TArray<FAssetData>& Assets) const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		Out.Add(MakeShared<FJsonValueObject>(AssetDataToJson(Asset)));
	}
	return Out;
}
