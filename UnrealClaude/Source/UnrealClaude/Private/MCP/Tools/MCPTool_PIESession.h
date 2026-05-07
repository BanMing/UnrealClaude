// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Control Play-In-Editor (PIE) sessions
 *
 * Provides agent-side control of PIE — start/stop/pause/resume the runtime
 * play session inside the editor without leaving the agent loop.
 * This complements compile tools so an agent can: change C++ → trigger build →
 * start PIE → inject input → verify behavior → stop PIE, all without manual
 * editor interaction.
 *
 * Actions:
 *  - start:    Start a PIE session (mode: viewport / new_window / standalone)
 *  - stop:     Request end of current PIE session
 *  - pause:    Pause an active PIE session (PlayWorld->bIsLevelStreamingFrozen)
 *  - resume:   Resume a paused PIE session
 *  - get_state: Return current PIE state (running/paused/stopped + map name)
 *  - wait_for: Block (with timeout) until PIE reaches a target state
 *
 * Adapted from yes-ue-mcp's PieSessionTool.
 */
class FMCPTool_PIESession : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("pie_session");
		Info.Description = TEXT(
			"Control a Play-In-Editor (PIE) session.\n\n"
			"Actions:\n"
			"- 'start': Begin a PIE session. Optional 'mode' (viewport|new_window|standalone) and 'map' (level path).\n"
			"- 'stop': Request end of current PIE session.\n"
			"- 'pause': Pause active PIE.\n"
			"- 'resume': Resume paused PIE.\n"
			"- 'get_state': Return current state (running|paused|stopped) plus active map name.\n"
			"- 'wait_for': Block until PIE reaches target_state (running|paused|stopped) or timeout.\n\n"
			"Returns: { state: string, map?: string, message: string }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("action"), TEXT("string"), TEXT("One of: start, stop, pause, resume, get_state, wait_for"), true),
			FMCPToolParameter(TEXT("mode"), TEXT("string"), TEXT("PIE mode for 'start': viewport (default), new_window, standalone"), false, TEXT("viewport")),
			FMCPToolParameter(TEXT("map"), TEXT("string"), TEXT("Optional map asset path to load before starting PIE (e.g. /Game/Maps/L_Test)"), false),
			FMCPToolParameter(TEXT("target_state"), TEXT("string"), TEXT("For 'wait_for' action: state to wait for (running|paused|stopped)"), false),
			FMCPToolParameter(TEXT("timeout"), TEXT("number"), TEXT("Timeout seconds for 'start' or 'wait_for' (default 30)"), false, TEXT("30"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ExecuteStart(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteStop();
	FMCPToolResult ExecutePause();
	FMCPToolResult ExecuteResume();
	FMCPToolResult ExecuteGetState() const;
	FMCPToolResult ExecuteWaitFor(const TSharedRef<FJsonObject>& Params);

	/** Get current PIE state as a string: running | paused | stopped */
	static FString GetCurrentPIEStateString();

	/** Try to fetch the live PIE world (returns nullptr if no PIE running) */
	static class UWorld* GetPIEWorld();
};
