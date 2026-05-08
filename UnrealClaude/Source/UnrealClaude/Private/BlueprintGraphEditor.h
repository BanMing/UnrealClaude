// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"

/**
 * Blueprint graph and node manipulation
 *
 * Responsibilities:
 * - Finding graphs (event graphs, function graphs)
 * - Creating/deleting Blueprint nodes
 * - Managing pin connections
 * - Node ID system for tracking
 *
 * Supported Node Types:
 * - Flow: Branch, Sequence
 * - Functions: CallFunction, PrintString
 * - Variables: VariableGet, VariableSet
 * - Events: Event (BeginPlay, Tick, EndPlay)
 * - Math: Add, Subtract, Multiply, Divide
 *
 * Node ID System:
 * - Auto-generated descriptive IDs stored in NodeComment
 * - Format: "{NodeType}_{Context}_{Counter}"
 * - Thread-safe counter for uniqueness
 */
class FBlueprintGraphEditor
{
public:
	// ===== Graph Finding =====

	/**
	 * Find a graph in Blueprint by name
	 * @param Blueprint - Blueprint to search
	 * @param GraphName - Graph name (empty for default EventGraph)
	 * @param bFunctionGraph - true to search function graphs
	 * @param OutError - Error message if not found
	 * @return Graph or nullptr
	 */
	static UEdGraph* FindGraph(
		UBlueprint* Blueprint,
		const FString& GraphName,
		bool bFunctionGraph,
		FString& OutError
	);

	// ===== Node Management =====

	/**
	 * Create a Blueprint node in the specified graph
	 *
	 * Supported NodeTypes:
	 * - "CallFunction" - params: { function, target_class }
	 * - "Branch" / "IfThenElse"
	 * - "Event" - params: { event: "BeginPlay"|"Tick"|"EndPlay" }
	 * - "VariableGet" / "VariableSet" - params: { variable }
	 * - "Sequence" - params: { num_outputs }
	 * - "PrintString"
	 * - "Add", "Subtract", "Multiply", "Divide"
	 * - "DynamicCast" / "Cast" - params: { target_class }
	 * - "Knot" / "Reroute"
	 * - "SwitchEnum" / "Switch" - params: { enum }
	 * - "MakeArray" - params: { element_type (optional) }
	 *
	 * @param Graph - Graph to add node to
	 * @param NodeType - Type of node (case insensitive)
	 * @param NodeParams - JSON parameters for the node
	 * @param PosX - X position in editor
	 * @param PosY - Y position in editor
	 * @param OutNodeId - Generated unique node ID
	 * @param OutError - Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateNode(
		UEdGraph* Graph,
		const FString& NodeType,
		const TSharedPtr<FJsonObject>& NodeParams,
		int32 PosX,
		int32 PosY,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Delete a node from graph by ID
	 * @param Graph - Graph containing node
	 * @param NodeId - MCP-generated node ID
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool DeleteNode(UEdGraph* Graph, const FString& NodeId, FString& OutError);

	/**
	 * Find node by MCP-generated ID
	 * @param Graph - Graph to search
	 * @param NodeId - Node ID to find
	 * @return Node or nullptr
	 */
	static UEdGraphNode* FindNodeById(UEdGraph* Graph, const FString& NodeId);

	// ===== Pin & Connection Management =====

