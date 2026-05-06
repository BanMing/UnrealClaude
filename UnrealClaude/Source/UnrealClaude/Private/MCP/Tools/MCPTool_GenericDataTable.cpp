// Copyright Ban Ming. All Rights Reserved.

#include "MCPTool_GenericDataTable.h"

#include "Engine/DataTable.h"
#include "DataTableEditorUtils.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

FMCPToolResult FMCPTool_GenericDataTable::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Pull the operation selector. All branches share asset_path so
	// we let each helper extract the rest of its inputs to keep error
	// messages local to the operation that needs them.
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	// Step 2: Dispatch. Lower-case match keeps the public surface stable
	// against minor LLM casing drift; we mirror the case the docstring
	// advertises in the rejection message below.
	if (Operation == TEXT("create_table"))
	{
		return ExecuteCreateTable(Params);
	}
	if (Operation == TEXT("add_row"))
	{
		return ExecuteAddRow(Params);
	}
	if (Operation == TEXT("update_row"))
	{
		return ExecuteUpdateRow(Params);
	}
	if (Operation == TEXT("remove_row"))
	{
		return ExecuteRemoveRow(Params);
	}
	if (Operation == TEXT("query_table"))
	{
		return ExecuteQueryTable(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create_table, add_row, update_row, remove_row, query_table"),
		*Operation));
}

UScriptStruct* FMCPTool_GenericDataTable::ResolveRowStruct(const FString& StructPath, FString& OutError) const
{
	// Step 1: Load by script-path. UScriptStruct lives in the script
	// package, so LoadObject is the canonical accessor.
	if (StructPath.IsEmpty())
	{
		OutError = TEXT("row_struct_path is empty");
		return nullptr;
	}

	UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *StructPath);
	if (!Struct)
	{
		OutError = FString::Printf(TEXT("Could not resolve row struct '%s' (expected '/Script/<Module>.<Struct>')"), *StructPath);
		return nullptr;
	}

	// Step 2: Verify FTableRowBase ancestry. Without this guard a caller
	// could pass an arbitrary USTRUCT and produce a corrupt DataTable that
	// crashes on row iteration.
	const UScriptStruct* TableRowBase = TBaseStructure<FTableRowBase>::Get();
	if (!Struct->IsChildOf(TableRowBase))
	{
		OutError = FString::Printf(TEXT("Row struct '%s' must derive from FTableRowBase"), *StructPath);
		return nullptr;
	}

	return Struct;
}

UDataTable* FMCPTool_GenericDataTable::LoadTable(const FString& AssetPath, FString& OutError) const
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("asset_path is empty");
		return nullptr;
	}

	UDataTable* Table = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (!Table)
	{
		OutError = FString::Printf(TEXT("DataTable not found at '%s'"), *AssetPath);
		return nullptr;
	}
	return Table;
}

