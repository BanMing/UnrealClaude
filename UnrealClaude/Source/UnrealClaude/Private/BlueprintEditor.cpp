// Copyright Natali Caggiano. All Rights Reserved.

#include "BlueprintEditor.h"
#include "UnrealClaudeModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"

// ===== Variable Management =====

bool FBlueprintEditor::AddVariable(
	UBlueprint* Blueprint,
	const FString& VariableName,
	const FEdGraphPinType& PinType,
	FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	if (!ValidateVariableName(VariableName, OutError))
	{
		return false;
	}

	// Check for existing variable
	FName VarName(*VariableName);
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			OutError = FString::Printf(TEXT("Variable '%s' already exists"), *VariableName);
			return false;
		}
	}

	// Add the variable
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType))
	{
		OutError = TEXT("Failed to add variable");
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Added variable '%s' to Blueprint '%s'"),
		*VariableName, *Blueprint->GetName());
	return true;
}

bool FBlueprintEditor::RemoveVariable(
	UBlueprint* Blueprint,
	const FString& VariableName,
	FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	FName VarName(*VariableName);

	// Verify variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found"), *VariableName);
		return false;
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);

	UE_LOG(LogUnrealClaude, Log, TEXT("Removed variable '%s' from Blueprint '%s'"),
		*VariableName, *Blueprint->GetName());
	return true;
}

// ===== Function Management =====

bool FBlueprintEditor::AddFunction(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	if (!ValidateFunctionName(FunctionName, OutError))
	{
		return false;
	}

	// Check for existing function
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			OutError = FString::Printf(TEXT("Function '%s' already exists"), *FunctionName);
			return false;
		}
	}

	// Create function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		OutError = TEXT("Failed to create function graph");
		return false;
	}

	// Initialize and add to Blueprint
	// nullptr cast for UE 5.7 template deduction
	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, false, static_cast<UFunction*>(nullptr));

	// Ensure function entry node exists
	bool bHasEntry = false;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (Cast<UK2Node_FunctionEntry>(Node))
		{
			bHasEntry = true;
			break;
		}
	}

	if (!bHasEntry)
	{
		// Create function entry node
		UK2Node_FunctionEntry* EntryNode = NewObject<UK2Node_FunctionEntry>(NewGraph);
		EntryNode->CreateNewGuid();
		EntryNode->PostPlacedNewNode();
		EntryNode->AllocateDefaultPins();
		NewGraph->AddNode(EntryNode);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Added function '%s' to Blueprint '%s'"),
		*FunctionName, *Blueprint->GetName());
	return true;
}

bool FBlueprintEditor::RemoveFunction(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	// Find function graph
	UEdGraph* GraphToRemove = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			GraphToRemove = Graph;
			break;
		}
	}

	if (!GraphToRemove)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found"), *FunctionName);
		return false;
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, GraphToRemove);

	UE_LOG(LogUnrealClaude, Log, TEXT("Removed function '%s' from Blueprint '%s'"),
		*FunctionName, *Blueprint->GetName());
	return true;
}

bool FBlueprintEditor::AddCustomEvent(
	UBlueprint* Blueprint,
	const FString& EventName,
	FString& OutError)
{
	// Step 1. Validate inputs.
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	if (!ValidateFunctionName(EventName, OutError))
	{
		return false;
	}

	// Step 2. Locate the canonical Event Graph (UbergraphPages[0]).
	if (Blueprint->UbergraphPages.Num() == 0)
	{
		OutError = TEXT("Blueprint has no Event Graph (UbergraphPages is empty)");
		return false;
	}
	UEdGraph* EventGraph = Blueprint->UbergraphPages[0];
	if (!EventGraph)
	{
		OutError = TEXT("Blueprint Event Graph is null");
		return false;
	}

	// Step 3. Reject duplicates: a CustomEvent with the same name already in this graph
	// would silently shadow the request and confuse later wiring calls.
	const FName EventFName(*EventName);
	for (UEdGraphNode* ExistingNode : EventGraph->Nodes)
	{
		if (UK2Node_CustomEvent* ExistingEvent = Cast<UK2Node_CustomEvent>(ExistingNode))
		{
			if (ExistingEvent->CustomFunctionName == EventFName)
			{
				OutError = FString::Printf(TEXT("CustomEvent '%s' already exists in Event Graph"), *EventName);
				return false;
			}
		}
	}

	// Step 4. Construct the new CustomEvent node via the standard K2 graph factory.
	FGraphNodeCreator<UK2Node_CustomEvent> NodeCreator(*EventGraph);
	UK2Node_CustomEvent* NewEvent = NodeCreator.CreateNode(/*bSelectNewNode*/ false);
	if (!NewEvent)
	{
		OutError = TEXT("Failed to allocate UK2Node_CustomEvent");
		return false;
	}
	NewEvent->CustomFunctionName = EventFName;
	NodeCreator.Finalize();

	// Step 5. Mark structurally modified so dependent callers (variable maps, compiler) refresh.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogUnrealClaude, Log, TEXT("Added CustomEvent '%s' to Blueprint '%s' Event Graph"),
		*EventName, *Blueprint->GetName());
	return true;
}

