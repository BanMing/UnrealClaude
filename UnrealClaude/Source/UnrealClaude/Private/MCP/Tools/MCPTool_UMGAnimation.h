// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: edit UMG widget animation keyframes (Sequencer surface).
 *
 * Operations:
 *   get_all_animations         - list every UWidgetAnimation on a blueprint
 *                                (optional `detailed: true` adds per-widget track + key-count breakdown)
 *   create_animation           - find-or-create a UWidgetAnimation by name
 *   delete_animation           - remove a named animation (requires confirm_delete)
 *   get_animation_keyframes    - dump every track + key for the given animation
 *   get_widget_animation_data  - per-widget timeline (filtered float/color/vector2d)
 *   set_property_keys          - upsert keys on a property track (auto-detects type)
 *   remove_property_track      - drop the entire track for a property
 *   remove_keys                - delete keys at specific time(s) on a property track
 *   append_widget_tracks       - batch wrapper for set_property_keys (per widget)
 *   set_animation_data         - L2 batch wrapper (widget + tracks list)
 *   sample_at_time             - evaluate every track at one or more query times (interpolated)
 *   append_time_slice          - write keys for many widgets+properties at a single time
 *
 * State model: fully stateless. Every operation requires:
 *   - widget_blueprint_path
 *   - animation_name (where applicable)
 *   - widget_name    (where applicable)
 *
 * Track type detection (for set_property_keys + append_widget_tracks):
 *   - keys[*].value: number               -> UMovieSceneFloatTrack
 *   - keys[*].value: { r,g,b,a }          -> UMovieSceneColorTrack
 *   - keys[*].value: { x,y }              -> UMovieSceneDoubleVectorTrack(NumChannels=2)
 *
 * Frame conversion uses MovieScene->GetTickResolution() (not 60Hz hardcoded).
 */
class FMCPTool_UMGAnimation : public FMCPToolBase
{
public:
    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("umg_animation");
        Info.Description = TEXT(
            "Edit UMG widget animations (Sequencer surface). Supports listing, "
            "create/delete animations, and upsert/remove of property keys on "
            "Float/Color/Vector2D tracks.\n\n"
            "Track type is auto-detected from keys[*].value shape. Times are in "
            "seconds; frame conversion uses MovieScene's TickResolution.");

        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: get_all_animations | create_animation | delete_animation | "
                     "get_animation_keyframes | get_widget_animation_data | set_property_keys | "
                     "remove_property_track | remove_keys | append_widget_tracks | set_animation_data | "
                     "sample_at_time | append_time_slice"),
                true),
            FMCPToolParameter(TEXT("widget_blueprint_path"), TEXT("string"),
                TEXT("UWidgetBlueprint asset path (e.g. /Game/UI/WBP_PaogeBattleHUD)"), true),
            FMCPToolParameter(TEXT("animation_name"), TEXT("string"),
                TEXT("Animation name (required by every operation that targets an animation)"), false),
            FMCPToolParameter(TEXT("widget_name"), TEXT("string"),
                TEXT("Widget FName under WidgetTree; required for property-keys operations"), false),
            FMCPToolParameter(TEXT("property_name"), TEXT("string"),
                TEXT("Property name to key (e.g. RenderTransform.Translation, ColorAndOpacity)"), false),
            FMCPToolParameter(TEXT("keys"), TEXT("array"),
                TEXT("Array of {time, value}. value is number / {r,g,b,a} / {x,y}"), false),
            FMCPToolParameter(TEXT("times"), TEXT("array"),
                TEXT("remove_keys: array of times (seconds) to delete"), false),
            FMCPToolParameter(TEXT("time"), TEXT("number"),
                TEXT("remove_keys: single time (seconds) shortcut for `times`"), false),
            FMCPToolParameter(TEXT("tracks"), TEXT("array"),
                TEXT("append_widget_tracks / set_animation_data: array of {property, keys}"), false),
            FMCPToolParameter(TEXT("confirm_delete"), TEXT("boolean"),
                TEXT("delete_animation / remove_property_track / remove_keys: must be true to authorize destructive ops"),
                false, TEXT("false")),
            FMCPToolParameter(TEXT("property_filter"), TEXT("string"),
                TEXT("get_widget_animation_data: optional property name to narrow output"), false),
            FMCPToolParameter(TEXT("detailed"), TEXT("boolean"),
                TEXT("get_all_animations: when true, include per-widget per-property track summary + keys_count"),
                false, TEXT("false")),
            FMCPToolParameter(TEXT("widgets"), TEXT("array"),
                TEXT("append_time_slice: array of {widget_name, properties:{prop:value,...}}"), false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    // --- Read operations ---
    FMCPToolResult ExecuteGetAllAnimations(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetAnimationKeyframes(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteGetWidgetAnimationData(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSampleAtTime(const TSharedRef<FJsonObject>& Params);

    // --- Write operations ---
    FMCPToolResult ExecuteCreateAnimation(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteDeleteAnimation(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetPropertyKeys(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteRemovePropertyTrack(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteRemoveKeys(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteAppendWidgetTracks(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteSetAnimationData(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult ExecuteAppendTimeSlice(const TSharedRef<FJsonObject>& Params);
};
