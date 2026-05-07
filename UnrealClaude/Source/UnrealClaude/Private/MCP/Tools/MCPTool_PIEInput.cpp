// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_PIEInput.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"

UWorld* FMCPTool_PIEInput::GetPIEWorld()
{
	if (!GEditor)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			return Context.World();
		}
	}
	return nullptr;
}

APlayerController* FMCPTool_PIEInput::GetPlayerController(int32 PlayerIndex)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return nullptr;
	}
	return UGameplayStatics::GetPlayerController(PIEWorld, PlayerIndex);
}

FMCPToolResult FMCPTool_PIEInput::Execute(const TSharedRef<FJsonObject>& Params)
{
	if (!GetPIEWorld())
	{
		return FMCPToolResult::Error(TEXT("No active PIE session; start one via pie_session{action:start} first"));
	}

	FString Action;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("action"), Action, ParamError))
	{
		return ParamError.GetValue();
	}

	if (Action == TEXT("key"))     return ExecuteKey(Params);
	if (Action == TEXT("action"))  return ExecuteAction(Params);
	if (Action == TEXT("axis"))    return ExecuteAxis(Params);
	if (Action == TEXT("move_to")) return ExecuteMoveTo(Params);
	if (Action == TEXT("look_at")) return ExecuteLookAt(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown action '%s'. Valid: key, action, axis, move_to, look_at"), *Action));
}

FMCPToolResult FMCPTool_PIEInput::ExecuteKey(const TSharedRef<FJsonObject>& Params)
{
	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);

	FString KeyName;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("key"), KeyName, ParamError))
	{
		return ParamError.GetValue();
	}

	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No player controller at index %d"), PlayerIndex));
	}

	const FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid key '%s'"), *KeyName));
	}

	// Inject a press + release cycle via the canonical CreateSimulated factory
	// (UE 5.6+ replaces direct FInputKeyEventArgs constructors with this).
	if (PC->PlayerInput)
	{
		FInputKeyEventArgs PressArgs = FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, /*AmountDepressed=*/1.0f);
		PC->PlayerInput->InputKey(PressArgs);

		FInputKeyEventArgs ReleaseArgs = FInputKeyEventArgs::CreateSimulated(Key, IE_Released, /*AmountDepressed=*/0.0f);
		PC->PlayerInput->InputKey(ReleaseArgs);
	}
	else
	{
		return FMCPToolResult::Error(TEXT("PlayerController has no PlayerInput"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE input: key '%s' (player %d)"), *KeyName, PlayerIndex);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("key"));
	Data->SetStringField(TEXT("key"), KeyName);
	Data->SetNumberField(TEXT("player_index"), PlayerIndex);
	return FMCPToolResult::Success(FString::Printf(TEXT("Injected key '%s'"), *KeyName), Data);
}

FMCPToolResult FMCPTool_PIEInput::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);
	const double Value = ExtractOptionalNumber<double>(Params, TEXT("value"), 1.0);

	FString ActionName;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("action_name"), ActionName, ParamError))
	{
		return ParamError.GetValue();
	}

	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC || !PC->PlayerInput)
	{
		return FMCPToolResult::Error(TEXT("No player controller / input available"));
	}

	// Legacy action mapping path: resolve action -> first bound key, then inject press+release.
	// Enhanced Input actions need a different code path (UEnhancedInputComponent::InjectInputForAction);
	// for v1 we cover the legacy mapping path which still works for projects mixing systems.
	const TArray<FInputActionKeyMapping>& Mappings = PC->PlayerInput->GetKeysForAction(FName(*ActionName));
	if (Mappings.Num() == 0)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No legacy ActionMapping found for '%s'. Enhanced-Input action injection is a follow-up."),
			*ActionName));
	}

	const FKey Key = Mappings[0].Key;
	const EInputEvent Evt = Value > 0.0 ? IE_Pressed : IE_Released;
	FInputKeyEventArgs PressArgs = FInputKeyEventArgs::CreateSimulated(Key, Evt, static_cast<float>(FMath::Abs(Value)));
	PC->PlayerInput->InputKey(PressArgs);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE input: action '%s' value=%.3f via key '%s'"),
		*ActionName, Value, *Key.GetDisplayName().ToString());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("action"));
	Data->SetStringField(TEXT("action_name"), ActionName);
	Data->SetNumberField(TEXT("value"), Value);
	Data->SetStringField(TEXT("resolved_key"), Key.GetDisplayName().ToString());
	return FMCPToolResult::Success(FString::Printf(TEXT("Triggered action '%s'"), *ActionName), Data);
}