// ===== Name Validation =====

bool FBlueprintEditor::ValidateVariableName(const FString& VariableName, FString& OutError)
{
	if (VariableName.IsEmpty())
	{
		OutError = TEXT("Variable name cannot be empty");
		return false;
	}

	if (VariableName.Len() > MaxNameLength)
	{
		OutError = FString::Printf(TEXT("Variable name exceeds maximum length of %d characters"), MaxNameLength);
		return false;
	}

	// Must start with letter or underscore
	if (!FChar::IsAlpha(VariableName[0]) && VariableName[0] != TEXT('_'))
	{
		OutError = TEXT("Variable name must start with a letter or underscore");
		return false;
	}

	// Only alphanumeric and underscore
	for (TCHAR C : VariableName)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
		{
			OutError = FString::Printf(TEXT("Variable name contains invalid character: '%c'"), C);
			return false;
		}
	}

	return true;
}

bool FBlueprintEditor::ValidateFunctionName(const FString& FunctionName, FString& OutError)
{
	// Same validation rules as variables
	if (FunctionName.IsEmpty())
	{
		OutError = TEXT("Function name cannot be empty");
		return false;
	}

	if (FunctionName.Len() > MaxNameLength)
	{
		OutError = FString::Printf(TEXT("Function name exceeds maximum length of %d characters"), MaxNameLength);
		return false;
	}

	if (!FChar::IsAlpha(FunctionName[0]) && FunctionName[0] != TEXT('_'))
	{
		OutError = TEXT("Function name must start with a letter or underscore");
		return false;
	}

	for (TCHAR C : FunctionName)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
		{
			OutError = FString::Printf(TEXT("Function name contains invalid character: '%c'"), C);
			return false;
		}
	}

	return true;
}

// ===== Type Conversion =====

