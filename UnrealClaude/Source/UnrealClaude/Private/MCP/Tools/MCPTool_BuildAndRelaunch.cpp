// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from yes-ue-mcp (MIT) (c) 2024 softdaddy-o.
// https://github.com/softdaddy-o/yes-ue-mcp

#include "MCPTool_BuildAndRelaunch.h"
#include "UnrealClaudeModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/App.h"

FMCPToolResult FMCPTool_BuildAndRelaunch::Execute(const TSharedRef<FJsonObject>& Params)
{
#if !PLATFORM_WINDOWS
	return FMCPToolResult::Error(TEXT("build_and_relaunch is Windows-only in v1"));
#else

	const FString BuildConfig = ExtractOptionalString(Params, TEXT("build_config"), TEXT("Development"));
	const bool bSkipRelaunch = ExtractOptionalBool(Params, TEXT("skip_relaunch"), false);

	// Validate config.
	if (BuildConfig != TEXT("Development") && BuildConfig != TEXT("Debug") && BuildConfig != TEXT("Shipping"))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Invalid build_config '%s'. Valid: Development, Debug, Shipping"), *BuildConfig));
	}

	// Step 1: Resolve engine root and Build.bat.
	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString BuildBat = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("Build.bat"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*BuildBat))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Build.bat not found at: %s"), *BuildBat));
	}

	// Step 2: Resolve .uproject path.
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), FApp::GetProjectName() + FString(TEXT(".uproject"))));
	if (!PlatformFile.FileExists(*ProjectPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT(".uproject not found at: %s"), *ProjectPath));
	}

	// Step 3: Resolve UnrealEditor.exe (sibling to Build/).
	const FString EditorExe = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("Win64"), TEXT("UnrealEditor.exe"));

	// Step 4: Build the target name. Convention: <ProjectName>Editor.
	const FString TargetName = FString(FApp::GetProjectName()) + TEXT("Editor");

	// Step 5: Compose the cmd.exe command.
	// `-WaitMutex` makes UBT wait until the editor's mutex is released.
	// `-FromMsBuild` keeps output structured.
	FString Command = FString::Printf(
		TEXT("/c \"\"%s\" %s Win64 %s -Project=\"%s\" -WaitMutex -FromMsBuild\""),
		*BuildBat, *TargetName, *BuildConfig, *ProjectPath);

	if (!bSkipRelaunch)
	{
		// Append: && start "" "<EditorExe>" "<ProjectPath>"
		// Note: outer cmd already has the /c quote; we keep things compatible by closing+rejoining.
		Command = FString::Printf(
			TEXT("/c \"\"%s\" %s Win64 %s -Project=\"%s\" -WaitMutex -FromMsBuild && start \"\" \"%s\" \"%s\"\""),
			*BuildBat, *TargetName, *BuildConfig, *ProjectPath, *EditorExe, *ProjectPath);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Spawning build: cmd.exe %s"), *Command);

	// Step 6: Spawn detached.
	uint32 PID = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"),
		*Command,
		/*bLaunchDetached=*/ true,
		/*bLaunchHidden=*/ false,
		/*bLaunchReallyHidden=*/ false,
		&PID,
		/*PriorityModifier=*/ 0,
		/*OptionalWorkingDirectory=*/ nullptr,
		/*PipeWriteChild=*/ nullptr);

	if (!Handle.IsValid())
	{
		return FMCPToolResult::Error(TEXT("Failed to spawn cmd.exe for Build.bat"));
	}
	FPlatformProcess::CloseProc(Handle);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("spawned"), true);
	Data->SetStringField(TEXT("target"), TargetName);
	Data->SetStringField(TEXT("config"), BuildConfig);
	Data->SetStringField(TEXT("project"), ProjectPath);
	Data->SetStringField(TEXT("build_bat"), BuildBat);
	Data->SetBoolField(TEXT("relaunch"), !bSkipRelaunch);
	Data->SetNumberField(TEXT("pid"), PID);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Build process spawned (PID=%u). Close the editor now to release the build mutex."), PID),
		Data);
#endif
}
