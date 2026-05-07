// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_UMGModify.h"
#include "UMG/UMGCommonUtils.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace UMGModifyOps
{
    static const FString CreateWidget = TEXT("create_widget");
    static const FString SetWidgetProperties = TEXT("set_widget_properties");
    static const FString DeleteWidget = TEXT("delete_widget");
    static const FString ReparentWidget = TEXT("reparent_widget");
    static const FString SaveAsset = TEXT("save_asset");
}

FMCPToolResult FMCPTool_UMGModify::Execute(const TSharedRef<FJsonObject>& Params)
{
    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }
    Operation = Operation.ToLower();

    if (Operation == UMGModifyOps::CreateWidget)         { return ExecuteCreateWidget(Params); }
    if (Operation == UMGModifyOps::SetWidgetProperties)  { return ExecuteSetWidgetProperties(Params); }
    if (Operation == UMGModifyOps::DeleteWidget)         { return ExecuteDeleteWidget(Params); }
    if (Operation == UMGModifyOps::ReparentWidget)       { return ExecuteReparentWidget(Params); }
    if (Operation == UMGModifyOps::SaveAsset)            { return ExecuteSaveAsset(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: create_widget, set_widget_properties, delete_widget, reparent_widget, save_asset"),
        *Operation));
}

/**
 * Helper: register the GUID entry that the UMG editor compiler expects when a
 * widget is bIsVariable=true. Without it, BP compilation will trip an ensure
 * because the variable->guid back-reference table is incomplete.
 *
 * Steps:
 *   1. If the widget is already in the map, do nothing.
 *   2. Otherwise generate a fresh FGuid and add the binding.
 *
 * @param WidgetBlueprint  Owning blueprint (must be valid).
 * @param Widget           Widget that has bIsVariable=true.
 */
static void EnsureWidgetVariableGuid(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget)
{
    if (!WidgetBlueprint || !Widget || !Widget->bIsVariable)
    {
        return;
    }
    const FName WidgetFName = Widget->GetFName();
    if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(WidgetFName))
    {
        WidgetBlueprint->WidgetVariableNameToGuidMap.Add(WidgetFName, FGuid::NewGuid());
    }
}

