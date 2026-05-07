// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"

class UWidgetBlueprint;
class UWidgetAnimation;
class UMovieScene;
class UWidget;

/**
 * Shared helpers for the MCP UMG Animation tool.
 *
 * Why these live here:
 *   - The animation surface is large (10 operations) and the per-operation code
 *     repeatedly needs to (a) detect the value-type of an incoming JSON key,
 *     (b) convert wall-clock seconds into MovieScene frames using the actual
 *     TickResolution (NOT a hardcoded 60Hz), and (c) resolve a widget binding
 *     GUID, optionally creating a possessable + animation binding when missing.
 *   - Centralizing them keeps the tool dispatcher readable and ensures every
 *     operation uses the same bind-or-create pipeline.
 *
 * All helpers are stateless. The MCP layer for animation is fully stateless:
 * widget_blueprint_path / animation_name / widget_name are always required.
 */
namespace AnimationCommonUtils
{
    /**
     * Discriminator for the three keyframe value layouts we accept.
     *
     * Detection rule (see DetectKeyType):
     *   value: number                              -> Float
     *   value: { r:..., g:..., b:..., a:... }      -> Color
     *   value: { x:..., y:... }                    -> Vector2D
     *   anything else                              -> Unknown
     */
    enum class EKeyType : uint8
    {
        Float,
        Vector2D,
        Color,
        Unknown
    };

    /**
     * Inspect a single JSON key descriptor (must contain a "value" field) and
     * decide which track type the caller should write to.
     *
     * @param KeyObj  JSON descriptor of one key, shape { "time": ..., "value": ... }.
     * @return        Detected EKeyType, or EKeyType::Unknown if the value shape
     *                does not match any of the three supported layouts.
     */
    EKeyType DetectKeyType(const TSharedPtr<FJsonObject>& KeyObj);

    /**
     * Convert wall-clock seconds into a MovieScene frame number using the
     * provided tick resolution.
     *
     * Why this helper exists: MovieScene tick resolution is a runtime value
     * (typically 60000 fps in 5.x). Hardcoding 60 here breaks the math and
     * places keys at the wrong absolute frame. Always go through this helper.
     *
     * @param TimeSeconds      Time in seconds (matches the JSON contract).
     * @param TickResolution   MovieScene->GetTickResolution() of the target scene.
     * @return                 Rounded FFrameNumber.
     */
    FFrameNumber SecondsToFrame(double TimeSeconds, const FFrameRate& TickResolution);

    /**
     * Look up a widget animation on a widget blueprint by name.
     *
     * @param Blueprint       Owning widget blueprint (must be valid).
     * @param AnimationName   Animation FName-as-string to search for.
     * @return                Pointer to the matching UWidgetAnimation, or nullptr.
     */
    UWidgetAnimation* FindAnimationByName(UWidgetBlueprint* Blueprint, const FString& AnimationName);

    /**
     * Resolve (or create) a possessable binding for the named widget inside
     * the given animation.
     *
     * Steps:
     *   1. Search MovieScene possessables for a possessable whose name equals
     *      WidgetName; if found, return its GUID.
     *   2. Otherwise locate the widget on Blueprint->WidgetTree.
     *   3. Ensure bIsVariable + WidgetVariableNameToGuidMap entry are present
     *      (UMG compiler requires both); recompile blueprint if we just added
     *      either one.
     *   4. Call MovieScene->AddPossessable + push a FWidgetAnimationBinding so
     *      the widget shows up in the editor's track view.
     *
     * @param Blueprint       Owning widget blueprint.
     * @param Animation       Target animation (must own the MovieScene).
     * @param WidgetName      Widget FName-as-string (case-sensitive).
     * @param OutError        Receives an error string when binding cannot be
     *                        established (e.g. widget not found in tree).
     * @return                Valid FGuid on success; invalid FGuid on failure
     *                        (in which case OutError is populated).
     */
    FGuid ResolveOrCreateWidgetBinding(
        UWidgetBlueprint* Blueprint,
        UWidgetAnimation* Animation,
        const FString& WidgetName,
        FString& OutError);

    /**
     * Force a structural blueprint dirty + asset editor refresh so the
     * Sequencer panel picks up the changes we just made.
     *
     * @param Blueprint  Owning widget blueprint (must be valid).
     */
    void RefreshAfterWriteOp(UWidgetBlueprint* Blueprint);
}
