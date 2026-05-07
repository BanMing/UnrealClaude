// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "AnimationCommonUtils.h"

#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "MovieScene.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace AnimationCommonUtils
{
    EKeyType DetectKeyType(const TSharedPtr<FJsonObject>& KeyObj)
    {
        if (!KeyObj.IsValid() || !KeyObj->HasField(TEXT("value")))
        {
            return EKeyType::Unknown;
        }

        TSharedPtr<FJsonValue> ValueField = KeyObj->TryGetField(TEXT("value"));
        if (!ValueField.IsValid())
        {
            return EKeyType::Unknown;
        }

        // Step 1. Scalar number -> Float track.
        if (ValueField->Type == EJson::Number)
        {
            return EKeyType::Float;
        }

        // Step 2. Object with r/g (and usually b/a) -> Color track.
        // Object with x/y -> Vector2D track.
        if (ValueField->Type == EJson::Object)
        {
            TSharedPtr<FJsonObject> ValueObj = ValueField->AsObject();
            if (!ValueObj.IsValid())
            {
                return EKeyType::Unknown;
            }

            if (ValueObj->HasField(TEXT("r")) && ValueObj->HasField(TEXT("g")))
            {
                return EKeyType::Color;
            }
            if (ValueObj->HasField(TEXT("x")) && ValueObj->HasField(TEXT("y")))
            {
                return EKeyType::Vector2D;
            }
        }

        return EKeyType::Unknown;
    }

    FFrameNumber SecondsToFrame(double TimeSeconds, const FFrameRate& TickResolution)
    {
        // FFrameTime supports the exact * operator we need; round to the nearest frame.
        return (TimeSeconds * TickResolution).RoundToFrame();
    }

    UWidgetAnimation* FindAnimationByName(UWidgetBlueprint* Blueprint, const FString& AnimationName)
    {
        if (!Blueprint || AnimationName.IsEmpty())
        {
            return nullptr;
        }

        for (UWidgetAnimation* Anim : Blueprint->Animations)
        {
            if (Anim && Anim->GetName() == AnimationName)
            {
                return Anim;
            }
        }
        return nullptr;
    }

    FGuid ResolveOrCreateWidgetBinding(
        UWidgetBlueprint* Blueprint,
        UWidgetAnimation* Animation,
        const FString& WidgetName,
        FString& OutError)
    {
        if (!Blueprint || !Animation)
        {
            OutError = TEXT("ResolveOrCreateWidgetBinding: Blueprint or Animation is null.");
            return FGuid();
        }

        UMovieScene* MovieScene = Animation->GetMovieScene();
        if (!MovieScene)
        {
            OutError = TEXT("Animation has no MovieScene.");
            return FGuid();
        }

        // Step 1. Search existing possessables.
        for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
        {
            const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
            if (Possessable.GetName() == WidgetName)
            {
                return Possessable.GetGuid();
            }
        }

        // Step 2. Locate the widget on the WidgetTree.
        UWidget* Widget = Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
        if (!Widget)
        {
            OutError = FString::Printf(TEXT("Widget '%s' not found in WidgetTree."), *WidgetName);
            return FGuid();
        }

        // Step 3. Ensure variable bookkeeping is intact (UMG compiler requirement).
        bool bRecompile = false;
        if (!Widget->bIsVariable)
        {
            Widget->bIsVariable = true;
            bRecompile = true;
        }
        if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
        {
            Blueprint->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
            bRecompile = true;
        }
        if (bRecompile)
        {
            Blueprint->Modify();
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
        }

        // Step 4. Add possessable + bind it on the animation.
        FGuid NewGuid = MovieScene->AddPossessable(WidgetName, Widget->GetClass());

        FWidgetAnimationBinding NewBinding;
        NewBinding.WidgetName = FName(*WidgetName);
        NewBinding.AnimationGuid = NewGuid;
        NewBinding.bIsRootWidget = (Blueprint->WidgetTree && Widget == Blueprint->WidgetTree->RootWidget);
        Animation->AnimationBindings.Add(NewBinding);

        return NewGuid;
    }

    void RefreshAfterWriteOp(UWidgetBlueprint* Blueprint)
    {
        if (!Blueprint)
        {
            return;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

        if (GEditor)
        {
            if (UAssetEditorSubsystem* AssetSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            {
                AssetSub->OpenEditorForAsset(Blueprint);
            }
        }
    }
}
