// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_UMGSession.h"
#include "MCP/Sessions/UMGSessionSubsystem.h"
#include "UnrealClaudeModule.h"

FMCPToolResult FMCPTool_UMGSession::Execute(const TSharedRef<FJsonObject>& Params)
{
	UUMGSessionSubsystem* Session = UUMGSessionSubsystem::Get();
	if (!Session)
	{
		return FMCPToolResult::Error(TEXT("UMG session subsystem unavailable (editor not initialized)"));
	}

	FString Operation;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
	{
		return ParamError.GetValue();
	}

	// ----- get_target -----
	if (Operation == TEXT("get_target"))
	{
		const FString& Cur = Session->GetCurrentTarget();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("current_target"), Cur);
		Data->SetBoolField(TEXT("has_target"), !Cur.IsEmpty());
		return FMCPToolResult::Success(
			Cur.IsEmpty() ? TEXT("No UMG target anchored") : FString::Printf(TEXT("Current target: %s"), *Cur),
			Data);
	}

	// ----- set_target -----
	if (Operation == TEXT("set_target"))
	{
		FString AssetPath;
		if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
		{
			return ParamError.GetValue();
		}
		if (!Session->SetCurrentTarget(AssetPath))
		{
			return FMCPToolResult::Error(TEXT("Empty asset_path"));
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("current_target"), AssetPath);
		return FMCPToolResult::Success(FString::Printf(TEXT("Anchored to %s"), *AssetPath), Data);
	}

	// ----- get_last_edited -----
	if (Operation == TEXT("get_last_edited"))
	{
		const FString Last = Session->GetLastEdited();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("last_edited"), Last);
		Data->SetBoolField(TEXT("has_history"), !Last.IsEmpty());
		return FMCPToolResult::Success(
			Last.IsEmpty() ? TEXT("History empty") : FString::Printf(TEXT("Last edited: %s"), *Last),
			Data);
	}

	// ----- get_recently_edited -----
	if (Operation == TEXT("get_recently_edited"))
	{
		const int32 MaxCount = ExtractOptionalNumber<int32>(Params, TEXT("max_count"), 5);
		const TArray<FString> Recent = Session->GetRecentlyEdited(MaxCount);

		TArray<TSharedPtr<FJsonValue>> JsonRecent;
		for (const FString& P : Recent)
		{
			JsonRecent.Add(MakeShared<FJsonValueString>(P));
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("recent"), JsonRecent);
		Data->SetNumberField(TEXT("count"), Recent.Num());
		return FMCPToolResult::Success(
			FString::Printf(TEXT("%d recent entries"), Recent.Num()),
			Data);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get_target, set_target, get_last_edited, get_recently_edited"),
		*Operation));
}
