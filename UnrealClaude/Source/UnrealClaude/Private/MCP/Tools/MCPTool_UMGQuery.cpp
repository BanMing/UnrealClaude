// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_UMGQuery.h"
#include "UMG/UMGCommonUtils.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"

#include "JsonObjectConverter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UMGQueryOps
{
    static const FString GetWidgetTree = TEXT("get_widget_tree");
    static const FString QueryWidgetProperties = TEXT("query_widget_properties");
    static const FString GetWidgetSchema = TEXT("get_widget_schema");
    static const FString GetLayoutData = TEXT("get_layout_data");
    static const FString GetCreatableWidgetTypes = TEXT("get_creatable_widget_types");
}

FMCPToolResult FMCPTool_UMGQuery::Execute(const TSharedRef<FJsonObject>& Params)
{
    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }
    Operation = Operation.ToLower();

    if (Operation == UMGQueryOps::GetWidgetTree)             { return ExecuteGetWidgetTree(Params); }
    if (Operation == UMGQueryOps::QueryWidgetProperties)     { return ExecuteQueryWidgetProperties(Params); }
    if (Operation == UMGQueryOps::GetWidgetSchema)           { return ExecuteGetWidgetSchema(Params); }
    if (Operation == UMGQueryOps::GetLayoutData)             { return ExecuteGetLayoutData(Params); }
    if (Operation == UMGQueryOps::GetCreatableWidgetTypes)   { return ExecuteGetCreatableWidgetTypes(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: get_widget_tree, query_widget_properties, get_widget_schema, get_layout_data, get_creatable_widget_types"),
        *Operation));
}

FMCPToolResult FMCPTool_UMGQuery::ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params)
{
    // Step 1. Resolve the blueprint.
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

    // Step 2. Walk the tree starting from RootWidget (may be null on freshly created BPs).
    UWidget* Root = WBP->WidgetTree->RootWidget;
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
    if (Root)
    {
        Result->SetObjectField(TEXT("root"), UMGCommonUtils::ExportWidgetTreeToJson(Root));
    }
    else
    {
        Result->SetField(TEXT("root"), MakeShared<FJsonValueNull>());
    }

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Read widget tree of %s"), *BPPath),
        Result);
}

FMCPToolResult FMCPTool_UMGQuery::ExecuteQueryWidgetProperties(const TSharedRef<FJsonObject>& Params)
{
    FString BPPath;
    TOptional<FMCPToolResult> Error;
    if (!ExtractAndValidate(Params, TEXT("widget_blueprint_path"),
        FMCPParamValidator::ValidateBlueprintPath, BPPath, Error))
    {
        return Error.GetValue();
    }

    FString WidgetName;
    if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
    {
        return Error.GetValue();
    }

    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }

    UWidget* Target = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
    if (!Target)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' not found in %s"),
            *WidgetName, *BPPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    UMGCommonUtils::ExportWidgetPropertiesToJson(Target, Result.ToSharedRef());
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Queried widget %s in %s"), *WidgetName, *BPPath),
        Result);
}

FMCPToolResult FMCPTool_UMGQuery::ExecuteGetWidgetSchema(const TSharedRef<FJsonObject>& Params)
{
    FString WidgetType;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("widget_type"), WidgetType, Error))
    {
        return Error.GetValue();
    }

    UClass* Class = UMGCommonUtils::ResolveWidgetClass(WidgetType);
    if (!Class)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Could not resolve widget class: %s"), *WidgetType));
    }

    // Walk all UPROPERTYs on the class (and parents) and emit name + cpp type.
    TArray<TSharedPtr<FJsonValue>> PropArr;
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        FProperty* Prop = *It;
        TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
        One->SetStringField(TEXT("name"), Prop->GetName());
        One->SetStringField(TEXT("cpp_type"), Prop->GetCPPType());
        One->SetStringField(TEXT("class"), Prop->GetClass()->GetName());
        One->SetBoolField(TEXT("is_editable"), Prop->HasAnyPropertyFlags(CPF_Edit));
        PropArr.Add(MakeShared<FJsonValueObject>(One));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_type"), WidgetType);
    Result->SetStringField(TEXT("class_name"), Class->GetName());
    Result->SetArrayField(TEXT("properties"), PropArr);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Schema for %s (%d properties)"), *Class->GetName(), PropArr.Num()),
        Result);
}