	/**
	 * Connect two pins
	 * @param Graph - Graph containing nodes
	 * @param SourceNodeId - Source node ID
	 * @param SourcePinName - Source pin (empty for auto exec)
	 * @param TargetNodeId - Target node ID
	 * @param TargetPinName - Target pin (empty for auto exec)
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool ConnectPins(
		UEdGraph* Graph,
		const FString& SourceNodeId,
		const FString& SourcePinName,
		const FString& TargetNodeId,
		const FString& TargetPinName,
		FString& OutError
	);

	/**
	 * Disconnect two pins
	 * @param Graph - Graph containing nodes
	 * @param SourceNodeId - Source node ID
	 * @param SourcePinName - Source pin name
	 * @param TargetNodeId - Target node ID
	 * @param TargetPinName - Target pin name
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool DisconnectPins(
		UEdGraph* Graph,
		const FString& SourceNodeId,
		const FString& SourcePinName,
		const FString& TargetNodeId,
		const FString& TargetPinName,
		FString& OutError
	);

	/**
	 * Set default value for an input pin
	 * @param Graph - Graph containing node
	 * @param NodeId - Node ID
	 * @param PinName - Pin name
	 * @param Value - Default value as string
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool SetPinDefaultValue(
		UEdGraph* Graph,
		const FString& NodeId,
		const FString& PinName,
		const FString& Value,
		FString& OutError
	);

	/**
	 * Find pin on node by name
	 * @param Node - Node to search
	 * @param PinName - Pin name (case insensitive)
	 * @param Direction - Pin direction (EGPD_MAX for any)
	 * @return Pin or nullptr
	 */
	static UEdGraphPin* FindPinByName(
		UEdGraphNode* Node,
		const FString& PinName,
		EEdGraphPinDirection Direction = EGPD_MAX
	);

	/**
	 * Get first exec pin (input or output)
	 * @param Node - Node to search
	 * @param bOutput - true for output, false for input
	 * @return Exec pin or nullptr
	 */
	static UEdGraphPin* GetExecPin(UEdGraphNode* Node, bool bOutput);

	// ===== Serialization =====

	/**
	 * Serialize node info to JSON
	 * @param Node - Node to serialize
	 * @return JSON object with node info
	 */
	static TSharedPtr<FJsonObject> SerializeNodeInfo(UEdGraphNode* Node);

	// ===== Graph-Level Operations =====

	/**
	 * Assign MCP IDs to all nodes that don't have one
	 * Makes pre-existing Blueprint nodes referenceable by MCP tools
	 * @param Graph - Graph to process
	 */
	static void AssignTemporaryIds(UEdGraph* Graph);

	/**
	 * Serialize all nodes in a graph with full pin/connection detail.
	 * Read-only: does not modify the graph. Node IDs in the output fall back to
	 * the node's UObject name when no MCP_ID has been assigned by a prior modify op.
	 * @param Graph - Graph to serialize
	 * @return JSON with node_count, nodes array, and connections array
	 */
	static TSharedPtr<FJsonObject> SerializeAllNodes(UEdGraph* Graph);

	/**
	 * Remove nodes from a function graph
	 * @param Graph - Function graph to clear
	 * @param OutError - Error message if failed
	 * @param bPreserveEntryResult - true to keep FunctionEntry/FunctionResult, false to remove all
	 * @return true if successful
	 */
	static bool ClearFunctionBody(UEdGraph* Graph, FString& OutError, bool bPreserveEntryResult = true);

	/**
	 * Export all graph nodes to UE clipboard text format
	 * Uses FEdGraphUtilities::ExportNodesToText
	 * @param Graph - Graph to export
	 * @param OutText - Exported clipboard text
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool ExportGraphToText(UEdGraph* Graph, FString& OutText, FString& OutError);

	/**
	 * Import nodes from UE clipboard text format
	 * Uses FEdGraphUtilities::ImportNodesFromText
	 * @param Graph - Target graph
	 * @param Text - Clipboard text to import
	 * @param OutImportedNodes - Array of imported nodes
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool ImportGraphFromText(UEdGraph* Graph, const FString& Text, TArray<UEdGraphNode*>& OutImportedNodes, FString& OutError);

	/**
	 * Save Blueprint asset to disk
	 * @param Blueprint - Blueprint to save
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);

	// ===== Node ID System =====

	/**
	 * Generate unique descriptive node ID
	 * @param NodeType - Type of node
	 * @param Context - Additional context (function name, etc.)
	 * @param Graph - Graph to ensure uniqueness
	 * @return Unique ID string
	 */
	static FString GenerateNodeId(const FString& NodeType, const FString& Context, UEdGraph* Graph);

	/**
	 * Store node ID in node metadata
	 * @param Node - Node to tag
	 * @param NodeId - ID to store
	 */
	static void SetNodeId(UEdGraphNode* Node, const FString& NodeId);

