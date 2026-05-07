// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_MaterialGraph.h"
#include "Material/MaterialCommonUtils.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"

#include "MaterialEditingLibrary.h"

#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"

#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace MaterialGraphOps
{
    static const FString SetTarget = TEXT("set_target");
    static const FString DefineVariable = TEXT("define_variable");
    static const FString AddNode = TEXT("add_node");
    static const FString DeleteNode = TEXT("delete_node");
    static const FString ConnectNodes = TEXT("connect_nodes");
    static const FString ConnectPins = TEXT("connect_pins");
    static const FString SetNodeProperties = TEXT("set_node_properties");
    static const FString GetNodeInfo = TEXT("get_node_info");
    static const FString SetOutputNode = TEXT("set_output_node");
    static const FString CompileAsset = TEXT("compile_asset");
}

UMaterial* FMCPTool_MaterialGraph::ResolveTargetMaterial(const TSharedRef<FJsonObject>& Params, FString& OutError)
{
    // Step 1. Check optional material_path.
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
        // Cache so subsequent calls without material_path keep working.
        MaterialCommonUtils::SetCachedTarget(Mat);
        return Mat;
    }

    // Step 2. Fall back to cached target.
    UMaterial* Cached = MaterialCommonUtils::GetCachedTarget();
    if (!Cached)
    {
        OutError = TEXT("No target material set. Call material_graph operation=set_target first, or pass material_path.");
        return nullptr;
    }
    return Cached;
}

