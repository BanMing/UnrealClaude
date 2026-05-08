// Copyright Natali Caggiano. All Rights Reserved.

#include "BlueprintGraphEditor.h"
#include "MCPScopedTransaction.h"
#include "UnrealClaudeModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/UObjectIterator.h"
#include "HAL/PlatformAtomics.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Knot.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_MakeArray.h"
#include "EdGraphUtilities.h"
#include "EditorAssetLibrary.h"

// Static member initialization
volatile int32 FBlueprintGraphEditor::NodeIdCounter = 0;
const FString FBlueprintGraphEditor::NodeIdPrefix = TEXT("MCP_ID:");

// ===== Graph Finding =====

UEdGraph* FBlueprintGraphEditor::FindGraph(
	UBlueprint* Blueprint,
	const FString& GraphName,
	bool bFunctionGraph,
	FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return nullptr;
	}

	// Get the appropriate graph array (UE 5.7 uses TObjectPtr)
	auto& Graphs = bFunctionGraph ? Blueprint->FunctionGraphs : Blueprint->UbergraphPages;

	// If no name specified, return the first graph (default)
	if (GraphName.IsEmpty())
	{
		if (Graphs.Num() > 0 && Graphs[0])
		{
			return Graphs[0];
		}
		OutError = bFunctionGraph ? TEXT("No function graphs found") : TEXT("No event graphs found");
		return nullptr;
	}

	// Search by name
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	// Build list of available graphs for error message
	TArray<FString> AvailableGraphs;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph)
		{
			AvailableGraphs.Add(Graph->GetName());
		}
	}

	OutError = FString::Printf(TEXT("Graph '%s' not found. Available: %s"),
		*GraphName,
		*FString::Join(AvailableGraphs, TEXT(", ")));
	return nullptr;
}

// ===== Node Management =====

UEdGraphNode* FBlueprintGraphEditor::CreateNode(
	UEdGraph* Graph,
	const FString& NodeType,
	const TSharedPtr<FJsonObject>& NodeParams,
	int32 PosX,
	int32 PosY,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return nullptr;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		OutError = TEXT("Could not find Blueprint for graph");
		return nullptr;
	}

	// Open an undo transaction so the entire node-creation is reversible via Ctrl+Z.
	// The transaction commits automatically when Tx goes out of scope (RAII).
	TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(
		NSLOCTEXT("UnrealClaudeMCP", "CreateNode", "MCP: Create blueprint node"));

	// Mark the graph as modified so the transaction system records its pre-state.
	Graph->Modify();

	UEdGraphNode* NewNode = nullptr;
	FString Context;

	// Dispatch to appropriate creation function
	if (NodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
	{
		FString FunctionName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("function")) : TEXT("");
		FString TargetClass = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("target_class")) : TEXT("");
		Context = FunctionName;
		NewNode = CreateCallFunctionNode(Graph, FunctionName, TargetClass, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("Branch"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("IfThenElse"), ESearchCase::IgnoreCase))
	{
		NewNode = CreateBranchNode(Graph, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
	{
		FString EventName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("event")) : TEXT("");
		Context = EventName;
		NewNode = CreateEventNode(Graph, EventName, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase))
	{
		FString VariableName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("variable")) : TEXT("");
		Context = VariableName;
		NewNode = CreateVariableGetNode(Graph, Blueprint, VariableName, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase))
	{
		FString VariableName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("variable")) : TEXT("");
		Context = VariableName;
		NewNode = CreateVariableSetNode(Graph, Blueprint, VariableName, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
	{
		int32 NumOutputs = NodeParams.IsValid() ? (int32)NodeParams->GetNumberField(TEXT("num_outputs")) : 2;
		if (NumOutputs < 2) NumOutputs = 2;
		NewNode = CreateSequenceNode(Graph, NumOutputs, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("Add"), ESearchCase::IgnoreCase) ||
			 NodeType.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase) ||
			 NodeType.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase) ||
			 NodeType.Equals(TEXT("Divide"), ESearchCase::IgnoreCase))
	{
		Context = NodeType;
		NewNode = CreateMathNode(Graph, NodeType, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("PrintString"), ESearchCase::IgnoreCase))
	{
		// Convenience alias for CallFunction with PrintString
		Context = TEXT("PrintString");
		NewNode = CreateCallFunctionNode(Graph, TEXT("PrintString"), TEXT("KismetSystemLibrary"), PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("DynamicCast"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Cast"), ESearchCase::IgnoreCase))
	{
		FString ClassName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("target_class")) : TEXT("");
		Context = ClassName;
		NewNode = CreateDynamicCastNode(Graph, ClassName, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("Knot"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Reroute"), ESearchCase::IgnoreCase))
	{
		NewNode = CreateKnotNode(Graph, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("SwitchEnum"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Switch"), ESearchCase::IgnoreCase))
	{
		FString EnumName = NodeParams.IsValid() ? NodeParams->GetStringField(TEXT("enum")) : TEXT("");
		Context = EnumName;
		NewNode = CreateSwitchEnumNode(Graph, EnumName, PosX, PosY, OutError);
	}
	else if (NodeType.Equals(TEXT("MakeArray"), ESearchCase::IgnoreCase))
	{
		FString ElementType = (NodeParams.IsValid() && NodeParams->HasField(TEXT("element_type")))
			? NodeParams->GetStringField(TEXT("element_type"))
			: TEXT("");
		NewNode = CreateMakeArrayNode(Graph, ElementType, PosX, PosY, OutError);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown node type: '%s'. Supported: CallFunction, Branch, Event, VariableGet, VariableSet, Sequence, Add, Subtract, Multiply, Divide, PrintString, DynamicCast, Knot, SwitchEnum, MakeArray"), *NodeType);
		return nullptr;
	}

	if (NewNode)
	{
		// Record the node's pre-creation state in the transaction so individual
		// node changes are captured in addition to the graph-level Modify above.
		NewNode->Modify();

		// Generate and set node ID
		OutNodeId = GenerateNodeId(NodeType, Context, Graph);
		SetNodeId(NewNode, OutNodeId);

		// Mark blueprint as modified
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		UE_LOG(LogUnrealClaude, Log, TEXT("Created node '%s' (type: %s) at (%d, %d)"), *OutNodeId, *NodeType, PosX, PosY);
	}

	return NewNode;
}

bool FBlueprintGraphEditor::DeleteNode(UEdGraph* Graph, const FString& NodeId, FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	UEdGraphNode* Node = FindNodeById(Graph, NodeId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found"), *NodeId);
		return false;
	}

	// Open undo transaction before any destructive change.
	TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(
		NSLOCTEXT("UnrealClaudeMCP", "DeleteNode", "MCP: Delete blueprint node"));

	// Record pre-deletion state of both the graph and the node.
	Graph->Modify();
	Node->Modify();

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);

	// Break all connections first
	Node->BreakAllNodeLinks();

	// Remove from graph
	Graph->RemoveNode(Node);

	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Deleted node '%s'"), *NodeId);
	return true;
}

UEdGraphNode* FBlueprintGraphEditor::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph || NodeId.IsEmpty())
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && GetNodeId(Node) == NodeId)
		{
			return Node;
		}
	}

	// Fallback: match by UObject name (e.g., "K2Node_Event_0") for pre-existing nodes without MCP IDs.
	// This allows connect_pins/disconnect_pins to reference original Blueprint nodes
	// that were not created by MCP (e.g., Event Tick, Event BeginPlay).
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->GetName() == NodeId)
		{
			return Node;
		}
	}

	return nullptr;
}

