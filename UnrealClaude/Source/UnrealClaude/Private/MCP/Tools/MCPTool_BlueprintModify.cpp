// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlueprintModify.h"
#include "BlueprintUtils.h"
#include "MCP/MCPParamValidator.h"
#include "MCP/MCPBlueprintLoadContext.h"
#include "MCP/Sessions/UMGSessionSubsystem.h"
#include "UnrealClaudeModule.h"
#include "Engine/Blueprint.h"

// Operation name constants
namespace BlueprintModifyOps
{
	static const FString Create = TEXT("create");
	static const FString AddVariable = TEXT("add_variable");
	static const FString RemoveVariable = TEXT("remove_variable");
	static const FString AddFunction = TEXT("add_function");
	static const FString AddCustomEvent = TEXT("add_custom_event");
	static const FString RemoveFunction = TEXT("remove_function");
	static const FString AddNode = TEXT("add_node");
	static const FString AddNodes = TEXT("add_nodes");
	static const FString DeleteNode = TEXT("delete_node");
	static const FString ConnectPins = TEXT("connect_pins");
	static const FString DisconnectPins = TEXT("disconnect_pins");
	static const FString SetPinValue = TEXT("set_pin_value");
	static const FString ExportFunction = TEXT("export_function");
	static const FString ImportNodes = TEXT("import_nodes");
	static const FString ReplaceFunctionBody = TEXT("replace_function_body");
	static const FString BatchModify = TEXT("batch_modify");
}

