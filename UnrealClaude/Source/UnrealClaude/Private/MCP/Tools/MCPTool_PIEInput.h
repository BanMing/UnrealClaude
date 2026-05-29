// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Inject input into a running PIE session
 *
 * Allows an agent to drive a Play-In-Editor session without manual keyboard /
 * gamepad use — useful for verification of gameplay code paths after a
 * trigger_live_coding cycle.
 *
 * Actions:
 *  - key:           Inject a raw key press (FKey by short name, e.g. "W", "SpaceBar")
 *  - action:        Trigger a legacy ActionMapping (action_name + value).
 *                   Resolves action_name → first bound FKey → raw key injection.
 *  - inject_action: Inject an Enhanced-Input UInputAction via
 *                   UEnhancedInputLocalPlayerSubsystem::InjectInputForAction().
 *                   Resolves action_path (asset path to a UInputAction) and a
 *                   value_type (Digital / Axis1D / Axis2D / Axis3D). This is the
 *                   correct path for projects using IMC_/IA_ Enhanced-Input setup;
 *                   the legacy 'action' variant does NOT route through Enhanced Input.
 *  - axis:          Drive an axis input (axis_name + value)
 *  - move_to:       Teleport the controlled pawn to a world location (SetActorLocation)
 *  - look_at:       Rotate the player controller to face a world location (SetControlRotation)
 *  - mouse:         Inject a Slate-routed mouse event at absolute screen coords
 *                   (cursor positioned + ProcessMouseButtonDown/Up dispatched so UMG
 *                   widgets receive the press). The legacy 'key' action with
 *                   LeftMouseButton goes through PlayerInput and bypasses Slate's
 *                   UMG event chain — 'mouse' is the right call for hand-card clicks.
 *  - click_widget:  Resolve a UUserWidget by class name / instance index (and
 *                   optionally an inner-tree FName for the hit target), compute its
 *                   absolute cached geometry center, then fire a Slate mouse event
 *                   there. Lets agents click hand cards / HUD buttons by name.
 *
 * Scope (v1):
 *  - move_to is implemented as a TELEPORT (no NavMesh pathfinding). NavMesh-based
 *    AIBlueprintHelperLibrary::SimpleMoveToLocation is intentionally deferred to a
 *    follow-up to keep the dependency surface (AIModule + NavigationSystem) out of
 *    UnrealClaude.Build.cs.
 *  - look_at sets the player controller's control rotation to face the target;
 *    pawn rotation follows depending on the pawn's bUseControllerRotation* flags.
 *
 * Adapted from yes-ue-mcp's PieInputTool with scope reductions noted above.
 */
