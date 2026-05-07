// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_StateTreeModify.h"
#include "UnrealClaudeModule.h"

#include "Editor.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"

namespace
{
    EStateTreeStateType ParseStateType(const FString& In)
    {
        if (In.Equals(TEXT("Group"),   ESearchCase::IgnoreCase)) return EStateTreeStateType::Group;
        if (In.Equals(TEXT("Linked"),  ESearchCase::IgnoreCase)) return EStateTreeStateType::Linked;
        if (In.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase)) return EStateTreeStateType::Subtree;
        return EStateTreeStateType::State;
    }

    EStateTreeStateSelectionBehavior ParseSelectionBehavior(const FString& In)
    {
        if (In.Equals(TEXT("None"),                    ESearchCase::IgnoreCase)) return EStateTreeStateSelectionBehavior::None;
        if (In.Equals(TEXT("TrySelectChildrenInOrder"),ESearchCase::IgnoreCase)) return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
        if (In.Equals(TEXT("TryFollowTransitions"),    ESearchCase::IgnoreCase)) return EStateTreeStateSelectionBehavior::TryFollowTransitions;
        return EStateTreeStateSelectionBehavior::TryEnterState;
    }

    EStateTreeTransitionTrigger ParseTrigger(const FString& In)
    {
        if (In.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase)) return EStateTreeTransitionTrigger::OnStateFailed;
        if (In.Equals(TEXT("OnTick"),        ESearchCase::IgnoreCase)) return EStateTreeTransitionTrigger::OnTick;
        if (In.Equals(TEXT("OnEvent"),       ESearchCase::IgnoreCase)) return EStateTreeTransitionTrigger::OnEvent;
        return EStateTreeTransitionTrigger::OnStateCompleted;
    }

    EStateTreeTransitionPriority ParsePriority(const FString& In)
    {
        if (In.Equals(TEXT("Low"),      ESearchCase::IgnoreCase)) return EStateTreeTransitionPriority::Low;
        if (In.Equals(TEXT("High"),     ESearchCase::IgnoreCase)) return EStateTreeTransitionPriority::High;
        if (In.Equals(TEXT("Critical"), ESearchCase::IgnoreCase)) return EStateTreeTransitionPriority::Critical;
        return EStateTreeTransitionPriority::Normal;
    }
}

bool FMCPTool_StateTreeModify::LoadStateTreeAndEditorData(
    const FString& AssetPath,
    UStateTree*& OutStateTree,
    UStateTreeEditorData*& OutEditorData,
    TOptional<FMCPToolResult>& OutError)
{
    OutStateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
    if (!OutStateTree)
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load StateTree: %s"), *AssetPath));
        return false;
    }

    OutEditorData = Cast<UStateTreeEditorData>(OutStateTree->EditorData);
    if (!OutEditorData)
    {
        OutError = FMCPToolResult::Error(
            TEXT("StateTree has no editor data; cannot modify (editor must be running)"));
        return false;
    }
    return true;
}

UStateTreeState* FMCPTool_StateTreeModify::FindStateByName(
    UStateTreeEditorData* EditorData,
    const FString& StateName,
    UStateTreeState*& OutParent,
    bool bWantParent)
{
    OutParent = nullptr;
    if (!EditorData)
    {
        return nullptr;
    }

    // Step 1: Walk SubTrees (root states), then descend.
    for (UStateTreeState* RootState : EditorData->SubTrees)
    {
        if (!RootState)
        {
            continue;
        }
        if (RootState->Name.ToString() == StateName)
        {
            // Root state has no parent.
            return RootState;
        }

        // BFS via stack — children list pairs (state, parent).
        TArray<TPair<UStateTreeState*, UStateTreeState*>> Stack;
        for (UStateTreeState* Child : RootState->Children)
        {
            if (Child)
            {
                Stack.Add({ Child, RootState });
            }
        }
        while (Stack.Num() > 0)
        {
            TPair<UStateTreeState*, UStateTreeState*> Cur = Stack.Pop();
            UStateTreeState* CurState = Cur.Key;
            UStateTreeState* CurParent = Cur.Value;
            if (!CurState)
            {
                continue;
            }
            if (CurState->Name.ToString() == StateName)
            {
                if (bWantParent)
                {
                    OutParent = CurParent;
                }
                return CurState;
            }
            for (UStateTreeState* Sub : CurState->Children)
            {
                if (Sub)
                {
                    Stack.Add({ Sub, CurState });
                }
            }
        }
    }
    return nullptr;
}

