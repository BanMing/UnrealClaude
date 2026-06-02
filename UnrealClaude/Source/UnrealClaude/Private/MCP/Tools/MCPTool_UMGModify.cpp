// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_UMGModify.h"
#include "UMG/UMGCommonUtils.h"
#include "MCP/Sessions/UMGSessionSubsystem.h"

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
    static const FString SetRootWidget = TEXT("set_root_widget");
    static const FString ReplaceWidget = TEXT("replace_widget");
    static const FString SaveAsset = TEXT("save_asset");
}

FMCPToolResult FMCPTool_UMGModify::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 0. UMG session anchor fallback (no-op if path already provided).
    UUMGSessionSubsystem::ApplyWidgetBlueprintPathFallback(Params);

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
    if (Operation == UMGModifyOps::SetRootWidget)        { return ExecuteSetRootWidget(Params); }
    if (Operation == UMGModifyOps::ReplaceWidget)        { return ExecuteReplaceWidget(Params); }
    if (Operation == UMGModifyOps::SaveAsset)            { return ExecuteSaveAsset(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: create_widget, set_widget_properties, delete_widget, reparent_widget, set_root_widget, replace_widget, save_asset"),
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

/**
 * ExecuteSetRootWidget — promote an existing UPanelWidget in the tree to be
 * the WidgetTree's RootWidget.
 *
 * Use case: a script wants to swap the outermost container (e.g. replace a
 * UCanvasPanel root with a USizeBox root for a constrained-size widget).
 * Without this op the caller has to create the new root, delete the old
 * root, and then manually wire children — error-prone.
 *
 * Steps:
 *   1. Resolve blueprint + widget_name.
 *   2. The named widget must already exist in the tree (caller is expected
 *      to create it first via create_widget). It must be a UPanelWidget
 *      subclass (UWidgetTree::RootWidget contract).
 *   3. Detach from its current parent (if any) — RootWidget cannot be a
 *      child of another panel simultaneously.
 *   4. Assign Tree->RootWidget; mark blueprint structurally modified so
 *      the editor recompiles bindings against the new root.
 */
FMCPToolResult FMCPTool_UMGModify::ExecuteSetRootWidget(const TSharedRef<FJsonObject>& Params)
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

    // Step 2 — load + locate the widget.
    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }
    UWidgetTree* Tree = WBP->WidgetTree;
    if (!Tree)
    {
        return FMCPToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
    }

    UWidget* Target = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
    if (!Target)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Widget '%s' not found in tree (create it first via create_widget)"), *WidgetName));
    }

    UPanelWidget* TargetPanel = Cast<UPanelWidget>(Target);
    if (!TargetPanel)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Widget '%s' is not a UPanelWidget subclass; only panels can be the tree root"), *WidgetName));
    }

    // Step 3 — detach from any existing parent so we don't leave the widget
    // dual-parented (root + child of another panel) which violates the
    // WidgetTree invariant.
    if (UPanelWidget* OldParent = TargetPanel->GetParent())
    {
        OldParent->RemoveChild(TargetPanel);
    }

    // Step 4 — assign root and mark dirty. The previous root and its
    // descendants are orphaned — the editor compile pass will warn about
    // unreferenced widgets so the caller can delete them explicitly if
    // desired.
    UWidget* PreviousRoot = Tree->RootWidget;
    Tree->RootWidget = TargetPanel;
    Tree->Modify();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("previous_root"),
        PreviousRoot ? PreviousRoot->GetName() : FString(TEXT("(none)")));

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Set tree root to '%s' in %s (previous: %s)"),
            *WidgetName, *BPPath,
            PreviousRoot ? *PreviousRoot->GetName() : TEXT("(none)")),
        Result);
}

/**
 * ExecuteReplaceWidget — delete a widget and create a replacement at the
 * same parent + sibling index in one atomic call.
 *
 * Use case: swap a UTextBlock for a UCommonTextBlock (or any class change)
 * without losing the layout slot in the parent panel. Slot properties are
 * NOT cloned (different panel slot types carry different fields); callers
 * are expected to re-apply slot config via set_widget_properties after.
 *
 * Steps:
 *   1. Resolve blueprint + target widget_name + replacement_name + replacement_type.
 *   2. Capture old widget's parent + sibling index.
 *   3. Resolve replacement UClass; verify UWidget subclass.
 *   4. Remove old widget from parent; strip GUID binding.
 *   5. Construct the replacement under the WidgetTree.
 *   6. Add replacement back to the parent — if a sibling index was captured,
 *      shift it into that position via RemoveChild + InsertChildAt.
 *   7. Register GUID for replacement; mark blueprint dirty.
 */
