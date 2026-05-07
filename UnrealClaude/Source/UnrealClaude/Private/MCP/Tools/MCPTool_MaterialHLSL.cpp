// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_MaterialHLSL.h"
#include "Material/MaterialCommonUtils.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"

namespace MaterialHLSLOps
{
    static const FString SetTarget = TEXT("hlsl_set_target");
    static const FString Get = TEXT("hlsl_get");
    static const FString Set = TEXT("hlsl_set");
    static const FString Compile = TEXT("hlsl_compile");
}

UMaterial* FMCPTool_MaterialHLSL::ResolveTargetMaterial(const TSharedRef<FJsonObject>& Params, FString& OutError)
{
    FString Path;
    if (Params->TryGetStringField(TEXT("material_path"), Path) && !Path.IsEmpty())
    {
        FString Status;
        UMaterial* Mat = MaterialCommonUtils::ResolveOrCreateMaterial(Path, /*bCreateIfNotFound=*/false, Status);
        if (!Mat)
        {
            OutError = Status;
            return nullptr;
        }
        MaterialCommonUtils::SetCachedTarget(Mat);
        return Mat;
    }

    UMaterial* Cached = MaterialCommonUtils::GetCachedTarget();
    if (!Cached)
    {
        OutError = TEXT("No target material set. Call material_hlsl operation=hlsl_set_target first, or pass material_path.");
        return nullptr;
    }
    return Cached;
}

FMCPToolResult FMCPTool_MaterialHLSL::Execute(const TSharedRef<FJsonObject>& Params)
{
    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }
    Operation = Operation.ToLower();

    if (Operation == MaterialHLSLOps::SetTarget) { return ExecuteSetTarget(Params); }
    if (Operation == MaterialHLSLOps::Get)       { return ExecuteGet(Params); }
    if (Operation == MaterialHLSLOps::Set)       { return ExecuteSet(Params); }
    if (Operation == MaterialHLSLOps::Compile)   { return ExecuteCompile(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: hlsl_set_target, hlsl_get, hlsl_set, hlsl_compile"),
        *Operation));
}

FMCPToolResult FMCPTool_MaterialHLSL::ExecuteSetTarget(const TSharedRef<FJsonObject>& Params)
{
    FString Path;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("material_path"), Path, Error))
    {
        return Error.GetValue();
    }

    bool bCreateIfMissing = true;
    Params->TryGetBoolField(TEXT("create_if_missing"), bCreateIfMissing);

    FString Status;
    UMaterial* Mat = MaterialCommonUtils::ResolveOrCreateMaterial(Path, bCreateIfMissing, Status);
    if (!Mat) { return FMCPToolResult::Error(Status); }

    MaterialCommonUtils::SetCachedTarget(Mat);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("material_path"), Mat->GetPathName());
    return FMCPToolResult::Success(Status, Result);
}

FMCPToolResult FMCPTool_MaterialHLSL::ExecuteGet(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    UMaterialExpression* Expr = MaterialCommonUtils::FindExpressionByHandle(Mat, Handle);
    UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr);
    if (!Custom)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' is not a UMaterialExpressionCustom"), *Handle));
    }

    TArray<TSharedPtr<FJsonValue>> InputArr;
    for (const FCustomInput& Inp : Custom->Inputs)
    {
        InputArr.Add(MakeShared<FJsonValueString>(Inp.InputName.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("handle"), Handle);
    Result->SetStringField(TEXT("description"), Custom->Description);
    Result->SetStringField(TEXT("hlsl_code"), Custom->Code);
    Result->SetNumberField(TEXT("output_type"), (int32)Custom->OutputType);
    Result->SetArrayField(TEXT("inputs"), InputArr);

    return FMCPToolResult::Success(FString::Printf(TEXT("Read HLSL on %s (%d inputs)"), *Handle, Custom->Inputs.Num()), Result);
}

FMCPToolResult FMCPTool_MaterialHLSL::ExecuteSet(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    FString HlslCode;
    if (!ExtractRequiredString(Params, TEXT("hlsl_code"), HlslCode, Error)) { return Error.GetValue(); }

    UMaterialExpression* Expr = MaterialCommonUtils::FindExpressionByHandle(Mat, Handle);
    UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr);
    if (!Custom)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' is not a UMaterialExpressionCustom"), *Handle));
    }

    // Step 1. Unescape literal "\n" sequences from JSON into real newlines.
    Custom->Code = HlslCode.Replace(TEXT("\\n"), TEXT("\n"));

    // Step 2. Rebuild the input array. Topology-agnostic: prior connections are reset.
    Custom->Inputs.Empty();
    const TArray<TSharedPtr<FJsonValue>>* InputNames = nullptr;
    if (Params->TryGetArrayField(TEXT("input_names"), InputNames) && InputNames)
    {
        for (const TSharedPtr<FJsonValue>& V : *InputNames)
        {
            FCustomInput NewInput;
            NewInput.InputName = *V->AsString();
            Custom->Inputs.Add(NewInput);
        }
    }

    // Step 3. Force property reflection + UI refresh.
    Custom->PostEditChange();
    Mat->MarkPackageDirty();
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("input_count"), Custom->Inputs.Num());
    Result->SetNumberField(TEXT("code_length"), Custom->Code.Len());
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Set HLSL on %s (%d inputs, %d chars)"), *Handle, Custom->Inputs.Num(), Custom->Code.Len()),
        Result);
}

FMCPToolResult FMCPTool_MaterialHLSL::ExecuteCompile(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    Mat->PreEditChange(nullptr);
    Mat->PostEditChange();
    Mat->ForceRecompileForRendering();
    Mat->MarkPackageDirty();

    return FMCPToolResult::Success(FString::Printf(TEXT("Recompiled %s"), *Mat->GetPathName()));
}