FMCPToolResult FMCPTool_UMGModify::ExecuteCreateWidget(const TSharedRef<FJsonObject>& Params)
{
    // Step 1. Resolve all required inputs.
    FString BPPath;
    TOptional<FMCPToolResult> Error;
    if (!ExtractAndValidate(Params, TEXT("widget_blueprint_path"),
        FMCPParamValidator::ValidateBlueprintPath, BPPath, Error))
    {
        return Error.GetValue();
    }

    FString WidgetType;
    if (!ExtractRequiredString(Params, TEXT("widget_type"), WidgetType, Error))
    {
        return Error.GetValue();
    }

    FString WidgetName;
    if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
    {
        return Error.GetValue();
    }

    const FString ParentName = ExtractOptionalString(Params, TEXT("parent_name"));
    const bool bIsVariable = ExtractOptionalBool(Params, TEXT("is_variable"), true);

    // Step 2. Load blueprint and validate tree.
    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }
    UWidgetTree* Tree = WBP->WidgetTree;
    if (!Tree) { return FMCPToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree")); }

    // Step 3. Resolve widget class via the 4-tier fallback.
    UClass* WidgetClass = UMGCommonUtils::ResolveWidgetClass(WidgetType);
    if (!WidgetClass)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Could not resolve widget type: %s"), *WidgetType));
    }
    if (!WidgetClass->IsChildOf(UWidget::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Resolved class %s is not a UWidget subclass"), *WidgetClass->GetName()));
    }

    // Step 4. Construct the widget under the WidgetTree.
    UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
    if (!NewWidget)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to ConstructWidget of type %s"), *WidgetClass->GetName()));
    }
    NewWidget->bIsVariable = bIsVariable;

    // Step 5. Attach to parent (or auto-promote to root if tree is empty).
    UPanelWidget* ParentPanel = nullptr;
    if (!ParentName.IsEmpty())
    {
        UWidget* ParentWidget = Tree->FindWidget(FName(*ParentName));
        if (!ParentWidget)
        {
            return FMCPToolResult::Error(FString::Printf(
                TEXT("Parent widget not found: %s"), *ParentName));
        }
        ParentPanel = Cast<UPanelWidget>(ParentWidget);
        if (!ParentPanel)
        {
            return FMCPToolResult::Error(FString::Printf(
                TEXT("Parent widget %s is not a UPanelWidget"), *ParentName));
        }
    }

    if (ParentPanel)
    {
        ParentPanel->AddChild(NewWidget);
    }
    else if (Tree->RootWidget == nullptr)
    {
        // Empty tree path: only allowed if NewWidget is itself a panel (root must be a panel).
        if (UPanelWidget* AsPanel = Cast<UPanelWidget>(NewWidget))
        {
            Tree->RootWidget = AsPanel;
        }
        else
        {
            return FMCPToolResult::Error(TEXT(
                "WidgetTree is empty: the first widget must be a UPanelWidget subclass to act as root"));
        }
    }
    else
    {
        // Tree has a root, no explicit parent — attach to root if it's a panel.
        if (UPanelWidget* RootPanel = Cast<UPanelWidget>(Tree->RootWidget))
        {
            RootPanel->AddChild(NewWidget);
        }
        else
        {
            return FMCPToolResult::Error(TEXT(
                "Cannot auto-attach: root widget is not a UPanelWidget; pass parent_name explicitly"));
        }
    }

    // Step 6. GUID + structural-modified accounting.
    EnsureWidgetVariableGuid(WBP, NewWidget);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    // Step 7. Optional: apply initial properties JSON in the same call.
    const TSharedPtr<FJsonObject>* PropsObj = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
    {
        // Make a deep copy so we don't mutate the caller's JSON.
        TSharedPtr<FJsonObject> Working = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*PropsObj)->Values)
        {
            Working->SetField(P.Key, P.Value);
        }
        UMGCommonUtils::ExpandCanvasSlotAliases(Working);
        UMGCommonUtils::NormalizeJsonKeysToPascalCase(Working);

        TArray<FString> ApplyErrors;
        // Slot first if present.
        const TSharedPtr<FJsonObject>* SlotJson = nullptr;
        if (Working->TryGetObjectField(TEXT("Slot"), SlotJson) && SlotJson && SlotJson->IsValid()
            && NewWidget->Slot)
        {
            UMGCommonUtils::ApplyJsonToObject(*SlotJson, NewWidget->Slot, ApplyErrors);
            Working->RemoveField(TEXT("Slot"));
        }
        UMGCommonUtils::ApplyJsonToObject(Working, NewWidget, ApplyErrors);
        // ApplyErrors are non-fatal — surface them to the caller.
    }

    // Step 8. Build response.
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
    Result->SetStringField(TEXT("widget_name"), NewWidget->GetName());
    Result->SetStringField(TEXT("widget_class"), WidgetClass->GetName());
    Result->SetBoolField(TEXT("is_variable"), NewWidget->bIsVariable);
    Result->SetStringField(TEXT("parent_name"), ParentName);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Created %s '%s' in %s"), *WidgetClass->GetName(), *WidgetName, *BPPath),
        Result);
}

