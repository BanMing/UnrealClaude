// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_TriggerLiveCoding.h"
#include "UnrealClaudeModule.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

FMCPToolResult FMCPTool_TriggerLiveCoding::Execute(const TSharedRef<FJsonObject>& Params)
{
#if WITH_LIVE_CODING
	const bool bWait = ExtractOptionalBool(Params, TEXT("wait_for_completion"), true);

	// Step 1: Resolve LiveCoding module.
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");
	if (!LiveCoding)
	{
		return FMCPToolResult::Error(TEXT("Live Coding module not loaded"));
	}

	// Step 2: Verify Live Coding session-enabled.
	if (!LiveCoding->IsEnabledForSession())
	{
		return FMCPToolResult::Error(
			TEXT("Live Coding not enabled in this session. Press Ctrl+Alt+F11 in the editor to enable."));
	}

	// Step 3: Refuse re-entrant compile.
	if (LiveCoding->IsCompiling())
	{
		return FMCPToolResult::Error(TEXT("Live Coding compile already in progress"));
	}

	// Step 4: Trigger compile. ELiveCodingCompileFlags::None is the canonical pattern
	// already used by FScriptExecutionManager::TriggerLiveCodingCompile in this plugin.
	LiveCoding->Compile(ELiveCodingCompileFlags::None, nullptr);

	UE_LOG(LogUnrealClaude, Log, TEXT("Live Coding compile triggered (wait=%s)"),
		bWait ? TEXT("true") : TEXT("false"));

	double WaitSeconds = 0.0;

	// Step 5: Optionally wait for completion.
	if (bWait)
	{
		const double Start = FPlatformTime::Seconds();
		const double MaxWait = 60.0;
		const float PollInterval = 0.5f;

		while (LiveCoding->IsCompiling())
		{
			FPlatformProcess::Sleep(PollInterval);
			WaitSeconds = FPlatformTime::Seconds() - Start;
			if (WaitSeconds > MaxWait)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Live Coding compile timed out after %.1fs"), WaitSeconds));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("triggered"), true);
	Data->SetBoolField(TEXT("waited"), bWait);
	Data->SetNumberField(TEXT("wait_seconds"), WaitSeconds);
	return FMCPToolResult::Success(
		bWait ? FString::Printf(TEXT("Live Coding compile finished in %.1fs"), WaitSeconds)
			  : TEXT("Live Coding compile triggered (not waiting)"),
		Data);
#else
	return FMCPToolResult::Error(TEXT("WITH_LIVE_CODING is 0 in this build (Win64 editor only)"));
#endif
}
