// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: write operations against UStateTree assets (editor-only).
 *
 * Operations:
 *   add_state         - Create a new state under root or a parent state.
 *                       Inputs: state_name, state_type?, parent_state?, selection_behavior?
 *   add_task          - Add a task struct (FStateTreeTaskBase subclass) to a state.
 *                       Inputs: state_name, task_class, task_name?
 *   add_transition    - Add a transition from a source state to a target.
 *                       Target may be a state name, or one of: Succeeded | Failed | Next.
 *                       Inputs: source_state, target_state, trigger?, priority?
 *   remove_state      - Remove a state (and its children) from the tree.
 *                       Inputs: state_name, confirm_delete (must be true)
 *
 * All write operations:
 *   - require the editor to be running (UStateTreeEditorData lives there)
 *   - wrap mutations in a FScopedTransaction so undo works
 *   - call Modify() on StateTree + EditorData + the affected state
 *   - call MarkPackageDirty() on the StateTree asset
 *
 * Adapted from yes-ue-mcp's AddStateTreeStateTool / AddStateTreeTaskTool /
 * AddStateTreeTransitionTool / RemoveStateTreeStateTool — consolidated into a
 * single MCP surface to match UnrealClaude's compound-tool convention
 * (see umg_modify, blueprint_modify).
 */
class FMCPTool_StateTreeModify : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("statetree_modify");
        Info.Description = TEXT(
            "Mutate a UStateTree asset: add states / tasks / transitions, or remove "
            "states. Read with statetree_query first.\n\n"
            "Operations:\n"
            "- add_state:      requires state_name. Optional state_type "
            "(State|Group|Linked|Subtree), parent_state (else added at root), "
            "selection_behavior (None|TryEnterState|TrySelectChildrenInOrder|TryFollowTransitions).\n"
            "- add_task:       requires state_name + task_class (UScriptStruct name "
            "inheriting from FStateTreeTaskBase). Optional task_name.\n"
            "- add_transition: requires source_state + target_state. target_state may be a "
            "state name OR one of 'Succeeded' / 'Failed' / 'Next'. Optional trigger "
            "(OnStateCompleted|OnStateFailed|OnTick|OnEvent), priority (Low|Normal|High|Critical).\n"
            "- remove_state:   requires state_name + confirm_delete=true. Removes children too.");
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: add_state | add_task | add_transition | remove_state"), true),
            FMCPToolParameter(TEXT("asset_path"), TEXT("string"),
                TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)"), true),

            // add_state / add_task / remove_state
            FMCPToolParameter(TEXT("state_name"), TEXT("string"),
                TEXT("State name (required by add_state, add_task, remove_state)"), false),

            // add_state
            FMCPToolParameter(TEXT("state_type"), TEXT("string"),
                TEXT("add_state: State | Group | Linked | Subtree"), false, TEXT("State")),
            FMCPToolParameter(TEXT("parent_state"), TEXT("string"),
                TEXT("add_state: optional parent state name (else inserted at root)"), false),
            FMCPToolParameter(TEXT("selection_behavior"), TEXT("string"),
                TEXT("add_state: None | TryEnterState | TrySelectChildrenInOrder | TryFollowTransitions"),
                false, TEXT("TryEnterState")),

            // add_task
            FMCPToolParameter(TEXT("task_class"), TEXT("string"),
                TEXT("add_task: UScriptStruct name inheriting from FStateTreeTaskBase"), false),
            FMCPToolParameter(TEXT("task_name"), TEXT("string"),
                TEXT("add_task: optional display name (defaults to task_class)"), false),

            // add_transition
            FMCPToolParameter(TEXT("source_state"), TEXT("string"),
                TEXT("add_transition: name of the source state"), false),
            FMCPToolParameter(TEXT("target_state"), TEXT("string"),
                TEXT("add_transition: target state name OR Succeeded | Failed | Next"), false),
            FMCPToolParameter(TEXT("trigger"), TEXT("string"),
                TEXT("add_transition: OnStateCompleted | OnStateFailed | OnTick | OnEvent"),
                false, TEXT("OnStateCompleted")),
            FMCPToolParameter(TEXT("priority"), TEXT("string"),
                TEXT("add_transition: Low | Normal | High | Critical"),
                false, TEXT("Normal")),

            // remove_state
            FMCPToolParameter(TEXT("confirm_delete"), TEXT("boolean"),
                TEXT("remove_state: must be true to authorize destructive op"),
                false, TEXT("false"))
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult ExecuteAddState(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteAddTask(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteAddTransition(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteRemoveState(const TSharedRef<FJsonObject>& Params);

    /**
     * Load StateTree + cast its editor data. Returns nullptrs and writes an
     * error result on failure.
     *
     * @param AssetPath - asset path
     * @param OutStateTree - loaded StateTree (out)
     * @param OutEditorData - cast editor data (out)
     * @param OutError - error result if load fails (out)
     * @return true on success
     */
    static bool LoadStateTreeAndEditorData(
        const FString& AssetPath,
        class UStateTree*& OutStateTree,
        class UStateTreeEditorData*& OutEditorData,
        TOptional<FMCPToolResult>& OutError);

    /**
     * Recursively search StateTree editor data for a state by name.
     * Returns the state and (when bWantParent is true) its parent in the
     * editor hierarchy, or nullptr in OutParent if it lives at root.
     *
     * @param EditorData - editor data to search
     * @param StateName - state name to find
     * @param OutParent - parent state (nullptr if root). Only set when bWantParent.
     * @param bWantParent - whether to fill OutParent
     * @return the found state, or nullptr if not found
     */
    static class UStateTreeState* FindStateByName(
        class UStateTreeEditorData* EditorData,
        const FString& StateName,
        class UStateTreeState*& OutParent,
        bool bWantParent);
};