FMCPToolResult FMCPTool_UMGModify::ExecuteSetWidgetProperties(const TSharedRef<FJsonObject>& Params)
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

    const TSharedPtr<FJsonObject>* PropsObj = nullptr;
    if (!Params->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj || !PropsObj->IsValid())
    {
        return FMCPToolResult::Error(TEXT("Missing required object parameter: properties"));
    }

    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }

    UWidget* Target = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
    if (!Target)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
    }

    // Step 1. Deep-copy and pre-process the JSON: expand slot aliases, then normalize keys.
    TSharedPtr<FJsonObject> Working = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*PropsObj)->Values)
    {
        Working->SetField(P.Key, P.Value);
    }
    UMGCommonUtils::ExpandCanvasSlotAliases(Working);
    UMGCommonUtils::NormalizeJsonKeysToPascalCase(Working);

    TArray<FString> Errors;

    // Step 2. Apply slot props first (separate UObject).
    const TSharedPtr<FJsonObject>* SlotJson = nullptr;
    if (Working->TryGetObjectField(TEXT("Slot"), SlotJson) && SlotJson && SlotJson->IsValid())
    {
        if (Target->Slot)
        {
            UMGCommonUtils::ApplyJsonToObject(*SlotJson, Target->Slot, Errors);
        }
        else
        {
            Errors.Add(TEXT("Widget has no Slot object; ignoring 'Slot' field"));
        }
        Working->RemoveField(TEXT("Slot"));
    }

    // Step 3. Apply remaining props directly to the widget.
    UMGCommonUtils::ApplyJsonToObject(Working, Target, Errors);

    // Step 4. Mark dirty.
    Target->Modify();
    if (Target->Slot)
    {
        Target->Slot->Modify();
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    // Step 5. Build response.
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetNumberField(TEXT("error_count"), Errors.Num());
    {
        TArray<TSharedPtr<FJsonValue>> ErrArr;
        for (const FString& E : Errors)
        {
            ErrArr.Add(MakeShared<FJsonValueString>(E));
        }
        Result->SetArrayField(TEXT("errors"), ErrArr);
    }

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Applied properties to %s (%d soft errors)"), *WidgetName, Errors.Num()),
        Result);
}

FMCPToolResult FMCPTool_UMGModify::ExecuteDeleteWidget(const TSharedRef<FJsonObject>& Params)
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

    // Detach: prefer parent->RemoveChild for proper slot cleanup.
    bool bRemoved = false;
    if (UPanelWidget* Parent = Target->GetParent())
    {
        bRemoved = Parent->RemoveChild(Target);
    }
    else if (WBP->WidgetTree && WBP->WidgetTree->RootWidget == Target)
    {
        WBP->WidgetTree->RootWidget = nullptr;
        bRemoved = true;
    }

    // Strip GUID binding if present.
    WBP->WidgetVariableNameToGuidMap.Remove(Target->GetFName());

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("removed"), bRemoved);
    Result->SetStringField(TEXT("widget_name"), WidgetName);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Deleted widget %s (removed=%s)"), *WidgetName, bRemoved ? TEXT("true") : TEXT("false")),
        Result);
}

FMCPToolResult FMCPTool_UMGModify::ExecuteReparentWidget(const TSharedRef<FJsonObject>& Params)
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

    FString ParentName;
    if (!ExtractRequiredString(Params, TEXT("parent_name"), ParentName, Error))
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

    UWidget* ParentW = UMGCommonUtils::FindWidgetByName(WBP, FName(*ParentName));
    if (!ParentW)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget '%s' not found"), *ParentName));
    }
    UPanelWidget* NewParent = Cast<UPanelWidget>(ParentW);
    if (!NewParent)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget '%s' is not a panel"), *ParentName));
    }

    // Detach from old parent.
    if (UPanelWidget* OldParent = Target->GetParent())
    {
        OldParent->RemoveChild(Target);
    }

    // Attach to new parent.
    NewParent->AddChild(Target);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("new_parent"), ParentName);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Reparented %s -> %s"), *WidgetName, *ParentName),
        Result);
}

FMCPToolResult FMCPTool_UMGModify::ExecuteSaveAsset(const TSharedRef<FJsonObject>& Params)
{
    FString BPPath;
    TOptional<FMCPToolResult> Error;
    if (!ExtractAndValidate(Params, TEXT("widget_blueprint_path"),
        FMCPParamValidator::ValidateBlueprintPath, BPPath, Error))
    {
        return Error.GetValue();
    }

    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }

    UPackage* Package = WBP->GetOutermost();
    if (!Package)
    {
        return FMCPToolResult::Error(TEXT("WidgetBlueprint has no owning package"));
    }

    Package->MarkPackageDirty();

    FString PackageFilename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
    {
        return FMCPToolResult::Error(TEXT("Could not resolve package filename"));
    }

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    SaveArgs.bForceByteSwapping = false;
    SaveArgs.bWarnOfLongFilename = false;
    SaveArgs.SaveFlags = SAVE_None;

    const bool bSaved = UPackage::SavePackage(Package, WBP, *PackageFilename, SaveArgs);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("package_filename"), PackageFilename);

    if (!bSaved)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Save failed for %s"), *PackageFilename));
    }
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Saved %s"), *PackageFilename),
        Result);
}
