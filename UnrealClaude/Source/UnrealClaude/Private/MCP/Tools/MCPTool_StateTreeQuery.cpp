// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_StateTreeQuery.h"
#include "UnrealClaudeModule.h"

#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeTypes.h"
#include "StateTreeIndexTypes.h"
#include "StructUtils/InstancedStructContainer.h"
#include "PropertyBag.h"

namespace
{
    // Strip a trailing /Game/.../X.X duplicate suffix when callers pass the asset name twice,
    // and tolerate the editor-style "/Game/Path/Asset" form which LoadObject also accepts.
    FString NormalizeAssetPath(const FString& In)
    {
        FString Path = In;
        Path.TrimStartAndEndInline();
        return Path;
    }
}

FMCPToolResult FMCPTool_StateTreeQuery::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Extract required asset_path.
    FString AssetPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, ParamError))
    {
        return ParamError.GetValue();
    }
    AssetPath = NormalizeAssetPath(AssetPath);

    // Step 2: Optional include + detailed.
    FString Include = ExtractOptionalString(Params, TEXT("include"), TEXT("all")).ToLower();
    bool bDetailed = ExtractOptionalBool(Params, TEXT("detailed"), true);

    UE_LOG(LogUnrealClaude, Log, TEXT("statetree_query: path='%s', include='%s'"),
        *AssetPath, *Include);

    // Step 3: Load StateTree asset.
    UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
    if (!StateTree)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load StateTree: %s"), *AssetPath));
    }

    // Step 4: Build response JSON.
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), StateTree->GetName());
    Result->SetStringField(TEXT("path"), AssetPath);
    if (StateTree->GetSchema())
    {
        Result->SetStringField(TEXT("schema"), StateTree->GetSchema()->GetName());
    }

    const bool bAll = (Include == TEXT("all"));
    if (bAll || Include == TEXT("states"))      Result->SetObjectField(TEXT("states"),       ExtractStates(StateTree, bDetailed));
    if (bAll || Include == TEXT("transitions")) Result->SetObjectField(TEXT("transitions"),  ExtractTransitions(StateTree));
    if (bAll || Include == TEXT("tasks"))       Result->SetObjectField(TEXT("tasks"),        ExtractTasks(StateTree));
    if (bAll || Include == TEXT("evaluators"))  Result->SetObjectField(TEXT("evaluators"),   ExtractEvaluators(StateTree));
    if (bAll || Include == TEXT("parameters"))  Result->SetObjectField(TEXT("parameters"),   ExtractParameters(StateTree));

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Inspected StateTree '%s'"), *StateTree->GetName()),
        Result);
}

TSharedPtr<FJsonObject> FMCPTool_StateTreeQuery::ExtractStates(UStateTree* StateTree, bool bDetailed) const
{
    TSharedPtr<FJsonObject> StatesObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> StateArray;

    TConstArrayView<FCompactStateTreeState> States = StateTree->GetStates();
    for (int32 i = 0; i < States.Num(); ++i)
    {
        const FCompactStateTreeState& State = States[i];

        TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
        StateObj->SetStringField(TEXT("name"), State.Name.ToString());
        StateObj->SetNumberField(TEXT("index"), i);
        StateObj->SetStringField(TEXT("type"), GetStateTypeString(static_cast<uint8>(State.Type)));

        if (bDetailed)
        {
            StateObj->SetStringField(TEXT("selection_behavior"),
                GetSelectionBehaviorString(static_cast<uint8>(State.SelectionBehavior)));
            StateObj->SetNumberField(TEXT("depth"), State.Depth);

            if (State.Parent.IsValid())
            {
                StateObj->SetNumberField(TEXT("parent_index"), State.Parent.Index);
            }

            if (State.HasChildren())
            {
                StateObj->SetNumberField(TEXT("children_begin"), State.ChildrenBegin);
                StateObj->SetNumberField(TEXT("children_end"), State.ChildrenEnd);
                StateObj->SetNumberField(TEXT("children_count"),
                    static_cast<int32>(State.ChildrenEnd) - static_cast<int32>(State.ChildrenBegin));
            }

            StateObj->SetNumberField(TEXT("tasks_begin"), State.TasksBegin);
            StateObj->SetNumberField(TEXT("transitions_begin"), State.TransitionsBegin);
            StateObj->SetBoolField(TEXT("enabled"), State.bEnabled);
        }

        StateArray.Add(MakeShared<FJsonValueObject>(StateObj));
    }

    StatesObj->SetArrayField(TEXT("items"), StateArray);
    StatesObj->SetNumberField(TEXT("count"), StateArray.Num());
    return StatesObj;
}

