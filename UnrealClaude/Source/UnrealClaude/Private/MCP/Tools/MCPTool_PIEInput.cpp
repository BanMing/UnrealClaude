// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_PIEInput.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"

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

	if (Action == TEXT("key"))           return ExecuteKey(Params);
	if (Action == TEXT("action"))        return ExecuteAction(Params);
	if (Action == TEXT("inject_action")) return ExecuteInjectAction(Params);
	if (Action == TEXT("axis"))          return ExecuteAxis(Params);
	if (Action == TEXT("move_to"))       return ExecuteMoveTo(Params);
	if (Action == TEXT("look_at"))       return ExecuteLookAt(Params);
	if (Action == TEXT("mouse"))         return ExecuteMouse(Params);
	if (Action == TEXT("click_widget"))  return ExecuteClickWidget(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown action '%s'. Valid: key, action, inject_action, axis, move_to, look_at, mouse, click_widget"), *Action));
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

FMCPToolResult FMCPTool_PIEInput::ExecuteInjectAction(const TSharedRef<FJsonObject>& Params)
{
	// Enhanced-Input action injection.
	//
	// Routes through UEnhancedInputLocalPlayerSubsystem::InjectInputForAction,
	// which is the canonical Enhanced-Input entry point. This is distinct from
	// the legacy 'action' variant above, which only resolves PlayerInput
	// ActionMapping entries and does NOT drive Enhanced-Input bindings.
	//
	// Execution steps:
	//   1. Extract params: player_index, action_path (required), value_type
	//      (default "Digital"), value (default 1.0), axis_y / axis_z (default 0).
	//   2. Resolve PC -> LocalPlayer -> EnhancedInputLocalPlayerSubsystem.
	//   3. Load the UInputAction asset from action_path.
	//   4. Construct FInputActionValue based on value_type.
	//   5. Call EIS->InjectInputForAction with empty Modifiers / Triggers arrays.
	//   6. Log and return success.

	const int32 PlayerIndex = ExtractOptionalNumber<int32>(Params, TEXT("player_index"), 0);
	const double Value = ExtractOptionalNumber<double>(Params, TEXT("value"), 1.0);
	const double AxisY = ExtractOptionalNumber<double>(Params, TEXT("axis_y"), 0.0);
	const double AxisZ = ExtractOptionalNumber<double>(Params, TEXT("axis_z"), 0.0);

	FString ActionPath;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("action_path"), ActionPath, ParamError))
	{
		return ParamError.GetValue();
	}

	// value_type is optional; default Digital.
	FString ValueType = TEXT("Digital");
	if (Params->HasTypedField<EJson::String>(TEXT("value_type")))
	{
		ValueType = Params->GetStringField(TEXT("value_type"));
	}

	// Step 2 — Resolve PC -> LocalPlayer -> EnhancedInputLocalPlayerSubsystem.
	APlayerController* PC = GetPlayerController(PlayerIndex);
	if (!PC)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No player controller at index %d"), PlayerIndex));
	}
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		return FMCPToolResult::Error(TEXT("PlayerController has no LocalPlayer (Enhanced Input requires a local player)"));
	}
	UEnhancedInputLocalPlayerSubsystem* EIS = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!EIS)
	{
		return FMCPToolResult::Error(TEXT("UEnhancedInputLocalPlayerSubsystem unavailable on the LocalPlayer"));
	}

	// Step 3 — Load the UInputAction asset.
	UInputAction* IA = LoadObject<UInputAction>(nullptr, *ActionPath);
	if (!IA)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to load UInputAction at '%s' (check asset path)"), *ActionPath));
	}

	// Step 4 — Construct FInputActionValue.
	// FInputActionValue stores the value in a typed union; the constructor chosen
	// here determines the runtime type tag. InjectInputForAction validates the
	// shape matches the UInputAction's ValueType, so this MUST agree with the
	// asset's declared shape.
	FInputActionValue ActionValue;
	if (ValueType.Equals(TEXT("Digital"), ESearchCase::IgnoreCase))
	{
		ActionValue = FInputActionValue(Value != 0.0);
	}
	else if (ValueType.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
	{
		ActionValue = FInputActionValue(static_cast<float>(Value));
	}
	else if (ValueType.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
	{
		ActionValue = FInputActionValue(FVector2D(Value, AxisY));
	}
	else if (ValueType.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
	{
		ActionValue = FInputActionValue(FVector(Value, AxisY, AxisZ));
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown value_type '%s'. Valid: Digital, Axis1D, Axis2D, Axis3D"), *ValueType));
	}

	// Step 5 — Inject. Empty Modifiers / Triggers arrays: bypass the asset's
	// trigger/modifier pipeline and deliver the value directly to the binding,
	// which is what callers expect for scripted PIE injection.
	EIS->InjectInputForAction(IA, ActionValue, /*Modifiers=*/{}, /*Triggers=*/{});

	UE_LOG(LogUnrealClaude, Log,
		TEXT("PIE input: inject_action path='%s' type=%s value=%.3f (axis_y=%.3f axis_z=%.3f) player=%d"),
		*ActionPath, *ValueType, Value, AxisY, AxisZ, PlayerIndex);

	// Step 6 — Return success JSON.
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("inject_action"));
	Data->SetStringField(TEXT("action_path"), ActionPath);
	Data->SetStringField(TEXT("value_type"), ValueType);
	Data->SetNumberField(TEXT("value"), Value);
	if (ValueType.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) ||
		ValueType.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
	{
		Data->SetNumberField(TEXT("axis_y"), AxisY);
	}
	if (ValueType.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
	{
		Data->SetNumberField(TEXT("axis_z"), AxisZ);
	}
	Data->SetNumberField(TEXT("player_index"), PlayerIndex);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Injected Enhanced-Input action '%s'"), *ActionPath), Data);
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

// ============================================================================
// Slate-routed mouse injection
// ============================================================================

namespace
{
	/**
	 * Resolve "Left" / "Right" / "Middle" (case-insensitive) → FKey.
	 *
	 * @param ButtonName       Free-form button label.
	 * @param OutKey           Resolved FKey on success.
	 * @return true on success.
	 */
	bool ResolveMouseButtonKey(const FString& ButtonName, FKey& OutKey)
	{
		if (ButtonName.Equals(TEXT("Left"), ESearchCase::IgnoreCase) ||
			ButtonName.Equals(TEXT("LMB"), ESearchCase::IgnoreCase))
		{
			OutKey = EKeys::LeftMouseButton;
			return true;
		}
		if (ButtonName.Equals(TEXT("Right"), ESearchCase::IgnoreCase) ||
			ButtonName.Equals(TEXT("RMB"), ESearchCase::IgnoreCase))
		{
			OutKey = EKeys::RightMouseButton;
			return true;
		}
		if (ButtonName.Equals(TEXT("Middle"), ESearchCase::IgnoreCase) ||
			ButtonName.Equals(TEXT("MMB"), ESearchCase::IgnoreCase))
		{
			OutKey = EKeys::MiddleMouseButton;
			return true;
		}
		return false;
	}
}

bool FMCPTool_PIEInput::FireSlateMouseEvent(double AbsX, double AbsY, const FKey& Button,
	const FString& EventType, FString& OutError)
{
	// Slate is the canonical UMG input router. PlayerInput-level key injection
	// (the path used by ExecuteKey) does NOT reach UMG widgets, which is why
	// clicking hand cards via 'key:LeftMouseButton' fails. This function uses
	// the Slate APIs directly so UUserWidget::NativeOnMouseButtonDown fires.

	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("FSlateApplication not initialized");
		return false;
	}
	FSlateApplication& Slate = FSlateApplication::Get();

	// Step 1 — position the platform cursor. SetCursorPos drives the OS-level
	// cursor; downstream FPointerEvent payloads must agree on the same coords.
	const FVector2D AbsPos(AbsX, AbsY);
	Slate.SetCursorPos(AbsPos);

	// Build PressedButtons set for the event. For 'down' / 'click' the
	// button is held; for 'up' the button has just been released so it must
	// NOT be in PressedButtons (Slate dispatches up-event by checking the
	// effecting button + the absence of it in the pressed set).
	const bool bIsDown = EventType.Equals(TEXT("down"), ESearchCase::IgnoreCase) ||
		EventType.Equals(TEXT("click"), ESearchCase::IgnoreCase);
	const bool bIsUp = EventType.Equals(TEXT("up"), ESearchCase::IgnoreCase) ||
		EventType.Equals(TEXT("click"), ESearchCase::IgnoreCase);
	const bool bIsMove = EventType.Equals(TEXT("move"), ESearchCase::IgnoreCase);

	if (!bIsDown && !bIsUp && !bIsMove)
	{
		OutError = FString::Printf(TEXT("Unknown event '%s'. Valid: click, down, up, move"), *EventType);
		return false;
	}

	TSet<FKey> PressedForDown;
	if (Button.IsValid()) { PressedForDown.Add(Button); }
	const TSet<FKey> PressedForUpOrMove; // empty

	const FModifierKeysState Modifiers; // no modifiers
	const uint32 PointerIndex = 0;

	// Step 2 — synthesize a move first so hover state updates against the
	// new cursor location. Skipping this can leave UMG with stale hover and
	// the button-down lands on the *previous* hovered widget.
	{
		FPointerEvent MoveEvent(
			PointerIndex,
			AbsPos,
			AbsPos,
			PressedForUpOrMove,
			FKey(),       // no effecting button on a hover-refresh
			0.0f,         // wheel delta
			Modifiers
		);
		Slate.ProcessMouseMoveEvent(MoveEvent, /*bIsSynthetic=*/false);
	}

	// Step 3 — dispatch down / up. Pass an empty platform-window pointer so
	// Slate resolves the topmost relevant window itself; this matches the
	// path FSlateApplication takes for OS-driven mouse events that arrive
	// before window association is established.
	const TSharedPtr<FGenericWindow> NullPlatformWindow;

	if (bIsMove)
	{
		// Already moved above; nothing more to do.
	}
	else if (EventType.Equals(TEXT("down"), ESearchCase::IgnoreCase))
	{
		FPointerEvent DownEvent(PointerIndex, AbsPos, AbsPos, PressedForDown,
			Button, 0.0f, Modifiers);
		Slate.ProcessMouseButtonDownEvent(NullPlatformWindow, DownEvent);
	}
	else if (EventType.Equals(TEXT("up"), ESearchCase::IgnoreCase))
	{
		FPointerEvent UpEvent(PointerIndex, AbsPos, AbsPos, PressedForUpOrMove,
			Button, 0.0f, Modifiers);
		Slate.ProcessMouseButtonUpEvent(UpEvent);
	}
	else // click = down + up
	{
		FPointerEvent DownEvent(PointerIndex, AbsPos, AbsPos, PressedForDown,
			Button, 0.0f, Modifiers);
		Slate.ProcessMouseButtonDownEvent(NullPlatformWindow, DownEvent);

		FPointerEvent UpEvent(PointerIndex, AbsPos, AbsPos, PressedForUpOrMove,
			Button, 0.0f, Modifiers);
		Slate.ProcessMouseButtonUpEvent(UpEvent);
	}

	return true;
}

bool FMCPTool_PIEInput::ResolveWidgetCenter(const FString& WidgetClassFilter,
	const FString& InnerWidgetName, int32 InstanceIndex,
	FVector2D& OutAbsCenter, FString& OutResolvedName, FString& OutError)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		OutError = TEXT("No active PIE world");
		return false;
	}

	// Enumerate all UUserWidget instances in the PIE world. We deliberately
	// pass UUserWidget base class + TopLevelOnly=false so nested user widgets
	// (e.g. WBP_HandCard inside a WBP_PaogeCombatHUD hand container) are
	// reached.
	TArray<UUserWidget*> AllUserWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PIEWorld, AllUserWidgets,
		UUserWidget::StaticClass(), /*TopLevelOnly=*/false);

	// Filter by class name OR full path. Asset paths sent by callers (e.g.
	// "/Game/UI/WBP_HandCard") resolve to a generated class "WBP_HandCard_C";
	// strip trailing "_C" and any leading path on both sides for matching.
	const FString FilterTrimmed = WidgetClassFilter
		.Replace(TEXT("_C"), TEXT(""), ESearchCase::CaseSensitive);
	FString FilterShort = FilterTrimmed;
	int32 LastSlash;
	if (FilterTrimmed.FindLastChar(TEXT('/'), LastSlash))
	{
		FilterShort = FilterTrimmed.RightChop(LastSlash + 1);
		// Path of form "/Game/UI/WBP_HandCard.WBP_HandCard" — strip ".X" tail.
		int32 Dot;
		if (FilterShort.FindChar(TEXT('.'), Dot))
		{
			FilterShort = FilterShort.Left(Dot);
		}
	}

	TArray<UUserWidget*> Matches;
	for (UUserWidget* W : AllUserWidgets)
	{
		if (!W || !W->GetClass()) { continue; }
		FString ClassName = W->GetClass()->GetName();
		// Generated BP class names end with _C; strip for comparison.
		if (ClassName.EndsWith(TEXT("_C")))
		{
			ClassName = ClassName.LeftChop(2);
		}
		const FString ClassPath = W->GetClass()->GetPathName();
		if (ClassName.Equals(FilterShort, ESearchCase::IgnoreCase) ||
			ClassPath.Contains(FilterShort))
		{
			Matches.Add(W);
		}
	}

	if (Matches.Num() == 0)
	{
		OutError = FString::Printf(
			TEXT("No UUserWidget instance matches '%s' in PIE world (checked %d widgets)"),
			*WidgetClassFilter, AllUserWidgets.Num());
		return false;
	}
	if (InstanceIndex < 0 || InstanceIndex >= Matches.Num())
	{
		OutError = FString::Printf(
			TEXT("instance_index %d out of range (found %d matches for '%s')"),
			InstanceIndex, Matches.Num(), *WidgetClassFilter);
		return false;
	}

	UUserWidget* Picked = Matches[InstanceIndex];

	// Resolve inner widget if requested. UUserWidget::WidgetTree owns the
	// child widgets; GetWidgetFromName walks by FName.
	UWidget* Target = Picked;
	if (!InnerWidgetName.IsEmpty())
	{
		UWidget* Inner = Picked->GetWidgetFromName(FName(*InnerWidgetName));
		if (!Inner)
		{
			OutError = FString::Printf(
				TEXT("widget_name '%s' not found inside '%s'"),
				*InnerWidgetName, *Picked->GetName());
			return false;
		}
		Target = Inner;
	}

	const FGeometry& Geo = Target->GetCachedGeometry();
	const FVector2D AbsPos = FVector2D(Geo.GetAbsolutePosition());
	const FVector2D AbsSize = FVector2D(Geo.GetAbsoluteSize());
	if (AbsSize.IsNearlyZero())
	{
		OutError = FString::Printf(
			TEXT("Widget '%s' has zero cached geometry — not laid out yet?"),
			*Target->GetName());
		return false;
	}

	OutAbsCenter = AbsPos + AbsSize * 0.5;
	OutResolvedName = FString::Printf(TEXT("%s/%s"), *Picked->GetName(), *Target->GetName());
	return true;
}

