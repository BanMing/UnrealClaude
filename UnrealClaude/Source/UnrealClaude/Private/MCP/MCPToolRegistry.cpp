// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPTaskQueue.h"
#include "UnrealClaudeModule.h"
#include "UnrealClaudeConstants.h"
#include "Containers/Ticker.h"

// Include all tool implementations
#include "Tools/MCPTool_SpawnActor.h"
#include "Tools/MCPTool_GetLevelActors.h"
#include "Tools/MCPTool_SetProperty.h"
#include "Tools/MCPTool_RunConsoleCommand.h"
#include "Tools/MCPTool_DeleteActors.h"
#include "Tools/MCPTool_MoveActor.h"
#include "Tools/MCPTool_GetOutputLog.h"
#include "Tools/MCPTool_ExecuteScript.h"
#include "Tools/MCPTool_CleanupScripts.h"
#include "Tools/MCPTool_GetScriptHistory.h"
#include "Tools/MCPTool_CaptureViewport.h"
#include "Tools/MCPTool_BlueprintQuery.h"
#include "Tools/MCPTool_BlueprintModify.h"
#include "Tools/MCPTool_AnimBlueprintModify.h"
#include "Tools/MCPTool_AssetSearch.h"
#include "Tools/MCPTool_AssetDependencies.h"
#include "Tools/MCPTool_AssetReferencers.h"
#include "Tools/MCPTool_EnhancedInput.h"
#include "Tools/MCPTool_Character.h"
#include "Tools/MCPTool_CharacterData.h"
#include "Tools/MCPTool_GenericDataTable.h"
#include "Tools/MCPTool_Material.h"
#include "Tools/MCPTool_Asset.h"
#include "Tools/MCPTool_OpenLevel.h"
#include "Tools/MCPTool_UMGQuery.h"
#include "Tools/MCPTool_UMGModify.h"
#include "Tools/MCPTool_MaterialGraph.h"
#include "Tools/MCPTool_MaterialHLSL.h"
#include "Tools/MCPTool_UMGAnimation.h"

// PR-A: PIE control + Build/LiveCoding (yes-ue-mcp MIT, adapted)
#include "Tools/MCPTool_PIESession.h"
#include "Tools/MCPTool_PIEInput.h"
#include "Tools/MCPTool_TriggerLiveCoding.h"
#include "Tools/MCPTool_BuildAndRelaunch.h"

// PR-B: UMG session anchor (UmgMcp MIT, adapted)
#include "Tools/MCPTool_UMGSession.h"

// PR-E: StateTree tool family (yes-ue-mcp MIT, adapted)
#include "Tools/MCPTool_StateTreeQuery.h"
#include "Tools/MCPTool_StateTreeModify.h"

// PR-F: VibeUE three-piece (VibeUE MIT, adapted)
#include "Tools/MCPTool_AssetManage.h"
#include "Tools/MCPTool_LogsRead.h"
#include "Tools/MCPTool_WebResearch.h"

// PR-G: ue-mcp two-piece ()
#include "Tools/MCPTool_NiagaraModify.h"
#include "Tools/MCPTool_GASModify.h"

// Task queue tools
#include "Tools/MCPTool_TaskSubmit.h"
#include "Tools/MCPTool_TaskStatus.h"
#include "Tools/MCPTool_TaskResult.h"
#include "Tools/MCPTool_TaskList.h"
#include "Tools/MCPTool_TaskCancel.h"

FMCPToolRegistry::FMCPToolRegistry()
{
	RegisterBuiltinTools();
}

FMCPToolRegistry::~FMCPToolRegistry()
{
	StopTaskQueue();
	Tools.Empty();
}

void FMCPToolRegistry::StartTaskQueue()
{
	if (TaskQueue.IsValid())
	{
		TaskQueue->Start();
	}
}

void FMCPToolRegistry::StopTaskQueue()
{
	if (TaskQueue.IsValid())
	{
		TaskQueue->Shutdown();
	}
}

