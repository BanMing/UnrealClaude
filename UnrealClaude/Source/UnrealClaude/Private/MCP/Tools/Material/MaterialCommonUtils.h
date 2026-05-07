// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UMaterial;
class UMaterialExpression;
class UEdGraphNode;
struct FExpressionInput;

namespace MaterialCommonUtils
{
    /** Resolve the cached "current target" material; returns nullptr if not set. */
    UMaterial* GetCachedTarget();

    /** Set the cached target material. Pass nullptr to clear. */
    void SetCachedTarget(UMaterial* Mat);

    /**
     * Resolve a material asset by path.
     * Behaviour parity with UmgMcp: prefer the live editor instance; fall back to LoadObject;
     * optionally create a new UI/translucent material if missing.
     */
    UMaterial* ResolveOrCreateMaterial(const FString& AssetPath, bool bCreateIfNotFound, FString& OutStatus);

    /** Find a material expression on Mat by its FName or Desc tag. */
    UMaterialExpression* FindExpressionByHandle(UMaterial* Mat, const FString& Handle);

    /** True when Handle refers to the material's root output (Master / Output / MaterialRoot / asset name). */
    bool IsRootHandle(UMaterial* Mat, const FString& Handle);

    /**
     * Locate the FExpressionInput on the material root (or any expression) for a given pin name.
     * Applies smart aliasing for the root node:
     *   "Output"/"FinalColor"/"最终颜色" -> EmissiveColor (UI domain) or BaseColor (default)
     *   "Opacity"/"不透明度", "OpacityMask"/"不透明度蒙版" pass through.
     */
    FExpressionInput* FindInputProperty(UObject* Owner, const FString& PinName);

    /** Locate the UEdGraphNode_Root inside Mat->MaterialGraph; nullptr if not present. */
    UEdGraphNode* FindRootGraphNode(UMaterial* Mat);

    /** Refresh the open material editor (if any) after a mutation. */
    void ForceRefreshMaterialEditor(UMaterial* Mat);
}
