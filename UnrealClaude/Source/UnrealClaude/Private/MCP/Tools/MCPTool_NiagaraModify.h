// Copyright Ban Ming. All Rights Reserved.
// 
// 
// 

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Niagara VFX system operations (editor-only).
 *
 * Compound tool dispatching on the "operation" string parameter.
 *
 * Operations:
 *   list_systems       - Find all UNiagaraSystem assets under a content path via
 *                        FARFilter. Returns {systems:[{path, name}], count}.
 *
 *   get_info           - Load a UNiagaraSystem and inspect its emitter handles
 *                        and ExposedParameters bag. Returns {name, path,
 *                        emitter_count, emitter_names, parameters:[{name, type}]}.
 *
 *   spawn_at_location  - Spawn a UNiagaraSystem into the editor preview world at
 *                        a given world-space location via
 *                        UNiagaraFunctionLibrary::SpawnSystemAtLocation.
 *                        Returns {actor_name, system_path, location, auto_destroy}.
 *
 *   set_parameter      - Best-effort write of a user-exposed parameter directly
 *                        into the system asset's ExposedParameters bag
 *                        (FNiagaraParameterStore). Only succeeds if the named
 *                        variable already exists in the bag. Supported value
 *                        shapes: number (float), {x,y,z} (FVector), {r,g,b,a}
 *                        (FLinearColor). Returns {parameter_name, type, applied}.
 *
 * NOTE: Niagara module dependency must be declared in UnrealClaude.Build.cs
 * before this file will compile (tracked separately as task #10).
 */
class FMCPTool_NiagaraModify : public FMCPToolBase
{
public:
    /**
     * Return tool metadata: name, description, parameter schema, and annotations.
     * Called by the MCP registry to advertise the tool to LLM clients.
     * @return FMCPToolInfo populated with niagara_modify's contract.
     */
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("niagara_modify");
        Info.Description = TEXT(
            "Inspect and manipulate Niagara VFX systems in the Unreal Editor.\n\n"
            "Operations:\n"
            "- list_systems:      Search content for UNiagaraSystem assets. "
            "Optional path (default /Game) and recursive flag (default true). "
            "Returns {systems:[{path,name}], count}.\n"
            "- get_info:          Load a system and return emitter list and "
            "ExposedParameters. Requires system_path. Returns {name, path, "
            "emitter_count, emitter_names, parameters:[{name,type}]}.\n"
            "- spawn_at_location: Spawn a Niagara system into the editor world "
            "at a given location. Requires system_path and location {x,y,z}. "
            "Optional auto_destroy (default true). Returns {actor_name, "
            "system_path, location, auto_destroy}.\n"
            "- set_parameter:     Write a value to a named user-exposed parameter "
            "in the system asset's ExposedParameters bag. Best-effort: only "
            "succeeds if the variable already exists. Requires system_path, "
            "parameter_name, and value (number for float, {x,y,z} for vector, "
            "{r,g,b,a} for color). Returns {parameter_name, type, applied}.");
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: list_systems | get_info | spawn_at_location | set_parameter"),
                true),

            // list_systems
            FMCPToolParameter(TEXT("path"), TEXT("string"),
                TEXT("list_systems: content root to search (default /Game)"),
                false, TEXT("/Game")),
            FMCPToolParameter(TEXT("recursive"), TEXT("boolean"),
                TEXT("list_systems: recurse into subdirectories (default true)"),
                false, TEXT("true")),

            // get_info / spawn_at_location / set_parameter
            FMCPToolParameter(TEXT("system_path"), TEXT("string"),
                TEXT("Full asset path to the UNiagaraSystem "
                     "(e.g. /Game/VFX/NS_Explosion)"),
                false),

            // spawn_at_location
            FMCPToolParameter(TEXT("location"), TEXT("object"),
                TEXT("spawn_at_location: world-space position {x,y,z}"),
                false),
            FMCPToolParameter(TEXT("auto_destroy"), TEXT("boolean"),
                TEXT("spawn_at_location: destroy component when complete (default true)"),
                false, TEXT("true")),

            // set_parameter
            FMCPToolParameter(TEXT("parameter_name"), TEXT("string"),
                TEXT("set_parameter: name of the exposed variable to write"),
                false),
            FMCPToolParameter(TEXT("value"), TEXT("object"),
                TEXT("set_parameter: number for float, {x,y,z} for vector, "
                     "{r,g,b,a} for linear color"),
                false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    /**
     * Dispatch to the appropriate operation handler based on the "operation"
     * string parameter.
     * @param Params - Validated JSON parameter object from the MCP request.
     * @return Success or Error result with structured JSON data.
     */
    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    /**
     * Handle the list_systems operation.
     * Runs an FARFilter on UNiagaraSystem class under the requested content path.
     * @param Params - Full parameter object (reads "path", "recursive").
     * @return Success with {systems:[{path,name}], count} or Error.
     */
    FMCPToolResult ExecuteListSystems(const TSharedRef<FJsonObject>& Params);

    /**
     * Handle the get_info operation.
     * Loads the UNiagaraSystem at system_path and introspects its emitter
     * handles and ExposedParameters bag.
     * @param Params - Full parameter object (reads "system_path").
     * @return Success with {name, path, emitter_count, emitter_names,
     *         parameters:[{name,type}]} or Error.
     */
    FMCPToolResult ExecuteGetInfo(const TSharedRef<FJsonObject>& Params);

    /**
     * Handle the spawn_at_location operation.
     * Validates the editor world, loads the system, and calls
     * UNiagaraFunctionLibrary::SpawnSystemAtLocation.
     * @param Params - Full parameter object (reads "system_path", "location",
     *                 "auto_destroy").
     * @return Success with {actor_name, system_path, location, auto_destroy}
     *         or Error.
     */
    FMCPToolResult ExecuteSpawnAtLocation(const TSharedRef<FJsonObject>& Params);

    /**
     * Handle the set_parameter operation.
     * Loads the system asset and writes a value into its ExposedParameters bag.
     * Infers type from the JSON value shape (number/object-with-x,y,z/
     * object-with-r,g,b,a). Only succeeds if the named variable already exists.
     * @param Params - Full parameter object (reads "system_path",
     *                 "parameter_name", "value").
     * @return Success with {parameter_name, type, applied:true} or Error.
     */
    FMCPToolResult ExecuteSetParameter(const TSharedRef<FJsonObject>& Params);
};
