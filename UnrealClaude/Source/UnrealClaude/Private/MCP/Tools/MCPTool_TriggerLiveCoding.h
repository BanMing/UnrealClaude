// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Trigger Unreal Live Coding compilation
 *
 * Wraps ILiveCodingModule::Compile() so an agent can recompile native C++ from
 * the editor session after editing source files. Mirrors the internal call
 * already made by FScriptExecutionManager::TriggerLiveCodingCompile, but exposes
 * it as a first-class MCP tool.
 *
 * Live Coding limitations (UE 5.x):
 *  - Cannot patch USTRUCT layout changes, UCLASS hierarchy, or new UPROPERTY.
 *    Those require a full editor restart + Build.bat.
 *  - Live Coding must be enabled in this session (Ctrl+Alt+F11) for Compile to run.
 *
 * Editor / Win64 only — guarded by WITH_LIVE_CODING.
 */
class FMCPTool_TriggerLiveCoding : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("trigger_live_coding");
		Info.Description = TEXT(
			"Trigger an Unreal Live Coding recompile of native C++.\n\n"
			"Requires: Live Coding enabled in this editor session (Ctrl+Alt+F11).\n"
			"Limitations: cannot patch USTRUCT layout / UCLASS hierarchy / new UPROPERTY.\n"
			"For those, edit the C++, close the editor, run Build.bat, then reopen.\n\n"
			"Parameters:\n"
			"- 'wait_for_completion' (bool, default true): block until compile finishes (max 60s).\n\n"
			"Returns: { compiled: bool, errors: [string], wait_seconds: number }"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("wait_for_completion"), TEXT("boolean"),
				TEXT("Block until compile finishes (default true). If false, returns immediately."),
				false, TEXT("true"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
