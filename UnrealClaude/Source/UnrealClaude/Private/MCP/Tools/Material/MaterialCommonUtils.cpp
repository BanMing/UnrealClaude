// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MaterialCommonUtils.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"

#include "EdGraph/EdGraphNode.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    /** Process-wide cached target material. Mirrors UmgMcp's TargetMaterial slot. */
    static TWeakObjectPtr<UMaterial> GCachedTargetMaterial;
}

UMaterial* MaterialCommonUtils::GetCachedTarget()
{
    if (GCachedTargetMaterial.IsValid())
    {
        return GCachedTargetMaterial.Get();
    }

    // Fallback: pick up a material currently being edited (parity with UmgMcp).
    if (GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
            for (UObject* Asset : EditedAssets)
            {
                if (UMaterial* Mat = Cast<UMaterial>(Asset))
                {
                    GCachedTargetMaterial = Mat;
                    return Mat;
                }
            }
        }
    }
    return nullptr;
}

void MaterialCommonUtils::SetCachedTarget(UMaterial* Mat)
{
    GCachedTargetMaterial = Mat;
}

UMaterial* MaterialCommonUtils::ResolveOrCreateMaterial(const FString& AssetPath, bool bCreateIfNotFound, FString& OutStatus)
{
    UMaterial* TargetMat = nullptr;

    // Step 1. Prefer the live editor instance (so graph edits hit the same UMaterial the user sees).
    if (GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            FSoftObjectPath ObjectPath(AssetPath);
            UObject* AssetObj = ObjectPath.ResolveObject();
            if (AssetObj)
            {
                if (AssetEditorSubsystem->FindEditorForAsset(AssetObj, false))
                {
                    TargetMat = Cast<UMaterial>(AssetObj);
                    if (TargetMat)
                    {
                        OutStatus = FString::Printf(TEXT("Using live editor instance for %s"), *AssetPath);
                        return TargetMat;
                    }
                }
            }
        }
    }

    // Step 2. Fallback: load from disk.
    TargetMat = LoadObject<UMaterial>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);
    if (TargetMat)
    {
        OutStatus = FString::Printf(TEXT("Loaded material %s"), *AssetPath);
        return TargetMat;
    }

    if (!bCreateIfNotFound)
    {
        OutStatus = FString::Printf(TEXT("Material not found and create_if_missing=false: %s"), *AssetPath);
        return nullptr;
    }

    // Step 3. Create a default UI/translucent material when allowed.
    if (!FPackageName::IsValidObjectPath(AssetPath))
    {
        OutStatus = FString::Printf(TEXT("Invalid asset path: %s"), *AssetPath);
        return nullptr;
    }

    const FString ShortName = FPackageName::GetShortName(AssetPath);
    UPackage* Package = CreatePackage(*AssetPath);
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();

    UMaterial* NewMat = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(),
        Package,
        *ShortName,
        RF_Public | RF_Standalone,
        nullptr,
        GWarn));

    if (!NewMat)
    {
        OutStatus = FString::Printf(TEXT("Failed to create material at %s"), *AssetPath);
        return nullptr;
    }

    NewMat->MaterialDomain = MD_UI;
    NewMat->BlendMode = BLEND_Translucent;

    FAssetRegistryModule::AssetCreated(NewMat);
    NewMat->MarkPackageDirty();

    OutStatus = FString::Printf(TEXT("Created new UI/translucent material at %s"), *AssetPath);
    return NewMat;
}

UMaterialExpression* MaterialCommonUtils::FindExpressionByHandle(UMaterial* Mat, const FString& Handle)
{
    if (!Mat)
    {
        return nullptr;
    }

    for (UMaterialExpression* Expr : Mat->GetExpressions())
    {
        if (!Expr)
        {
            continue;
        }
        if (Expr->GetName() == Handle)
        {
            return Expr;
        }
        if (Expr->Desc.Equals(Handle, ESearchCase::IgnoreCase))
        {
            return Expr;
        }
    }
    return nullptr;
}

