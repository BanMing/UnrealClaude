// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP
//
// Adapted from UmgMcp's UUmgFileTransformation::ExportWidgetToJson. The
// CDO-diff approach and the slot special-case are theirs; the error handling,
// the slot/property split, the file-write path, and the decision to omit the
// apply direction are not. See the header for why apply is withheld.

#include "MCPTool_UMGSnapshot.h"
#include "UMG/UMGCommonUtils.h"
#include "MCP/Sessions/UMGSessionSubsystem.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"

#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

TSharedPtr<FJsonObject> FMCPTool_UMGSnapshot::ExportChangedProperties(
	UObject* Object, bool bSkipSlot)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!Object)
	{
		return Out;
	}

	// The class default object is the baseline. Anything equal to it was not
	// authored and is therefore noise in a review diff.
	UObject* Defaults = Object->GetClass()->GetDefaultObject();

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;

		// Slot is the widget's layout binding, owned by the parent panel's
		// slot type rather than by the widget. It is serialized separately so
		// the diff shows that ownership instead of implying the widget owns
		// its own position.
		if (bSkipSlot && Property->GetFName() == TEXT("Slot"))
		{
			continue;
		}

		// Only designer-visible, persistent state. Transient fields are
		// runtime scratch and would make two snapshots of an unchanged asset
		// differ.
		if (!Property->HasAnyPropertyFlags(CPF_Edit) ||
			Property->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

#if WITH_EDITOR
		if (Property->HasAnyPropertyFlags(CPF_EditorOnly))
		{
			continue;
		}
#endif

		const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
		const void* Default = Property->ContainerPtrToValuePtr<void>(Defaults);
		if (Property->Identical(Value, Default))
		{
			continue;
		}

		if (TSharedPtr<FJsonValue> Json =
				FJsonObjectConverter::UPropertyToJsonValue(Property, Value))
		{
			Out->SetField(Property->GetName(), Json);
		}
	}

	return Out;
}

TSharedPtr<FJsonObject> FMCPTool_UMGSnapshot::ExportSlot(UPanelSlot* Slot)
{
	if (!Slot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Out = ExportChangedProperties(Slot, /*bSkipSlot=*/false);

	// Content and Parent are structural back-references — they restate the
	// tree shape the "children" nesting already conveys, and they serialize
	// as object paths that churn on every re-save, which would make a diff
	// noisy for an asset nobody edited.
	Out->RemoveField(TEXT("Content"));
	Out->RemoveField(TEXT("Parent"));

	return Out->Values.Num() > 0 ? Out : nullptr;
}

TSharedPtr<FJsonObject> FMCPTool_UMGSnapshot::ExportWidget(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("name"), Widget->GetName());
	Out->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());

	// is_variable decides whether the widget gets a C++/Blueprint-visible
	// member, which is what BindWidget / BindWidgetOptional match against.
	// Flipping it silently breaks those bindings, so it belongs in the diff.
	Out->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

	const TSharedPtr<FJsonObject> Properties =
		ExportChangedProperties(Widget, /*bSkipSlot=*/true);
	if (Properties->Values.Num() > 0)
	{
		Out->SetObjectField(TEXT("properties"), Properties);
	}

	if (const TSharedPtr<FJsonObject> SlotJson = ExportSlot(Widget->Slot))
	{
		Out->SetObjectField(TEXT("slot"), SlotJson);
	}

	// Recurse in sibling order. Order is part of the authored state — it
	// drives both layout and focus navigation — so it is preserved rather
	// than sorted.
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			if (TSharedPtr<FJsonObject> ChildJson = ExportWidget(Panel->GetChildAt(i)))
			{
				Children.Add(MakeShared<FJsonValueObject>(ChildJson));
			}
		}
		if (Children.Num() > 0)
		{
			Out->SetArrayField(TEXT("children"), Children);
		}
	}

	return Out;
}

/**
 * Execute — snapshot a Widget Blueprint to JSON.
 *
 * Steps:
 *   1. Resolve the blueprint path (honouring the UMG session anchor so the
 *      caller can omit it mid-conversation, matching umg_query/umg_modify).
 *   2. Pick the subtree root: the named widget, or the tree root.
 *   3. Serialize recursively.
 *   4. Optionally write the JSON to disk for a git-tracked review artifact.
 */
FMCPToolResult FMCPTool_UMGSnapshot::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Step 1 — same path fallback the other UMG tools use, so this tool can be
	// dropped into an existing editing loop without re-stating the asset.
	UUMGSessionSubsystem::ApplyWidgetBlueprintPathFallback(Params);

	FString BPPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractAndValidate(Params, TEXT("widget_blueprint_path"),
		FMCPParamValidator::ValidateBlueprintPath, BPPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}
	if (!WBP->WidgetTree)
	{
		return FMCPToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
	}

	// Step 2 — whole tree, or one named subtree.
	FString WidgetName;
	Params->TryGetStringField(TEXT("widget_name"), WidgetName);

	UWidget* Root = nullptr;
	if (!WidgetName.IsEmpty())
	{
		Root = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
		if (!Root)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Widget '%s' not found in %s"), *WidgetName, *BPPath));
		}
	}
	else
	{
		Root = WBP->WidgetTree->RootWidget;
		if (!Root)
		{
			// Not an error: a freshly created Widget Blueprint legitimately
			// has no root yet, and reporting that plainly is more useful than
			// failing the call.
			TSharedPtr<FJsonObject> EmptyResult = MakeShared<FJsonObject>();
			EmptyResult->SetStringField(TEXT("widget_blueprint_path"), BPPath);
			EmptyResult->SetBoolField(TEXT("empty"), true);
			return FMCPToolResult::Success(
				FString::Printf(TEXT("%s has no root widget (empty tree)"), *BPPath),
				EmptyResult);
		}
	}

	// Step 3 — serialize.
	const TSharedPtr<FJsonObject> TreeJson = ExportWidget(Root);
	if (!TreeJson.IsValid())
	{
		return FMCPToolResult::Error(TEXT("Failed to serialize widget tree"));
	}

	TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
	Snapshot->SetStringField(TEXT("widget_blueprint_path"), BPPath);
	Snapshot->SetObjectField(TEXT("root"), TreeJson);

	// Pretty-printed, because the entire point is human review. Compact JSON
	// would put the whole tree on one line and diff as a single changed line.
	FString Serialized;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(Snapshot.ToSharedRef(), Writer);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
	Result->SetObjectField(TEXT("snapshot"), Snapshot);

	// Step 4 — optional file write, for checking snapshots into git alongside
	// the binary asset they describe.
	FString WriteTo;
	if (Params->TryGetStringField(TEXT("write_to_file"), WriteTo) && !WriteTo.IsEmpty())
	{
		const FString Dir = FPaths::GetPath(WriteTo);
		if (!Dir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		}

		// UTF-8 without BOM: this file is read by diff tools and parsers, not
		// by Excel. (The project's BOM rule applies to CJK-bearing CSVs.)
		if (FFileHelper::SaveStringToFile(Serialized, *WriteTo,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Result->SetStringField(TEXT("written_to"), WriteTo);
		}
		else
		{
			// Report rather than fail: the snapshot itself succeeded and is in
			// the result payload, so the caller still has what they asked for.
			Result->SetStringField(TEXT("write_error"), FString::Printf(
				TEXT("Could not write to '%s' (check the path and permissions)"),
				*WriteTo));
		}
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Snapshotted %s%s"), *BPPath,
			WidgetName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (subtree '%s')"), *WidgetName)),
		Result);
}
