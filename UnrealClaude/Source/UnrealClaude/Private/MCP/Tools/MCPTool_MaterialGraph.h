// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: edit a UMaterial's expression graph.
 *
 * Operations:
 *   set_target          - load (and optionally create) a UMaterial; cache it as the implicit target
 *   define_variable     - get-or-create a Scalar/Vector/Texture parameter
 *   add_node            - add a UMaterialExpression by class name (Engine path or short name)
 *   delete_node         - remove an expression by handle
 *   connect_nodes       - connect first-output to first-input (smart default pin)
 *   connect_pins        - connect a specific output pin to a specific input pin (root-aware)
 *   set_node_properties - reflect JSON onto an expression (or root material)
 *   get_node_info       - dump a node's pins and incoming connections as JSON
 *   set_output_node     - wire a node's first output to the root EmissiveColor/BaseColor/MaterialAttributes
 *   compile_asset       - PostEditChange + ForceRecompileForRendering
 *
 * State: a process-wide "current target" material is cached after `set_target`. Other operations
 * accept an optional `material_path` to override per-call (stateless per-call usage is preferred).
 */
class FMCPTool_MaterialGraph : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("material_graph");
        Info.Description = TEXT(
            "Edit a UMaterial's expression graph. Supports add/delete/connect nodes, "
            "set_target/define_variable/set_output_node/compile_asset.\n\n"
            "Pass `material_path` per call (stateless) or call `set_target` first to cache it. "
            "Smart pin aliasing: 'Output'/'FinalColor' on the root resolve to EmissiveColor "
            "(UI domain) or BaseColor (default)."
        );
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: set_target | define_variable | add_node | delete_node | connect_nodes | connect_pins | set_node_properties | get_node_info | set_output_node | compile_asset"), true),
            FMCPToolParameter(TEXT("material_path"), TEXT("string"),
                TEXT("UMaterial asset path. Required for set_target; optional for others (defaults to cached target)."), false),
            FMCPToolParameter(TEXT("create_if_missing"), TEXT("boolean"),
                TEXT("set_target only: create a new UI/translucent material when missing (default true)"), false, TEXT("true")),
            FMCPToolParameter(TEXT("param_name"), TEXT("string"),
                TEXT("define_variable: parameter name"), false),
            FMCPToolParameter(TEXT("param_type"), TEXT("string"),
                TEXT("define_variable: 'Scalar' | 'Vector' | 'Texture'"), false),
            FMCPToolParameter(TEXT("node_class"), TEXT("string"),
                TEXT("add_node: short name (Constant3Vector) or full Engine path (/Script/Engine.MaterialExpressionConstant3Vector)"), false),
            FMCPToolParameter(TEXT("node_name"), TEXT("string"),
                TEXT("add_node: optional Desc tag for stable lookup"), false),
            FMCPToolParameter(TEXT("node_handle"), TEXT("string"),
                TEXT("Handle (FName or Desc) for delete/connect/set/get/set_output operations"), false),
            FMCPToolParameter(TEXT("from_handle"), TEXT("string"),
                TEXT("connect_nodes/connect_pins: source node handle"), false),
            FMCPToolParameter(TEXT("to_handle"), TEXT("string"),
                TEXT("connect_nodes/connect_pins: target node handle (use 'MaterialRoot' for the root)"), false),
            FMCPToolParameter(TEXT("from_pin"), TEXT("string"),
                TEXT("connect_pins: source pin name (empty = first output)"), false),
            FMCPToolParameter(TEXT("to_pin"), TEXT("string"),
                TEXT("connect_pins: target pin name (Output/FinalColor/EmissiveColor/Opacity/etc.)"), false),
            FMCPToolParameter(TEXT("properties"), TEXT("object"),
                TEXT("set_node_properties: JSON object of property names to values"), false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult ExecuteSetTarget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteDefineVariable(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteAddNode(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteConnectNodes(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteConnectPins(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetNodeProperties(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetNodeInfo(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetOutputNode(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteCompileAsset(const TSharedRef<FJsonObject>& Params);

    /** Resolve target material: optional `material_path`, else cached target. */
    class UMaterial* ResolveTargetMaterial(const TSharedRef<FJsonObject>& Params, FString& OutError);
};