FMCPToolResult FMCPTool_StateTreeModify::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Required operation + asset_path.
    FString Operation;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 2: Dispatch.
    if (Operation == TEXT("add_state"))      return ExecuteAddState(Params);
    if (Operation == TEXT("add_task"))       return ExecuteAddTask(Params);
    if (Operation == TEXT("add_transition")) return ExecuteAddTransition(Params);
    if (Operation == TEXT("remove_state"))   return ExecuteRemoveState(Params);

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation '%s'. Expected: add_state | add_task | add_transition | remove_state"),
        *Operation));
}

FMCPToolResult FMCPTool_StateTreeModify::ExecuteAddState(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse params.
    FString AssetPath, StateName;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("state_name"), StateName, ParamError)) return ParamError.GetValue();

    const FString StateTypeStr      = ExtractOptionalString(Params, TEXT("state_type"),         TEXT("State"));
    const FString ParentStateName   = ExtractOptionalString(Params, TEXT("parent_state"),       TEXT(""));
    const FString SelectBehaviorStr = ExtractOptionalString(Params, TEXT("selection_behavior"), TEXT("TryEnterState"));

    // Step 2: Load asset + editor data.
    UStateTree* StateTree = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    TOptional<FMCPToolResult> LoadErr;
    if (!LoadStateTreeAndEditorData(AssetPath, StateTree, EditorData, LoadErr)) return LoadErr.GetValue();

    // Step 3: Find parent if specified.
    UStateTreeState* ParentState = nullptr;
    if (!ParentStateName.IsEmpty())
    {
        UStateTreeState* Unused = nullptr;
        ParentState = FindStateByName(EditorData, ParentStateName, Unused, /*bWantParent*/false);
        if (!ParentState)
        {
            return FMCPToolResult::Error(FString::Printf(
                TEXT("Parent state '%s' not found"), *ParentStateName));
        }
    }

    // Step 4: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(
        FString::Printf(TEXT("MCP: Add StateTree State '%s'"), *StateName)));
    StateTree->Modify();
    EditorData->Modify();
    if (ParentState) ParentState->Modify();

    UStateTreeState* NewState = NewObject<UStateTreeState>(EditorData, NAME_None, RF_Transactional);
    NewState->Name              = FName(*StateName);
    NewState->Type              = ParseStateType(StateTypeStr);
    NewState->SelectionBehavior = ParseSelectionBehavior(SelectBehaviorStr);

    if (ParentState)
    {
        ParentState->Children.Add(NewState);
        NewState->Parent = ParentState;
    }
    else
    {
        EditorData->SubTrees.Add(NewState);
    }

    StateTree->MarkPackageDirty();

    // Step 5: Build response.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"),         AssetPath);
    Data->SetStringField(TEXT("state_name"),         StateName);
    Data->SetStringField(TEXT("state_type"),         StateTypeStr);
    Data->SetStringField(TEXT("selection_behavior"), SelectBehaviorStr);
    if (ParentState)
    {
        Data->SetStringField(TEXT("parent_state"), ParentStateName);
    }
    return FMCPToolResult::Success(
        FString::Printf(TEXT("State '%s' added to StateTree"), *StateName), Data);
}