TSharedPtr<FJsonObject> FMCPTool_StateTreeQuery::ExtractTransitions(UStateTree* StateTree) const
{
    TSharedPtr<FJsonObject> TransitionsObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> TransitionArray;

    TConstArrayView<FCompactStateTreeState> States = StateTree->GetStates();
    int32 TransitionIndex = 0;

    for (const FCompactStateTreeState& State : States)
    {
        // Walk transitions starting from State.TransitionsBegin until GetTransitionFromIndex returns null.
        FStateTreeIndex16 TransIdx = FStateTreeIndex16(State.TransitionsBegin);
        while (const FCompactStateTransition* Transition = StateTree->GetTransitionFromIndex(TransIdx))
        {
            TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
            TransObj->SetNumberField(TEXT("index"), TransitionIndex++);
            TransObj->SetStringField(TEXT("state_name"), State.Name.ToString());

            FString TriggerStr;
            switch (Transition->Trigger)
            {
            case EStateTreeTransitionTrigger::OnStateCompleted: TriggerStr = TEXT("OnStateCompleted"); break;
            case EStateTreeTransitionTrigger::OnStateFailed:    TriggerStr = TEXT("OnStateFailed");    break;
            case EStateTreeTransitionTrigger::OnTick:           TriggerStr = TEXT("OnTick");           break;
            case EStateTreeTransitionTrigger::OnEvent:          TriggerStr = TEXT("OnEvent");          break;
            default:                                            TriggerStr = TEXT("Unknown");
            }
            TransObj->SetStringField(TEXT("trigger"), TriggerStr);

            if (Transition->State.IsValid())
            {
                TransObj->SetNumberField(TEXT("target_state_index"), Transition->State.Index);
            }

            TransObj->SetNumberField(TEXT("priority"), static_cast<int32>(Transition->Priority));
            if (Transition->ConditionsNum > 0)
            {
                TransObj->SetNumberField(TEXT("conditions_count"), Transition->ConditionsNum);
            }

            TransitionArray.Add(MakeShared<FJsonValueObject>(TransObj));

            // Step forward; break when out of range.
            TransIdx = FStateTreeIndex16(TransIdx.Get() + 1);
            if (!StateTree->GetTransitionFromIndex(TransIdx))
            {
                break;
            }
        }
    }

    TransitionsObj->SetArrayField(TEXT("items"), TransitionArray);
    TransitionsObj->SetNumberField(TEXT("count"), TransitionArray.Num());
    return TransitionsObj;
}

TSharedPtr<FJsonObject> FMCPTool_StateTreeQuery::ExtractTasks(UStateTree* StateTree) const
{
    TSharedPtr<FJsonObject> TasksObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> TaskArray;

    const FInstancedStructContainer& Nodes = StateTree->GetNodes();
    for (int32 i = 0; i < Nodes.Num(); ++i)
    {
        FConstStructView Node = Nodes[i];
        if (const FStateTreeTaskBase* Task = Node.GetPtr<const FStateTreeTaskBase>())
        {
            TSharedPtr<FJsonObject> TaskObj = MakeShared<FJsonObject>();
            TaskObj->SetNumberField(TEXT("index"), i);
            TaskObj->SetStringField(TEXT("name"), Task->Name.ToString());
            if (const UScriptStruct* Struct = Node.GetScriptStruct())
            {
                TaskObj->SetStringField(TEXT("type"), Struct->GetName());
            }
            TaskObj->SetBoolField(TEXT("enabled"), Task->bTaskEnabled);
            TaskArray.Add(MakeShared<FJsonValueObject>(TaskObj));
        }
    }

    TasksObj->SetArrayField(TEXT("items"), TaskArray);
    TasksObj->SetNumberField(TEXT("count"), TaskArray.Num());
    return TasksObj;
}