class FMCPTool_PIEInput : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("pie_input");
		Info.Description = TEXT(
			"Inject input into the active PIE session.\n\n"
			"Actions:\n"
			"- 'key':           Press an FKey by short name (e.g. 'W', 'SpaceBar', 'LeftMouseButton').\n"
			"- 'action':        Trigger a legacy ActionMapping with optional scalar value.\n"
			"                   Does NOT route through Enhanced Input — use 'inject_action' for IMC_*/IA_*.\n"
			"- 'inject_action': Inject an Enhanced-Input UInputAction via\n"
			"                   UEnhancedInputLocalPlayerSubsystem::InjectInputForAction.\n"
			"                   Requires 'action_path' (e.g. '/Game/Input/IA_PlayHandCard0') and\n"
			"                   'value_type' (Digital / Axis1D / Axis2D / Axis3D, default Digital).\n"
			"- 'axis':          Drive a named axis with a scalar value.\n"
			"- 'move_to':       TELEPORT the controlled pawn to (x,y,z). v1 = direct SetActorLocation;\n"
			"                   NavMesh pathfinding is deferred to a follow-up.\n"
			"- 'look_at':       Rotate the player controller to face (x,y,z) via SetControlRotation.\n"
			"- 'mouse':         Slate-routed mouse event at absolute screen pixels (x, y).\n"
			"                   Use 'button' (Left|Right|Middle, default Left) and 'event'\n"
			"                   (click|down|up|move, default click). UMG widgets receive\n"
			"                   the press — this is the correct path for clicking hand\n"
			"                   cards / HUD buttons. PlayerInput key injection does NOT route\n"
			"                   through Slate.\n"
			"- 'click_widget':  Resolve a UUserWidget by 'widget_class' (class short name or\n"
			"                   asset path) and optional 'widget_name' (FName inside the\n"
			"                   widget tree of the matched UUserWidget). Picks the\n"
			"                   'instance_index'-th match (default 0). Computes absolute\n"
			"                   center via cached geometry and fires a Slate mouse 'event'\n"
			"                   ('button' defaults Left, 'event' defaults click).\n\n"
			"Requires an active PIE session (start one via 'pie_session{action:start}').\n"
			"Returns: { applied: string, ... }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("action"), TEXT("string"), TEXT("One of: key, action, inject_action, axis, move_to, look_at, mouse, click_widget"), true),
			FMCPToolParameter(TEXT("player_index"), TEXT("number"), TEXT("Player controller index (default 0)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("FKey short name for 'key' action (e.g. 'W')"), false),
			FMCPToolParameter(TEXT("action_name"), TEXT("string"), TEXT("Legacy ActionMapping name for 'action' variant"), false),
			FMCPToolParameter(TEXT("action_path"), TEXT("string"), TEXT("Asset path to UInputAction for 'inject_action' (e.g. '/Game/Input/IA_PlayHandCard0')"), false),
			FMCPToolParameter(TEXT("value_type"), TEXT("string"), TEXT("FInputActionValue shape for 'inject_action': Digital | Axis1D | Axis2D | Axis3D (default Digital)"), false, TEXT("Digital")),
			FMCPToolParameter(TEXT("axis_name"), TEXT("string"), TEXT("Axis name for 'axis' action"), false),
			FMCPToolParameter(TEXT("value"), TEXT("number"), TEXT("Scalar value for 'action' / 'inject_action' / 'axis' (default 1.0). For inject_action Digital, treated as truthy/falsy; for Axis1D, X scalar; for Axis2D/3D, X component."), false, TEXT("1.0")),
			FMCPToolParameter(TEXT("axis_y"), TEXT("number"), TEXT("Y component for 'inject_action' value_type Axis2D / Axis3D (default 0.0)"), false, TEXT("0.0")),
			FMCPToolParameter(TEXT("axis_z"), TEXT("number"), TEXT("Z component for 'inject_action' value_type Axis3D (default 0.0)"), false, TEXT("0.0")),
			FMCPToolParameter(TEXT("x"), TEXT("number"), TEXT("Target X for 'move_to' / 'look_at' (world cm) or absolute screen X (pixels) for 'mouse'"), false),
			FMCPToolParameter(TEXT("y"), TEXT("number"), TEXT("Target Y for 'move_to' / 'look_at' (world cm) or absolute screen Y (pixels) for 'mouse'"), false),
			FMCPToolParameter(TEXT("z"), TEXT("number"), TEXT("Target Z for 'move_to' / 'look_at'"), false),
			FMCPToolParameter(TEXT("button"), TEXT("string"), TEXT("Mouse button for 'mouse' / 'click_widget': Left | Right | Middle (default Left)"), false, TEXT("Left")),
			FMCPToolParameter(TEXT("event"), TEXT("string"), TEXT("Mouse event for 'mouse' / 'click_widget': click | down | up | move (default click)"), false, TEXT("click")),
			FMCPToolParameter(TEXT("widget_class"), TEXT("string"), TEXT("UUserWidget class short name or asset path for 'click_widget' (e.g. 'WBP_HandCard' or '/Game/UI/WBP_HandCard')"), false),
			FMCPToolParameter(TEXT("widget_name"), TEXT("string"), TEXT("Optional inner FName to click within the matched UUserWidget tree; if omitted, click the root widget center"), false),
			FMCPToolParameter(TEXT("instance_index"), TEXT("number"), TEXT("Which match to pick when multiple instances of widget_class exist (default 0)"), false, TEXT("0"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ExecuteKey(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAction(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteInjectAction(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAxis(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMoveTo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteLookAt(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMouse(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteClickWidget(const TSharedRef<FJsonObject>& Params);

	/** Resolve the PIE world (returns nullptr if no PIE running) */
	static class UWorld* GetPIEWorld();

	/** Resolve the player controller for the given index in the active PIE world */
	static class APlayerController* GetPlayerController(int32 PlayerIndex);

	/**
	 * Fire a Slate-routed mouse event at absolute screen coords.
	 *
	 * Steps: (1) clamp coords through FSlateApplication::SetCursorPos so the
	 * platform reports the new position; (2) build an FPointerEvent with the
	 * effecting button and PressedButtons set; (3) for "click", dispatch
	 * ProcessMouseMoveEvent (hover refresh) then ProcessMouseButtonDownEvent
	 * + ProcessMouseButtonUpEvent against the topmost regular window so UMG
	 * widgets see the press.
	 *
	 * @param AbsX             Absolute screen X (DPI-scaled pixels).
	 * @param AbsY             Absolute screen Y (DPI-scaled pixels).
	 * @param Button           One of FKey LeftMouseButton / RightMouseButton / MiddleMouseButton.
	 * @param EventType        "click" | "down" | "up" | "move" (lowercase).
	 * @param OutError         Set on failure path; check before returning success.
	 * @return true on success.
	 */
	static bool FireSlateMouseEvent(double AbsX, double AbsY, const struct FKey& Button,
		const FString& EventType, FString& OutError);

	/**
	 * Resolve the absolute screen center of a UUserWidget (optionally a named
	 * inner child within that user widget's tree).
	 *
	 * @param WidgetClassFilter   Class short name (e.g. "WBP_HandCard") or asset
	 *                            path (e.g. "/Game/UI/WBP_HandCard"). Matched
	 *                            against UClass::GetName() and GetPathName().
	 * @param InnerWidgetName     Optional FName of a child widget inside the
	 *                            UUserWidget tree to use for the geometry hit.
	 *                            Empty = use the user widget's own root.
	 * @param InstanceIndex       Which match to pick when multiple UUserWidget
	 *                            instances match the filter (default 0).
	 * @param OutAbsCenter        Absolute screen center on success.
	 * @param OutResolvedName     Diagnostic: name of the widget we measured.
	 * @param OutError            Set on failure path.
	 * @return true on success.
	 */
	static bool ResolveWidgetCenter(const FString& WidgetClassFilter, const FString& InnerWidgetName,
		int32 InstanceIndex, FVector2D& OutAbsCenter, FString& OutResolvedName, FString& OutError);
};
