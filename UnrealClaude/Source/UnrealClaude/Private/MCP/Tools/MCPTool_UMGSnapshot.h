// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class UPanelSlot;
class UWidget;
class UWidgetBlueprint;

/**
 * MCP Tool: export a Widget Blueprint's authored state as reviewable JSON.
 *
 * == Why this exists ==
 *
 * A .uasset is an opaque binary in version control. A UI change shows up in
 * `git diff` as "Content/UI/WBP_Shop.uasset | Bin 48213 -> 48951 bytes", which
 * means UI work is the one part of the project that cannot be reviewed, cannot
 * be diffed, and cannot be spot-checked without opening the editor. Every
 * other change in the repository is readable text.
 *
 * This tool produces that text. Point it at a Widget Blueprint and it returns
 * the widget tree plus every property that differs from its class default,
 * in a stable shape that diffs cleanly between two runs.
 *
 * == Why export only, for now ==
 *
 * The upstream project this is adapted from also offers an apply-JSON
 * direction. That half is deliberately NOT ported yet: upstream's
 * implementation dispatches the work to the game thread and returns success
 * unconditionally, so a failed apply reports as a success. A write path that
 * can silently not-happen is worse than no write path, because the caller
 * stops checking. Reading is useful on its own and carries no such risk.
 *
 * == Why "only non-default properties" ==
 *
 * A UWidget has hundreds of reflected properties, nearly all untouched. A full
 * dump buries the handful a designer actually set, and — worse for diffing —
 * changes shape whenever the engine adds a property, producing noise in every
 * file at once on engine upgrade. Comparing against the CDO yields exactly the
 * authored intent.
 */
class FMCPTool_UMGSnapshot : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("umg_snapshot");
		Info.Description = TEXT(
			"Export a UMG Widget Blueprint's authored state as reviewable JSON.\n\n"
			"Returns the widget hierarchy plus, per widget, only the properties that\n"
			"differ from the class default — i.e. what a designer actually set.\n"
			"Slot (layout) properties are captured under a nested \"slot\" object.\n\n"
			"Primary use: make UI changes diffable. A .uasset is binary in git, so\n"
			"snapshotting before and after an edit turns an unreviewable blob into a\n"
			"text diff.\n\n"
			"Read-only — it never modifies the asset."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("widget_blueprint_path"), TEXT("string"),
				TEXT("Path to the WidgetBlueprint asset (e.g. /Game/UI/WBP_PaogeButton)"), true),
			FMCPToolParameter(TEXT("widget_name"), TEXT("string"),
				TEXT("Optional: snapshot only this widget and its subtree, instead of the whole tree"), false),
			FMCPToolParameter(TEXT("write_to_file"), TEXT("string"),
				TEXT("Optional: absolute path to also write the JSON to, for git-tracked review artifacts"), false)
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/**
	 * Serialize one widget and, recursively, its children.
	 *
	 * @param Widget  Widget to serialize. Null yields an invalid pointer.
	 * @return        JSON object: name, class, non-default properties, children.
	 */
	static TSharedPtr<FJsonObject> ExportWidget(UWidget* Widget);

	/**
	 * Serialize the non-default properties of a widget's layout slot.
	 *
	 * Split out from the widget's own properties because slot fields belong to
	 * the PARENT panel's slot type, not to the widget — a UButton in a Canvas
	 * and the same button in a VerticalBox carry entirely different layout
	 * fields. Keeping them in a separate object makes that ownership visible
	 * in the diff instead of implying the widget owns them.
	 *
	 * @param Slot  The widget's panel slot. May be null (root widgets).
	 * @return      JSON object, or invalid when nothing differs from default.
	 */
	static TSharedPtr<FJsonObject> ExportSlot(UPanelSlot* Slot);

	/**
	 * Collect every edit-exposed property whose value differs from the CDO.
	 *
	 * @param Object       Instance to read.
	 * @param bSkipSlot    True to skip the "Slot" property (handled separately).
	 * @return             JSON object of changed properties; may be empty.
	 */
	static TSharedPtr<FJsonObject> ExportChangedProperties(UObject* Object, bool bSkipSlot);
};