// ===== Pin & Connection Management =====

bool FBlueprintGraphEditor::ConnectPins(
	UEdGraph* Graph,
	const FString& SourceNodeId,
	const FString& SourcePinName,
	const FString& TargetNodeId,
	const FString& TargetPinName,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	// Find source node by MCP-generated ID
	UEdGraphNode* SourceNode = FindNodeById(Graph, SourceNodeId);
	if (!SourceNode)
	{
		OutError = FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId);
		return false;
	}

	// Find target node
	UEdGraphNode* TargetNode = FindNodeById(Graph, TargetNodeId);
	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId);
		return false;
	}

	// Open undo transaction after node validation so aborted calls don't pollute
	// the undo stack, but before any graph state is mutated.
	TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(
		NSLOCTEXT("UnrealClaudeMCP", "ConnectPins", "MCP: Connect blueprint pins"));

	// Record pre-connection state of both involved nodes and the graph.
	Graph->Modify();
	SourceNode->Modify();
	TargetNode->Modify();

	// Find pins - auto-detect exec pins if names are empty
	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;

	if (SourcePinName.IsEmpty())
	{
		// Auto-select first exec output
		SourcePin = GetExecPin(SourceNode, true);
		if (!SourcePin)
		{
			OutError = FString::Printf(TEXT("No exec output pin found on node '%s'"), *SourceNodeId);
			return false;
		}
	}
	else
	{
		SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
		if (!SourcePin)
		{
			// Try input direction for bidirectional data pins
			SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Input);
		}
		if (!SourcePin)
		{
			OutError = FString::Printf(TEXT("Pin '%s' not found on source node '%s'"), *SourcePinName, *SourceNodeId);
			return false;
		}
	}

	if (TargetPinName.IsEmpty())
	{
		// Auto-select first exec input
		TargetPin = GetExecPin(TargetNode, false);
		if (!TargetPin)
		{
			OutError = FString::Printf(TEXT("No exec input pin found on node '%s'"), *TargetNodeId);
			return false;
		}
	}
	else
	{
		TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
		if (!TargetPin)
		{
			// Try output direction for bidirectional data pins
			TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Output);
		}
		if (!TargetPin)
		{
			OutError = FString::Printf(TEXT("Pin '%s' not found on target node '%s'"), *TargetPinName, *TargetNodeId);
			return false;
		}
	}

	// Use TryCreateConnection instead of raw MakeLinkTo.
	// TryCreateConnection handles CONNECT_RESPONSE_BREAK_OTHERS automatically,
	// which is required for exec pins (only one outgoing exec link allowed).
	// Without this, connecting a new exec target would leave stale old links.
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		if (!Schema->TryCreateConnection(SourcePin, TargetPin))
		{
			OutError = FString::Printf(TEXT("Cannot connect pins: schema rejected the connection"));
			return false;
		}
	}
	else
	{
		SourcePin->MakeLinkTo(TargetPin);
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Connected '%s.%s' -> '%s.%s'"),
		*SourceNodeId, *SourcePin->PinName.ToString(),
		*TargetNodeId, *TargetPin->PinName.ToString());

	return true;
}