FMCPToolResult FMCPTool_MaterialGraph::Execute(const TSharedRef<FJsonObject>& Params)
{
    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }
    Operation = Operation.ToLower();

    if (Operation == MaterialGraphOps::SetTarget)          { return ExecuteSetTarget(Params); }
    if (Operation == MaterialGraphOps::DefineVariable)     { return ExecuteDefineVariable(Params); }
    if (Operation == MaterialGraphOps::AddNode)            { return ExecuteAddNode(Params); }
    if (Operation == MaterialGraphOps::DeleteNode)         { return ExecuteDeleteNode(Params); }
    if (Operation == MaterialGraphOps::ConnectNodes)       { return ExecuteConnectNodes(Params); }
    if (Operation == MaterialGraphOps::ConnectPins)        { return ExecuteConnectPins(Params); }
    if (Operation == MaterialGraphOps::SetNodeProperties)  { return ExecuteSetNodeProperties(Params); }
    if (Operation == MaterialGraphOps::GetNodeInfo)        { return ExecuteGetNodeInfo(Params); }
    if (Operation == MaterialGraphOps::SetOutputNode)      { return ExecuteSetOutputNode(Params); }
    if (Operation == MaterialGraphOps::CompileAsset)       { return ExecuteCompileAsset(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: set_target, define_variable, add_node, delete_node, connect_nodes, connect_pins, set_node_properties, get_node_info, set_output_node, compile_asset"),
        *Operation));
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteSetTarget(const TSharedRef<FJsonObject>& Params)
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
    if (!Mat)
    {
        return FMCPToolResult::Error(Status);
    }

    MaterialCommonUtils::SetCachedTarget(Mat);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("material_path"), Mat->GetPathName());
    Result->SetStringField(TEXT("status"), Status);
    Result->SetNumberField(TEXT("material_domain"), (int32)Mat->MaterialDomain);
    return FMCPToolResult::Success(Status, Result);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteDefineVariable(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString ParamName, ParamType;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("param_name"), ParamName, Error)) { return Error.GetValue(); }
    if (!ExtractRequiredString(Params, TEXT("param_type"), ParamType, Error)) { return Error.GetValue(); }

    // Step 1. Reuse existing parameter if present.
    for (UMaterialExpression* Expr : Mat->GetExpressions())
    {
        if (UMaterialExpressionParameter* P = Cast<UMaterialExpressionParameter>(Expr))
        {
            if (P->ParameterName.ToString() == ParamName)
            {
                TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
                R->SetStringField(TEXT("handle"), Expr->GetName());
                R->SetBoolField(TEXT("created"), false);
                return FMCPToolResult::Success(FString::Printf(TEXT("Reused parameter %s"), *ParamName), R);
            }
        }
        if (UMaterialExpressionTextureSampleParameter* T = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
        {
            if (T->ParameterName.ToString() == ParamName)
            {
                TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
                R->SetStringField(TEXT("handle"), Expr->GetName());
                R->SetBoolField(TEXT("created"), false);
                return FMCPToolResult::Success(FString::Printf(TEXT("Reused texture parameter %s"), *ParamName), R);
            }
        }
    }

    // Step 2. Create.
    UClass* NewClass = nullptr;
    if (ParamType.Equals(TEXT("Scalar"), ESearchCase::IgnoreCase))
    {
        NewClass = UMaterialExpressionScalarParameter::StaticClass();
    }
    else if (ParamType.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
    {
        NewClass = UMaterialExpressionVectorParameter::StaticClass();
    }
    else if (ParamType.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
    {
        NewClass = UMaterialExpressionTextureSampleParameter2D::StaticClass();
    }

    if (!NewClass)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Unknown param_type '%s'. Valid: Scalar, Vector, Texture"), *ParamType));
    }

    UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, NewClass);
    if (!NewExpr)
    {
        return FMCPToolResult::Error(TEXT("CreateMaterialExpression failed"));
    }

    if (UMaterialExpressionParameter* P = Cast<UMaterialExpressionParameter>(NewExpr))
    {
        P->ParameterName = *ParamName;
    }
    if (UMaterialExpressionTextureSampleParameter* T = Cast<UMaterialExpressionTextureSampleParameter>(NewExpr))
    {
        T->ParameterName = *ParamName;
    }

    // Auto-position so stacked parameters don't overlap.
    NewExpr->MaterialExpressionEditorX = -200;
    if (Mat->GetEditorOnlyData())
    {
        NewExpr->MaterialExpressionEditorY = Mat->GetEditorOnlyData()->ExpressionCollection.Expressions.Num() * 100;
    }
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("handle"), NewExpr->GetName());
    Result->SetBoolField(TEXT("created"), true);
    return FMCPToolResult::Success(FString::Printf(TEXT("Created %s parameter %s"), *ParamType, *ParamName), Result);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteAddNode(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString NodeClass;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_class"), NodeClass, Error)) { return Error.GetValue(); }

    const FString NodeName = ExtractOptionalString(Params, TEXT("node_name"));

    // Step 1. Resolve UClass — explicit path first, then standard /Script/Engine.MaterialExpression<X>.
    UClass* ExprClass = FindObject<UClass>(nullptr, *NodeClass);
    if (!ExprClass)
    {
        const FString EnginePath = TEXT("/Script/Engine.MaterialExpression") + NodeClass;
        ExprClass = FindObject<UClass>(nullptr, *EnginePath);
        if (!ExprClass)
        {
            ExprClass = LoadObject<UClass>(nullptr, *EnginePath);
        }
    }

    if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Invalid node class: %s"), *NodeClass));
    }

    UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass);
    if (!NewExpr)
    {
        return FMCPToolResult::Error(TEXT("CreateMaterialExpression failed"));
    }

    if (!NodeName.IsEmpty())
    {
        NewExpr->Desc = NodeName;
    }
    NewExpr->MaterialExpressionEditorX = -200;
    if (Mat->GetEditorOnlyData())
    {
        NewExpr->MaterialExpressionEditorY = Mat->GetEditorOnlyData()->ExpressionCollection.Expressions.Num() * 100;
    }

    Mat->MarkPackageDirty();
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("handle"), NewExpr->GetName());
    if (!NodeName.IsEmpty())
    {
        Result->SetStringField(TEXT("desc"), NodeName);
    }
    return FMCPToolResult::Success(FString::Printf(TEXT("Added %s"), *ExprClass->GetName()), Result);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    UMaterialExpression* Expr = MaterialCommonUtils::FindExpressionByHandle(Mat, Handle);
    if (!Expr)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *Handle));
    }

    if (Mat->GetEditorOnlyData())
    {
        Mat->GetEditorOnlyData()->ExpressionCollection.Expressions.Remove(Expr);
    }
    Mat->MarkPackageDirty();
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    return FMCPToolResult::Success(FString::Printf(TEXT("Deleted node %s"), *Handle));
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteConnectNodes(const TSharedRef<FJsonObject>& Params)
{
    // ConnectNodes is just ConnectPins with empty pin names.
    Params->SetStringField(TEXT("from_pin"), TEXT(""));
    Params->SetStringField(TEXT("to_pin"), TEXT(""));
    return ExecuteConnectPins(Params);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteConnectPins(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString FromHandle, ToHandle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("from_handle"), FromHandle, Error)) { return Error.GetValue(); }
    if (!ExtractRequiredString(Params, TEXT("to_handle"), ToHandle, Error)) { return Error.GetValue(); }

    const FString FromPin = ExtractOptionalString(Params, TEXT("from_pin"));
    const FString ToPin = ExtractOptionalString(Params, TEXT("to_pin"));

    const bool bIsRoot = MaterialCommonUtils::IsRootHandle(Mat, ToHandle);

    if (bIsRoot && Mat->MaterialGraph)
    {
        // === ROOT NODE: graph-based connection ===
        // Step 1. Find source graph node.
        UMaterialGraphNode* SourceGraphNode = nullptr;
        for (UEdGraphNode* Node : Mat->MaterialGraph->Nodes)
        {
            UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
            if (MatNode && MatNode->MaterialExpression)
            {
                if (MatNode->MaterialExpression->GetName().Equals(FromHandle, ESearchCase::IgnoreCase) ||
                    MatNode->MaterialExpression->Desc.Equals(FromHandle, ESearchCase::IgnoreCase))
                {
                    SourceGraphNode = MatNode;
                    break;
                }
            }
        }
        if (!SourceGraphNode)
        {
            return FMCPToolResult::Error(FString::Printf(TEXT("Source node '%s' not found in MaterialGraph"), *FromHandle));
        }

        UEdGraphNode* RootNode = MaterialCommonUtils::FindRootGraphNode(Mat);
        if (!RootNode)
        {
            return FMCPToolResult::Error(TEXT("Root node not found in MaterialGraph (open the material editor first)"));
        }

        // Step 2. Resolve source output pin.
        UEdGraphPin* SourcePin = nullptr;
        if (FromPin.IsEmpty() || FromPin.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
        {
            for (UEdGraphPin* Pin : SourceGraphNode->Pins)
            {
                if (Pin->Direction == EGPD_Output) { SourcePin = Pin; break; }
            }
        }
        else
        {
            for (UEdGraphPin* Pin : SourceGraphNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinName.ToString().Equals(FromPin, ESearchCase::IgnoreCase))
                {
                    SourcePin = Pin;
                    break;
                }
            }
        }
        if (!SourcePin)
        {
            return FMCPToolResult::Error(FString::Printf(TEXT("Source output pin '%s' not found"), *FromPin));
        }

        // Step 3. Resolve target pin name with smart aliasing.
        FString TargetPinName = ToPin;
        const FString CleanPinName = TargetPinName.TrimStartAndEnd().Replace(TEXT(" "), TEXT(""));
        if (CleanPinName.IsEmpty() || CleanPinName.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
        {
            TargetPinName = (Mat->MaterialDomain == MD_UI) ? TEXT("EmissiveColor") : TEXT("BaseColor");
        }
        else if (CleanPinName.Equals(TEXT("FinalColor"), ESearchCase::IgnoreCase))
        {
            TargetPinName = TEXT("EmissiveColor");
        }
        else
        {
            TargetPinName = CleanPinName;
        }

        // Strategy 1: stable property name lookup -> localized display name -> pin match.
        UEdGraphPin* TargetPin = nullptr;
        FProperty* MatProp = nullptr;
#if WITH_EDITOR
        if (Mat->GetEditorOnlyData())
        {
            MatProp = Mat->GetEditorOnlyData()->GetClass()->FindPropertyByName(*TargetPinName);
        }
#endif
        if (!MatProp)
        {
            MatProp = Mat->GetClass()->FindPropertyByName(*TargetPinName);
        }
        if (MatProp)
        {
            const FString LocalizedDisplayName = MatProp->GetDisplayNameText().ToString();
            for (UEdGraphPin* Pin : RootNode->Pins)
            {
                if (Pin->Direction == EGPD_Input &&
                    (Pin->PinName.ToString().Equals(TargetPinName, ESearchCase::IgnoreCase) ||
                     Pin->PinName.ToString().Equals(LocalizedDisplayName, ESearchCase::IgnoreCase) ||
                     Pin->PinName.ToString().Replace(TEXT(" "), TEXT("")).Equals(TargetPinName, ESearchCase::IgnoreCase)))
                {
                    TargetPin = Pin;
                    break;
                }
            }
        }

        // Strategy 2: heuristic / localized fallback for Emissive / Opacity / BaseColor.
        if (!TargetPin)
        {
            auto FindByContains = [RootNode](const TArray<FString>& Needles) -> UEdGraphPin*
            {
                for (UEdGraphPin* Pin : RootNode->Pins)
                {
                    if (Pin->Direction != EGPD_Input) continue;
                    const FString PinStr = Pin->PinName.ToString();
                    for (const FString& Needle : Needles)
                    {
                        if (PinStr.Contains(Needle))
                        {
                            return Pin;
                        }
                    }
                }
                return nullptr;
            };

            if (TargetPinName.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase))
            {
                TargetPin = FindByContains({TEXT("Final"), TEXT("Emissive"), TEXT("最终"), TEXT("自发光")});
            }
            else if (TargetPinName.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
            {
                TargetPin = FindByContains({TEXT("不透明"), TEXT("Opacity")});
            }
            else if (TargetPinName.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))
            {
                TargetPin = FindByContains({TEXT("Base"), TEXT("基础")});
            }
        }

        // Strategy 3: direct name match.
        if (!TargetPin)
        {
            for (UEdGraphPin* Pin : RootNode->Pins)
            {
                if (Pin->Direction == EGPD_Input && Pin->PinName.ToString().Equals(TargetPinName, ESearchCase::IgnoreCase))
                {
                    TargetPin = Pin;
                    break;
                }
            }
        }

        if (!TargetPin)
        {
            return FMCPToolResult::Error(FString::Printf(TEXT("Could not resolve root input pin '%s'"), *TargetPinName));
        }

        SourcePin->MakeLinkTo(TargetPin);

        // Sync the data layer so that Material->EmissiveColor.Expression also reflects this.
        if (FExpressionInput* DataInput = MaterialCommonUtils::FindInputProperty(Mat, TargetPinName))
        {
            if (SourceGraphNode->MaterialExpression)
            {
                DataInput->Expression = SourceGraphNode->MaterialExpression;
                DataInput->OutputIndex = 0;
            }
        }

        if (Mat->MaterialGraph) { Mat->MaterialGraph->NotifyGraphChanged(); }
        Mat->Modify();
        Mat->PostEditChange();
        Mat->MarkPackageDirty();
        MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

        TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
        R->SetStringField(TEXT("from"), FromHandle);
        R->SetStringField(TEXT("to_pin"), TargetPin->PinName.ToString());
        return FMCPToolResult::Success(FString::Printf(TEXT("Connected %s -> Root.%s"), *FromHandle, *TargetPin->PinName.ToString()), R);
    }

    if (bIsRoot)
    {
        // Graph not available (material not open in editor) — reflection fallback for the root.
        UMaterialExpression* FromExpr = MaterialCommonUtils::FindExpressionByHandle(Mat, FromHandle);
        if (!FromExpr) { return FMCPToolResult::Error(FString::Printf(TEXT("Source '%s' not found"), *FromHandle)); }

        if (FExpressionInput* InputPtr = MaterialCommonUtils::FindInputProperty(Mat, ToPin))
        {
            InputPtr->Expression = FromExpr;
            InputPtr->OutputIndex = 0;
            Mat->PostEditChange();
            Mat->MarkPackageDirty();
            MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);
            return FMCPToolResult::Success(FString::Printf(TEXT("Connected %s -> Root.%s (reflection)"), *FromHandle, *ToPin));
        }
        return FMCPToolResult::Error(TEXT("Could not resolve root input via reflection"));
    }

    // === NORMAL NODE: reflection-based connection ===
    UMaterialExpression* FromNode = MaterialCommonUtils::FindExpressionByHandle(Mat, FromHandle);
    if (!FromNode) { return FMCPToolResult::Error(FString::Printf(TEXT("Source '%s' not found"), *FromHandle)); }

    UMaterialExpression* TargetExpr = MaterialCommonUtils::FindExpressionByHandle(Mat, ToHandle);
    if (!TargetExpr) { return FMCPToolResult::Error(FString::Printf(TEXT("Target '%s' not found"), *ToHandle)); }

    FExpressionInput* InputPtr = nullptr;
    if (ToPin.IsEmpty())
    {
        const TArray<FString> TryPins = {TEXT("Input"), TEXT("Coordinates"), TEXT("UV"), TEXT("Alpha"), TEXT("A")};
        for (const FString& Try : TryPins)
        {
            InputPtr = MaterialCommonUtils::FindInputProperty(TargetExpr, Try);
            if (InputPtr) break;
        }
    }
    else
    {
        InputPtr = MaterialCommonUtils::FindInputProperty(TargetExpr, ToPin);
    }

    // Custom node inputs are stored in TArray<FCustomInput>, not as UPROPERTY structs.
    if (UMaterialExpressionCustom* CustomNode = Cast<UMaterialExpressionCustom>(TargetExpr))
    {
        if (!InputPtr)
        {
            for (FCustomInput& Inp : CustomNode->Inputs)
            {
                if (Inp.InputName.ToString().Equals(ToPin, ESearchCase::IgnoreCase))
                {
                    InputPtr = &Inp.Input;
                    break;
                }
            }
        }
    }

    if (!InputPtr)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Could not resolve target pin '%s' on '%s'"), *ToPin, *ToHandle));
    }

    InputPtr->Expression = FromNode;
    InputPtr->OutputIndex = 0;

    Mat->Modify();
    Mat->PostEditChange();
    Mat->MarkPackageDirty();
    TargetExpr->PostEditChange();
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    return FMCPToolResult::Success(FString::Printf(TEXT("Connected %s -> %s.%s"), *FromHandle, *ToHandle, *ToPin));
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteSetNodeProperties(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("properties"), PropsPtr) || !PropsPtr || !PropsPtr->IsValid())
    {
        return FMCPToolResult::Error(TEXT("Missing required 'properties' object"));
    }
    const TSharedPtr<FJsonObject>& Props = *PropsPtr;

    const bool bTargetRoot = MaterialCommonUtils::IsRootHandle(Mat, Handle);
    UObject* TargetObject = bTargetRoot ? (UObject*)Mat : (UObject*)MaterialCommonUtils::FindExpressionByHandle(Mat, Handle);
    if (!TargetObject)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *Handle));
    }

    int32 AppliedCount = 0;
    for (const auto& Pair : Props->Values)
    {
        const FString& PropName = Pair.Key;
        const TSharedPtr<FJsonValue>& JsonVal = Pair.Value;

        FProperty* Prop = TargetObject->GetClass()->FindPropertyByName(*PropName);
        if (!Prop)
        {
            continue;
        }

        if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            FloatProp->SetPropertyValue_InContainer(TargetObject, JsonVal->AsNumber());
        }
        else if (FDoubleProperty* DblProp = CastField<FDoubleProperty>(Prop))
        {
            DblProp->SetPropertyValue_InContainer(TargetObject, JsonVal->AsNumber());
        }
        else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            IntProp->SetPropertyValue_InContainer(TargetObject, (int32)JsonVal->AsNumber());
        }
        else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            BoolProp->SetPropertyValue_InContainer(TargetObject, JsonVal->AsBool());
        }
        else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
        {
            if (JsonVal->Type == EJson::String)
            {
                const int64 EnumValue = EnumProp->GetEnum()->GetValueByNameString(JsonVal->AsString());
                if (EnumValue != INDEX_NONE)
                {
                    EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(TargetObject), EnumValue);
                }
            }
            else if (JsonVal->Type == EJson::Number)
            {
                EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(TargetObject), (int64)JsonVal->AsNumber());
            }
        }
        else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
        {
            if (ByteProp->Enum && JsonVal->Type == EJson::String)
            {
                const int64 EnumValue = ByteProp->Enum->GetValueByNameString(*JsonVal->AsString(), EGetByNameFlags::None);
                if (EnumValue != INDEX_NONE)
                {
                    ByteProp->SetPropertyValue_InContainer(TargetObject, (uint8)EnumValue);
                }
            }
            else if (JsonVal->Type == EJson::Number)
            {
                ByteProp->SetPropertyValue_InContainer(TargetObject, (uint8)JsonVal->AsNumber());
            }
        }
        else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
        {
            StrProp->SetPropertyValue_InContainer(TargetObject, JsonVal->AsString());
        }
        else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
        {
            NameProp->SetPropertyValue_InContainer(TargetObject, FName(*JsonVal->AsString()));
        }
        else
        {
            continue;
        }
        AppliedCount++;
    }

    TargetObject->PostEditChange();
    if (bTargetRoot) { Mat->MarkPackageDirty(); }
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("applied_count"), AppliedCount);
    return FMCPToolResult::Success(FString::Printf(TEXT("Applied %d properties to %s"), AppliedCount, *Handle), Result);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteGetNodeInfo(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    const bool bIsRoot = MaterialCommonUtils::IsRootHandle(Mat, Handle);

    UEdGraphNode* TargetNode = nullptr;
    if (Mat->MaterialGraph)
    {
        if (bIsRoot)
        {
            TargetNode = MaterialCommonUtils::FindRootGraphNode(Mat);
        }
        else
        {
            for (UEdGraphNode* Node : Mat->MaterialGraph->Nodes)
            {
                UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
                if (MatNode && MatNode->MaterialExpression)
                {
                    if (MatNode->MaterialExpression->GetName().Equals(Handle, ESearchCase::IgnoreCase) ||
                        MatNode->MaterialExpression->Desc.Equals(Handle, ESearchCase::IgnoreCase))
                    {
                        TargetNode = Node;
                        break;
                    }
                }
            }
        }
    }

    TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> PinsArray;
    TSharedPtr<FJsonObject> ConnectionsObj = MakeShared<FJsonObject>();
    TSet<FString> UniquePins;

    if (TargetNode)
    {
        RootObj->SetStringField(TEXT("source"), TEXT("graph"));
        for (UEdGraphPin* Pin : TargetNode->Pins)
        {
            if (Pin->Direction != EGPD_Input) continue;
            const FString PinName = Pin->PinName.ToString();
            if (UniquePins.Contains(PinName)) continue;
            UniquePins.Add(PinName);

            FString StableId = PinName;
            if (bIsRoot)
            {
#if WITH_EDITOR
                if (Mat->GetEditorOnlyData())
                {
                    for (TFieldIterator<FProperty> PropIt(Mat->GetEditorOnlyData()->GetClass()); PropIt; ++PropIt)
                    {
                        if (PropIt->GetDisplayNameText().ToString().Equals(PinName, ESearchCase::IgnoreCase))
                        {
                            StableId = PropIt->GetName();
                            break;
                        }
                    }
                }
#endif
            }

            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), PinName);
            PinObj->SetStringField(TEXT("id"), StableId);
            PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));

            if (Pin->LinkedTo.Num() > 0)
            {
                UEdGraphPin* ConnectedPin = Pin->LinkedTo[0];
                if (ConnectedPin && ConnectedPin->GetOwningNode())
                {
                    FString SourceHandle = ConnectedPin->GetOwningNode()->GetName();
                    if (UMaterialGraphNode* SourceMat = Cast<UMaterialGraphNode>(ConnectedPin->GetOwningNode()))
                    {
                        if (SourceMat->MaterialExpression)
                        {
                            SourceHandle = SourceMat->MaterialExpression->GetName();
                            if (!SourceMat->MaterialExpression->Desc.IsEmpty())
                            {
                                SourceHandle = SourceMat->MaterialExpression->Desc;
                            }
                        }
                    }
                    ConnectionsObj->SetStringField(StableId, SourceHandle);
                }
            }
        }
    }
    else if (bIsRoot)
    {
        RootObj->SetStringField(TEXT("source"), TEXT("reflection"));
        TArray<UObject*> SearchTargets;
#if WITH_EDITOR
        if (Mat->GetEditorOnlyData()) { SearchTargets.Add((UObject*)Mat->GetEditorOnlyData()); }
#endif
        SearchTargets.Add(Mat);

        for (UObject* Target : SearchTargets)
        {
            for (TFieldIterator<FProperty> PropIt(Target->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                const FString PropName = Prop->GetName();
                if (UniquePins.Contains(PropName)) continue;

                FStructProperty* StructProp = CastField<FStructProperty>(Prop);
                if (StructProp && StructProp->Struct)
                {
                    const FString StructName = StructProp->Struct->GetName();
                    if (!StructName.Equals(TEXT("ExpressionInput")) && !StructName.Contains(TEXT("MaterialInput")))
                    {
                        continue;
                    }
                    UniquePins.Add(PropName);

                    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                    PinObj->SetStringField(TEXT("name"), Prop->GetDisplayNameText().ToString());
                    PinObj->SetStringField(TEXT("id"), PropName);
                    PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));

                    FExpressionInput* InputPtr = StructProp->ContainerPtrToValuePtr<FExpressionInput>(Target);
                    if (InputPtr && InputPtr->Expression)
                    {
                        FString SourceHandle = InputPtr->Expression->GetName();
                        if (!InputPtr->Expression->Desc.IsEmpty())
                        {
                            SourceHandle = InputPtr->Expression->Desc;
                        }
                        ConnectionsObj->SetStringField(PropName, SourceHandle);
                    }
                }
            }
        }
    }
    else
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *Handle));
    }

    RootObj->SetStringField(TEXT("handle"), Handle);
    RootObj->SetArrayField(TEXT("pins"), PinsArray);
    RootObj->SetObjectField(TEXT("connections"), ConnectionsObj);

    return FMCPToolResult::Success(FString::Printf(TEXT("Inspected %s"), *Handle), RootObj);
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteSetOutputNode(const TSharedRef<FJsonObject>& Params)
{
    FString ResolveError;
    UMaterial* Mat = ResolveTargetMaterial(Params, ResolveError);
    if (!Mat) { return FMCPToolResult::Error(ResolveError); }

    FString Handle;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("node_handle"), Handle, Error)) { return Error.GetValue(); }

    if (!Mat->MaterialGraph)
    {
        return FMCPToolResult::Error(TEXT("MaterialGraph not available — open the material editor first"));
    }

    UMaterialGraphNode* SourceNode = nullptr;
    for (UEdGraphNode* Node : Mat->MaterialGraph->Nodes)
    {
        UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
        if (MatNode && MatNode->MaterialExpression)
        {
            if (MatNode->MaterialExpression->GetName().Equals(Handle, ESearchCase::IgnoreCase) ||
                MatNode->MaterialExpression->Desc.Equals(Handle, ESearchCase::IgnoreCase))
            {
                SourceNode = MatNode;
                break;
            }
        }
    }
    if (!SourceNode) { return FMCPToolResult::Error(FString::Printf(TEXT("Source '%s' not found"), *Handle)); }

    UEdGraphNode* RootNode = MaterialCommonUtils::FindRootGraphNode(Mat);
    if (!RootNode) { return FMCPToolResult::Error(TEXT("Root node not found")); }

    UEdGraphPin* SourcePin = nullptr;
    for (UEdGraphPin* Pin : SourceNode->Pins)
    {
        if (Pin->Direction == EGPD_Output) { SourcePin = Pin; break; }
    }
    if (!SourcePin) { return FMCPToolResult::Error(TEXT("No output pin on source")); }

    FString ConnectedTo;
    if (Mat->bUseMaterialAttributes)
    {
        for (UEdGraphPin* Pin : RootNode->Pins)
        {
            if (Pin->Direction == EGPD_Input && Pin->PinName.ToString().Equals(TEXT("MaterialAttributes"), ESearchCase::IgnoreCase))
            {
                SourcePin->MakeLinkTo(Pin);
                ConnectedTo = Pin->PinName.ToString();
                break;
            }
        }
    }
    else
    {
        for (UEdGraphPin* Pin : RootNode->Pins)
        {
            if (Pin->Direction != EGPD_Input) continue;
            const FString PinName = Pin->PinName.ToString();
            if (PinName.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase) ||
                PinName.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))
            {
                SourcePin->MakeLinkTo(Pin);
                ConnectedTo = PinName;
                break;
            }
        }
    }

    if (ConnectedTo.IsEmpty())
    {
        return FMCPToolResult::Error(TEXT("Could not find a suitable root output pin"));
    }

    Mat->PostEditChange();
    Mat->MarkPackageDirty();
    MaterialCommonUtils::ForceRefreshMaterialEditor(Mat);

    return FMCPToolResult::Success(FString::Printf(TEXT("Wired %s -> Root.%s"), *Handle, *ConnectedTo));
}

FMCPToolResult FMCPTool_MaterialGraph::ExecuteCompileAsset(const TSharedRef<FJsonObject>& Params)
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