	/**
	 * Get node ID from metadata
	 * @param Node - Node to query
	 * @return Node ID or empty string
	 */
	static FString GetNodeId(UEdGraphNode* Node);

	/**
	 * Read-only ID resolver. Returns the stored MCP_ID if present, otherwise
	 * falls back to the node's UObject name (e.g., "K2Node_Event_0"). Used by
	 * serialize paths so pre-existing nodes can be referenced without writing
	 * to Node->NodeComment. FindNodeById already resolves UObject-name IDs.
	 * @param Node - Node to query
	 * @return Stable ID string suitable for FindNodeById
	 */
	static FString GetNodeIdOrName(UEdGraphNode* Node);

private:
	// Thread-safe counter for unique IDs
	static volatile int32 NodeIdCounter;

	// Node creation helpers
	static UEdGraphNode* CreateCallFunctionNode(UEdGraph* Graph, const FString& FunctionName, const FString& TargetClass, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateBranchNode(UEdGraph* Graph, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateEventNode(UEdGraph* Graph, const FString& EventName, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateSequenceNode(UEdGraph* Graph, int32 NumOutputs, int32 PosX, int32 PosY, FString& OutError);
	static UEdGraphNode* CreateMathNode(UEdGraph* Graph, const FString& MathOp, int32 PosX, int32 PosY, FString& OutError);

	/**
	 * Create a UK2Node_DynamicCast node targeting the given UClass.
	 * @param Graph       - Graph to add the node to
	 * @param ClassName   - Short or path-qualified UClass name (e.g. "Pawn", "PaogeCharacter")
	 * @param PosX        - Node X position in the graph
	 * @param PosY        - Node Y position in the graph
	 * @param OutError    - Human-readable failure description if nullptr is returned
	 * @return Created node, or nullptr on failure (class not found, etc.)
	 */
	static UEdGraphNode* CreateDynamicCastNode(UEdGraph* Graph, const FString& ClassName, int32 PosX, int32 PosY, FString& OutError);

	/**
	 * Create a UK2Node_Knot (reroute / wire passthrough) node.
	 * No parameters are required beyond position.
	 * @param Graph    - Graph to add the node to
	 * @param PosX     - Node X position in the graph
	 * @param PosY     - Node Y position in the graph
	 * @param OutError - Unused for Knot nodes, kept for consistency
	 * @return Created node (never fails unless Graph is null)
	 */
	static UEdGraphNode* CreateKnotNode(UEdGraph* Graph, int32 PosX, int32 PosY, FString& OutError);

	/**
	 * Create a UK2Node_SwitchEnum node and reconstruct its output pins from the enum values.
	 * @param Graph    - Graph to add the node to
	 * @param EnumName - Short or path-qualified UEnum name (e.g. "EBattleState")
	 * @param PosX     - Node X position in the graph
	 * @param PosY     - Node Y position in the graph
	 * @param OutError - Human-readable failure description if nullptr is returned
	 * @return Created node, or nullptr on failure (enum not found, etc.)
	 */
	static UEdGraphNode* CreateSwitchEnumNode(UEdGraph* Graph, const FString& EnumName, int32 PosX, int32 PosY, FString& OutError);

	/**
	 * Create a UK2Node_MakeArray node. The array element type is left as wildcard
	 * when ElementType is empty; the editor infers the type from the first connection.
	 * @param Graph       - Graph to add the node to
	 * @param ElementType - Optional element type hint (e.g. "float", "int32"). Empty = wildcard.
	 * @param PosX        - Node X position in the graph
	 * @param PosY        - Node Y position in the graph
	 * @param OutError    - Human-readable failure description if nullptr is returned
	 * @return Created node, or nullptr on failure
	 */
	static UEdGraphNode* CreateMakeArrayNode(UEdGraph* Graph, const FString& ElementType, int32 PosX, int32 PosY, FString& OutError);

	// ID prefix for node comments
	static const FString NodeIdPrefix;
};