bool FBlueprintGraphEditor::DisconnectPins(
	UEdGraph* Graph,
	const FString& SourceNodeId,
	const FString& SourcePinName,
	const FString& TargetNodeId,
	const FString& TargetPinName,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	// Find nodes
	UEdGraphNode* SourceNode = FindNodeById(Graph, SourceNodeId);
	if (!SourceNode)
	{
		OutError = FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId);
		return false;
	}

	UEdGraphNode* TargetNode = FindNodeById(Graph, TargetNodeId);
	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId);
		return false;
	}

	// Find pins
	UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_MAX);
	if (!SourcePin)
	{
		OutError = FString::Printf(TEXT("Pin '%s' not found on source node '%s'"), *SourcePinName, *SourceNodeId);
		return false;
	}

	UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_MAX);
	if (!TargetPin)
	{
		OutError = FString::Printf(TEXT("Pin '%s' not found on target node '%s'"), *TargetPinName, *TargetNodeId);
		return false;
	}

	// Open undo transaction after lookups succeed but before any mutation, so a
	// failed validation does not leave an empty entry on the undo stack.
	TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(
		NSLOCTEXT("UnrealClaudeMCP", "DisconnectPins", "MCP: Disconnect blueprint pins"));

	// Record pre-disconnect state of the graph and both involved nodes.
	Graph->Modify();
	SourceNode->Modify();
	TargetNode->Modify();

	// Break the link
	SourcePin->BreakLinkTo(TargetPin);

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Disconnected '%s.%s' from '%s.%s'"),
		*SourceNodeId, *SourcePinName,
		*TargetNodeId, *TargetPinName);

	return true;
}

bool FBlueprintGraphEditor::SetPinDefaultValue(
	UEdGraph* Graph,
	const FString& NodeId,
	const FString& PinName,
	const FString& Value,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	UEdGraphNode* Node = FindNodeById(Graph, NodeId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found"), *NodeId);
		return false;
	}

	UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		OutError = FString::Printf(TEXT("Input pin '%s' not found on node '%s'"), *PinName, *NodeId);
		return false;
	}

	// Open undo transaction after pin lookup succeeds. The pin's owning node
	// is what the transaction system records, since pins are not UObjects.
	TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(
		NSLOCTEXT("UnrealClaudeMCP", "SetPinDefaultValue", "MCP: Set pin default value"));

	// Record pre-change state of the node owning the pin.
	Node->Modify();

	// Set the default value
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		Schema->TrySetDefaultValue(*Pin, Value);
	}
	else
	{
		Pin->DefaultValue = Value;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Set pin '%s.%s' default value to '%s'"), *NodeId, *PinName, *Value);
	return true;
}

UEdGraphPin* FBlueprintGraphEditor::FindPinByName(
	UEdGraphNode* Node,
	const FString& PinName,
	EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			if (Direction == EGPD_MAX || Pin->Direction == Direction)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

UEdGraphPin* FBlueprintGraphEditor::GetExecPin(UEdGraphNode* Node, bool bOutput)
{
	if (!Node)
	{
		return nullptr;
	}

	EEdGraphPinDirection Direction = bOutput ? EGPD_Output : EGPD_Input;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return Pin;
		}
	}

	return nullptr;
}

// ===== Serialization =====

TSharedPtr<FJsonObject> FBlueprintGraphEditor::SerializeNodeInfo(UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

	if (!Node)
	{
		return NodeObj;
	}

	// Use GetNodeIdOrName so read-only callers (SerializeAllNodes) return a
	// resolvable ID for nodes never touched by a modify op, without mutating
	// NodeComment. Modify callers that just assigned an MCP_ID still get it back.
	NodeObj->SetStringField(TEXT("node_id"), GetNodeIdOrName(Node));
	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> Pins;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));

			// Convert pin type to string representation
			FString TypeStr;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				TypeStr = TEXT("bool");
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
			{
				TypeStr = TEXT("int32");
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
			{
				TypeStr = (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double) ? TEXT("double") : TEXT("float");
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
			{
				TypeStr = TEXT("FString");
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				TypeStr = TEXT("exec");
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					TypeStr = Struct->GetName();
				}
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			{
				if (UClass* Class = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					TypeStr = Class->GetName() + TEXT("*");
				}
			}
			else
			{
				TypeStr = Pin->PinType.PinCategory.ToString();
			}

			PinObj->SetStringField(TEXT("type"), TypeStr);
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			}
			PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}
	NodeObj->SetArrayField(TEXT("pins"), Pins);

	return NodeObj;
}

