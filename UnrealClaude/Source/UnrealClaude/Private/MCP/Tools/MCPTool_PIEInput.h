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
			"- 'look_at':       Rotate the player controller to face (x,y,z) via SetControlRotation.\n\n"
			"Requires an active PIE session (start one via 'pie_session{action:start}').\n"
			"Returns: { applied: string, ... }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("action"), TEXT("string"), TEXT("One of: key, action, inject_action, axis, move_to, look_at"), true),
			FMCPToolParameter(TEXT("player_index"), TEXT("number"), TEXT("Player controller index (default 0)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("FKey short name for 'key' action (e.g. 'W')"), false),
			FMCPToolParameter(TEXT("action_name"), TEXT("string"), TEXT("Legacy ActionMapping name for 'action' variant"), false),
			FMCPToolParameter(TEXT("action_path"), TEXT("string"), TEXT("Asset path to UInputAction for 'inject_action' (e.g. '/Game/Input/IA_PlayHandCard0')"), false),
			FMCPToolParameter(TEXT("value_type"), TEXT("string"), TEXT("FInputActionValue shape for 'inject_action': Digital | Axis1D | Axis2D | Axis3D (default Digital)"), false, TEXT("Digital")),
			FMCPToolParameter(TEXT("axis_name"), TEXT("string"), TEXT("Axis name for 'axis' action"), false),
			FMCPToolParameter(TEXT("value"), TEXT("number"), TEXT("Scalar value for 'action' / 'inject_action' / 'axis' (default 1.0). For inject_action Digital, treated as truthy/falsy; for Axis1D, X scalar; for Axis2D/3D, X component."), false, TEXT("1.0")),
			FMCPToolParameter(TEXT("axis_y"), TEXT("number"), TEXT("Y component for 'inject_action' value_type Axis2D / Axis3D (default 0.0)"), false, TEXT("0.0")),
			FMCPToolParameter(TEXT("axis_z"), TEXT("number"), TEXT("Z component for 'inject_action' value_type Axis3D (default 0.0)"), false, TEXT("0.0")),
			FMCPToolParameter(TEXT("x"), TEXT("number"), TEXT("Target X for 'move_to' / 'look_at'"), false),
			FMCPToolParameter(TEXT("y"), TEXT("number"), TEXT("Target Y for 'move_to' / 'look_at'"), false),
			FMCPToolParameter(TEXT("z"), TEXT("number"), TEXT("Target Z for 'move_to' / 'look_at'"), false)
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

	/** Resolve the PIE world (returns nullptr if no PIE running) */
	static class UWorld* GetPIEWorld();

	/** Resolve the player controller for the given index in the active PIE world */
	static class APlayerController* GetPlayerController(int32 PlayerIndex);
};
