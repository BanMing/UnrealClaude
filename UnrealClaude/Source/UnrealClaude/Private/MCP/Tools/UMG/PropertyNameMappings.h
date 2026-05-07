// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"

/**
 * UMG property-name mapping helpers.
 *
 * Why this file exists:
 * Unreal's JSON serializers (FJsonObjectConverter etc.) emit field names in
 * camelCase, but FProperty reflection expects PascalCase. This file maps the
 * known camelCase keys back to their canonical PascalCase reflection names so
 * we can route incoming JSON to UPROPERTY setters reliably.
 *
 * Append new mappings whenever a UMG property is observed to mismatch.
 */
namespace UMGPropertyNameMappings
{
    /**
     * Forward mapping (camelCase -> PascalCase). Static-init once, returned by reference.
     * @return Reference to the immutable mapping table.
     */
    inline const TMap<FString, FString>& GetForwardMappings()
    {
        static const TMap<FString, FString> Mappings = {
            // Slot.Size struct properties
            {TEXT("sizeRule"), TEXT("SizeRule")},
            {TEXT("value"), TEXT("Value")},

            // Slot alignment properties
            {TEXT("horizontalAlignment"), TEXT("HorizontalAlignment")},
            {TEXT("verticalAlignment"), TEXT("VerticalAlignment")},

            // Padding / margin
            {TEXT("padding"), TEXT("Padding")},
            {TEXT("left"), TEXT("Left")},
            {TEXT("top"), TEXT("Top")},
            {TEXT("right"), TEXT("Right")},
            {TEXT("bottom"), TEXT("Bottom")},

            // Color channels
            {TEXT("r"), TEXT("R")},
            {TEXT("g"), TEXT("G")},
            {TEXT("b"), TEXT("B")},
            {TEXT("a"), TEXT("A")},

            // Font / text properties
            {TEXT("size"), TEXT("Size")},
            {TEXT("typefaceFontName"), TEXT("TypefaceFontName")},

            // Common UWidget properties
            {TEXT("isEnabled"), TEXT("IsEnabled")},
            {TEXT("visibility"), TEXT("Visibility")},
            {TEXT("renderOpacity"), TEXT("RenderOpacity")},
            {TEXT("toolTipText"), TEXT("ToolTipText")},
        };
        return Mappings;
    }

    /**
     * Reverse mapping (PascalCase -> camelCase). Used when emitting JSON that
     * external schemas expect in camelCase form.
     * @return Reference to the immutable reverse mapping table.
     */
    inline const TMap<FString, FString>& GetReverseMappings()
    {
        static const TMap<FString, FString> Reverse = {
            {TEXT("SizeRule"), TEXT("sizeRule")},
            {TEXT("Value"), TEXT("value")},
            {TEXT("HorizontalAlignment"), TEXT("horizontalAlignment")},
            {TEXT("VerticalAlignment"), TEXT("verticalAlignment")},
            {TEXT("Padding"), TEXT("padding")},
            {TEXT("Left"), TEXT("left")},
            {TEXT("Top"), TEXT("top")},
            {TEXT("Right"), TEXT("right")},
            {TEXT("Bottom"), TEXT("bottom")},
            {TEXT("R"), TEXT("r")},
            {TEXT("G"), TEXT("g")},
            {TEXT("B"), TEXT("b")},
            {TEXT("A"), TEXT("a")},
            {TEXT("Size"), TEXT("size")},
            {TEXT("TypefaceFontName"), TEXT("typefaceFontName")},
            {TEXT("IsEnabled"), TEXT("isEnabled")},
            {TEXT("Visibility"), TEXT("visibility")},
            {TEXT("RenderOpacity"), TEXT("renderOpacity")},
            {TEXT("ToolTipText"), TEXT("toolTipText")},
        };
        return Reverse;
    }

    /**
     * Normalize a single key: prefer the explicit map, otherwise upper-case the
     * leading character (the camelCase->PascalCase common case).
     *
     * Steps:
     *   1. Look up Key in the forward mapping table
     *   2. If found, return the mapped PascalCase form
     *   3. Otherwise, capitalize the first character if it is lower-case
     *
     * @param Key  Original property name (camelCase or PascalCase).
     * @return     PascalCase form suitable for FProperty reflection lookup.
     */
    inline FString NormalizePropertyName(const FString& Key)
    {
        const TMap<FString, FString>& Mappings = GetForwardMappings();
        if (const FString* Mapped = Mappings.Find(Key))
        {
            return *Mapped;
        }

        FString Out = Key;
        if (Out.Len() > 0 && FChar::IsLower(Out[0]))
        {
            Out[0] = FChar::ToUpper(Out[0]);
        }
        return Out;
    }
}
