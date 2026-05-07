// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: UMG session anchor — get/set the implicit "current widget" used as
 * a fallback by the other UMG tools when widget_blueprint_path is omitted.
 *
 * Operations:
 *  - get_target          → { current_target }
 *  - set_target          → validate + write subsystem state, push into history
 *  - get_last_edited     → { last_edited }   (history[0])
 *  - get_recently_edited → { recent: [path, ...] }  (LIFO, capped)
 *
 * Token-saving: after one set_target call, follow-up umg_query / umg_modify /
 * umg_animation calls can drop the widget_blueprint_path parameter.
 */
class FMCPTool_UMGSession : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("umg_session");
		Info.Description = TEXT(
			"Manage the UMG session anchor — the implicit current widget used by\n"
			"other UMG tools when widget_blueprint_path is omitted.\n\n"
			"Operations:\n"
			"  get_target           - return the current anchored widget asset path\n"
			"  set_target           - set the current anchor (asset_path required)\n"
			"  get_last_edited      - return the most recently edited widget blueprint\n"
			"  get_recently_edited  - return up to max_count recent paths (LIFO)\n\n"
			"State is in-memory only; it does not persist across editor restarts."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("One of: get_target | set_target | get_last_edited | get_recently_edited"), true),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"),
				TEXT("Widget blueprint asset path for set_target (e.g. /Game/UI/WBP_PaogeCombatHUD)"), false),
			FMCPToolParameter(TEXT("max_count"), TEXT("number"),
				TEXT("Maximum entries returned by get_recently_edited (default 5)"), false, TEXT("5"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