bool FMCPTool_GenericDataTable::SaveAsset(UObject* Asset, FString& OutError) const
{
	// Mirror MCPTool_CharacterData::SaveAsset so both tools land the same
	// FSavePackageArgs flags on disk; future Validator passes can rely on
	// uniform Standalone / Public package state.
	if (!Asset)
	{
		OutError = TEXT("Cannot save null asset");
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Asset has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	const FSavePackageResultStruct Result = UPackage::Save(Package, Asset, *PackageFileName, SaveArgs);
	if (Result.Result != ESavePackageResult::Success)
	{
		OutError = FString::Printf(TEXT("Failed to save asset: %s"), *PackageFileName);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMCPTool_GenericDataTable::RowToJson(const UScriptStruct* RowStruct, const uint8* RowMemory) const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!RowStruct || !RowMemory)
	{
		return Json;
	}

	// FJsonObjectConverter walks the UPROPERTY reflection — same machinery
	// the editor's "Export to JSON" command uses, so output matches the
	// asset's source-of-truth representation.
	FJsonObjectConverter::UStructToJsonObject(RowStruct, RowMemory, Json.ToSharedRef(), 0, 0);
	return Json;
}

FMCPToolResult FMCPTool_GenericDataTable::ExecuteCreateTable(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Required parameters: asset_path + row_struct_path.
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString StructPath;
	if (!ExtractRequiredString(Params, TEXT("row_struct_path"), StructPath, Error))
	{
		return Error.GetValue();
	}

	// Step 2: Resolve and validate the row struct before touching the
	// filesystem; a struct-resolution error shouldn't leave a half-created
	// .uasset on disk.
	FString StructError;
	UScriptStruct* RowStruct = ResolveRowStruct(StructPath, StructError);
	if (!RowStruct)
	{
		return FMCPToolResult::Error(StructError);
	}

	// Step 3: Refuse to overwrite an existing table — create_table is
	// constructive only. Update operations have their own dispatch path.
	if (LoadObject<UDataTable>(nullptr, *AssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Asset already exists at '%s'; use add_row/update_row instead"), *AssetPath));
	}

	// Step 4: Split asset_path into package-path + asset-name. UE convention:
	// '/Game/Cards/DT_Cards' -> package '/Game/Cards/DT_Cards', asset 'DT_Cards'.
	FString PackagePath;
	FString AssetName;
	if (!AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd) || AssetName.IsEmpty())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Could not split asset_path '%s' into package + name"), *AssetPath));
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *AssetPath));
	}

	// Step 5: Construct the empty DataTable, bind its row struct, and save.
	UDataTable* Table = NewObject<UDataTable>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Table)
	{
		return FMCPToolResult::Error(TEXT("Failed to construct UDataTable"));
	}
	Table->RowStruct = RowStruct;
	Package->MarkPackageDirty();

	FString SaveError;
	if (!SaveAsset(Table, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	FAssetRegistryModule::AssetCreated(Table);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created DataTable '%s' (row struct: %s)"), *AssetPath, *RowStruct->GetName()),
		ResultData);
}

FMCPToolResult FMCPTool_GenericDataTable::ExecuteAddRow(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Required: asset_path + row_name + row_data (JSON object).
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString RowName;
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Error))
	{
		return Error.GetValue();
	}

	const TSharedPtr<FJsonObject>* RowDataPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("row_data"), RowDataPtr) || !RowDataPtr || !(*RowDataPtr).IsValid())
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: row_data (must be a JSON object)"));
	}
	const TSharedRef<FJsonObject> RowData = (*RowDataPtr).ToSharedRef();

	// Step 2: Load + validate target table.
	FString LoadError;
	UDataTable* Table = LoadTable(AssetPath, LoadError);
	if (!Table)
	{
		return FMCPToolResult::Error(LoadError);
	}
	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable '%s' has no row struct"), *AssetPath));
	}

	const FName RowKey(*RowName);
	if (Table->GetRowMap().Contains(RowKey))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' already exists"), *RowName));
	}

	// Step 3: Allocate a new row through FDataTableEditorUtils. The returned
	// pointer is owned by the table; AddRow has already CDO-initialized it,
	// so we only need to overwrite fields the JSON payload supplies.
	uint8* RowMemory = FDataTableEditorUtils::AddRow(Table, RowKey);
	if (!RowMemory)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("FDataTableEditorUtils::AddRow failed for '%s'"), *RowName));
	}

	// Step 4: Marshal JSON onto the row buffer. JsonObjectToUStruct only
	// writes properties that appear in the payload, leaving CDO defaults
	// for everything else — useful for Story 2-14 placeholders that omit
	// soft asset refs.
	FText FailReason;
	if (!FJsonObjectConverter::JsonObjectToUStruct(RowData, RowStruct, RowMemory, 0, 0, false, &FailReason))
	{
		// Roll back the partial row so the asset stays clean for retry.
		FDataTableEditorUtils::RemoveRow(Table, RowKey);
		return FMCPToolResult::Error(FString::Printf(TEXT("JsonObjectToUStruct failed for row '%s': %s"), *RowName, *FailReason.ToString()));
	}

	Table->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Table, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("row_name"), RowName);
	ResultData->SetObjectField(TEXT("row"), RowToJson(RowStruct, RowMemory));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added row '%s' to '%s'"), *RowName, *AssetPath),
		ResultData);
}

