// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: read-only inspection of UStateTree assets.
 *
 * Sections (selectable via `include` parameter):
 *   states        - every state's name, type, parent, depth, children, enable flag
 *   transitions   - state-by-state transition list with trigger / target / priority
 *   tasks         - task instances (FStateTreeTaskBase) with name + struct type
 *   evaluators    - evaluator instances (FStateTreeEvaluatorBase) with name + type
 *   parameters    - default parameter bag (name + type)
 *   all           - everything above (default)
 *
 * Read-before-write convention: this tool MUST be called before
 * `statetree_modify` to confirm asset path + state names + indices.
 *
 * Adapted from yes-ue-mcp's QueryStateTreeTool — schema preserved, dispatcher
 * shape rewritten to FMCPToolBase pattern. UE 5.6/5.7 APIs match.
 */
class FMCPTool_StateTreeQuery : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("statetree_query");
        Info.Description = TEXT(
            "Read-only inspection of a UStateTree asset.\n\n"
            "Use this BEFORE calling statetree_modify to confirm asset path + "
            "current state names. Returns states, transitions, tasks, evaluators, "
            "and default parameters.\n\n"
            "Returns: { name, path, schema?, states?, transitions?, tasks?, "
            "evaluators?, parameters? }");
        Info.Parameters = {
            FMCPToolParameter(TEXT("asset_path"), TEXT("string"),
                TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)"), true),
            FMCPToolParameter(TEXT("include"), TEXT("string"),
                TEXT("Sections to include: 'states', 'transitions', 'tasks', 'evaluators', "
                     "'parameters', or 'all'"),
                false, TEXT("all")),
            FMCPToolParameter(TEXT("detailed"), TEXT("boolean"),
                TEXT("Include per-state detail (selection_behavior, depth, parent, children, "
                     "enabled flag, task/transition begin indices)"),
                false, TEXT("true"))
        };
        Info.Annotations = FMCPToolAnnotations::ReadOnly();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> ExtractStates(class UStateTree* StateTree, bool bDetailed) const;
    TSharedPtr<FJsonObject> ExtractTransitions(class UStateTree* StateTree) const;
    TSharedPtr<FJsonObject> ExtractTasks(class UStateTree* StateTree) const;
    TSharedPtr<FJsonObject> ExtractEvaluators(class UStateTree* StateTree) const;
    TSharedPtr<FJsonObject> ExtractParameters(class UStateTree* StateTree) const;

    static FString GetStateTypeString(uint8 StateType);
    static FString GetSelectionBehaviorString(uint8 SelectionBehavior);
};
