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
 *  - key:        Inject a raw key press (FKey by short name, e.g. "W", "SpaceBar")
 *  - action:     Trigger a named Enhanced-Input action (action_name + value)
 *  - axis:       Drive an axis input (axis_name + value)
 *  - move_to:    Teleport the controlled pawn to a world location (SetActorLocation)
 *  - look_at:    Rotate the player controller to face a world location (SetControlRotation)
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
			"- 'key':     Press an FKey by short name (e.g. 'W', 'SpaceBar', 'LeftMouseButton').\n"
			"- 'action':  Trigger a named Enhanced-Input action with optional scalar value.\n"
			"- 'axis':    Drive a named axis with a scalar value.\n"
			"- 'move_to': TELEPORT the controlled pawn to (x,y,z). v1 = direct SetActorLocation;\n"
			"             NavMesh pathfinding is deferred to a follow-up.\n"
			"- 'look_at': Rotate the player controller to face (x,y,z) via SetControlRotation.\n\n"
			"Requires an active PIE session (start one via 'pie_session{action:start}').\n"
			"Returns: { applied: string, ... }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("action"), TEXT("string"), TEXT("One of: key, action, axis, move_to, look_at"), true),
			FMCPToolParameter(TEXT("player_index"), TEXT("number"), TEXT("Player controller index (default 0)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("FKey short name for 'key' action (e.g. 'W')"), false),
			FMCPToolParameter(TEXT("action_name"), TEXT("string"), TEXT("Action name for 'action' action"), false),
			FMCPToolParameter(TEXT("axis_name"), TEXT("string"), TEXT("Axis name for 'axis' action"), false),
			FMCPToolParameter(TEXT("value"), TEXT("number"), TEXT("Scalar value for 'action' / 'axis' (default 1.0)"), false, TEXT("1.0")),
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
	FMCPToolResult ExecuteAxis(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMoveTo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteLookAt(const TSharedRef<FJsonObject>& Params);

	/** Resolve the PIE world (returns nullptr if no PIE running) */
	static class UWorld* GetPIEWorld();

	/** Resolve the player controller for the given index in the active PIE world */
	static class APlayerController* GetPlayerController(int32 PlayerIndex);
};
