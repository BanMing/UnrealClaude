// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Mutate UMG Widget Blueprints.
 *
 * Operations:
 *   - create_widget          : add a new UWidget under a parent (auto-roots if tree is empty)
 *   - set_widget_properties  : reflect JSON onto a widget + its slot, with FSlateBrush intercept
 *   - delete_widget          : remove a widget from its parent panel
 *   - reparent_widget        : move a widget under a new panel parent
 *   - set_root_widget        : promote an existing UPanelWidget to be the tree root
 *   - replace_widget         : delete + create at the same sibling slot in one call
 *   - save_asset             : flush the WidgetBlueprint to disk
 *
 * All operations:
 *   - require widget_blueprint_path explicitly (stateless)
 *   - call MarkBlueprintAsStructurallyModified after the change
 *   - register a GUID in WidgetVariableNameToGuidMap when bIsVariable is true
 */
class FMCPTool_UMGModify : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("umg_modify");
        Info.Description = TEXT(
            "Mutate UMG Widget Blueprints. Auto-marks blueprint as structurally modified.\n\n"
            "Operations:\n"
            "  create_widget         - add a new widget under parent\n"
            "  set_widget_properties - apply JSON object to a widget (and Slot)\n"
            "  delete_widget         - remove a widget from its parent\n"
            "  reparent_widget       - re-attach to a new panel parent\n"
            "  set_root_widget       - promote an existing UPanelWidget subclass to tree root\n"
            "                          (the previous root and its descendants are dropped from the\n"
            "                          tree; pass an existing widget_name that has been detached or\n"
            "                          newly created at the tree level)\n"
            "  replace_widget        - delete a widget at its current sibling slot and create a\n"
            "                          replacement of replacement_type at the same parent + index\n"
            "                          in one atomic call; pass replacement_name and replacement_type\n"
            "  save_asset            - flush blueprint to disk\n\n"
            "All operations are stateless: pass widget_blueprint_path every call."
        );
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: create_widget | set_widget_properties | delete_widget | reparent_widget | set_root_widget | replace_widget | save_asset"), true),
            FMCPToolParameter(TEXT("widget_blueprint_path"), TEXT("string"),
                TEXT("Path to the WidgetBlueprint asset"), false),
            FMCPToolParameter(TEXT("widget_type"), TEXT("string"),
                TEXT("UMG widget class name or path (for create_widget)"), false),
            FMCPToolParameter(TEXT("widget_name"), TEXT("string"),
                TEXT("FName for the new widget, or target widget for set/delete/reparent/set_root_widget/replace_widget"), false),
            FMCPToolParameter(TEXT("parent_name"), TEXT("string"),
                TEXT("Parent panel widget FName (empty -> root for empty trees)"), false),
            FMCPToolParameter(TEXT("is_variable"), TEXT("boolean"),
                TEXT("If true, register the widget as a variable (creates GUID entry)"), false, TEXT("true")),
            FMCPToolParameter(TEXT("properties"), TEXT("object"),
                TEXT("JSON object of properties to apply (for set_widget_properties / create_widget initial state)"), false),
            FMCPToolParameter(TEXT("replacement_name"), TEXT("string"),
                TEXT("FName for the new widget when operation=replace_widget"), false),
            FMCPToolParameter(TEXT("replacement_type"), TEXT("string"),
                TEXT("UMG widget class name or path for the replacement when operation=replace_widget"), false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult ExecuteCreateWidget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetWidgetProperties(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteDeleteWidget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteReparentWidget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetRootWidget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteReplaceWidget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSaveAsset(const TSharedRef<FJsonObject>& Params);
};
