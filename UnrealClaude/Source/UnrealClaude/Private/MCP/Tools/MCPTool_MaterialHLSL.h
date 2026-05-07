// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: edit HLSL on UMaterialExpressionCustom nodes.
 *
 * Operations:
 *   hlsl_set_target - same as material_graph::set_target (cache the working material)
 *   hlsl_get        - dump a Custom node's code, output type, and named inputs
 *   hlsl_set        - replace a Custom node's HLSL body and rebuild its FCustomInput[] array
 *   hlsl_compile    - PostEditChange + ForceRecompileForRendering on the material
 *
 * Why a separate tool from material_graph:
 *   HLSL editing is a text workflow with a fundamentally different mental model. Splitting it
 *   keeps the operation surface focused for LLM use.
 */
class FMCPTool_MaterialHLSL : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("material_hlsl");
        Info.Description = TEXT(
            "Edit HLSL on a UMaterialExpressionCustom node. Operations: "
            "hlsl_set_target / hlsl_get / hlsl_set / hlsl_compile.\n\n"
            "hlsl_set unescapes \\n into newlines and rebuilds the input pin array "
            "(FCustomInput[] is replaced wholesale)."
        );
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: hlsl_set_target | hlsl_get | hlsl_set | hlsl_compile"), true),
            FMCPToolParameter(TEXT("material_path"), TEXT("string"),
                TEXT("UMaterial asset path. Required for hlsl_set_target; optional for others."), false),
            FMCPToolParameter(TEXT("create_if_missing"), TEXT("boolean"),
                TEXT("hlsl_set_target only: create the material when missing (default true)"), false, TEXT("true")),
            FMCPToolParameter(TEXT("node_handle"), TEXT("string"),
                TEXT("Custom node handle (FName or Desc). Required for hlsl_get / hlsl_set."), false),
            FMCPToolParameter(TEXT("hlsl_code"), TEXT("string"),
                TEXT("hlsl_set: HLSL body. Use literal \\n for line breaks; the tool unescapes them."), false),
            FMCPToolParameter(TEXT("input_names"), TEXT("array"),
                TEXT("hlsl_set: array of input pin names to expose on the Custom node"), false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult ExecuteSetTarget(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGet(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSet(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteCompile(const TSharedRef<FJsonObject>& Params);

    class UMaterial* ResolveTargetMaterial(const TSharedRef<FJsonObject>& Params, FString& OutError);
};