FMCPToolResult FMCPTool_PIEInput::ExecuteAxis(const TSharedRef<FJsonObject>& Params)
{
	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);
	const double Value = ExtractOptionalNumber<double>(Params, TEXT("value"), 1.0);

	FString AxisName;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("axis_name"), AxisName, ParamError))
	{
		return ParamError.GetValue();
	}

	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC || !PC->PlayerInput)
	{
		return FMCPToolResult::Error(TEXT("No player controller / input available"));
	}

	// Legacy axis mapping: resolve axis -> first bound key, inject as analog.
	const TArray<FInputAxisKeyMapping>& Mappings = PC->PlayerInput->GetKeysForAxis(FName(*AxisName));
	if (Mappings.Num() == 0)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No legacy AxisMapping found for '%s'. Enhanced-Input axis injection is a follow-up."),
			*AxisName));
	}

	const FKey Key = Mappings[0].Key;
	FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, static_cast<float>(Value));
	PC->PlayerInput->InputKey(Args);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE input: axis '%s' value=%.3f via key '%s'"),
		*AxisName, Value, *Key.GetDisplayName().ToString());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("axis"));
	Data->SetStringField(TEXT("axis_name"), AxisName);
	Data->SetNumberField(TEXT("value"), Value);
	Data->SetStringField(TEXT("resolved_key"), Key.GetDisplayName().ToString());
	return FMCPToolResult::Success(FString::Printf(TEXT("Drove axis '%s'"), *AxisName), Data);
}

FMCPToolResult FMCPTool_PIEInput::ExecuteMoveTo(const TSharedRef<FJsonObject>& Params)
{
	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);
	const double X = ExtractOptionalNumber<double>(Params, TEXT("x"), 0.0);
	const double Y = ExtractOptionalNumber<double>(Params, TEXT("y"), 0.0);
	const double Z = ExtractOptionalNumber<double>(Params, TEXT("z"), 0.0);

	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No player controller at index %d"), PlayerIndex));
	}
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FMCPToolResult::Error(TEXT("PlayerController has no possessed pawn"));
	}

	// v1: TELEPORT — direct SetActorLocation. NavMesh pathfinding is deferred.
	const FVector Target(X, Y, Z);
	const bool bMoved = Pawn->SetActorLocation(Target, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
		ETeleportType::TeleportPhysics);

	if (!bMoved)
	{
		return FMCPToolResult::Error(TEXT("SetActorLocation refused (collision / blocking)"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE input: move_to (%.1f, %.1f, %.1f)"), X, Y, Z);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("move_to"));
	Data->SetNumberField(TEXT("x"), X);
	Data->SetNumberField(TEXT("y"), Y);
	Data->SetNumberField(TEXT("z"), Z);
	Data->SetStringField(TEXT("mode"), TEXT("teleport"));
	return FMCPToolResult::Success(TEXT("Pawn teleported"), Data);
}

FMCPToolResult FMCPTool_PIEInput::ExecuteLookAt(const TSharedRef<FJsonObject>& Params)
{
	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);
	const double X = ExtractOptionalNumber<double>(Params, TEXT("x"), 0.0);
	const double Y = ExtractOptionalNumber<double>(Params, TEXT("y"), 0.0);
	const double Z = ExtractOptionalNumber<double>(Params, TEXT("z"), 0.0);

	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No player controller at index %d"), PlayerIndex));
	}
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FMCPToolResult::Error(TEXT("PlayerController has no possessed pawn"));
	}

	const FVector Origin = Pawn->GetActorLocation();
	const FVector Target(X, Y, Z);
	const FRotator LookRot = (Target - Origin).Rotation();

	PC->SetControlRotation(LookRot);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE input: look_at (%.1f, %.1f, %.1f) → rot (P=%.1f Y=%.1f R=%.1f)"),
		X, Y, Z, LookRot.Pitch, LookRot.Yaw, LookRot.Roll);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("look_at"));
	Data->SetNumberField(TEXT("x"), X);
	Data->SetNumberField(TEXT("y"), Y);
	Data->SetNumberField(TEXT("z"), Z);
	Data->SetNumberField(TEXT("pitch"), LookRot.Pitch);
	Data->SetNumberField(TEXT("yaw"), LookRot.Yaw);
	Data->SetNumberField(TEXT("roll"), LookRot.Roll);
	return FMCPToolResult::Success(TEXT("Control rotation set"), Data);
}