// ===== Graph-Level Operations =====

void FBlueprintGraphEditor::AssignTemporaryIds(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Skip nodes that already have MCP IDs
		if (!GetNodeId(Node).IsEmpty())
		{
			continue;
		}

		// Determine node type and context for ID generation
		FString NodeType;
		FString Context;

		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			NodeType = TEXT("CallFunction");
			Context = CallNode->GetFunctionName().ToString();
		}
		else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			NodeType = TEXT("Event");
			Context = EventNode->GetFunctionName().ToString();
		}
		else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(Node))
		{
			NodeType = TEXT("VariableGet");
			Context = GetNode->GetVarName().ToString();
		}
		else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
		{
			NodeType = TEXT("VariableSet");
			Context = SetNode->GetVarName().ToString();
		}
		else if (Cast<UK2Node_FunctionEntry>(Node))
		{
			NodeType = TEXT("FunctionEntry");
			Context = TEXT("Entry");
		}
		else if (Cast<UK2Node_FunctionResult>(Node))
		{
			NodeType = TEXT("FunctionResult");
			Context = TEXT("Result");
		}
		else if (Cast<UK2Node_IfThenElse>(Node))
		{
			NodeType = TEXT("Branch");
		}
		else if (Cast<UK2Node_ExecutionSequence>(Node))
		{
			NodeType = TEXT("Sequence");
		}
		else
		{
			NodeType = Node->GetClass()->GetName();
		}

		FString NodeId = GenerateNodeId(NodeType, Context, Graph);
		SetNodeId(Node, NodeId);
	}
}

TSharedPtr<FJsonObject> FBlueprintGraphEditor::SerializeAllNodes(UEdGraph* Graph)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Graph)
	{
		Result->SetNumberField(TEXT("node_count"), 0);
		Result->SetArrayField(TEXT("nodes"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetArrayField(TEXT("connections"), TArray<TSharedPtr<FJsonValue>>());
		return Result;
	}

	// Read-only serialization: no longer calls AssignTemporaryIds (which writes
	// to Node->NodeComment). IDs fall back to UObject names for nodes that have
	// no pre-existing MCP_ID, which FindNodeById already resolves.

	// Serialize each node
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		TSharedPtr<FJsonObject> NodeJson = SerializeNodeInfo(Node);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	// Extract connections by iterating all output pins
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString SourceNodeId = GetNodeIdOrName(Node);
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}

				FString TargetNodeId = GetNodeIdOrName(LinkedPin->GetOwningNode());

				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("source_node_id"), SourceNodeId);
				ConnObj->SetStringField(TEXT("source_pin"), Pin->PinName.ToString());
				ConnObj->SetStringField(TEXT("target_node_id"), TargetNodeId);
				ConnObj->SetStringField(TEXT("target_pin"), LinkedPin->PinName.ToString());
				ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}
	}

	Result->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("connections"), ConnectionsArray);

	return Result;
}

bool FBlueprintGraphEditor::ClearFunctionBody(UEdGraph* Graph, FString& OutError, bool bPreserveEntryResult)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if (bPreserveEntryResult &&
			(Cast<UK2Node_FunctionEntry>(Node) || Cast<UK2Node_FunctionResult>(Node)))
		{
			continue;
		}

		NodesToRemove.Add(Node);
	}

	for (UEdGraphNode* Node : NodesToRemove)
	{
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	}

	// Break stale links on any remaining nodes
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->BreakAllNodeLinks();
		}
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Cleared function body: removed %d nodes (preserve entry/result: %s)"),
		NodesToRemove.Num(), bPreserveEntryResult ? TEXT("yes") : TEXT("no"));
	return true;
}

bool FBlueprintGraphEditor::ExportGraphToText(UEdGraph* Graph, FString& OutText, FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	if (Graph->Nodes.Num() == 0)
	{
		OutError = TEXT("Graph has no nodes to export");
		return false;
	}

	TSet<UObject*> NodesToExport;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodesToExport.Add(Node);
		}
	}

	FEdGraphUtilities::ExportNodesToText(NodesToExport, OutText);

	if (OutText.IsEmpty())
	{
		OutError = TEXT("Export produced empty text");
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Exported %d nodes (%d chars)"), NodesToExport.Num(), OutText.Len());
	return true;
}