FMCPToolResult FMCPTool_GenericDataTable::ExecuteUpdateRow(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Required parameters mirror add_row.
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString RowName;
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Error))
	{
		return Error.GetValue();
	}

	const TSharedPtr<FJsonObject>* RowDataPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("row_data"), RowDataPtr) || !RowDataPtr || !(*RowDataPtr).IsValid())
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: row_data (must be a JSON object)"));
	}
	const TSharedRef<FJsonObject> RowData = (*RowDataPtr).ToSharedRef();

	// Step 2: Load table + locate row in place; FindRowUnchecked returns
	// the same pointer the table itself owns, so writes land directly on
	// the asset's row buffer.
	FString LoadError;
	UDataTable* Table = LoadTable(AssetPath, LoadError);
	if (!Table)
	{
		return FMCPToolResult::Error(LoadError);
	}
	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable '%s' has no row struct"), *AssetPath));
	}

	uint8* RowMemory = Table->FindRowUnchecked(FName(*RowName));
	if (!RowMemory)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in '%s'"), *RowName, *AssetPath));
	}

	// Step 3: Patch in place. JsonObjectToUStruct preserves untouched
	// fields, so update_row functions as a partial-update (PATCH) operation.
	FText FailReason;
	if (!FJsonObjectConverter::JsonObjectToUStruct(RowData, RowStruct, RowMemory, 0, 0, false, &FailReason))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("JsonObjectToUStruct failed for row '%s': %s"), *RowName, *FailReason.ToString()));
	}

	Table->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Table, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("row_name"), RowName);
	ResultData->SetObjectField(TEXT("row"), RowToJson(RowStruct, RowMemory));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Updated row '%s' in '%s'"), *RowName, *AssetPath),
		ResultData);
}

FMCPToolResult FMCPTool_GenericDataTable::ExecuteRemoveRow(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString RowName;
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UDataTable* Table = LoadTable(AssetPath, LoadError);
	if (!Table)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const FName RowKey(*RowName);
	if (!Table->GetRowMap().Contains(RowKey))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in '%s'"), *RowName, *AssetPath));
	}

	if (!FDataTableEditorUtils::RemoveRow(Table, RowKey))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("FDataTableEditorUtils::RemoveRow failed for '%s'"), *RowName));
	}

	Table->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Table, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("row_name"), RowName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed row '%s' from '%s'"), *RowName, *AssetPath),
		ResultData);
}

FMCPToolResult FMCPTool_GenericDataTable::ExecuteQueryTable(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UDataTable* Table = LoadTable(AssetPath, LoadError);
	if (!Table)
	{
		return FMCPToolResult::Error(LoadError);
	}
	const UScriptStruct* RowStruct = Table->GetRowStruct();

	const FString RowFilter = ExtractOptionalString(Params, TEXT("row_name"));
	const int32 Limit = FMath::Clamp(ExtractOptionalNumber<int32>(Params, TEXT("limit"), 25), 1, 1000);
	const int32 Offset = FMath::Max(0, ExtractOptionalNumber<int32>(Params, TEXT("offset"), 0));

	// Step 1: Walk RowMap directly. This is the same iteration the engine's
	// own FindRow uses; output ordering follows insertion order (FName hash
	// stable across save/load).
	TArray<TSharedPtr<FJsonValue>> RowsArray;
	int32 TotalMatches = 0;
	int32 SkippedCount = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		if (!RowFilter.IsEmpty() && !Pair.Key.ToString().Contains(RowFilter))
		{
			continue;
		}
		++TotalMatches;
		if (SkippedCount < Offset)
		{
			++SkippedCount;
			continue;
		}
		if (RowsArray.Num() >= Limit)
		{
			continue;
		}
		TSharedPtr<FJsonObject> RowJson = RowToJson(RowStruct, Pair.Value);
		RowJson->SetStringField(TEXT("row_name"), Pair.Key.ToString());
		RowsArray.Add(MakeShared<FJsonValueObject>(RowJson));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("row_struct"), RowStruct ? RowStruct->GetPathName() : FString());
	ResultData->SetArrayField(TEXT("rows"), RowsArray);
	ResultData->SetNumberField(TEXT("count"), RowsArray.Num());
	ResultData->SetNumberField(TEXT("total"), TotalMatches);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d row(s) in '%s'"), TotalMatches, *AssetPath),
		ResultData);
}