void FMCPToolRegistry::RegisterBuiltinTools()
{
	UE_LOG(LogUnrealClaude, Log, TEXT("Registering MCP tools..."));

	// Register all built-in tools
	RegisterTool(MakeShared<FMCPTool_SpawnActor>());
	RegisterTool(MakeShared<FMCPTool_GetLevelActors>());
	RegisterTool(MakeShared<FMCPTool_SetProperty>());
	RegisterTool(MakeShared<FMCPTool_RunConsoleCommand>());
	RegisterTool(MakeShared<FMCPTool_DeleteActors>());
	RegisterTool(MakeShared<FMCPTool_MoveActor>());
	RegisterTool(MakeShared<FMCPTool_GetOutputLog>());

	// Script execution tools
	RegisterTool(MakeShared<FMCPTool_ExecuteScript>());
	RegisterTool(MakeShared<FMCPTool_CleanupScripts>());
	RegisterTool(MakeShared<FMCPTool_GetScriptHistory>());

	// Viewport capture
	RegisterTool(MakeShared<FMCPTool_CaptureViewport>());

	// Blueprint tools
	RegisterTool(MakeShared<FMCPTool_BlueprintQuery>());
	RegisterTool(MakeShared<FMCPTool_BlueprintModify>());
	RegisterTool(MakeShared<FMCPTool_AnimBlueprintModify>());

	// Asset tools
	RegisterTool(MakeShared<FMCPTool_AssetSearch>());
	RegisterTool(MakeShared<FMCPTool_AssetDependencies>());
	RegisterTool(MakeShared<FMCPTool_AssetReferencers>());

	// Enhanced Input tools
	RegisterTool(MakeShared<FMCPTool_EnhancedInput>());

	// Character tools
	RegisterTool(MakeShared<FMCPTool_Character>());
	RegisterTool(MakeShared<FMCPTool_CharacterData>());
	RegisterTool(MakeShared<FMCPTool_GenericDataTable>());

	// Material and Asset tools
	RegisterTool(MakeShared<FMCPTool_Material>());
	RegisterTool(MakeShared<FMCPTool_Asset>());

	// Level management tools
	RegisterTool(MakeShared<FMCPTool_OpenLevel>());

	// UMG widget tools (Story 1: UMG CRUD — adapted from UmgMcp MIT)
	RegisterTool(MakeShared<FMCPTool_UMGQuery>());
	RegisterTool(MakeShared<FMCPTool_UMGModify>());

	// Material graph tools (Story 2: Material Graph + HLSL — adapted from UmgMcp MIT)
	RegisterTool(MakeShared<FMCPTool_MaterialGraph>());
	RegisterTool(MakeShared<FMCPTool_MaterialHLSL>());

	// UMG animation tools (Story 3: UMG Animation keyframes — adapted from UmgMcp MIT)
	RegisterTool(MakeShared<FMCPTool_UMGAnimation>());

	// PR-A: PIE + Build/LiveCoding tools (yes-ue-mcp MIT, adapted)
	RegisterTool(MakeShared<FMCPTool_PIESession>());
	RegisterTool(MakeShared<FMCPTool_PIEInput>());
	RegisterTool(MakeShared<FMCPTool_TriggerLiveCoding>());
	RegisterTool(MakeShared<FMCPTool_BuildAndRelaunch>());

	// PR-B: UMG session anchor tool (UmgMcp MIT, adapted) — implicit current widget
	// fallback used by umg_query/umg_modify/umg_animation when widget_blueprint_path
	// is omitted. State lives in UUMGSessionSubsystem (editor subsystem).
	RegisterTool(MakeShared<FMCPTool_UMGSession>());

	// PR-E: StateTree tools (yes-ue-mcp MIT, adapted) — read+modify of UStateTree assets.
	// Read-before-write convention: statetree_query first to confirm asset path + state names,
	// then statetree_modify (compound: add_state | add_task | add_transition | remove_state).
	RegisterTool(MakeShared<FMCPTool_StateTreeQuery>());
	RegisterTool(MakeShared<FMCPTool_StateTreeModify>());

	// PR-F: VibeUE three-piece (VibeUE MIT, adapted)
	// asset_manage  — compound asset CRUD with reference preservation (search/find/list_folder/
	//                 open_in_editor/save/save_all_dirty/duplicate/move/delete)
	// logs_read     — file-based UE log reader (list/info/read/tail/head/filter/errors/warnings/since)
	// web_research  — UE-process-internal web search (DuckDuckGo / Jina Reader / Nominatim)
	RegisterTool(MakeShared<FMCPTool_AssetManage>());
	RegisterTool(MakeShared<FMCPTool_LogsRead>());
	RegisterTool(MakeShared<FMCPTool_WebResearch>());

	// PR-G: ue-mcp two-piece ()
	// niagara_modify — compound Niagara ops (list_systems/get_info/spawn_at_location/set_parameter)
	// gas_modify     — compound GAS ops (list/create/configure abilities, effects, attribute sets)
	RegisterTool(MakeShared<FMCPTool_NiagaraModify>());
	RegisterTool(MakeShared<FMCPTool_GASModify>());

	// Create and register async task queue tools
	// Task queue takes a raw pointer since the registry always outlives it
	TaskQueue = MakeShared<FMCPTaskQueue>(this);

	// Wire up execute_script to use the task queue for async execution
	// This allows script execution to handle permission dialogs without timing out
	if (TSharedPtr<IMCPTool>* ExecuteScriptToolPtr = Tools.Find(TEXT("execute_script")))
	{
		if (FMCPTool_ExecuteScript* ExecuteScriptTool = static_cast<FMCPTool_ExecuteScript*>(ExecuteScriptToolPtr->Get()))
		{
			ExecuteScriptTool->SetTaskQueue(TaskQueue);
			UE_LOG(LogUnrealClaude, Log, TEXT("  Wired up execute_script to task queue for async execution"));
		}
	}

	RegisterTool(MakeShared<FMCPTool_TaskSubmit>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskStatus>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskResult>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskList>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskCancel>(TaskQueue));

	UE_LOG(LogUnrealClaude, Log, TEXT("Registered %d MCP tools"), Tools.Num());
}