FMCPToolResult FMCPTool_PIEInput::ExecuteMouse(const TSharedRef<FJsonObject>& Params)
{
	const double X = ExtractOptionalNumber<double>(Params, TEXT("x"), 0.0);
	const double Y = ExtractOptionalNumber<double>(Params, TEXT("y"), 0.0);
	const FString ButtonName = ExtractOptionalString(Params, TEXT("button"), TEXT("Left"));
	const FString EventType = ExtractOptionalString(Params, TEXT("event"), TEXT("click"));

	FKey Button;
	if (!ResolveMouseButtonKey(ButtonName, Button))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown button '%s'. Valid: Left, Right, Middle"), *ButtonName));
	}

	FString Err;
	if (!FireSlateMouseEvent(X, Y, Button, EventType, Err))
	{
		return FMCPToolResult::Error(Err);
	}

	UE_LOG(LogUnrealClaude, Log,
		TEXT("PIE input: mouse event=%s button=%s at (%.1f, %.1f)"),
		*EventType, *ButtonName, X, Y);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("mouse"));
	Data->SetStringField(TEXT("button"), ButtonName);
	Data->SetStringField(TEXT("event"), EventType);
	Data->SetNumberField(TEXT("x"), X);
	Data->SetNumberField(TEXT("y"), Y);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Slate mouse '%s' fired at (%.0f, %.0f)"), *EventType, X, Y), Data);
}

