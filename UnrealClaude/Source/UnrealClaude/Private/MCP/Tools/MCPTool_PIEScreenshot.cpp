// Copyright Ban Ming. All Rights Reserved.

#include "MCPTool_PIEScreenshot.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

/**
 * Queue a Slate-composited PIE screenshot.
 *
 * Steps:
 *   1. Validate GEditor and that a PIE viewport is active. Without an active
 *      PIE viewport the FScreenshotRequest::OnScreenshotCaptured delegate
 *      never fires because UGameViewportClient::ProcessScreenShots is the
 *      function that drains the queue, and it only runs inside a PIE/game
 *      viewport tick. Failing fast is more useful than silently queueing
 *      a screenshot that will never be written.
 *   2. Call FScreenshotRequest::RequestScreenshot("", bShowUI=true,
 *      bAddFilenameSuffix=true). The empty filename triggers UE's default
 *      ScreenShot00000.png naming inside Saved/Screenshots/<Platform>/.
 *      bShowUI=true is the load-bearing flag: it tells the renderer to
 *      capture from the post-Slate backbuffer instead of the pre-Slate
 *      scene color, which is what makes HUD/UMG widgets appear in the PNG.
 *   3. Return immediately with the directory path so the Python driver
 *      can poll for the new file. Blocking inside Execute() would prevent
 *      the game thread from ticking, which would prevent the screenshot
 *      from ever being captured (the capture happens on the next tick).
 */
FMCPToolResult FMCPTool_PIEScreenshot::Execute(const TSharedRef<FJsonObject>& /*Params*/)
{
	// Step 1 — sanity: editor + PIE viewport.
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor is not available."));
	}

	FViewport* PIEViewport = GEditor->GetPIEViewport();
	if (!PIEViewport)
	{
		return FMCPToolResult::Error(TEXT(
			"No PIE viewport is active. Start a PIE session before requesting a "
			"PIE screenshot — FScreenshotRequest only drains inside the game "
			"viewport tick, not the editor viewport."));
	}

	// Step 2 — queue the request. Filename="" -> default Saved/Screenshots path
	// with auto-incremented suffix. bShowUI=true is the critical bit that pulls
	// from the post-Slate backbuffer so HUD widgets are included.
	FScreenshotRequest::RequestScreenshot(
		/*Filename=*/ FString(),
		/*bInShowUI=*/ true,
		/*bAddFilenameSuffix=*/ true
	);

	// Step 3 — build the directory path we tell the caller to poll. In Editor
	// builds UE writes to Saved/Screenshots/WindowsEditor/<file>.png on Win64
	// (the subfolder is "<Platform>Editor" derived from FPlatformProperties).
	const FString ScreenshotDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("WindowsEditor"));

	// Step 4 — return the path + a clear flag so the driver knows the request
	// was queued (not the file's existence — that's the poller's job).
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("screenshot_dir"), ScreenshotDir);
	ResultData->SetBoolField(TEXT("requested"), true);
	ResultData->SetStringField(TEXT("note"), TEXT(
		"PNG will appear in screenshot_dir within ~1 frame. Caller polls for the "
		"new file (filename diff against pre-call snapshot)."));

	UE_LOG(LogUnrealClaude, Log, TEXT(
		"capture_pie_screenshot: queued FScreenshotRequest(bShowUI=true). "
		"PNG will land in %s"), *ScreenshotDir);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Queued PIE screenshot; PNG will appear in %s"), *ScreenshotDir),
		ResultData
	);
}