bool FBlueprintGraphEditor::ImportGraphFromText(UEdGraph* Graph, const FString& Text, TArray<UEdGraphNode*>& OutImportedNodes, FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return false;
	}

	if (Text.IsEmpty())
	{
		OutError = TEXT("Import text is empty");
		return false;
	}

	// Check if text can be imported
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
	{
		OutError = TEXT("Text cannot be imported into this graph (incompatible schema or format)");
		return false;
	}

	TSet<UEdGraphNode*> ImportedNodeSet;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, ImportedNodeSet);

	if (ImportedNodeSet.Num() == 0)
	{
		OutError = TEXT("No nodes were imported from the provided text");
		return false;
	}

	// Convert set to array
	for (UEdGraphNode* Node : ImportedNodeSet)
	{
		OutImportedNodes.Add(Node);
	}

	// Assign MCP IDs to all imported nodes
	AssignTemporaryIds(Graph);

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Imported %d nodes from text"), ImportedNodeSet.Num());
	return true;
}

bool FBlueprintGraphEditor::SaveBlueprint(UBlueprint* Blueprint, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	FString AssetPath = Blueprint->GetPathName();

	// Strip the object name suffix (e.g., "/Game/BP_Test.BP_Test" -> "/Game/BP_Test")
	int32 DotIndex;
	if (AssetPath.FindLastChar(TEXT('.'), DotIndex))
	{
		AssetPath = AssetPath.Left(DotIndex);
	}

	if (!UEditorAssetLibrary::SaveAsset(AssetPath, false))
	{
		OutError = FString::Printf(TEXT("Failed to save Blueprint at '%s'"), *AssetPath);
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Saved Blueprint: %s"), *AssetPath);
	return true;
}

// ===== Node ID System =====

FString FBlueprintGraphEditor::GenerateNodeId(const FString& NodeType, const FString& Context, UEdGraph* Graph)
{
	FString BaseId;
	if (Context.IsEmpty())
	{
		BaseId = NodeType;
	}
	else
	{
		BaseId = FString::Printf(TEXT("%s_%s"), *NodeType, *Context);
	}

	// Ensure uniqueness with atomic increment for thread safety
	int32 Counter = FPlatformAtomics::InterlockedIncrement(&NodeIdCounter);
	FString NodeId = FString::Printf(TEXT("%s_%d"), *BaseId, Counter);

	// Verify uniqueness in graph
	if (Graph)
	{
		while (FindNodeById(Graph, NodeId) != nullptr)
		{
			Counter = FPlatformAtomics::InterlockedIncrement(&NodeIdCounter);
			NodeId = FString::Printf(TEXT("%s_%d"), *BaseId, Counter);
		}
	}

	return NodeId;
}

void FBlueprintGraphEditor::SetNodeId(UEdGraphNode* Node, const FString& NodeId)
{
	if (Node)
	{
		// Store ID in node comment (visible in editor, persisted)
		Node->NodeComment = NodeIdPrefix + NodeId;
	}
}

FString FBlueprintGraphEditor::GetNodeId(UEdGraphNode* Node)
{
	if (!Node)
	{
		return FString();
	}

	// Extract ID from node comment
	if (Node->NodeComment.StartsWith(NodeIdPrefix))
	{
		return Node->NodeComment.RightChop(NodeIdPrefix.Len());
	}

	return FString();
}

FString FBlueprintGraphEditor::GetNodeIdOrName(UEdGraphNode* Node)
{
	if (!Node)
	{
		return FString();
	}

	// Prefer an MCP_ID previously written by a modify op.
	const FString McpId = GetNodeId(Node);
	if (!McpId.IsEmpty())
	{
		return McpId;
	}

	// Fallback: the UObject name. Stable for a live node and accepted by the
	// existing FindNodeById fallback branch, so callers can still resolve it.
	return Node->GetName();
}

// ===== Private Node Creation Helpers =====

UEdGraphNode* FBlueprintGraphEditor::CreateCallFunctionNode(
	UEdGraph* Graph,
	const FString& FunctionName,
	const FString& TargetClass,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	if (FunctionName.IsEmpty())
	{
		OutError = TEXT("Function name is required");
		return nullptr;
	}

	// Enhanced function lookup: supports project BlueprintFunctionLibrary subclasses,
	// Blueprint self-context functions, and the original hardcoded engine libraries.
	UFunction* Function = nullptr;
	UClass* FunctionOwner = nullptr;

	// Try to find class by name
	if (!TargetClass.IsEmpty())
	{
		FunctionOwner = FindObject<UClass>(nullptr, *TargetClass);
		if (!FunctionOwner)
		{
			// Try common library classes by short name
			if (TargetClass.Equals(TEXT("KismetSystemLibrary"), ESearchCase::IgnoreCase))
			{
				FunctionOwner = UKismetSystemLibrary::StaticClass();
			}
			else if (TargetClass.Equals(TEXT("KismetMathLibrary"), ESearchCase::IgnoreCase))
			{
				FunctionOwner = UKismetMathLibrary::StaticClass();
			}
			else if (TargetClass.Equals(TEXT("GameplayStatics"), ESearchCase::IgnoreCase))
			{
				FunctionOwner = UGameplayStatics::StaticClass();
			}
			else if (TargetClass.Equals(TEXT("KismetArrayLibrary"), ESearchCase::IgnoreCase))
			{
				FunctionOwner = UKismetArrayLibrary::StaticClass();
			}
			else if (TargetClass.Equals(TEXT("KismetStringLibrary"), ESearchCase::IgnoreCase))
			{
				FunctionOwner = UKismetStringLibrary::StaticClass();
			}
		}

		// Final fallback: search by /Script/ path or short name across all packages
		if (!FunctionOwner)
		{
			if (TargetClass.StartsWith(TEXT("/Script/")))
			{
				// Caller already provided a fully-qualified /Script/ path — try direct lookup
				FunctionOwner = FindObject<UClass>(nullptr, *TargetClass);
			}
			if (!FunctionOwner)
			{
				// Search by short class name across native (faster) packages first
				FunctionOwner = FindFirstObject<UClass>(*TargetClass, EFindFirstObjectOptions::NativeFirst);
			}
			// If NativeFirst missed a project class, retry without the native bias
			if (!FunctionOwner)
			{
				FunctionOwner = FindFirstObject<UClass>(*TargetClass, EFindFirstObjectOptions::None);
			}
		}
	}
	else
	{
		// Default to KismetSystemLibrary for common functions
		FunctionOwner = UKismetSystemLibrary::StaticClass();
	}

	if (FunctionOwner)
	{
		Function = FunctionOwner->FindFunctionByName(FName(*FunctionName));
	}

	if (!Function)
	{
		Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(*FunctionName));
	}
	if (!Function)
	{
		Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*FunctionName));
	}
	if (!Function)
	{
		Function = UGameplayStatics::StaticClass()->FindFunctionByName(FName(*FunctionName));
	}
	if (!Function)
	{
		Function = UKismetArrayLibrary::StaticClass()->FindFunctionByName(FName(*FunctionName));
	}
	if (!Function)
	{
		Function = UKismetStringLibrary::StaticClass()->FindFunctionByName(FName(*FunctionName));
	}

	// Search the Blueprint's own generated class for self-context functions (e.g., custom BP functions)
	if (!Function)
	{
		if (UBlueprint* OwnerBP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
		{
			if (UClass* BPClass = OwnerBP->GeneratedClass)
			{
				Function = BPClass->FindFunctionByName(FName(*FunctionName));
			}
			if (!Function && OwnerBP->SkeletonGeneratedClass)
			{
				Function = OwnerBP->SkeletonGeneratedClass->FindFunctionByName(FName(*FunctionName));
			}
		}
	}

	// Search all loaded BlueprintFunctionLibrary subclasses for the function
	if (!Function)
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* TestClass = *ClassIt;
			if (TestClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass())
				&& !TestClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				Function = TestClass->FindFunctionByName(FName(*FunctionName));
				if (Function)
				{
					break;
				}
			}
		}
	}

	if (!Function)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found"), *FunctionName);
		return nullptr;
	}

	// Create the node
	FGraphNodeCreator<UK2Node_CallFunction> NodeCreator(*Graph);
	UK2Node_CallFunction* CallNode = NodeCreator.CreateNode();
	CallNode->SetFromFunction(Function);
	CallNode->NodePosX = PosX;
	CallNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return CallNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateBranchNode(
	UEdGraph* Graph,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	FGraphNodeCreator<UK2Node_IfThenElse> NodeCreator(*Graph);
	UK2Node_IfThenElse* BranchNode = NodeCreator.CreateNode();
	BranchNode->NodePosX = PosX;
	BranchNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return BranchNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateEventNode(
	UEdGraph* Graph,
	const FString& EventName,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	if (EventName.IsEmpty())
	{
		OutError = TEXT("Event name is required");
		return nullptr;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		OutError = TEXT("Could not find Blueprint for graph");
		return nullptr;
	}

	// Find the event function
	UFunction* EventFunc = nullptr;

	// Check common events
	if (EventName.Equals(TEXT("BeginPlay"), ESearchCase::IgnoreCase))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName("ReceiveBeginPlay"));
	}
	else if (EventName.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName("ReceiveTick"));
	}
	else if (EventName.Equals(TEXT("EndPlay"), ESearchCase::IgnoreCase))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName("ReceiveEndPlay"));
	}
	else
	{
		// Try to find in parent class
		if (Blueprint->ParentClass)
		{
			EventFunc = Blueprint->ParentClass->FindFunctionByName(FName(*EventName));
		}
	}

	if (!EventFunc)
	{
		OutError = FString::Printf(TEXT("Event '%s' not found. Common events: BeginPlay, Tick, EndPlay"), *EventName);
		return nullptr;
	}

	// Create the event node
	FGraphNodeCreator<UK2Node_Event> NodeCreator(*Graph);
	UK2Node_Event* EventNode = NodeCreator.CreateNode();
	EventNode->EventReference.SetFromField<UFunction>(EventFunc, false);
	EventNode->bOverrideFunction = true;
	EventNode->NodePosX = PosX;
	EventNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return EventNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateVariableGetNode(
	UEdGraph* Graph,
	UBlueprint* Blueprint,
	const FString& VariableName,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	if (VariableName.IsEmpty())
	{
		OutError = TEXT("Variable name is required");
		return nullptr;
	}

	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return nullptr;
	}

	// Verify variable exists — two-phase search.
	// Phase 1: function local variables declared in UK2Node_FunctionEntry (if this is a function graph).
	// Phase 2: Blueprint member variables in Blueprint->NewVariables.
	// SetSelfMember works for both local and member variables in Kismet.
	FName VarName(*VariableName);
	bool bFound = false;

	// Phase 1: scan the graph's FunctionEntry node for local variable declarations
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode)
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == VarName)
				{
					bFound = true;
					break;
				}
			}
			break; // Only one FunctionEntry per graph
		}
	}

	// Phase 2: check Blueprint member variables
	if (!bFound)
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarName)
			{
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found in Blueprint or function locals"), *VariableName);
		return nullptr;
	}

	// Create the node
	FGraphNodeCreator<UK2Node_VariableGet> NodeCreator(*Graph);
	UK2Node_VariableGet* GetNode = NodeCreator.CreateNode();
	GetNode->VariableReference.SetSelfMember(VarName);
	GetNode->NodePosX = PosX;
	GetNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return GetNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateVariableSetNode(
	UEdGraph* Graph,
	UBlueprint* Blueprint,
	const FString& VariableName,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	if (VariableName.IsEmpty())
	{
		OutError = TEXT("Variable name is required");
		return nullptr;
	}

	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return nullptr;
	}

	// Verify variable exists — two-phase search.
	// Phase 1: function local variables declared in UK2Node_FunctionEntry (if this is a function graph).
	// Phase 2: Blueprint member variables in Blueprint->NewVariables.
	// SetSelfMember works for both local and member variables in Kismet.
	FName VarName(*VariableName);
	bool bFound = false;

	// Phase 1: scan the graph's FunctionEntry node for local variable declarations
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode)
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == VarName)
				{
					bFound = true;
					break;
				}
			}
			break; // Only one FunctionEntry per graph
		}
	}

	// Phase 2: check Blueprint member variables
	if (!bFound)
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarName)
			{
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found in Blueprint or function locals"), *VariableName);
		return nullptr;
	}

	// Create the node
	FGraphNodeCreator<UK2Node_VariableSet> NodeCreator(*Graph);
	UK2Node_VariableSet* SetNode = NodeCreator.CreateNode();
	SetNode->VariableReference.SetSelfMember(VarName);
	SetNode->NodePosX = PosX;
	SetNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return SetNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateSequenceNode(
	UEdGraph* Graph,
	int32 NumOutputs,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	FGraphNodeCreator<UK2Node_ExecutionSequence> NodeCreator(*Graph);
	UK2Node_ExecutionSequence* SeqNode = NodeCreator.CreateNode();
	SeqNode->NodePosX = PosX;
	SeqNode->NodePosY = PosY;
	NodeCreator.Finalize();

	// Add additional output pins if needed (default is 2)
	while (SeqNode->Pins.Num() < NumOutputs + 1) // +1 for input exec
	{
		SeqNode->AddInputPin();
	}

	return SeqNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateMathNode(
	UEdGraph* Graph,
	const FString& MathOp,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	// Find the appropriate math function
	FName FunctionName;
	if (MathOp.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
	{
		FunctionName = FName("Add_FloatFloat");
	}
	else if (MathOp.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase))
	{
		FunctionName = FName("Subtract_FloatFloat");
	}
	else if (MathOp.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase))
	{
		FunctionName = FName("Multiply_FloatFloat");
	}
	else if (MathOp.Equals(TEXT("Divide"), ESearchCase::IgnoreCase))
	{
		FunctionName = FName("Divide_FloatFloat");
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown math operation: '%s'. Supported: Add, Subtract, Multiply, Divide"), *MathOp);
		return nullptr;
	}

	UFunction* MathFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(FunctionName);
	if (!MathFunc)
	{
		OutError = FString::Printf(TEXT("Math function '%s' not found"), *FunctionName.ToString());
		return nullptr;
	}

	// Create a call function node for the math operation
	FGraphNodeCreator<UK2Node_CallFunction> NodeCreator(*Graph);
	UK2Node_CallFunction* MathNode = NodeCreator.CreateNode();
	MathNode->SetFromFunction(MathFunc);
	MathNode->NodePosX = PosX;
	MathNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return MathNode;
}

