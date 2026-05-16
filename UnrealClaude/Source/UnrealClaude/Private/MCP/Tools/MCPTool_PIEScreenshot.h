// Copyright Ban Ming. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Request a PIE screenshot WITH UMG/HUD widgets composited into the frame.
 *
 * Why this exists: `capture_viewport` uses `FViewport::ReadPixels` and HighResShot
 * uses `FViewport::TakeHighResScreenShot` — both pipelines capture the 3D scene
 * BEFORE Slate composition, so HUD widgets are stripped from the output. Only
 * `FScreenshotRequest::RequestScreenshot(Filename, bShowUI=true, ...)` routes
 * through the backbuffer-after-Slate path that the F9 keypress hits, which is
 * the only UE 5.x API that includes UMG widgets in PIE screenshots.
 *
 * Semantics:
 * - Queues the screenshot via `FScreenshotRequest::RequestScreenshot` and returns
 *   immediately. The actual PNG is written by the render thread on the next frame.
 * - Caller is expected to poll the screenshot directory for the new file. This
 *   keeps the tool simple and avoids blocking the game thread inside Execute().
 * - The screenshot lands in `<ProjectDir>/Saved/Screenshots/WindowsEditor/` with
 *   an auto-suffixed filename (ScreenShot00000.png, ScreenShot00001.png, ...).
 *
 * Parameters: none required. (Future: optional filename override, resolution.)
 *
 * Returns: { screenshot_dir, requested: true } so the caller knows where to poll.
 */
class FMCPTool_PIEScreenshot : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("capture_pie_screenshot");
		Info.Description = TEXT(
			"Request a PIE screenshot WITH UMG/HUD widgets composited into the frame.\n\n"
			"This is the only MCP capture path that includes Slate widgets. Use this "
			"(NOT capture_viewport or HighResShot) when you need to capture HUDs, menus, "
			"or any UMG-based UI in a PIE session. Routes through "
			"FScreenshotRequest::RequestScreenshot(bShowUI=true), the same path the F9 "
			"keypress uses.\n\n"
			"Output: A PNG file written asynchronously to "
			"<ProjectDir>/Saved/Screenshots/WindowsEditor/ with an auto-incremented "
			"suffix. Caller should poll the directory for the new file (typically "
			"appears within ~1 frame, ~16-33ms at 30-60fps).\n\n"
			"Returns: { screenshot_dir: string, requested: bool } — the path where the "
			"PNG will appear, NOT the file itself."
		);
		Info.Parameters = {};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