FMCPToolResult FMCPTool_UMGQuery::ExecuteGetLayoutData(const TSharedRef<FJsonObject>& Params)
{
    FString BPPath;
    TOptional<FMCPToolResult> Error;
    if (!ExtractAndValidate(Params, TEXT("widget_blueprint_path"),
        FMCPParamValidator::ValidateBlueprintPath, BPPath, Error))
    {
        return Error.GetValue();
    }

    FString WidgetName;
    if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
    {
        return Error.GetValue();
    }

    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }

    UWidget* Target = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
    if (!Target)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
    }

    TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
    Layout->SetStringField(TEXT("widget_name"), WidgetName);

    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Target->Slot))
    {
        const FAnchorData LD = CanvasSlot->GetLayout();
        // Position
        TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
        Pos->SetNumberField(TEXT("x"), LD.Offsets.Left);
        Pos->SetNumberField(TEXT("y"), LD.Offsets.Top);
        Layout->SetObjectField(TEXT("position"), Pos);

        // Size
        TSharedPtr<FJsonObject> Size = MakeShared<FJsonObject>();
        Size->SetNumberField(TEXT("x"), LD.Offsets.Right);
        Size->SetNumberField(TEXT("y"), LD.Offsets.Bottom);
        Layout->SetObjectField(TEXT("size"), Size);

        // Anchors
        TSharedPtr<FJsonObject> Anchors = MakeShared<FJsonObject>();
        Anchors->SetNumberField(TEXT("min_x"), LD.Anchors.Minimum.X);
        Anchors->SetNumberField(TEXT("min_y"), LD.Anchors.Minimum.Y);
        Anchors->SetNumberField(TEXT("max_x"), LD.Anchors.Maximum.X);
        Anchors->SetNumberField(TEXT("max_y"), LD.Anchors.Maximum.Y);
        Layout->SetObjectField(TEXT("anchors"), Anchors);

        // Alignment
        TSharedPtr<FJsonObject> Align = MakeShared<FJsonObject>();
        Align->SetNumberField(TEXT("x"), LD.Alignment.X);
        Align->SetNumberField(TEXT("y"), LD.Alignment.Y);
        Layout->SetObjectField(TEXT("alignment"), Align);

        Layout->SetBoolField(TEXT("is_canvas_slot"), true);
    }
    else
    {
        Layout->SetBoolField(TEXT("is_canvas_slot"), false);
        if (Target->Slot)
        {
            Layout->SetStringField(TEXT("slot_class"), Target->Slot->GetClass()->GetName());
        }
    }

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Layout for %s"), *WidgetName),
        Layout);
}

FMCPToolResult FMCPTool_UMGQuery::ExecuteGetCreatableWidgetTypes(const TSharedRef<FJsonObject>& Params)
{
    FString Filter = ExtractOptionalString(Params, TEXT("filter"));
    Filter = Filter.ToLower();

    // Walk the loaded UClass set and pick UWidget subclasses.
    UClass* WidgetBase = LoadObject<UClass>(nullptr, TEXT("/Script/UMG.Widget"));
    if (!WidgetBase)
    {
        return FMCPToolResult::Error(TEXT("Could not load UWidget base class"));
    }

    TArray<TSharedPtr<FJsonValue>> Types;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* C = *It;
        if (!C || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        if (!C->IsChildOf(WidgetBase))
        {
            continue;
        }
        FString Name = C->GetName();
        if (!Filter.IsEmpty() && !Name.ToLower().Contains(Filter))
        {
            continue;
        }
        TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
        One->SetStringField(TEXT("class_name"), Name);
        One->SetStringField(TEXT("path"), C->GetPathName());
        Types.Add(MakeShared<FJsonValueObject>(One));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("types"), Types);
    Result->SetNumberField(TEXT("count"), Types.Num());

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Listed %d UWidget subclasses"), Types.Num()),
        Result);
}
