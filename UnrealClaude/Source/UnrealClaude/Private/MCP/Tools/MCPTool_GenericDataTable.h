// Copyright Ban Ming. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class UDataTable;
class UScriptStruct;

/**
 * MCP Tool: Generic DataTable CRUD
 *
 * Authors any UDataTable whose row struct derives from FTableRowBase. Designed
 * for the case where MCPTool_CharacterData (hardcoded to FCharacterStatsRow)
 * cannot be used because the project owns its own row struct (e.g.
 * FCardDataRow in the Paoge module).
 *
 * Operations:
 *   - 'create_table'  : Create a new UDataTable with a caller-supplied row
 *                       struct path.
 *   - 'add_row'       : Append a row to an existing table. Row payload is a
 *                       JSON object marshalled into the row struct via
 *                       FJsonObjectConverter::JsonObjectToUStruct, so any
 *                       reflection-friendly UPROPERTY can be populated.
 *   - 'update_row'    : Patch an existing row (only fields present in the
 *                       JSON payload are written; missing fields keep their
 *                       previous values).
 *   - 'remove_row'    : Delete a row by name.
 *   - 'query_table'   : Read rows back as JSON. Optional row_name filter,
 *                       limit/offset for paging.
 *
 * Rationale for the value-object payload contract: the underlying UE API
 * (FDataTableEditorUtils::AddRow, JsonObjectToUStruct) operates on the row
 * buffer in place, so callers never see raw pointers and we never need to
 * know the row struct at compile time.
 *
 * Sprint 2 Story 2-14 closes the AC by using create_table + add_row to
 * author DT_Cards.uasset against /Script/Paoge.CardDataRow.
 */
class FMCPTool_GenericDataTable : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("generic_datatable");
		Info.Description = TEXT(
			"Create and manage UDataTable assets backed by any FTableRowBase-derived row struct.\n\n"
			"Operations:\n"
			"- 'create_table': Create a new DataTable. Requires asset_path + row_struct_path.\n"
			"- 'add_row': Append a row. Requires asset_path + row_name + row_data (JSON object).\n"
			"- 'update_row': Patch an existing row. Only fields present in row_data are written.\n"
			"- 'remove_row': Delete a row by name. Requires asset_path + row_name.\n"
			"- 'query_table': Read rows. Optional row_name (substring filter), limit, offset.\n\n"
			"row_data shape: a JSON object whose keys match UPROPERTY names (PascalCase) of the row struct.\n"
			"row_struct_path shape: '/Script/<ModuleName>.<RowStructName>' (no F prefix), e.g. '/Script/Paoge.CardDataRow'."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation to perform: create_table, add_row, update_row, remove_row, query_table"), true),

			// Asset identity
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"),
				TEXT("Full DataTable asset path, e.g. '/Game/Cards/DT_Cards' (no extension)"), true),
			FMCPToolParameter(TEXT("row_struct_path"), TEXT("string"),
				TEXT("Row struct script path for create_table only, e.g. '/Script/Paoge.CardDataRow'"), false),

			// Row addressing
			FMCPToolParameter(TEXT("row_name"), TEXT("string"),
				TEXT("Row key for add_row / update_row / remove_row; substring filter for query_table"), false),
			FMCPToolParameter(TEXT("row_data"), TEXT("object"),
				TEXT("Row payload as JSON object; keys are UPROPERTY names of the row struct"), false),

			// Query paging
			FMCPToolParameter(TEXT("limit"), TEXT("number"),
				TEXT("Max rows for query_table (1-1000, default 25)"), false, TEXT("25")),
			FMCPToolParameter(TEXT("offset"), TEXT("number"),
				TEXT("Skip first N rows for query_table"), false, TEXT("0"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** Dispatch helpers — one per operation. */
	FMCPToolResult ExecuteCreateTable(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAddRow(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteUpdateRow(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRemoveRow(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteQueryTable(const TSharedRef<FJsonObject>& Params);

	/**
	 * Resolve a script-path string to a UScriptStruct that derives from
	 * FTableRowBase.
	 *
	 * @param StructPath  Script path like '/Script/Paoge.CardDataRow'.
	 * @param OutError    Set to a human-readable diagnostic on failure.
	 * @return            Loaded struct on success, nullptr otherwise.
	 */
	UScriptStruct* ResolveRowStruct(const FString& StructPath, FString& OutError) const;

	/**
	 * Load an existing UDataTable from disk.
	 *
	 * @param AssetPath  Asset path like '/Game/Cards/DT_Cards'.
	 * @param OutError   Set on failure.
	 * @return           DataTable pointer on success, nullptr otherwise.
	 */
	UDataTable* LoadTable(const FString& AssetPath, FString& OutError) const;

	/**
	 * Persist a package to disk using the same FSavePackageArgs flags as
	 * MCPTool_CharacterData::SaveAsset, so editor-driven authoring round
	 * trips identically.
	 *
	 * @param Asset     Asset whose outermost package will be saved.
	 * @param OutError  Set on failure.
	 * @return          true on success.
	 */
	bool SaveAsset(UObject* Asset, FString& OutError) const;

	/**
	 * Marshal a row-buffer pointer into a JSON object via the row struct's
	 * reflection metadata. Used by query_table responses.
	 *
	 * @param RowStruct  Schema for the row buffer.
	 * @param RowMemory  Pointer to the row instance owned by the DataTable.
	 * @return           JSON object describing the row.
	 */
	TSharedPtr<FJsonObject> RowToJson(const UScriptStruct* RowStruct, const uint8* RowMemory) const;
};