FMCPToolResult FMCPTool_PIEInput::ExecuteClickWidget(const TSharedRef<FJsonObject>& Params)
{
	FString WidgetClass;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("widget_class"), WidgetClass, ParamError))
	{
		return ParamError.GetValue();
	}

	const FString InnerName = ExtractOptionalString(Params, TEXT("widget_name"), FString());
	const int32 InstanceIndex = ExtractOptionalNumber<int32>(Params, TEXT("instance_index"), 0);
	const FString ButtonName = ExtractOptionalString(Params, TEXT("button"), TEXT("Left"));
	const FString EventType = ExtractOptionalString(Params, TEXT("event"), TEXT("click"));

	FKey Button;
	if (!ResolveMouseButtonKey(ButtonName, Button))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown button '%s'. Valid: Left, Right, Middle"), *ButtonName));
	}

	FVector2D AbsCenter;
	FString ResolvedName;
	FString Err;
	if (!ResolveWidgetCenter(WidgetClass, InnerName, InstanceIndex,
		AbsCenter, ResolvedName, Err))
	{
		return FMCPToolResult::Error(Err);
	}

	if (!FireSlateMouseEvent(AbsCenter.X, AbsCenter.Y, Button, EventType, Err))
	{
		return FMCPToolResult::Error(Err);
	}

	UE_LOG(LogUnrealClaude, Log,
		TEXT("PIE input: click_widget '%s' (instance %d, inner='%s') -> (%.1f, %.1f) event=%s button=%s"),
		*WidgetClass, InstanceIndex, *InnerName, AbsCenter.X, AbsCenter.Y, *EventType, *ButtonName);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("applied"), TEXT("click_widget"));
	Data->SetStringField(TEXT("widget_class"), WidgetClass);
	Data->SetStringField(TEXT("widget_name"), InnerName);
	Data->SetNumberField(TEXT("instance_index"), InstanceIndex);
	Data->SetStringField(TEXT("resolved_widget"), ResolvedName);
	Data->SetStringField(TEXT("button"), ButtonName);
	Data->SetStringField(TEXT("event"), EventType);
	Data->SetNumberField(TEXT("x"), AbsCenter.X);
	Data->SetNumberField(TEXT("y"), AbsCenter.Y);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Clicked widget '%s' at (%.0f, %.0f)"), *ResolvedName, AbsCenter.X, AbsCenter.Y),
		Data);
}