FMCPToolResult FMCPTool_BlueprintModify::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Get operation type
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Level 2: Variable/Function Operations
	if (Operation == BlueprintModifyOps::Create)
	{
		return ExecuteCreate(Params);
	}
	if (Operation == BlueprintModifyOps::AddVariable)
	{
		return ExecuteAddVariable(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveVariable)
	{
		return ExecuteRemoveVariable(Params);
	}
	if (Operation == BlueprintModifyOps::AddFunction)
	{
		return ExecuteAddFunction(Params);
	}
	if (Operation == BlueprintModifyOps::AddCustomEvent)
	{
		return ExecuteAddCustomEvent(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveFunction)
	{
		return ExecuteRemoveFunction(Params);
	}
	// Level 3: Node Operations
	if (Operation == BlueprintModifyOps::AddNode)
	{
		return ExecuteAddNode(Params);
	}
	if (Operation == BlueprintModifyOps::AddNodes)
	{
		return ExecuteAddNodes(Params);
	}
	if (Operation == BlueprintModifyOps::DeleteNode)
	{
		return ExecuteDeleteNode(Params);
	}
	// Level 4: Connection Operations
	if (Operation == BlueprintModifyOps::ConnectPins)
	{
		return ExecuteConnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::DisconnectPins)
	{
		return ExecuteDisconnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::SetPinValue)
	{
		return ExecuteSetPinValue(Params);
	}
	// Level 5: Graph I/O Operations
	if (Operation == BlueprintModifyOps::ExportFunction)
	{
		return ExecuteExportFunction(Params);
	}
	if (Operation == BlueprintModifyOps::ImportNodes)
	{
		return ExecuteImportNodes(Params);
	}
	if (Operation == BlueprintModifyOps::ReplaceFunctionBody)
	{
		return ExecuteReplaceFunctionBody(Params);
	}
	// Level 6: Batch Operations
	if (Operation == BlueprintModifyOps::BatchModify)
	{
		return ExecuteBatchModify(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: create, add_variable, remove_variable, add_function, add_custom_event, remove_function, add_node, add_nodes, delete_node, connect_pins, disconnect_pins, set_pin_value, export_function, import_nodes, replace_function_body, batch_modify"),
		*Operation));
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteCreate(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString PackagePath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintName;
	if (!ExtractRequiredString(Params, TEXT("blueprint_name"), BlueprintName, Error))
	{
		return Error.GetValue();
	}

	FString ParentClassName;
	if (!ExtractRequiredString(Params, TEXT("parent_class"), ParentClassName, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintTypeStr = ExtractOptionalString(Params, TEXT("blueprint_type"), TEXT("Normal"));

	// Validate package path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(PackagePath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Validate Blueprint name
	if (!FMCPParamValidator::ValidateBlueprintVariableName(BlueprintName, ValidationError))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid Blueprint name: %s"), *ValidationError));
	}

	// Find parent class
	FString ClassError;
	UClass* ParentClass = FBlueprintUtils::FindParentClass(ParentClassName, ClassError);
	if (!ParentClass)
	{
		return FMCPToolResult::Error(ClassError);
	}

	// Parse Blueprint type
	EBlueprintType BlueprintType = ParseBlueprintType(BlueprintTypeStr);

	// Create the Blueprint
	FString CreateError;
	UBlueprint* NewBlueprint = FBlueprintUtils::CreateBlueprint(
		PackagePath,
		BlueprintName,
		ParentClass,
		BlueprintType,
		CreateError
	);

	if (!NewBlueprint)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), NewBlueprint->GetName());
	ResultData->SetStringField(TEXT("blueprint_path"), NewBlueprint->GetPathName());
	ResultData->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	ResultData->SetStringField(TEXT("blueprint_type"), FBlueprintUtils::GetBlueprintTypeString(BlueprintType));
	ResultData->SetBoolField(TEXT("compiled"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created Blueprint: %s"), *NewBlueprint->GetPathName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	FString VariableType;
	if (!ExtractRequiredString(Params, TEXT("variable_type"), VariableType, Error))
	{
		return Error.GetValue();
	}

	// Validate variable name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintVariableName(VariableName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Parse variable type
	FEdGraphPinType PinType;
	FString TypeError;
	if (!FBlueprintUtils::ParsePinType(VariableType, PinType, TypeError))
	{
		return FMCPToolResult::Error(TypeError);
	}

	// Add the variable
	FString AddError;
	if (!FBlueprintUtils::AddVariable(Context.Blueprint, VariableName, PinType, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added variable '%s' (%s) to Blueprint"), *VariableName, *VariableType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the variable
	FString RemoveError;
	if (!FBlueprintUtils::RemoveVariable(Context.Blueprint, VariableName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed variable '%s' from Blueprint"), *VariableName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Validate function name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintFunctionName(FunctionName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Add the function
	FString AddError;
	if (!FBlueprintUtils::AddFunction(Context.Blueprint, FunctionName, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added function '%s' to Blueprint"), *FunctionName),
		ResultData
	);
}

// Add a UK2Node_CustomEvent to the Blueprint's Event Graph.
//
// This is a logic-light alternative to ExecuteAddFunction: instead of allocating a
// full FunctionGraph (which on certain UE versions can crash for WidgetBlueprints
// inside FBlueprintEditorUtils::AddFunctionGraph), it inserts a custom event node
// directly into UbergraphPages[0]. Suitable for UI button handlers and other
// fire-and-forget entry points that do not need parameter pins, return nodes, or
// recursion. Mirrors the parameter shape of ExecuteAddFunction (re-uses
// `function_name` so callers do not have to learn a second key).
//
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP
FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddCustomEvent(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Extract event name (we re-use `function_name` so the param surface
	// is identical to ExecuteAddFunction; callers can swap ops without re-keying).
	TOptional<FMCPToolResult> Error;
	FString EventName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), EventName, Error))
	{
		return Error.GetValue();
	}

	// Step 2: Validate identifier (same rules as a function name — alpha/_, alnum/_).
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintFunctionName(EventName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Step 3: Load + lock the Blueprint via the standard RAII context.
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Step 4: Insert the CustomEvent node (helper rejects duplicates / null graph).
	FString AddError;
	if (!FBlueprintUtils::AddCustomEvent(Context.Blueprint, EventName, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Step 5: Compile + mark dirty in one pass.
	if (auto CompileError = Context.CompileAndFinalize(TEXT("CustomEvent added")))
	{
		return CompileError.GetValue();
	}

	// Step 6: Build response payload — `event_name` is the canonical key for this
	// op; `function_name` is mirrored so legacy clients reading add_function results
	// continue to work without branching on op type.
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("function_name"), EventName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added CustomEvent '%s' to Blueprint Event Graph"), *EventName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the function
	FString RemoveError;
	if (!FBlueprintUtils::RemoveFunction(Context.Blueprint, FunctionName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed function '%s' from Blueprint"), *FunctionName),
		ResultData
	);
}

EBlueprintType FMCPTool_BlueprintModify::ParseBlueprintType(const FString& TypeString)
{
	FString LowerType = TypeString.ToLower();

	if (LowerType == TEXT("normal") || LowerType == TEXT("actor") || LowerType == TEXT("object"))
	{
		return BPTYPE_Normal;
	}
	if (LowerType == TEXT("functionlibrary") || LowerType == TEXT("function_library"))
	{
		return BPTYPE_FunctionLibrary;
	}
	if (LowerType == TEXT("interface"))
	{
		return BPTYPE_Interface;
	}
	if (LowerType == TEXT("macrolibrary") || LowerType == TEXT("macro_library") || LowerType == TEXT("macro"))
	{
		return BPTYPE_MacroLibrary;
	}

	// Default to normal
	return BPTYPE_Normal;
}

// ===== Level 3: Node Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeType;
	if (!ExtractRequiredString(Params, TEXT("node_type"), NodeType, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Step: detect whether the caller explicitly supplied pos_x / pos_y. Both
	// are typed Number fields in the schema; HasTypedField separates "user passed
	// 0 deliberately" from "user omitted the key" — without this distinction we
	// would never reach the cursor branch since ExtractOptionalNumber defaults
	// the missing case to 0.
	const bool bHasExplicitX = Params->HasTypedField<EJson::Number>(TEXT("pos_x"));
	const bool bHasExplicitY = Params->HasTypedField<EJson::Number>(TEXT("pos_y"));
	const bool bUseCursor = !bHasExplicitX && !bHasExplicitY;

	int32 PosX = (int32)ExtractOptionalNumber(Params, TEXT("pos_x"), 0);
	int32 PosY = (int32)ExtractOptionalNumber(Params, TEXT("pos_y"), 0);

	// Get node params object
	TSharedPtr<FJsonObject> NodeParams;
	const TSharedPtr<FJsonObject>* NodeParamsPtr;
	if (Params->TryGetObjectField(TEXT("node_params"), NodeParamsPtr))
	{
		NodeParams = *NodeParamsPtr;
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Cursor-driven auto-layout. When the caller omits BOTH pos_x and pos_y, the
	// node lands at the session cursor's current slot and the cursor advances
	// horizontally so a sequence of position-less add_node calls produces a
	// left-to-right ribbon. A graph switch implicitly clears the cursor inside
	// SetTargetGraph, so the first node in a new graph starts at origin.
	//
	// Cursor design portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
	UUMGSessionSubsystem* Session = UUMGSessionSubsystem::Get();
	if (Session)
	{
		// Step: anchor the active graph (clears cursor if graph changed).
		Session->SetTargetGraph(GraphName, bFunctionGraph);

		// Step: only consume cursor when caller did not supply explicit coords.
		if (bUseCursor)
		{
			const FVector2D CursorPos = Session->GetAndAdvanceCursorPosition();
			PosX = (int32)CursorPos.X;
			PosY = (int32)CursorPos.Y;
		}
	}

	// Create the node
	FString NodeId;
	FString CreateError;
	UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
	if (!NewNode)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Step: program-counter update — record this node id as the cursor anchor so
	// follow-up tools (e.g. a future "connect to last" helper) can chain off it
	// without the caller threading the id back through every params payload.
	if (Session)
	{
		Session->SetCursorNode(NodeId);
	}

	// Apply pin default values if provided
	if (NodeParams.IsValid())
	{
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if (NodeParams->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = FBlueprintUtils::SerializeNodeInfo(NewNode);
	ResultData->SetStringField(TEXT("blueprint_path"), Context.Blueprint->GetPathName());
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	// Echo the resolved position so callers can see whether the cursor was used
	// (helps debug "all nodes stacked" / "nodes not at expected coords" reports).
	ResultData->SetNumberField(TEXT("pos_x"), PosX);
	ResultData->SetNumberField(TEXT("pos_y"), PosY);
	ResultData->SetBoolField(TEXT("used_cursor"), bUseCursor);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created node '%s' (type: %s)"), *NodeId, *NodeType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNodes(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Get nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		return FMCPToolResult::Error(TEXT("'nodes' array is required"));
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Create all nodes using helper
	TArray<FString> CreatedNodeIds;
	TArray<TSharedPtr<FJsonValue>> CreatedNodes;
	FString CreateError;
	if (!CreateNodesFromSpec(Graph, *NodesArray, CreatedNodeIds, CreatedNodes, CreateError))
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Process connections using helper
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (Params->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		ProcessNodeConnections(Graph, *ConnectionsArray, CreatedNodeIds);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Nodes created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetArrayField(TEXT("nodes"), CreatedNodes);
	ResultData->SetNumberField(TEXT("node_count"), CreatedNodeIds.Num());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created %d nodes"), CreatedNodeIds.Num()),
		ResultData
	);
}

bool FMCPTool_BlueprintModify::CreateNodesFromSpec(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& NodesArray,
	TArray<FString>& OutCreatedNodeIds,
	TArray<TSharedPtr<FJsonValue>>& OutCreatedNodes,
	FString& OutError)
{
	for (int32 i = 0; i < NodesArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* NodeSpec;
		if (!NodesArray[i]->TryGetObject(NodeSpec))
		{
			OutError = FString::Printf(TEXT("Node at index %d is not a valid object"), i);
			return false;
		}

		FString NodeType = (*NodeSpec)->GetStringField(TEXT("type"));
		if (NodeType.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Node at index %d missing 'type' field"), i);
			return false;
		}

		int32 PosX = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_x"));
		int32 PosY = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_y"));

		// Get params (could be inline or nested)
		TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			NodeParams = *ParamsPtr;
		}
		else
		{
			// Copy common fields to params
			if ((*NodeSpec)->HasField(TEXT("function")))
				NodeParams->SetStringField(TEXT("function"), (*NodeSpec)->GetStringField(TEXT("function")));
			if ((*NodeSpec)->HasField(TEXT("target_class")))
				NodeParams->SetStringField(TEXT("target_class"), (*NodeSpec)->GetStringField(TEXT("target_class")));
			if ((*NodeSpec)->HasField(TEXT("event")))
				NodeParams->SetStringField(TEXT("event"), (*NodeSpec)->GetStringField(TEXT("event")));
			if ((*NodeSpec)->HasField(TEXT("variable")))
				NodeParams->SetStringField(TEXT("variable"), (*NodeSpec)->GetStringField(TEXT("variable")));
			if ((*NodeSpec)->HasField(TEXT("num_outputs")))
				NodeParams->SetNumberField(TEXT("num_outputs"), (*NodeSpec)->GetNumberField(TEXT("num_outputs")));
		}

		// Create node
		FString NodeId;
		FString CreateError;
		UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
		if (!NewNode)
		{
			OutError = FString::Printf(TEXT("Failed to create node %d: %s"), i, *CreateError);
			return false;
		}

		OutCreatedNodeIds.Add(NodeId);

		// Apply pin default values if provided
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}

		// Add to result
		TSharedPtr<FJsonObject> NodeInfo = FBlueprintUtils::SerializeNodeInfo(NewNode);
		NodeInfo->SetNumberField(TEXT("index"), i);
		OutCreatedNodes.Add(MakeShared<FJsonValueObject>(NodeInfo));
	}

	return true;
}

void FMCPTool_BlueprintModify::ProcessNodeConnections(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& ConnectionsArray,
	const TArray<FString>& CreatedNodeIds)
{
	for (int32 i = 0; i < ConnectionsArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* ConnSpec;
		if (!ConnectionsArray[i]->TryGetObject(ConnSpec))
		{
			continue;
		}

		// Get source - can be index or node_id
		FString SourceNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("from_node")))
		{
			int32 FromIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("from_node"));
			if (FromIndex >= 0 && FromIndex < CreatedNodeIds.Num())
			{
				SourceNodeId = CreatedNodeIds[FromIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("from_node")))
		{
			SourceNodeId = (*ConnSpec)->GetStringField(TEXT("from_node"));
		}

		// Get target - can be index or node_id
		FString TargetNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("to_node")))
		{
			int32 ToIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("to_node"));
			if (ToIndex >= 0 && ToIndex < CreatedNodeIds.Num())
			{
				TargetNodeId = CreatedNodeIds[ToIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("to_node")))
		{
			TargetNodeId = (*ConnSpec)->GetStringField(TEXT("to_node"));
		}

		FString SourcePin = (*ConnSpec)->GetStringField(TEXT("from_pin"));
		FString TargetPin = (*ConnSpec)->GetStringField(TEXT("to_pin"));

		if (!SourceNodeId.IsEmpty() && !TargetNodeId.IsEmpty())
		{
			FString ConnectError;
			FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError);
		}
	}
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Delete the node
	FString DeleteError;
	if (!FBlueprintUtils::DeleteNode(Graph, NodeId, DeleteError))
	{
		return FMCPToolResult::Error(DeleteError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node deleted")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted node '%s'"), *NodeId),
		ResultData
	);
}

// ===== Level 4: Connection Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteConnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString SourcePin = ExtractOptionalString(Params, TEXT("source_pin"), TEXT(""));
	FString TargetPin = ExtractOptionalString(Params, TEXT("target_pin"), TEXT(""));
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Connect the pins
	FString ConnectError;
	if (!FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError))
	{
		return FMCPToolResult::Error(ConnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins connected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), SourcePin.IsEmpty() ? TEXT("(auto exec)") : SourcePin);
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), TargetPin.IsEmpty() ? TEXT("(auto exec)") : TargetPin);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connected '%s' -> '%s'"), *SourceNodeId, *TargetNodeId),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDisconnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString SourcePin;
	if (!ExtractRequiredString(Params, TEXT("source_pin"), SourcePin, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetPin;
	if (!ExtractRequiredString(Params, TEXT("target_pin"), TargetPin, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Disconnect the pins
	FString DisconnectError;
	if (!FBlueprintUtils::DisconnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, DisconnectError))
	{
		return FMCPToolResult::Error(DisconnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins disconnected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), SourcePin);
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), TargetPin);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Disconnected '%s.%s' from '%s.%s'"), *SourceNodeId, *SourcePin, *TargetNodeId, *TargetPin),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteSetPinValue(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString PinName;
	if (!ExtractRequiredString(Params, TEXT("pin_name"), PinName, Error))
	{
		return Error.GetValue();
	}

	FString PinValue;
	if (!ExtractRequiredString(Params, TEXT("pin_value"), PinValue, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Set the pin value
	FString SetError;
	if (!FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinName, PinValue, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pin value set")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetStringField(TEXT("pin_value"), PinValue);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s.%s' = '%s'"), *NodeId, *PinName, *PinValue),
		ResultData
	);
}

// ===== Level 5: Graph I/O Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteExportFunction(const TSharedRef<FJsonObject>& Params)
{
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), true);

	// Load Blueprint (query mode - no editability check needed for export)
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadForQuery(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Export to clipboard text
	FString ExportedText;
	FString ExportError;
	if (!FBlueprintGraphEditor::ExportGraphToText(Graph, ExportedText, ExportError))
	{
		return FMCPToolResult::Error(ExportError);
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_path"), Context.Blueprint->GetPathName());
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetStringField(TEXT("clipboard_text"), ExportedText);
	ResultData->SetNumberField(TEXT("text_length"), ExportedText.Len());
	ResultData->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Exported function '%s' (%d nodes, %d chars)"),
			*Graph->GetName(), Graph->Nodes.Num(), ExportedText.Len()),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteImportNodes(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;
	FString ClipboardText;
	if (!ExtractRequiredString(Params, TEXT("clipboard_text"), ClipboardText, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), true);
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Import nodes from clipboard text
	TArray<UEdGraphNode*> ImportedNodes;
	FString ImportError;
	if (!FBlueprintGraphEditor::ImportGraphFromText(Graph, ClipboardText, ImportedNodes, ImportError))
	{
		return FMCPToolResult::Error(ImportError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Nodes imported")))
	{
		return CompileError.GetValue();
	}

	// Save if requested
	if (bSave)
	{
		FString SaveError;
		if (!FBlueprintGraphEditor::SaveBlueprint(Context.Blueprint, SaveError))
		{
			// Not fatal - compilation succeeded, save failed
			UE_LOG(LogUnrealClaude, Warning, TEXT("Blueprint compiled but save failed: %s"), *SaveError);
		}
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetNumberField(TEXT("imported_node_count"), ImportedNodes.Num());
	ResultData->SetBoolField(TEXT("saved"), bSave);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Imported %d nodes into '%s'"), ImportedNodes.Num(), *Graph->GetName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteReplaceFunctionBody(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;
	FString ClipboardText;
	if (!ExtractRequiredString(Params, TEXT("clipboard_text"), ClipboardText, Error))
	{
		return Error.GetValue();
	}

	FString GraphName;
	if (!ExtractRequiredString(Params, TEXT("graph_name"), GraphName, Error))
	{
		return Error.GetValue();
	}

	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), true);
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	int32 OriginalNodeCount = Graph->Nodes.Num();

	// Step 1: Clear all nodes (including Entry/Result) for clean roundtrip
	FString ClearError;
	if (!FBlueprintGraphEditor::ClearFunctionBody(Graph, ClearError, /*bPreserveEntryResult=*/ false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to clear function body: %s"), *ClearError));
	}

	int32 PreservedNodeCount = Graph->Nodes.Num();

	// Step 2: Import new nodes from clipboard text
	TArray<UEdGraphNode*> ImportedNodes;
	FString ImportError;
	if (!FBlueprintGraphEditor::ImportGraphFromText(Graph, ClipboardText, ImportedNodes, ImportError))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Body cleared but import failed: %s"), *ImportError));
	}

	// Step 3: Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function body replaced")))
	{
		return CompileError.GetValue();
	}

	// Step 4: Save if requested
	if (bSave)
	{
		FString SaveError;
		if (!FBlueprintGraphEditor::SaveBlueprint(Context.Blueprint, SaveError))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Blueprint compiled but save failed: %s"), *SaveError);
		}
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetNumberField(TEXT("original_node_count"), OriginalNodeCount);
	ResultData->SetNumberField(TEXT("preserved_node_count"), PreservedNodeCount);
	ResultData->SetNumberField(TEXT("imported_node_count"), ImportedNodes.Num());
	ResultData->SetNumberField(TEXT("final_node_count"), Graph->Nodes.Num());
	ResultData->SetBoolField(TEXT("saved"), bSave);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Replaced function '%s': %d nodes removed, %d imported"),
			*Graph->GetName(), OriginalNodeCount - PreservedNodeCount, ImportedNodes.Num()),
		ResultData
	);
}

// ===== Level 6: Batch Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteBatchModify(const TSharedRef<FJsonObject>& Params)
{
	// Validate that the steps array was provided
	const TArray<TSharedPtr<FJsonValue>>* StepsArray;
	if (!Params->TryGetArrayField(TEXT("steps"), StepsArray))
	{
		return FMCPToolResult::Error(TEXT("'steps' array is required for batch_modify"));
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), true);
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Track node IDs created in each step so later steps can reference them via "$N"
	TArray<FString> CreatedNodeIds;
	CreatedNodeIds.SetNum(StepsArray->Num());

	int32 SuccessCount = 0;
	TArray<TSharedPtr<FJsonValue>> StepResults;

	for (int32 i = 0; i < StepsArray->Num(); i++)
	{
		const TSharedPtr<FJsonObject>* StepObj;
		if (!(*StepsArray)[i]->TryGetObject(StepObj))
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("step"), i);
			ErrResult->SetStringField(TEXT("error"), TEXT("Invalid step object"));
			StepResults.Add(MakeShared<FJsonValueObject>(ErrResult));
			continue;
		}

		FString Op = (*StepObj)->GetStringField(TEXT("op")).ToLower();
		FString StepError;
		bool bStepSuccess = false;
		FString StepNodeId;

		if (Op == TEXT("disconnect"))
		{
			// Disconnect two pins; source/target node refs support "$N" step references
			FString SrcNode = ResolveStepRef((*StepObj)->GetStringField(TEXT("source_node")), CreatedNodeIds);
			FString SrcPin  = (*StepObj)->GetStringField(TEXT("source_pin"));
			FString TgtNode = ResolveStepRef((*StepObj)->GetStringField(TEXT("target_node")), CreatedNodeIds);
			FString TgtPin  = (*StepObj)->GetStringField(TEXT("target_pin"));
			bStepSuccess = FBlueprintGraphEditor::DisconnectPins(Graph, SrcNode, SrcPin, TgtNode, TgtPin, StepError);
		}
		else if (Op == TEXT("connect"))
		{
			// Connect two pins; source/target node refs support "$N" step references
			FString SrcNode = ResolveStepRef((*StepObj)->GetStringField(TEXT("source_node")), CreatedNodeIds);
			FString SrcPin  = (*StepObj)->GetStringField(TEXT("source_pin"));
			FString TgtNode = ResolveStepRef((*StepObj)->GetStringField(TEXT("target_node")), CreatedNodeIds);
			FString TgtPin  = (*StepObj)->GetStringField(TEXT("target_pin"));
			bStepSuccess = FBlueprintGraphEditor::ConnectPins(Graph, SrcNode, SrcPin, TgtNode, TgtPin, StepError);
		}
		else if (Op == TEXT("delete_node"))
		{
			// Delete a node by ID; supports "$N" step references
			FString NodeId = ResolveStepRef((*StepObj)->GetStringField(TEXT("node_id")), CreatedNodeIds);
			bStepSuccess = FBlueprintGraphEditor::DeleteNode(Graph, NodeId, StepError);
		}
		else if (Op == TEXT("add_node"))
		{
			// Create a node; records new node ID in CreatedNodeIds[i] for downstream "$i" references
			FString NodeType = (*StepObj)->GetStringField(TEXT("node_type"));
			int32 PosX = (int32)(*StepObj)->GetNumberField(TEXT("pos_x"));
			int32 PosY = (int32)(*StepObj)->GetNumberField(TEXT("pos_y"));

			TSharedPtr<FJsonObject> NodeParams;
			const TSharedPtr<FJsonObject>* ParamsPtr;
			if ((*StepObj)->TryGetObjectField(TEXT("params"), ParamsPtr))
			{
				NodeParams = *ParamsPtr;
			}

			FString NodeId;
			UEdGraphNode* NewNode = FBlueprintGraphEditor::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, StepError);
			if (NewNode)
			{
				bStepSuccess = true;
				StepNodeId = NodeId;
				CreatedNodeIds[i] = NodeId;

				// Apply inline pin_values if provided inside the params object
				if (NodeParams.IsValid())
				{
					const TSharedPtr<FJsonObject>* PinValuesPtr;
					if (NodeParams->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
					{
						for (const auto& PinValue : (*PinValuesPtr)->Values)
						{
							FString PinValueStr;
							if (PinValue.Value->TryGetString(PinValueStr))
							{
								FString PinError;
								FBlueprintGraphEditor::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
							}
						}
					}
				}
			}
		}
		else if (Op == TEXT("set_pin_value"))
		{
			// Set a pin's default value; node_id supports "$N" step references
			FString NodeId   = ResolveStepRef((*StepObj)->GetStringField(TEXT("node_id")), CreatedNodeIds);
			FString PinName  = (*StepObj)->GetStringField(TEXT("pin_name"));
			FString PinValue = (*StepObj)->GetStringField(TEXT("value"));
			bStepSuccess = FBlueprintGraphEditor::SetPinDefaultValue(Graph, NodeId, PinName, PinValue, StepError);
		}
		else
		{
			StepError = FString::Printf(
				TEXT("Unknown batch op: '%s'. Valid: disconnect, connect, delete_node, add_node, set_pin_value"),
				*Op);
		}

		// Record individual step outcome
		TSharedPtr<FJsonObject> StepResult = MakeShared<FJsonObject>();
		StepResult->SetNumberField(TEXT("step"), i);
		StepResult->SetStringField(TEXT("op"), Op);
		StepResult->SetBoolField(TEXT("success"), bStepSuccess);
		if (!StepNodeId.IsEmpty())
		{
			StepResult->SetStringField(TEXT("node_id"), StepNodeId);
		}
		if (!bStepSuccess && !StepError.IsEmpty())
		{
			StepResult->SetStringField(TEXT("error"), StepError);
		}
		StepResults.Add(MakeShared<FJsonValueObject>(StepResult));

		if (bStepSuccess)
		{
			SuccessCount++;
		}
	}

	// Compile Blueprint once after all steps complete
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Batch modify")))
	{
		return CompileError.GetValue();
	}

	// Save if requested (non-fatal on failure — compilation already succeeded)
	if (bSave)
	{
		FString SaveError;
		if (!FBlueprintGraphEditor::SaveBlueprint(Context.Blueprint, SaveError))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Batch compiled but save failed: %s"), *SaveError);
		}
	}

	// Build aggregate result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetNumberField(TEXT("total_steps"), StepsArray->Num());
	ResultData->SetNumberField(TEXT("success_count"), SuccessCount);
	ResultData->SetNumberField(TEXT("fail_count"), StepsArray->Num() - SuccessCount);
	ResultData->SetArrayField(TEXT("step_results"), StepResults);
	ResultData->SetBoolField(TEXT("saved"), bSave);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Batch modify: %d/%d steps succeeded"), SuccessCount, StepsArray->Num()),
		ResultData
	);
}

FString FMCPTool_BlueprintModify::ResolveStepRef(const FString& NodeRef, const TArray<FString>& CreatedNodeIds) const
{
	// Strings like "$0", "$1" are resolved to the node ID created by that step index.
	// If the index is out of range or that step created no node, the original string is returned unchanged.
	if (NodeRef.StartsWith(TEXT("$")))
	{
		FString IndexStr = NodeRef.RightChop(1);
		if (IndexStr.IsNumeric())
		{
			int32 Index = FCString::Atoi(*IndexStr);
			if (Index >= 0 && Index < CreatedNodeIds.Num() && !CreatedNodeIds[Index].IsEmpty())
			{
				return CreatedNodeIds[Index];
			}
		}
	}
	return NodeRef;
}