FMCPToolResult FMCPTool_UMGModify::ExecuteReplaceWidget(const TSharedRef<FJsonObject>& Params)
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

    FString ReplacementName;
    if (!ExtractRequiredString(Params, TEXT("replacement_name"), ReplacementName, Error))
    {
        return Error.GetValue();
    }

    FString ReplacementType;
    if (!ExtractRequiredString(Params, TEXT("replacement_type"), ReplacementType, Error))
    {
        return Error.GetValue();
    }

    const bool bIsVariable = ExtractOptionalBool(Params, TEXT("is_variable"), true);

    // Step 2 — load blueprint + locate target + capture parent slot info.
    FString LoadError;
    UWidgetBlueprint* WBP = UMGCommonUtils::LoadWidgetBlueprint(BPPath, LoadError);
    if (!WBP) { return FMCPToolResult::Error(LoadError); }
    UWidgetTree* Tree = WBP->WidgetTree;
    if (!Tree)
    {
        return FMCPToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
    }

    UWidget* OldWidget = UMGCommonUtils::FindWidgetByName(WBP, FName(*WidgetName));
    if (!OldWidget)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Widget '%s' not found"), *WidgetName));
    }

    UPanelWidget* OldParent = OldWidget->GetParent();
    const bool bWasRoot = (Tree->RootWidget == OldWidget);
    int32 SiblingIndex = INDEX_NONE;
    if (OldParent)
    {
        SiblingIndex = OldParent->GetChildIndex(OldWidget);
    }

    // Step 3 — resolve replacement class.
    UClass* ReplacementClass = UMGCommonUtils::ResolveWidgetClass(ReplacementType);
    if (!ReplacementClass)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Could not resolve replacement type: %s"), *ReplacementType));
    }
    if (!ReplacementClass->IsChildOf(UWidget::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Resolved class %s is not a UWidget subclass"), *ReplacementClass->GetName()));
    }

    // If we are replacing the root, the new widget must itself be a panel
    // (root contract). Catch this up front so we don't tear out the old
    // root and end up with an invalid tree on failure.
    if (bWasRoot && !ReplacementClass->IsChildOf(UPanelWidget::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Target '%s' is the tree root; replacement type %s must be a UPanelWidget subclass"),
            *WidgetName, *ReplacementClass->GetName()));
    }

    // Step 4 — detach the old widget. RemoveChild on the parent cleans up
    // the panel slot binding; for the root case, null out Tree->RootWidget.
    if (OldParent)
    {
        OldParent->RemoveChild(OldWidget);
    }
    else if (bWasRoot)
    {
        Tree->RootWidget = nullptr;
    }
    WBP->WidgetVariableNameToGuidMap.Remove(OldWidget->GetFName());

    // Step 5 — construct the replacement under the same tree.
    UWidget* NewWidget = Tree->ConstructWidget<UWidget>(ReplacementClass, FName(*ReplacementName));
    if (!NewWidget)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to construct replacement widget of type %s"), *ReplacementClass->GetName()));
    }
    NewWidget->bIsVariable = bIsVariable;

    // Step 6 — re-attach. Insert at the captured sibling index so the new
    // widget occupies the exact slot the old one held.
    if (OldParent)
    {
        OldParent->AddChild(NewWidget);
        if (SiblingIndex != INDEX_NONE)
        {
            // AddChild appends at end; UPanelWidget::ShiftChild reorders
            // the slots TArray so the child ends up at the captured sibling
            // index. ShiftChild clamps internally if the index exceeds the
            // current child count.
            const int32 AppendedIndex = OldParent->GetChildIndex(NewWidget);
            if (AppendedIndex != SiblingIndex)
            {
                OldParent->ShiftChild(SiblingIndex, NewWidget);
            }
        }
    }
    else if (bWasRoot)
    {
        Tree->RootWidget = NewWidget;
    }

    // Step 7 — GUID + structural mark.
    EnsureWidgetVariableGuid(WBP, NewWidget);
    Tree->Modify();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), BPPath);
    Result->SetStringField(TEXT("deleted_widget"), WidgetName);
    Result->SetStringField(TEXT("created_widget"), NewWidget->GetName());
    Result->SetStringField(TEXT("created_class"), ReplacementClass->GetName());
    Result->SetNumberField(TEXT("sibling_index"), SiblingIndex);
    Result->SetBoolField(TEXT("was_root"), bWasRoot);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Replaced '%s' with '%s' (%s) at sibling index %d"),
            *WidgetName, *ReplacementName, *ReplacementClass->GetName(), SiblingIndex),
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
