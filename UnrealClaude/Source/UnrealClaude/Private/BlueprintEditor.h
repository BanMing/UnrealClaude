// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"

/**
 * Blueprint variable and function management
 *
 * Responsibilities:
 * - Adding/removing member variables
 * - Adding/removing functions
 * - Type parsing and conversion
 * - Name validation
 *
 * Supported Types:
 * - Primitives: bool, int32, int64, float, double, byte, FString, FName, FText
 * - Structs: FVector, FRotator, FTransform, FLinearColor, FVector2D
 * - Containers: TArray<T>, TSet<T>
 * - Object references: AActor*, UTexture2D*, etc.
 */
class FBlueprintEditor
{
public:
	// ===== Variable Management =====

	/**
	 * Add member variable to Blueprint
	 * @param Blueprint - Blueprint to modify
	 * @param VariableName - Name of variable (must be valid identifier)
	 * @param PinType - Variable type
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool AddVariable(
		UBlueprint* Blueprint,
		const FString& VariableName,
		const FEdGraphPinType& PinType,
		FString& OutError
	);

	/**
	 * Remove variable from Blueprint
	 * @param Blueprint - Blueprint to modify
	 * @param VariableName - Name of variable to remove
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool RemoveVariable(
		UBlueprint* Blueprint,
		const FString& VariableName,
		FString& OutError
	);

	// ===== Function Management =====

	/**
	 * Add empty function to Blueprint
	 * @param Blueprint - Blueprint to modify
	 * @param FunctionName - Name of function (must be valid identifier)
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool AddFunction(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		FString& OutError
	);

	/**
	 * Remove function from Blueprint
	 * @param Blueprint - Blueprint to modify
	 * @param FunctionName - Name of function to remove
	 * @param OutError - Error message if failed
	 * @return true if successful
	 */
	static bool RemoveFunction(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		FString& OutError
	);

	/**
	 * Add a Custom Event node to the Blueprint's Event Graph (UbergraphPages[0]).
	 *
	 * Why this exists alongside AddFunction:
	 * AddFunction creates a full FunctionGraph via FBlueprintEditorUtils::AddFunctionGraph,
	 * which on some Unreal versions has been observed to crash for lightweight blueprints
	 * (notably WidgetBlueprints). For "logic-light" callers that just need a named entry
	 * point inside the Event Graph (e.g. a UI handler invoked from a button binding) a
	 * UK2Node_CustomEvent is sufficient and avoids the AddFunctionGraph code path entirely.
	 *
	 * Steps:
	 *   1. Validate Blueprint and event name.
	 *   2. Locate UbergraphPages[0] (the canonical Event Graph). Fail if missing.
	 *   3. Reject duplicates: scan existing UK2Node_CustomEvent nodes for the same name.
	 *   4. Construct via FGraphNodeCreator<UK2Node_CustomEvent> and Finalize.
	 *
	 * Note: this does NOT create a UFunction graph. Callers that need a true function
	 * (with parameter pins, return nodes, recursion) must continue to use AddFunction.
	 *
	 * @param Blueprint  Blueprint to modify (must be valid).
	 * @param EventName  Identifier for the new custom event (validated as a function name).
	 * @param OutError   Receives an explanatory error string on failure.
	 * @return           true on success.
	 *
	 * Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
	 * https://github.com/winyunq/UnrealMotionGraphicsMCP
	 */
	static bool AddCustomEvent(
		UBlueprint* Blueprint,
		const FString& EventName,
		FString& OutError
	);

	// ===== Type Conversion =====

	/**
	 * Parse type string to FEdGraphPinType
	 *
	 * Supported formats:
	 * - Primitives: "bool", "int32", "float", "FString"
	 * - Structs: "FVector", "FRotator", "FTransform"
	 * - Arrays: "TArray<int32>", "TArray<FVector>"
	 * - Objects: "AActor*", "UTexture2D*"
	 *
	 * @param TypeString - Type name string
	 * @param OutPinType - Output pin type
	 * @param OutError - Error message if parsing fails
	 * @return true if successful
	 */
	static bool ParsePinType(
		const FString& TypeString,
		FEdGraphPinType& OutPinType,
		FString& OutError
	);

	/**
	 * Convert FEdGraphPinType to string representation
	 * @param PinType - Pin type to convert
	 * @return Type string
	 */
	static FString PinTypeToString(const FEdGraphPinType& PinType);

	// ===== Name Validation =====

	/**
	 * Validate variable name follows Blueprint naming conventions
	 * Rules: max 128 chars, starts with letter/underscore, alphanumeric + underscore only
	 * @param VariableName - Name to validate
	 * @param OutError - Error message if invalid
	 * @return true if valid
	 */
	static bool ValidateVariableName(const FString& VariableName, FString& OutError);

	/**
	 * Validate function name follows Blueprint naming conventions
	 * (Same rules as variable names)
	 * @param FunctionName - Name to validate
	 * @param OutError - Error message if invalid
	 * @return true if valid
	 */
	static bool ValidateFunctionName(const FString& FunctionName, FString& OutError);

private:
	// Constants
	static constexpr int32 MaxNameLength = 128;

	// Helper for parsing container types
	static bool ParseContainerType(
		const FString& TypeString,
		FEdGraphPinType& OutPinType,
		FString& OutError
	);

	// Helper for parsing struct types
	static bool ParseStructType(
		const FString& TypeName,
		FEdGraphPinType& OutPinType,
		FString& OutError
	);
};
