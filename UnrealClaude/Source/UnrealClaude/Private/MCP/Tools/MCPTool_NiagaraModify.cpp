// Copyright Ban Ming. All Rights Reserved.
// 
// 
// 

#include "MCPTool_NiagaraModify.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"

// ---------------------------------------------------------------------------
// Execute — top-level dispatch
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_NiagaraModify::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Extract and validate the required "operation" string.
    FString Operation;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 2: Dispatch to the per-operation handler.
    if (Operation == TEXT("list_systems"))      return ExecuteListSystems(Params);
    if (Operation == TEXT("get_info"))          return ExecuteGetInfo(Params);
    if (Operation == TEXT("spawn_at_location")) return ExecuteSpawnAtLocation(Params);
    if (Operation == TEXT("set_parameter"))     return ExecuteSetParameter(Params);

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation '%s'. Expected: list_systems | get_info | "
             "spawn_at_location | set_parameter"),
        *Operation));
}

// ---------------------------------------------------------------------------
// list_systems
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_NiagaraModify::ExecuteListSystems(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Read optional search parameters.
    const FString SearchPath = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));
    const bool bRecursive    = ExtractOptionalBool(Params, TEXT("recursive"), true);

    // Step 2: Obtain the asset registry.
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Step 3: Build a filter targeting UNiagaraSystem under the requested path.
    FARFilter Filter;
    Filter.bRecursivePaths   = bRecursive;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*SearchPath));

    // Resolve the class path for UNiagaraSystem in the Niagara module.
    static const FTopLevelAssetPath NiagaraSystemClassPath(
        TEXT("/Script/Niagara"), TEXT("NiagaraSystem"));
    Filter.ClassPaths.Add(NiagaraSystemClassPath);

    // Step 4: Query and collect results.
    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    // Step 5: Build the JSON systems array.
    TArray<TSharedPtr<FJsonValue>> SystemsArray;
    SystemsArray.Reserve(Assets.Num());
    for (const FAssetData& Asset : Assets)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        SystemsArray.Add(MakeShared<FJsonValueObject>(Entry));
    }

    // Step 6: Build response data.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("systems"), SystemsArray);
    Data->SetNumberField(TEXT("count"), Assets.Num());

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d Niagara system%s under '%s'"),
            Assets.Num(), Assets.Num() == 1 ? TEXT("") : TEXT("s"), *SearchPath),
        Data);
}

// ---------------------------------------------------------------------------
// get_info
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_NiagaraModify::ExecuteGetInfo(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Extract required system_path.
    FString SystemPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 2: Load the Niagara system asset.
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load UNiagaraSystem: %s"), *SystemPath));
    }

    // Step 3: Collect emitter handles.
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    TArray<TSharedPtr<FJsonValue>> EmitterNamesArray;
    EmitterNamesArray.Reserve(EmitterHandles.Num());
    for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
    {
        EmitterNamesArray.Add(
            MakeShared<FJsonValueString>(Handle.GetName().ToString()));
    }

    // Step 4: Collect exposed parameters from the parameter store.
    // ReadParameterVariables returns TArrayView<const FNiagaraVariableWithOffset>.
    TArray<TSharedPtr<FJsonValue>> ParametersArray;
    const FNiagaraParameterStore& ParamStore = System->GetExposedParameters();
    for (const FNiagaraVariableWithOffset& VarWithOffset : ParamStore.ReadParameterVariables())
    {
        TSharedPtr<FJsonObject> ParamEntry = MakeShared<FJsonObject>();
        ParamEntry->SetStringField(TEXT("name"), VarWithOffset.GetName().ToString());
        ParamEntry->SetStringField(TEXT("type"), VarWithOffset.GetType().GetName());
        ParametersArray.Add(MakeShared<FJsonValueObject>(ParamEntry));
    }

    // Step 5: Build response data.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"),          System->GetName());
    Data->SetStringField(TEXT("path"),          SystemPath);
    Data->SetNumberField(TEXT("emitter_count"), EmitterHandles.Num());
    Data->SetArrayField(TEXT("emitter_names"),  EmitterNamesArray);
    Data->SetArrayField(TEXT("parameters"),     ParametersArray);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Niagara system '%s': %d emitter(s), %d exposed parameter(s)"),
            *System->GetName(), EmitterHandles.Num(), ParametersArray.Num()),
        Data);
}

// ---------------------------------------------------------------------------
// spawn_at_location
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_NiagaraModify::ExecuteSpawnAtLocation(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Validate editor context — need a live editor world to spawn into.
    UWorld* EditorWorld = nullptr;
    TOptional<FMCPToolResult> CtxError;
    if (TOptional<FMCPToolResult> Err = ValidateEditorContext(EditorWorld))
    {
        return Err.GetValue();
    }

    // Step 2: Extract required parameters.
    FString SystemPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 3: Extract location — required for spawn.
    if (!HasVectorParam(Params, TEXT("location")))
    {
        return FMCPToolResult::Error(
            TEXT("Missing required parameter: location ({x,y,z})"));
    }
    const FVector SpawnLocation = ExtractVectorParam(Params, TEXT("location"), FVector::ZeroVector);
    const bool bAutoDestroy     = ExtractOptionalBool(Params, TEXT("auto_destroy"), true);

    // Step 4: Load the Niagara system asset.
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load UNiagaraSystem: %s"), *SystemPath));
    }

    // Step 5: Spawn the system into the editor world.
    // SpawnSystemAtLocation returns the UNiagaraComponent; its owner actor
    // holds the actor name we report back.
    UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        EditorWorld,
        System,
        SpawnLocation,
        FRotator::ZeroRotator,
        FVector::OneVector,
        bAutoDestroy);

    if (!NiagaraComp)
    {
        return FMCPToolResult::Error(
            TEXT("UNiagaraFunctionLibrary::SpawnSystemAtLocation returned null"));
    }

    // Step 6: Collect the owner actor's name for the response.
    FString ActorName = TEXT("Unknown");
    if (AActor* OwnerActor = NiagaraComp->GetOwner())
    {
        ActorName = OwnerActor->GetName();
    }

    // Step 7: Build response data.
    TSharedPtr<FJsonObject> LocationJson = MakeShared<FJsonObject>();
    LocationJson->SetNumberField(TEXT("x"), SpawnLocation.X);
    LocationJson->SetNumberField(TEXT("y"), SpawnLocation.Y);
    LocationJson->SetNumberField(TEXT("z"), SpawnLocation.Z);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_name"),   ActorName);
    Data->SetStringField(TEXT("system_path"),  SystemPath);
    Data->SetObjectField(TEXT("location"),     LocationJson);
    Data->SetBoolField  (TEXT("auto_destroy"), bAutoDestroy);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Spawned Niagara system '%s' at (%.1f, %.1f, %.1f) — actor '%s'"),
            *System->GetName(), SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z,
            *ActorName),
        Data);
}