void FMCPToolRegistry::RegisterTool(TSharedPtr<IMCPTool> Tool)
{
	if (!Tool.IsValid())
	{
		return;
	}

	FMCPToolInfo Info = Tool->GetInfo();
	if (Info.Name.IsEmpty())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Cannot register tool with empty name"));
		return;
	}

	if (Tools.Contains(Info.Name))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Tool '%s' is already registered, replacing"), *Info.Name);
	}

	Tools.Add(Info.Name, Tool);
	UE_LOG(LogUnrealClaude, Log, TEXT("  Registered tool: %s"), *Info.Name);
}

void FMCPToolRegistry::UnregisterTool(const FString& ToolName)
{
	if (Tools.Remove(ToolName) > 0)
	{
		InvalidateToolCache();
		UE_LOG(LogUnrealClaude, Log, TEXT("Unregistered tool: %s"), *ToolName);
	}
}

void FMCPToolRegistry::InvalidateToolCache()
{
	bCacheValid = false;
	CachedToolInfo.Empty();
}

TArray<FMCPToolInfo> FMCPToolRegistry::GetAllTools() const
{
	// Return cached result if valid
	if (bCacheValid)
	{
		return CachedToolInfo;
	}

	// Rebuild cache
	CachedToolInfo.Empty(Tools.Num());
	for (const auto& Pair : Tools)
	{
		if (Pair.Value.IsValid())
		{
			CachedToolInfo.Add(Pair.Value->GetInfo());
		}
	}
	bCacheValid = true;

	return CachedToolInfo;
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& ToolName, const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<IMCPTool>* FoundTool = Tools.Find(ToolName);
	if (!FoundTool || !FoundTool->IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Executing MCP tool: %s"), *ToolName);

	// Execute on game thread to ensure safe access to engine objects
	FMCPToolResult Result;

	if (IsInGameThread())
	{
		Result = (*FoundTool)->Execute(Params);
	}
	else
	{
		// If called from non-game thread, dispatch to game thread and wait with timeout
		// Use shared pointers for all state to avoid use-after-free if timeout occurs
		TSharedPtr<FMCPToolResult> SharedResult = MakeShared<FMCPToolResult>();
		TSharedPtr<FEvent, ESPMode::ThreadSafe> CompletionEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(),
			[](FEvent* Event) { FPlatformProcess::ReturnSynchEventToPool(Event); });
		TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> bTaskCompleted = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

		// Use FTSTicker to dispatch to game thread at a safe point between subsystem ticks.
		// AsyncTask(GameThread) can fire during streaming manager iteration, causing
		// re-entrancy into LevelRenderAssetManagersLock (assertion crash).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("MCPTool_Execute"), 0.0f,
			[SharedResult, FoundTool, Params, CompletionEvent, bTaskCompleted](float) -> bool
		{
			*SharedResult = (*FoundTool)->Execute(Params);
			*bTaskCompleted = true;
			CompletionEvent->Trigger();
			return false; // One-shot, don't reschedule
		});

		// Wait with timeout to prevent indefinite hangs
		const uint32 TimeoutMs = UnrealClaudeConstants::MCPServer::GameThreadTimeoutMs;
		const bool bSignaled = CompletionEvent->Wait(TimeoutMs);

		if (!bSignaled || !(*bTaskCompleted))
		{
			UE_LOG(LogUnrealClaude, Error, TEXT("Tool '%s' execution timed out after %d ms"), *ToolName, TimeoutMs);
			return FMCPToolResult::Error(FString::Printf(TEXT("Tool execution timed out after %d seconds"), TimeoutMs / 1000));
		}

		// Copy result from shared storage
		Result = *SharedResult;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Tool '%s' execution %s: %s"),
		*ToolName,
		Result.bSuccess ? TEXT("succeeded") : TEXT("failed"),
		*Result.Message);

	return Result;
}

bool FMCPToolRegistry::HasTool(const FString& ToolName) const
{
	return Tools.Contains(ToolName);
}