bool MaterialCommonUtils::IsRootHandle(UMaterial* Mat, const FString& Handle)
{
    if (Handle.StartsWith(TEXT("Master")))
    {
        return true;
    }
    if (Handle.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (Handle.Equals(TEXT("MaterialRoot")))
    {
        return true;
    }
    return Mat && Handle.Equals(Mat->GetName());
}

FExpressionInput* MaterialCommonUtils::FindInputProperty(UObject* Owner, const FString& PinName)
{
    if (!Owner)
    {
        return nullptr;
    }

    TArray<UObject*> Targets;
#if WITH_EDITOR
    if (UMaterial* Mat = Cast<UMaterial>(Owner))
    {
        if (Mat->GetEditorOnlyData())
        {
            Targets.Add((UObject*)Mat->GetEditorOnlyData());
        }
    }
#endif
    Targets.Add(Owner);

    FString SearchName = PinName.TrimStartAndEnd().Replace(TEXT(" "), TEXT(""));

    if (UMaterial* Mat = Cast<UMaterial>(Owner))
    {
        if (SearchName.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
        {
            SearchName = (Mat->MaterialDomain == MD_UI) ? TEXT("EmissiveColor") : TEXT("BaseColor");
        }
        else if (SearchName.Equals(TEXT("FinalColor"), ESearchCase::IgnoreCase))
        {
            SearchName = TEXT("EmissiveColor");
        }
        else if (SearchName.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
        {
            SearchName = TEXT("Opacity");
        }
        else if (SearchName.Equals(TEXT("OpacityMask"), ESearchCase::IgnoreCase))
        {
            SearchName = TEXT("OpacityMask");
        }
        else if (SearchName.Equals(TEXT("WorldPositionOffset"), ESearchCase::IgnoreCase))
        {
            SearchName = TEXT("WorldPositionOffset");
        }
    }

    for (UObject* Target : Targets)
    {
        for (TFieldIterator<FProperty> PropIt(Target->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            const FString PropName = Prop->GetName();

            bool bMatch = PropName.Equals(SearchName, ESearchCase::IgnoreCase) ||
                Prop->GetDisplayNameText().ToString().Equals(SearchName, ESearchCase::IgnoreCase);

            if (!bMatch)
            {
                if (SearchName.Equals(TEXT("UV"), ESearchCase::IgnoreCase) && PropName.Equals(TEXT("Coordinates"), ESearchCase::IgnoreCase))
                {
                    bMatch = true;
                }
                if (SearchName.Equals(TEXT("Alpha"), ESearchCase::IgnoreCase) && PropName.Equals(TEXT("A"), ESearchCase::IgnoreCase))
                {
                    bMatch = true;
                }
            }

            if (!bMatch)
            {
                continue;
            }

            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                if (StructProp->Struct)
                {
                    const FString StructName = StructProp->Struct->GetName();
                    if (StructName.Equals(TEXT("ExpressionInput")) ||
                        StructName.Equals(TEXT("ScalarMaterialInput")) ||
                        StructName.Equals(TEXT("VectorMaterialInput")) ||
                        StructName.Equals(TEXT("ColorMaterialInput")) ||
                        StructName.Contains(TEXT("MaterialInput")))
                    {
                        return StructProp->ContainerPtrToValuePtr<FExpressionInput>(Target);
                    }
                }
            }
        }
    }
    return nullptr;
}

UEdGraphNode* MaterialCommonUtils::FindRootGraphNode(UMaterial* Mat)
{
    if (!Mat || !Mat->MaterialGraph)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Mat->MaterialGraph->Nodes)
    {
        if (Node && Node->IsA(UMaterialGraphNode_Root::StaticClass()))
        {
            return Node;
        }
    }
    return nullptr;
}

void MaterialCommonUtils::ForceRefreshMaterialEditor(UMaterial* Mat)
{
    if (!Mat)
    {
        return;
    }

    Mat->Modify();
    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    if (GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            if (IAssetEditorInstance* Ed = AssetEditorSubsystem->FindEditorForAsset(Mat, false))
            {
                IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(Ed);
                MaterialEditor->NotifyExternalMaterialChange();
                MaterialEditor->UpdateMaterialAfterGraphChange();
            }
        }
    }
}