bool FBlueprintEditor::ParsePinType(
	const FString& TypeString,
	FEdGraphPinType& OutPinType,
	FString& OutError)
{
	OutPinType.ResetToDefaults();
	FString CleanType = TypeString.TrimStartAndEnd();

	// Handle container types
	if (ParseContainerType(CleanType, OutPinType, OutError))
	{
		return OutError.IsEmpty();
	}

	// Primitive types
	if (CleanType == TEXT("bool") || CleanType == TEXT("Boolean"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (CleanType == TEXT("int") || CleanType == TEXT("int32") || CleanType == TEXT("Integer"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (CleanType == TEXT("int64"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		return true;
	}
	if (CleanType == TEXT("float") || CleanType == TEXT("Float"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		return true;
	}
	if (CleanType == TEXT("double") || CleanType == TEXT("Double"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (CleanType == TEXT("byte") || CleanType == TEXT("uint8") || CleanType == TEXT("Byte"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		return true;
	}
	if (CleanType == TEXT("FString") || CleanType == TEXT("String"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (CleanType == TEXT("FName") || CleanType == TEXT("Name"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		return true;
	}
	if (CleanType == TEXT("FText") || CleanType == TEXT("Text"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}

	// Struct and object types
	if (ParseStructType(CleanType, OutPinType, OutError))
	{
		return true;
	}

	// Object references (with * suffix)
	if (CleanType.EndsWith(TEXT("*")))
	{
		FString ClassName = CleanType.LeftChop(1).TrimEnd();
		UClass* Class = FindObject<UClass>(nullptr, *ClassName);

		if (!Class)
		{
			Class = LoadClass<UObject>(nullptr,
				*FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
		}
		if (!Class)
		{
			Class = LoadClass<UObject>(nullptr,
				*FString::Printf(TEXT("/Script/CoreUObject.%s"), *ClassName));
		}

		if (Class)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = Class;
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown class: %s"), *ClassName);
		return false;
	}

	OutError = FString::Printf(TEXT("Unknown type: %s"), *TypeString);
	return false;
}

/**
 * Splits the inner content of a TMap<...> string into its key and value type tokens.
 *
 * The split point is the first comma at brace depth 0, so nested containers such as
 * "FName, TArray<int32>" are handled correctly: angle brackets are tracked to avoid
 * splitting on commas that appear inside a nested template argument list.
 *
 * @param Inner      The string between the outer "TMap<" and ">", e.g. "FName, int32"
 * @param OutKey     Receives the key type substring (not yet trimmed)
 * @param OutValue   Receives the value type substring (not yet trimmed)
 * @param OutError   Receives an error description when the function returns false
 * @return true if a top-level comma was found and the split succeeded; false on error
 */
static bool SplitMapKeyValue(
	const FString& Inner,
	FString& OutKey,
	FString& OutValue,
	FString& OutError)
{
	// Walk the string tracking '<' / '>' nesting depth.
	// The split comma must be at depth 0.
	int32 Depth = 0;
	int32 SplitIndex = INDEX_NONE;

	for (int32 i = 0; i < Inner.Len(); ++i)
	{
		TCHAR Ch = Inner[i];
		if (Ch == TEXT('<'))
		{
			++Depth;
		}
		else if (Ch == TEXT('>'))
		{
			--Depth;
		}
		else if (Ch == TEXT(',') && Depth == 0)
		{
			SplitIndex = i;
			break; // First top-level comma is the key/value separator
		}
	}

	if (SplitIndex == INDEX_NONE)
	{
		OutError = TEXT("TMap requires two type parameters separated by a comma, e.g. TMap<FName, int32>");
		return false;
	}

	OutKey   = Inner.Left(SplitIndex);
	OutValue = Inner.Mid(SplitIndex + 1);
	return true;
}

bool FBlueprintEditor::ParseContainerType(
	const FString& TypeString,
	FEdGraphPinType& OutPinType,
	FString& OutError)
{
	// TArray<T>
	if (TypeString.StartsWith(TEXT("TArray<")) && TypeString.EndsWith(TEXT(">")))
	{
		FString InnerType = TypeString.Mid(7, TypeString.Len() - 8);
		FEdGraphPinType InnerPinType;
		if (!ParsePinType(InnerType, InnerPinType, OutError))
		{
			return true; // Error set
		}
		OutPinType = InnerPinType;
		OutPinType.ContainerType = EPinContainerType::Array;
		return true;
	}

	// TSet<T>
	if (TypeString.StartsWith(TEXT("TSet<")) && TypeString.EndsWith(TEXT(">")))
	{
		FString InnerType = TypeString.Mid(5, TypeString.Len() - 6);
		FEdGraphPinType InnerPinType;
		if (!ParsePinType(InnerType, InnerPinType, OutError))
		{
			return true; // Error set
		}
		OutPinType = InnerPinType;
		OutPinType.ContainerType = EPinContainerType::Set;
		return true;
	}

	// TMap<KeyType, ValueType>
	// Maps require two type parameters: the key drives the primary PinType fields and
	// the value is stored in PinValueType (FEdGraphTerminalType). Neither the key nor
	// the value may themselves be a container type — UE Blueprint enforces this.
	if (TypeString.StartsWith(TEXT("TMap<")) && TypeString.EndsWith(TEXT(">")))
	{
		// Strip "TMap<" prefix and trailing ">"
		FString Inner = TypeString.Mid(5, TypeString.Len() - 6);

		FString KeyType, ValueType;
		if (!SplitMapKeyValue(Inner, KeyType, ValueType, OutError))
		{
			return true; // Error set by SplitMapKeyValue
		}

		// Parse key type — becomes the primary PinType (PinCategory, PinSubCategory, etc.)
		FEdGraphPinType KeyPinType;
		if (!ParsePinType(KeyType.TrimStartAndEnd(), KeyPinType, OutError))
		{
			return true; // Error set
		}

		// Keys cannot be containers (UE Blueprint limitation)
		if (KeyPinType.ContainerType != EPinContainerType::None)
		{
			OutError = TEXT("TMap key type cannot be a container");
			return true;
		}

		// Parse value type — goes into PinValueType
		FEdGraphPinType ValuePinType;
		if (!ParsePinType(ValueType.TrimStartAndEnd(), ValuePinType, OutError))
		{
			return true; // Error set
		}

		// Values cannot be containers (UE Blueprint limitation)
		if (ValuePinType.ContainerType != EPinContainerType::None)
		{
			OutError = TEXT("TMap value type cannot be a container");
			return true;
		}

		// Assemble the final pin type.
		// The key occupies the primary fields; the value is packed into PinValueType
		// using the FEdGraphTerminalType::FromPinType static helper.
		OutPinType = KeyPinType;
		OutPinType.ContainerType = EPinContainerType::Map;
		OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(ValuePinType);

		return true;
	}

	return false; // Not a container type
}

bool FBlueprintEditor::ParseStructType(
	const FString& TypeName,
	FEdGraphPinType& OutPinType,
	FString& OutError)
{
	// Common struct types with TBaseStructure
	if (TypeName == TEXT("FVector") || TypeName == TEXT("Vector"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		return true;
	}
	if (TypeName == TEXT("FRotator") || TypeName == TEXT("Rotator"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		return true;
	}
	if (TypeName == TEXT("FTransform") || TypeName == TEXT("Transform"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		return true;
	}
	if (TypeName == TEXT("FLinearColor") || TypeName == TEXT("LinearColor"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		return true;
	}
	if (TypeName == TEXT("FColor") || TypeName == TEXT("Color"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
		return true;
	}
	if (TypeName == TEXT("FVector2D") || TypeName == TEXT("Vector2D"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		return true;
	}

	// Try finding by name
	UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *TypeName);
	if (Struct)
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = Struct;
		return true;
	}

	return false;
}

FString FBlueprintEditor::PinTypeToString(const FEdGraphPinType& PinType)
{
	// TMap is handled separately: it has two distinct type components (key and value)
	// that cannot be expressed with a simple prefix/suffix around a single base type.
	// We reconstruct each side via recursive calls and early-return before the
	// scalar/Array/Set path below.
	if (PinType.ContainerType == EPinContainerType::Map)
	{
		// Reconstruct a key-only PinType from the primary fields (no container, no value).
		FEdGraphPinType KeyOnlyType = PinType;
		KeyOnlyType.ContainerType = EPinContainerType::None;
		KeyOnlyType.PinValueType  = FEdGraphTerminalType();
		FString KeyStr = PinTypeToString(KeyOnlyType);

		// Reconstruct a scalar PinType from PinValueType terminal fields.
		FEdGraphPinType ValueAsPinType;
		ValueAsPinType.PinCategory          = PinType.PinValueType.TerminalCategory;
		ValueAsPinType.PinSubCategory       = PinType.PinValueType.TerminalSubCategory;
		ValueAsPinType.PinSubCategoryObject = PinType.PinValueType.TerminalSubCategoryObject;
		ValueAsPinType.ContainerType        = EPinContainerType::None;
		FString ValueStr = PinTypeToString(ValueAsPinType);

		return FString::Printf(TEXT("TMap<%s, %s>"), *KeyStr, *ValueStr);
	}

	// Container prefix/suffix
	FString Prefix, Suffix;
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		Prefix = TEXT("TArray<");
		Suffix = TEXT(">");
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		Prefix = TEXT("TSet<");
		Suffix = TEXT(">");
	}

	// Base type name
	FString TypeName;

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		TypeName = TEXT("bool");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		TypeName = TEXT("int32");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		TypeName = TEXT("int64");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		TypeName = (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
			? TEXT("double") : TEXT("float");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		// FByteProperty with a non-null Enum pointer means a UENUM variable; prefer enum name over "byte"
		if (UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
		{
			TypeName = Enum->GetName();
		}
		else
		{
			TypeName = TEXT("byte");
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		TypeName = TEXT("FString");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		TypeName = TEXT("FName");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		TypeName = TEXT("FText");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
		{
			TypeName = Struct->GetName();
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
	         PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
	{
		if (UClass* Class = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
		{
			TypeName = Class->GetName() + TEXT("*");
		}
	}
	else
	{
		TypeName = PinType.PinCategory.ToString();
	}

	return Prefix + TypeName + Suffix;
}