// ---------------------------------------------------------------------------
// set_parameter
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_NiagaraModify::ExecuteSetParameter(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Extract required parameters.
    FString SystemPath, ParameterName;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("system_path"),    SystemPath,    ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("parameter_name"), ParameterName, ParamError)) return ParamError.GetValue();

    // Step 2: Ensure a "value" field is present.
    if (!Params->HasField(TEXT("value")))
    {
        return FMCPToolResult::Error(
            TEXT("Missing required parameter: value "
                 "(number for float, {x,y,z} for vector, {r,g,b,a} for color)"));
    }

    // Step 3: Load the Niagara system asset.
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load UNiagaraSystem: %s"), *SystemPath));
    }

    // Step 4: Retrieve the mutable parameter store.
    FNiagaraParameterStore& ParamStore = System->GetExposedParameters();

    // Step 5: Search the store for a variable matching ParameterName.
    // ReadParameterVariables gives a view of all registered variables with their
    // byte offsets so we can write back through SetParameterValue.
    FNiagaraVariable FoundVar;
    bool bFound = false;
    for (const FNiagaraVariableWithOffset& VarWithOffset : ParamStore.ReadParameterVariables())
    {
        if (VarWithOffset.GetName().ToString() == ParameterName)
        {
            // Slice to the base FNiagaraVariable (name + type, no offset).
            FoundVar = FNiagaraVariable(VarWithOffset.GetType(), VarWithOffset.GetName());
            bFound   = true;
            break;
        }
    }

    if (!bFound)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Parameter '%s' not found in ExposedParameters of '%s'. "
                 "set_parameter is best-effort and only works on parameters "
                 "already in the system's ExposedParameters bag."),
            *ParameterName, *SystemPath));
    }

    // Step 6: Determine value type from JSON shape and write into the store.
    // Precedence:  number → float,  object with r/g/b → color,  object with x/y/z → vector.
    FString AppliedType;
    const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
    if (!ValueField || !ValueField->IsValid())
    {
        return FMCPToolResult::Error(TEXT("Invalid 'value' field in parameters"));
    }

    const EJson ValueType = (*ValueField)->Type;
    if (ValueType == EJson::Number)
    {
        // Float parameter.
        const float FloatVal = static_cast<float>((*ValueField)->AsNumber());
        ParamStore.SetParameterValue(FloatVal, FoundVar);
        AppliedType = TEXT("float");
    }
    else if (ValueType == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ValueObj = (*ValueField)->AsObject();
        if (ValueObj.IsValid())
        {
            // Detect color shape: requires at least "r" or "g" or "b".
            if (ValueObj->HasField(TEXT("r")) || ValueObj->HasField(TEXT("g")) || ValueObj->HasField(TEXT("b")))
            {
                // Linear color parameter.
                double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
                ValueObj->TryGetNumberField(TEXT("r"), R);
                ValueObj->TryGetNumberField(TEXT("g"), G);
                ValueObj->TryGetNumberField(TEXT("b"), B);
                ValueObj->TryGetNumberField(TEXT("a"), A);
                const FLinearColor ColorVal(
                    static_cast<float>(R), static_cast<float>(G),
                    static_cast<float>(B), static_cast<float>(A));
                ParamStore.SetParameterValue(ColorVal, FoundVar);
                AppliedType = TEXT("color");
            }
            else
            {
                // Vector parameter — expects x/y/z fields.
                double X = 0.0, Y = 0.0, Z = 0.0;
                ValueObj->TryGetNumberField(TEXT("x"), X);
                ValueObj->TryGetNumberField(TEXT("y"), Y);
                ValueObj->TryGetNumberField(TEXT("z"), Z);
                const FVector VecVal(
                    static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
                ParamStore.SetParameterValue(VecVal, FoundVar);
                AppliedType = TEXT("vector");
            }
        }
        else
        {
            return FMCPToolResult::Error(
                TEXT("'value' is an object but could not be parsed as vector or color"));
        }
    }
    else
    {
        return FMCPToolResult::Error(
            TEXT("Unsupported 'value' type. Use a number for float, "
                 "{x,y,z} for vector, or {r,g,b,a} for color."));
    }

    // Step 7: Mark the asset dirty so the change is saved with the package.
    System->MarkPackageDirty();

    // Step 8: Build response data.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("parameter_name"), ParameterName);
    Data->SetStringField(TEXT("type"),           AppliedType);
    Data->SetBoolField  (TEXT("applied"),        true);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Set parameter '%s' (%s) on '%s'"),
            *ParameterName, *AppliedType, *System->GetName()),
        Data);
}
