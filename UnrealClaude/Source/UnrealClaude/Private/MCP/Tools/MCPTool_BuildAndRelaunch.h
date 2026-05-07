// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Build the editor target and (optionally) relaunch the editor
 *
 * Spawns Engine/Build/BatchFiles/Build.bat asynchronously to recompile the
 * current project's editor target, then (unless skip_relaunch is true) starts
 * the editor again with the .uproject when the build completes.
 *
 * The spawned cmd.exe uses `-WaitMutex` so UBT waits for the running editor's
 * mutex to be released. This means the agent should call `request_editor_exit`
 * (or the user must close the editor) AFTER calling this tool but BEFORE the
 * build can proceed. The chained `start UnrealEditor.exe` runs once the build
 * succeeds.
 *
 * Use this tool when Live Coding cannot patch the change (USTRUCT layout / UCLASS
 * hierarchy / new UPROPERTY). Otherwise prefer trigger_live_coding.
 *
 * Windows-only — guarded by PLATFORM_WINDOWS.
 */
class FMCPTool_BuildAndRelaunch : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("build_and_relaunch");
		Info.Description = TEXT(
			"Spawn Build.bat to recompile the editor target, then relaunch the editor.\n\n"
			"Use this when Live Coding cannot patch the change (USTRUCT layout, UCLASS hierarchy,\n"
			"new UPROPERTY). Otherwise prefer 'trigger_live_coding'.\n\n"
			"Workflow: this tool spawns a detached cmd.exe with `-WaitMutex` — UBT will block\n"
			"until the editor's mutex is released. So you typically:\n"
			"  1. Call build_and_relaunch (kicks off background process)\n"
			"  2. Close the editor (via 'request_editor_exit' or manually)\n"
			"  3. Build proceeds; on success, editor relaunches with the .uproject\n\n"
			"Parameters:\n"
			"- 'build_config' (string, default 'Development'): Development | Debug | Shipping\n"
			"- 'skip_relaunch' (bool, default false): if true, do NOT chain the editor relaunch\n\n"
			"Windows-only.\n\n"
			"Returns: { spawned: bool, command: string, pid: number? }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("build_config"), TEXT("string"),
				TEXT("Build configuration: Development (default), Debug, Shipping"),
				false, TEXT("Development")),
			FMCPToolParameter(TEXT("skip_relaunch"), TEXT("boolean"),
				TEXT("If true, only build; do not chain editor relaunch (default false)"),
				false, TEXT("false"))
		};
		Info.Annotations = FMCPToolAnnotations::Destructive();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
