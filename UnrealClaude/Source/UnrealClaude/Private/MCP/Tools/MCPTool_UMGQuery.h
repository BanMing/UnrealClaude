// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Read-only queries against a UMG Widget Blueprint.
 *
 * Operations (selected via the "operation" parameter):
 *   - get_widget_tree            : recursive shallow tree (name, type, is_variable)
 *   - query_widget_properties    : full reflection dump for one widget + its slot
 *   - get_widget_schema          : enumerate UPROPERTYs and their types for a widget class
 *   - get_layout_data            : read layout box (Position/Size/Anchors) for one widget
 *   - get_creatable_widget_types : discover UWidget subclasses available in /Script/UMG
 *
 * Stateless: every call requires the explicit Widget Blueprint path.
 */
class FMCPTool_UMGQuery : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("umg_query");
        Info.Description = TEXT(
            "Query UMG Widget Blueprints (read-only).\n\n"
            "Operations:\n"
            "  get_widget_tree             - shallow tree of widget hierarchy\n"
            "  query_widget_properties     - full property dump for a widget\n"
            "  get_widget_schema           - list UPROPERTYs available on a widget class\n"
            "  get_layout_data             - read CanvasPanel layout box for a widget\n"
            "  get_creatable_widget_types  - list UWidget subclasses available in UMG\n\n"
            "All operations are stateless: pass widget_blueprint_path every call."
        );
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: get_widget_tree | query_widget_properties | get_widget_schema | get_layout_data | get_creatable_widget_types"), true),
            FMCPToolParameter(TEXT("widget_blueprint_path"), TEXT("string"),
                TEXT("Path to the WidgetBlueprint asset (e.g. /Game/UI/WBP_PaogeHUD)"), false),
            FMCPToolParameter(TEXT("widget_name"), TEXT("string"),
                TEXT("Target widget FName inside the blueprint (required for property/layout ops)"), false),
            FMCPToolParameter(TEXT("widget_type"), TEXT("string"),
                TEXT("Widget type (UMG class name or path) for get_widget_schema"), false),
            FMCPToolParameter(TEXT("filter"), TEXT("string"),
                TEXT("Substring filter for get_creatable_widget_types"), false)
        };
        Info.Annotations = FMCPToolAnnotations::ReadOnly();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteQueryWidgetProperties(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetWidgetSchema(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetLayoutData(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetCreatableWidgetTypes(const TSharedRef<FJsonObject>& Params);
};