FMCPToolResult FMCPTool_StateTreeModify::ExecuteAddTask(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse params.
    FString AssetPath, StateName, TaskClassName;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("asset_path"),  AssetPath,     ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("state_name"),  StateName,     ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("task_class"),  TaskClassName, ParamError)) return ParamError.GetValue();

    const FString TaskName = ExtractOptionalString(Params, TEXT("task_name"), TaskClassName);

    // Step 2: Load.
    UStateTree* StateTree = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    TOptional<FMCPToolResult> LoadErr;
    if (!LoadStateTreeAndEditorData(AssetPath, StateTree, EditorData, LoadErr)) return LoadErr.GetValue();

    // Step 3: Resolve task class (UScriptStruct inheriting from FStateTreeTaskBase).
    UScriptStruct* TaskStruct = nullptr;
    for (TObjectIterator<UScriptStruct> It; It; ++It)
    {
        UScriptStruct* Struct = *It;
        if (Struct->GetName() == TaskClassName || Struct->GetName().Contains(TaskClassName))
        {
            if (Struct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
            {
                TaskStruct = Struct;
                break;
            }
        }
    }
    if (!TaskStruct)
    {
        TArray<FString> Available;
        for (TObjectIterator<UScriptStruct> It; It; ++It)
        {
            UScriptStruct* Struct = *It;
            if (Struct->IsChildOf(FStateTreeTaskBase::StaticStruct()) &&
                Struct != FStateTreeTaskBase::StaticStruct())
            {
                Available.Add(Struct->GetName());
            }
        }
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Task class '%s' not found. Available: %s"),
            *TaskClassName, *FString::Join(Available, TEXT(", "))));
    }

    // Step 4: Find target state.
    UStateTreeState* Unused = nullptr;
    UStateTreeState* TargetState = FindStateByName(EditorData, StateName, Unused, /*bWantParent*/false);
    if (!TargetState)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("State '%s' not found in StateTree"), *StateName));
    }

    // Step 5: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(
        FString::Printf(TEXT("MCP: Add StateTree Task '%s'"), *TaskName)));
    StateTree->Modify();
    EditorData->Modify();
    TargetState->Modify();

    FStateTreeEditorNode NewTaskNode;
    NewTaskNode.Node.InitializeAs(TaskStruct);
    if (FStateTreeTaskBase* Task = NewTaskNode.Node.GetMutablePtr<FStateTreeTaskBase>())
    {
        Task->Name = FName(*TaskName);
        Task->bTaskEnabled = true;
    }
    TargetState->Tasks.Add(NewTaskNode);

    StateTree->MarkPackageDirty();

    // Step 6: Build response.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("state_name"), StateName);
    Data->SetStringField(TEXT("task_class"), TaskStruct->GetName());
    Data->SetStringField(TEXT("task_name"),  TaskName);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Task '%s' added to state '%s'"), *TaskName, *StateName), Data);
}

FMCPToolResult FMCPTool_StateTreeModify::ExecuteAddTransition(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse params.
    FString AssetPath, SourceStateName, TargetStateName;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("asset_path"),    AssetPath,       ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("source_state"),  SourceStateName, ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("target_state"),  TargetStateName, ParamError)) return ParamError.GetValue();

    const FString TriggerStr  = ExtractOptionalString(Params, TEXT("trigger"),  TEXT("OnStateCompleted"));
    const FString PriorityStr = ExtractOptionalString(Params, TEXT("priority"), TEXT("Normal"));

    // Step 2: Load.
    UStateTree* StateTree = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    TOptional<FMCPToolResult> LoadErr;
    if (!LoadStateTreeAndEditorData(AssetPath, StateTree, EditorData, LoadErr)) return LoadErr.GetValue();

    // Step 3: Find source state.
    UStateTreeState* Unused = nullptr;
    UStateTreeState* SourceState = FindStateByName(EditorData, SourceStateName, Unused, /*bWantParent*/false);
    if (!SourceState)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Source state '%s' not found"), *SourceStateName));
    }

    // Step 4: Build transition; target = special keyword OR a named state.