// ===== Advanced K2Node Helpers (P2: ported from unreal-engine-mcp) =====

UEdGraphNode* FBlueprintGraphEditor::CreateDynamicCastNode(
	UEdGraph* Graph,
	const FString& ClassName,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	// 1. Validate input — DynamicCast is meaningless without a target class.
	if (ClassName.IsEmpty())
	{
		OutError = TEXT("target_class is required for DynamicCast node");
		return nullptr;
	}

	// 2. Resolve UClass by name. Mirrors CreateCallFunctionNode's lookup chain:
	//    direct path → /Script/Engine. → /Script/CoreUObject. → FindFirstObject (native first, then any).
	UClass* TargetClass = FindObject<UClass>(nullptr, *ClassName);
	if (!TargetClass)
	{
		TargetClass = LoadClass<UObject>(nullptr,
			*FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
	}
	if (!TargetClass)
	{
		TargetClass = LoadClass<UObject>(nullptr,
			*FString::Printf(TEXT("/Script/CoreUObject.%s"), *ClassName));
	}
	if (!TargetClass)
	{
		TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	}
	if (!TargetClass)
	{
		TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
	}

	if (!TargetClass)
	{
		OutError = FString::Printf(TEXT("Target class '%s' not found"), *ClassName);
		return nullptr;
	}

	// 3. Create the cast node. CRITICAL: TargetType must be set BEFORE Finalize()
	//    so AllocateDefaultPins() generates the correct "As<ClassName>" output pin
	//    and the typed object input pin. Setting it after Finalize leaves the node
	//    with wildcard pins until ReconstructNode() is manually invoked.
	FGraphNodeCreator<UK2Node_DynamicCast> NodeCreator(*Graph);
	UK2Node_DynamicCast* CastNode = NodeCreator.CreateNode();
	CastNode->TargetType = TargetClass;
	CastNode->NodePosX = PosX;
	CastNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return CastNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateKnotNode(
	UEdGraph* Graph,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	// Knot (a.k.a. reroute) nodes are pure wire passthroughs — no class lookup,
	// no params, no special pin allocation. They infer pin type from the first
	// connection made to them at edit time.
	FGraphNodeCreator<UK2Node_Knot> NodeCreator(*Graph);
	UK2Node_Knot* KnotNode = NodeCreator.CreateNode();
	KnotNode->NodePosX = PosX;
	KnotNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return KnotNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateSwitchEnumNode(
	UEdGraph* Graph,
	const FString& EnumName,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	// 1. Validate input — SwitchEnum requires an enum to drive its cases.
	if (EnumName.IsEmpty())
	{
		OutError = TEXT("enum is required for SwitchEnum node");
		return nullptr;
	}

	// 2. Resolve UEnum by name. Same fallback chain as the class lookup above,
	//    but typed for UEnum.
	UEnum* TargetEnum = FindObject<UEnum>(nullptr, *EnumName);
	if (!TargetEnum)
	{
		TargetEnum = LoadObject<UEnum>(nullptr,
			*FString::Printf(TEXT("/Script/Engine.%s"), *EnumName));
	}
	if (!TargetEnum)
	{
		TargetEnum = LoadObject<UEnum>(nullptr,
			*FString::Printf(TEXT("/Script/CoreUObject.%s"), *EnumName));
	}
	if (!TargetEnum)
	{
		TargetEnum = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::NativeFirst);
	}
	if (!TargetEnum)
	{
		TargetEnum = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::None);
	}

	if (!TargetEnum)
	{
		OutError = FString::Printf(TEXT("Enum '%s' not found"), *EnumName);
		return nullptr;
	}

	// 3. Create the switch node, set the enum, then ReconstructNode so the per-value
	//    output exec pins regenerate to match the enum's entries. Without the
	//    explicit ReconstructNode call the node would keep its empty default pin set.
	FGraphNodeCreator<UK2Node_SwitchEnum> NodeCreator(*Graph);
	UK2Node_SwitchEnum* SwitchNode = NodeCreator.CreateNode();
	SwitchNode->Enum = TargetEnum;
	SwitchNode->NodePosX = PosX;
	SwitchNode->NodePosY = PosY;
	NodeCreator.Finalize();

	// Regenerate output pins from enum entries (one exec pin per enum value).
	SwitchNode->ReconstructNode();

	return SwitchNode;
}

UEdGraphNode* FBlueprintGraphEditor::CreateMakeArrayNode(
	UEdGraph* Graph,
	const FString& ElementType,
	int32 PosX,
	int32 PosY,
	FString& OutError)
{
	// MakeArray is wildcard by default — it picks up the element type from the
	// first connected input pin at edit time. We intentionally do NOT pre-set
	// the pin type here even when ElementType is provided, because doing so
	// reliably across all primitive/struct/object cases requires resolving the
	// type into FEdGraphPinType (covered by FBlueprintUtils::ParsePinType in
	// the variable-creation path, but not exposed here without coupling).
	// Leaving wildcard means the user (or a follow-up connect_pins call) drives
	// the typing — same behavior as dragging a MakeArray node from the palette.
	FGraphNodeCreator<UK2Node_MakeArray> NodeCreator(*Graph);
	UK2Node_MakeArray* MakeArrayNode = NodeCreator.CreateNode();
	MakeArrayNode->NodePosX = PosX;
	MakeArrayNode->NodePosY = PosY;
	NodeCreator.Finalize();

	return MakeArrayNode;
}
