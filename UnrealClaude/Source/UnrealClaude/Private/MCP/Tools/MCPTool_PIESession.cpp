// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_PIESession.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"

namespace
{
	// PIE state vocabulary used in JSON responses
	const TCHAR* StateRunning = TEXT("running");
	const TCHAR* StatePaused = TEXT("paused");
	const TCHAR* StateStopped = TEXT("stopped");
}

UWorld* FMCPTool_PIESession::GetPIEWorld()
{
	// Step 1: GEditor must be valid (we're an editor module).
	if (!GEditor)
	{
		return nullptr;
	}

	// Step 2: Iterate world contexts looking for the PIE world.
	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			return Context.World();
		}
	}
	return nullptr;
}

FString FMCPTool_PIESession::GetCurrentPIEStateString()
{
	// Step 1: No PIE world → stopped.
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return StateStopped;
	}

	// Step 2: Use UGameplayStatics::IsGamePaused for paused detection.
	if (UGameplayStatics::IsGamePaused(PIEWorld))
	{
		return StatePaused;
	}

	return StateRunning;
}

FMCPToolResult FMCPTool_PIESession::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Editor context is mandatory for any PIE control.
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("GEditor is null; PIE tools require editor context"));
	}

	// Extract required action.
	FString Action;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("action"), Action, ParamError))
	{
		return ParamError.GetValue();
	}

	// Dispatch by action.
	if (Action == TEXT("start"))    return ExecuteStart(Params);
	if (Action == TEXT("stop"))     return ExecuteStop();
	if (Action == TEXT("pause"))    return ExecutePause();
	if (Action == TEXT("resume"))   return ExecuteResume();
	if (Action == TEXT("get_state")) return ExecuteGetState();
	if (Action == TEXT("wait_for")) return ExecuteWaitFor(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown action '%s'. Valid: start, stop, pause, resume, get_state, wait_for"), *Action));
}

FMCPToolResult FMCPTool_PIESession::ExecuteStart(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Refuse double-start.
	if (GEditor->IsPlaySessionInProgress())
	{
		return FMCPToolResult::Error(TEXT("PIE session already in progress; call 'stop' first"));
	}

	const FString Mode = ExtractOptionalString(Params, TEXT("mode"), TEXT("viewport"));
	const FString MapPath = ExtractOptionalString(Params, TEXT("map"), TEXT(""));

	// Step 2: Optionally load a map before starting PIE.
	if (!MapPath.IsEmpty())
	{
		// Loads the editor world to the given map; PIE will then duplicate this world.
		const FString OpenCmd = FString::Printf(TEXT("OpenLevel %s"), *MapPath);
		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
		{
			GEditor->Exec(EditorWorld, *OpenCmd);
		}
	}

	// Step 3: Build play-session params per mode.
	FRequestPlaySessionParams SessionParams;

	// Default: PIE in editor (selected viewport).
	if (Mode == TEXT("new_window"))
	{
		SessionParams.SessionDestination = EPlaySessionDestinationType::NewProcess;
	}
	else if (Mode == TEXT("standalone"))
	{
		SessionParams.SessionDestination = EPlaySessionDestinationType::NewProcess;
	}
	// "viewport" leaves default (in-editor PIE).

	// Step 4: Request the play session.
	GEditor->RequestPlaySession(SessionParams);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE start requested (mode=%s, map=%s)"), *Mode, *MapPath);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), TEXT("starting"));
	Data->SetStringField(TEXT("mode"), Mode);
	if (!MapPath.IsEmpty())
	{
		Data->SetStringField(TEXT("map"), MapPath);
	}
	return FMCPToolResult::Success(TEXT("PIE session start requested"), Data);
}

FMCPToolResult FMCPTool_PIESession::ExecuteStop()
{
	if (!GEditor->IsPlaySessionInProgress() && !GetPIEWorld())
	{
		return FMCPToolResult::Error(TEXT("No PIE session running"));
	}

	GEditor->RequestEndPlayMap();

	UE_LOG(LogUnrealClaude, Log, TEXT("PIE stop requested"));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), TEXT("stopping"));
	return FMCPToolResult::Success(TEXT("PIE session stop requested"), Data);
}

FMCPToolResult FMCPTool_PIESession::ExecutePause()
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("No PIE session running"));
	}

	UGameplayStatics::SetGamePaused(PIEWorld, true);
	UE_LOG(LogUnrealClaude, Log, TEXT("PIE paused"));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), StatePaused);
	return FMCPToolResult::Success(TEXT("PIE paused"), Data);
}

FMCPToolResult FMCPTool_PIESession::ExecuteResume()
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("No PIE session running"));
	}

	UGameplayStatics::SetGamePaused(PIEWorld, false);
	UE_LOG(LogUnrealClaude, Log, TEXT("PIE resumed"));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), StateRunning);
	return FMCPToolResult::Success(TEXT("PIE resumed"), Data);
}

FMCPToolResult FMCPTool_PIESession::ExecuteGetState() const
{
	const FString State = GetCurrentPIEStateString();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), State);

	if (UWorld* PIEWorld = GetPIEWorld())
	{
		Data->SetStringField(TEXT("map"), PIEWorld->GetMapName());
	}

	return FMCPToolResult::Success(FString::Printf(TEXT("PIE state: %s"), *State), Data);
}

FMCPToolResult FMCPTool_PIESession::ExecuteWaitFor(const TSharedRef<FJsonObject>& Params)
{
	const FString TargetState = ExtractOptionalString(Params, TEXT("target_state"), TEXT("running"));
	const double TimeoutSec = ExtractOptionalNumber<double>(Params, TEXT("timeout"), 30.0);

	const FDateTime Start = FDateTime::UtcNow();
	const FTimespan Limit = FTimespan::FromSeconds(TimeoutSec);

	while ((FDateTime::UtcNow() - Start) < Limit)
	{
		const FString Cur = GetCurrentPIEStateString();
		if (Cur == TargetState)
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("state"), Cur);
			Data->SetNumberField(TEXT("waited_seconds"), (FDateTime::UtcNow() - Start).GetTotalSeconds());
			return FMCPToolResult::Success(FString::Printf(TEXT("PIE reached state '%s'"), *Cur), Data);
		}
		// Pump editor messages while waiting so the engine can advance PIE startup.
		FPlatformProcess::Sleep(0.1f);
	}

	const FString Final = GetCurrentPIEStateString();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), Final);
	Data->SetStringField(TEXT("target_state"), TargetState);
	Data->SetNumberField(TEXT("timeout_seconds"), TimeoutSec);
	return FMCPToolResult::Error(FString::Printf(
		TEXT("Timeout waiting for state '%s'; current = '%s'"), *TargetState, *Final));
}