#if WITH_EDITORONLY_DATA
    FStateTreeTransition NewTransition;
    NewTransition.Trigger  = ParseTrigger(TriggerStr);
    NewTransition.Priority = ParsePriority(PriorityStr);

    if (TargetStateName.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
    {
        NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::Succeeded);
    }
    else if (TargetStateName.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
    {
        NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::Failed);
    }
    else if (TargetStateName.Equals(TEXT("Next"), ESearchCase::IgnoreCase))
    {
        NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::NextSelectableState);
    }
    else
    {
        UStateTreeState* TargetState = FindStateByName(EditorData, TargetStateName, Unused, /*bWantParent*/false);
        if (!TargetState)
        {
            return FMCPToolResult::Error(FString::Printf(
                TEXT("Target state '%s' not found. Use 'Succeeded' / 'Failed' / 'Next' or a valid state name."),
                *TargetStateName));
        }
        NewTransition.State.ID       = TargetState->ID;
        NewTransition.State.LinkType = EStateTreeTransitionType::GotoState;
        NewTransition.State.Name     = TargetState->Name;
    }

    // Step 5: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(FString::Printf(
        TEXT("MCP: Add StateTree Transition: %s -> %s"), *SourceStateName, *TargetStateName)));
    StateTree->Modify();
    EditorData->Modify();
    SourceState->Modify();

    SourceState->Transitions.Add(NewTransition);
    StateTree->MarkPackageDirty();
#else
    return FMCPToolResult::Error(TEXT("Transition authoring requires editor data; not available in this build"));
#endif

    // Step 6: Build response.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"),   AssetPath);
    Data->SetStringField(TEXT("source_state"), SourceStateName);
    Data->SetStringField(TEXT("target_state"), TargetStateName);
    Data->SetStringField(TEXT("trigger"),      TriggerStr);
    Data->SetStringField(TEXT("priority"),     PriorityStr);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Transition added: %s -> %s (trigger: %s)"),
            *SourceStateName, *TargetStateName, *TriggerStr), Data);
}

FMCPToolResult FMCPTool_StateTreeModify::ExecuteRemoveState(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse params.
    FString AssetPath, StateName;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("state_name"), StateName, ParamError)) return ParamError.GetValue();

    // Step 2: Confirm gate.
    if (!ExtractOptionalBool(Params, TEXT("confirm_delete"), false))
    {
        return FMCPToolResult::Error(
            TEXT("remove_state requires confirm_delete=true (destructive: removes children too)"));
    }

    // Step 3: Load.
    UStateTree* StateTree = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    TOptional<FMCPToolResult> LoadErr;
    if (!LoadStateTreeAndEditorData(AssetPath, StateTree, EditorData, LoadErr)) return LoadErr.GetValue();

    // Step 4: Find state + parent. Parent==nullptr means it's a root SubTree.
    UStateTreeState* ParentState = nullptr;
    UStateTreeState* StateToRemove = FindStateByName(EditorData, StateName, ParentState, /*bWantParent*/true);
    if (!StateToRemove)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("State '%s' not found in StateTree"), *StateName));
    }

    const int32 ChildCount = StateToRemove->Children.Num();

    // Step 5: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(
        FString::Printf(TEXT("MCP: Remove StateTree State '%s'"), *StateName)));
    StateTree->Modify();
    EditorData->Modify();
    if (ParentState) ParentState->Modify();

    if (ParentState)
    {
        ParentState->Children.Remove(StateToRemove);
    }
    else
    {
        // Root state: remove from SubTrees.
        EditorData->SubTrees.Remove(StateToRemove);
    }
    StateTree->MarkPackageDirty();

    // Step 6: Build response.
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("state_name"), StateName);
    Data->SetNumberField(TEXT("children_removed"), ChildCount);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("State '%s' removed (along with %d children)"), *StateName, ChildCount),
        Data);
}