TSharedPtr<FJsonObject> FMCPTool_StateTreeQuery::ExtractEvaluators(UStateTree* StateTree) const
{
    TSharedPtr<FJsonObject> EvaluatorsObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> EvaluatorArray;

    const FInstancedStructContainer& Nodes = StateTree->GetNodes();
    for (int32 i = 0; i < Nodes.Num(); ++i)
    {
        FConstStructView Node = Nodes[i];
        if (const FStateTreeEvaluatorBase* Evaluator = Node.GetPtr<const FStateTreeEvaluatorBase>())
        {
            TSharedPtr<FJsonObject> EvalObj = MakeShared<FJsonObject>();
            EvalObj->SetNumberField(TEXT("index"), i);
            EvalObj->SetStringField(TEXT("name"), Evaluator->Name.ToString());
            if (const UScriptStruct* Struct = Node.GetScriptStruct())
            {
                EvalObj->SetStringField(TEXT("type"), Struct->GetName());
            }
            EvaluatorArray.Add(MakeShared<FJsonValueObject>(EvalObj));
        }
    }

    EvaluatorsObj->SetArrayField(TEXT("items"), EvaluatorArray);
    EvaluatorsObj->SetNumberField(TEXT("count"), EvaluatorArray.Num());
    return EvaluatorsObj;
}

TSharedPtr<FJsonObject> FMCPTool_StateTreeQuery::ExtractParameters(UStateTree* StateTree) const
{
    TSharedPtr<FJsonObject> ParamsObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> ParamArray;

    const FInstancedPropertyBag& DefaultParameters = StateTree->GetDefaultParameters();
    if (const UPropertyBag* PropertyBag = DefaultParameters.GetPropertyBagStruct())
    {
        for (const FPropertyBagPropertyDesc& Desc : PropertyBag->GetPropertyDescs())
        {
            TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
            ParamObj->SetStringField(TEXT("name"), Desc.Name.ToString());

            FString TypeStr;
            switch (Desc.ValueType)
            {
            case EPropertyBagPropertyType::Bool:   TypeStr = TEXT("bool");    break;
            case EPropertyBagPropertyType::Byte:   TypeStr = TEXT("byte");    break;
            case EPropertyBagPropertyType::Int32:  TypeStr = TEXT("int32");   break;
            case EPropertyBagPropertyType::Int64:  TypeStr = TEXT("int64");   break;
            case EPropertyBagPropertyType::Float:  TypeStr = TEXT("float");   break;
            case EPropertyBagPropertyType::Double: TypeStr = TEXT("double");  break;
            case EPropertyBagPropertyType::Name:   TypeStr = TEXT("FName");   break;
            case EPropertyBagPropertyType::String: TypeStr = TEXT("FString"); break;
            case EPropertyBagPropertyType::Text:   TypeStr = TEXT("FText");   break;
            case EPropertyBagPropertyType::Struct: TypeStr = TEXT("struct");  break;
            case EPropertyBagPropertyType::Object: TypeStr = TEXT("object");  break;
            case EPropertyBagPropertyType::Class:  TypeStr = TEXT("class");   break;
            case EPropertyBagPropertyType::Enum:   TypeStr = TEXT("enum");    break;
            default:                               TypeStr = TEXT("unknown");
            }
            ParamObj->SetStringField(TEXT("type"), TypeStr);
            ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
        }
    }

    ParamsObj->SetArrayField(TEXT("items"), ParamArray);
    ParamsObj->SetNumberField(TEXT("count"), ParamArray.Num());
    return ParamsObj;
}

FString FMCPTool_StateTreeQuery::GetStateTypeString(uint8 StateType)
{
    switch (static_cast<EStateTreeStateType>(StateType))
    {
    case EStateTreeStateType::State:       return TEXT("State");
    case EStateTreeStateType::Group:       return TEXT("Group");
    case EStateTreeStateType::Linked:      return TEXT("Linked");
    case EStateTreeStateType::LinkedAsset: return TEXT("LinkedAsset");
    case EStateTreeStateType::Subtree:     return TEXT("Subtree");
    default:                               return TEXT("Unknown");
    }
}

FString FMCPTool_StateTreeQuery::GetSelectionBehaviorString(uint8 SelectionBehavior)
{
    switch (static_cast<EStateTreeStateSelectionBehavior>(SelectionBehavior))
    {
    case EStateTreeStateSelectionBehavior::None:                     return TEXT("None");
    case EStateTreeStateSelectionBehavior::TryEnterState:            return TEXT("TryEnterState");
    case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder: return TEXT("TrySelectChildrenInOrder");
    case EStateTreeStateSelectionBehavior::TryFollowTransitions:     return TEXT("TryFollowTransitions");
    default:                                                         return TEXT("Unknown");
    }
}
