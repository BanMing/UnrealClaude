// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWidgetBlueprint;
class UWidget;
class UPanelWidget;

/**
 * Shared helpers for the UMG Query / Modify / Animation MCP tools.
 *
 * The UMG MCP surface is split across several tools but they share a small
 * number of load-bearing primitives:
 *   - Resolving a Widget Blueprint asset from an MCP path string.
 *   - Resolving a UWidget subclass name with engine-vs-game fallbacks.
 *   - Walking the WidgetTree.
 *   - Normalizing JSON keys camelCase -> PascalCase before reflection apply.
 *
 * All helpers are stateless. Every entry point requires the caller to pass
 * the explicit Widget Blueprint path (the MCP layer is fully stateless).
 */
namespace UMGCommonUtils
{
    /**
     * Resolve and load a UWidgetBlueprint by package path.
     *
     * Steps:
     *   1. Validate the path is non-empty and starts with /Game or /Script.
     *   2. LoadObject<UWidgetBlueprint> on the path.
     *   3. If load failed, populate OutError with a useful diagnostic and return nullptr.
     *
     * @param BlueprintPath  Object path (e.g. "/Game/UI/WBP_PaogeHUD").
     * @param OutError       Receives an error string when the asset cannot be loaded.
     * @return               The loaded widget blueprint, or nullptr on failure.
     */
    UWidgetBlueprint* LoadWidgetBlueprint(const FString& BlueprintPath, FString& OutError);

    /**
     * Resolve a UWidget subclass given a free-form type spec.
     *
     * Resolution order (4-tier fallback, from most specific to most generic):
     *   1. Contains '/' -> treat as full asset path; LoadObject / FindObject
     *   2. Starts with /Game/ -> append "_C" if missing, then LoadClass
     *   3. /Script/UMG.<TypeName>            (native class shorthand)
     *   4. /Script/UMG.U<TypeName>           (auto-prefix UMG U-class names)
     *
     * @param WidgetType  Type name supplied by the LLM (e.g. "VerticalBox", "/Game/UI/WBP_Foo").
     * @return            Resolved UClass, or nullptr if every fallback failed.
     */
    UClass* ResolveWidgetClass(const FString& WidgetType);

    /**
     * Find a widget by FName inside a Widget Blueprint's WidgetTree.
     *
     * @param WidgetBlueprint  Owning blueprint (must be valid).
     * @param WidgetName       FName of the widget to find.
     * @return                 The widget if present, otherwise nullptr.
     */
    UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FName& WidgetName);

    /**
     * Recursively serialize a widget subtree into a JSON object.
     *
     * Output schema:
     *   { "name": ..., "type": ..., "is_variable": bool, "children": [ ... ] }
     *
     * Slot data is intentionally NOT included here; callers that need it must
     * use ExportSlotToJson on the parent's child link.
     *
     * @param Widget  Root of the subtree (may be nullptr -> returns empty object).
     * @return        Newly-allocated JSON object (never null).
     */
    TSharedPtr<FJsonObject> ExportWidgetTreeToJson(UWidget* Widget);

    /**
     * Serialize a widget's properties (and its slot if it has one) into JSON.
     *
     * Why this exists separately from ExportWidgetTreeToJson:
     *   - Tree exports stay shallow for token efficiency.
     *   - Property dumps are heavier and only needed on demand.
     *
     * @param Widget          Target widget (must be valid).
     * @param OutObject       Receives serialized properties (function fills it in-place).
     */
    void ExportWidgetPropertiesToJson(UWidget* Widget, const TSharedRef<FJsonObject>& OutObject);

    /**
     * Recursively walk a JSON object/array and rewrite every key into the
     * canonical PascalCase form used by UMG reflection.
     *
     * Rules:
     *   - Lookup in PropertyNameMappings::GetForwardMappings first.
     *   - Otherwise capitalize the first character.
     *   - Recurse into nested objects and arrays.
     *
     * Mutates the JSON tree in place.
     *
     * @param JsonObject  Root object to normalize.
     */
    void NormalizeJsonKeysToPascalCase(const TSharedPtr<FJsonObject>& JsonObject);

    /**
     * Expand convenience aliases that the LLM commonly uses for CanvasPanelSlot
     * layout into the underlying nested struct paths the engine expects.
     *
     * Aliases handled:
     *   Slot.Position [x,y]   -> Slot.LayoutData.Offsets.{Left, Top}
     *   Slot.Size     [x,y]   -> Slot.LayoutData.Offsets.{Right, Bottom}
     *   Slot.Anchors          -> Slot.LayoutData.Anchors
     *   Slot.Alignment [x,y]  -> Slot.LayoutData.Alignment.{X, Y}
     *
     * Unmatched keys are left untouched.
     *
     * @param JsonObject  Root JSON object containing a "Slot" subobject (or not).
     */
    void ExpandCanvasSlotAliases(const TSharedPtr<FJsonObject>& JsonObject);

    /**
     * Apply a JSON object to an existing UObject's UPROPERTY fields via reflection.
     *
     * Special handling:
     *   - FSlateBrush.ResourceObject is intercepted (FJsonObjectConverter cannot
     *     resolve UObject* references safely). The string is treated as an asset
     *     path, LoadObject is called, and the result is written via FObjectProperty.
     *
     * Steps:
     *   1. Normalize keys to PascalCase.
     *   2. Walk top-level fields.
     *   3. For each known special-case key, call out to a custom setter.
     *   4. For everything else, defer to FJsonObjectConverter::JsonObjectToUStruct.
     *
     * @param JsonObject     Source JSON values (mutated by NormalizeJsonKeys).
     * @param TargetObject   UObject to apply onto (must be valid).
     * @param OutErrors      Receives one error string per property that failed to apply.
     * @return               true if at least one property applied without error.
     */
    bool ApplyJsonToObject(
        const TSharedPtr<FJsonObject>& JsonObject,
        UObject* TargetObject,
        TArray<FString>& OutErrors);
}
