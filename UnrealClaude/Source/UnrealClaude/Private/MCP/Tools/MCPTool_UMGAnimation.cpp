// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "MCPTool_UMGAnimation.h"
#include "UMG/UMGCommonUtils.h"
#include "UMG/AnimationCommonUtils.h"
#include "MCP/Sessions/UMGSessionSubsystem.h"

#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"

#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Sections/MovieSceneColorSection.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "Sections/MovieSceneVectorSection.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Algo/Sort.h"

namespace UMGAnimationOps
{
    static const FString GetAllAnimations      = TEXT("get_all_animations");
    static const FString CreateAnimation       = TEXT("create_animation");
    static const FString DeleteAnimation       = TEXT("delete_animation");
    static const FString GetAnimationKeyframes = TEXT("get_animation_keyframes");
    static const FString GetWidgetAnimationData = TEXT("get_widget_animation_data");
    static const FString SetPropertyKeys       = TEXT("set_property_keys");
    static const FString RemovePropertyTrack   = TEXT("remove_property_track");
    static const FString RemoveKeys            = TEXT("remove_keys");
    static const FString AppendWidgetTracks    = TEXT("append_widget_tracks");
    static const FString SetAnimationData      = TEXT("set_animation_data");
    static const FString SampleAtTime          = TEXT("sample_at_time");
    static const FString AppendTimeSlice       = TEXT("append_time_slice");
}

// =============================================================================
//  Dispatcher
// =============================================================================
FMCPToolResult FMCPTool_UMGAnimation::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 0. UMG session anchor fallback (no-op if path already provided).
    UUMGSessionSubsystem::ApplyWidgetBlueprintPathFallback(Params);

    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }
    Operation = Operation.ToLower();

    if (Operation == UMGAnimationOps::GetAllAnimations)       { return ExecuteGetAllAnimations(Params); }
    if (Operation == UMGAnimationOps::CreateAnimation)        { return ExecuteCreateAnimation(Params); }
    if (Operation == UMGAnimationOps::DeleteAnimation)        { return ExecuteDeleteAnimation(Params); }
    if (Operation == UMGAnimationOps::GetAnimationKeyframes)  { return ExecuteGetAnimationKeyframes(Params); }
    if (Operation == UMGAnimationOps::GetWidgetAnimationData) { return ExecuteGetWidgetAnimationData(Params); }
    if (Operation == UMGAnimationOps::SetPropertyKeys)        { return ExecuteSetPropertyKeys(Params); }
    if (Operation == UMGAnimationOps::RemovePropertyTrack)    { return ExecuteRemovePropertyTrack(Params); }
    if (Operation == UMGAnimationOps::RemoveKeys)             { return ExecuteRemoveKeys(Params); }
    if (Operation == UMGAnimationOps::AppendWidgetTracks)     { return ExecuteAppendWidgetTracks(Params); }
    if (Operation == UMGAnimationOps::SetAnimationData)       { return ExecuteSetAnimationData(Params); }
    if (Operation == UMGAnimationOps::SampleAtTime)           { return ExecuteSampleAtTime(Params); }
    if (Operation == UMGAnimationOps::AppendTimeSlice)        { return ExecuteAppendTimeSlice(Params); }

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation: '%s'. Valid: get_all_animations, create_animation, delete_animation, "
             "get_animation_keyframes, get_widget_animation_data, set_property_keys, remove_property_track, "
             "remove_keys, append_widget_tracks, set_animation_data, sample_at_time, append_time_slice"),
        *Operation));
}

// =============================================================================
//  Local helpers (file scope)
// =============================================================================
namespace
{
    /** Extract widget_blueprint_path -> UWidgetBlueprint*. Returns nullptr + error result on failure. */
    UWidgetBlueprint* ResolveBlueprint(const TSharedRef<FJsonObject>& Params, FMCPToolResult& OutError)
    {
        FString Path;
        if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), Path) || Path.IsEmpty())
        {
            OutError = FMCPToolResult::Error(TEXT("Missing required parameter: widget_blueprint_path"));
            return nullptr;
        }

        FString LoadError;
        UWidgetBlueprint* Blueprint = UMGCommonUtils::LoadWidgetBlueprint(Path, LoadError);
        if (!Blueprint)
        {
            OutError = FMCPToolResult::Error(LoadError);
            return nullptr;
        }
        return Blueprint;
    }

    /** Extract animation_name (required). */
    bool ResolveAnimationName(const TSharedRef<FJsonObject>& Params, FString& OutAnimationName, FMCPToolResult& OutError)
    {
        if (!Params->TryGetStringField(TEXT("animation_name"), OutAnimationName) || OutAnimationName.IsEmpty())
        {
            OutError = FMCPToolResult::Error(TEXT("Missing required parameter: animation_name"));
            return false;
        }
        return true;
    }

    /** Extract widget_name (required). */
    bool ResolveWidgetName(const TSharedRef<FJsonObject>& Params, FString& OutWidgetName, FMCPToolResult& OutError)
    {
        if (!Params->TryGetStringField(TEXT("widget_name"), OutWidgetName) || OutWidgetName.IsEmpty())
        {
            OutError = FMCPToolResult::Error(TEXT("Missing required parameter: widget_name"));
            return false;
        }
        return true;
    }

    /** Bundle of (Blueprint, Animation, MovieScene) extracted by ResolveAnimationContext. */
    struct FAnimationContext
    {
        UWidgetBlueprint* Blueprint = nullptr;
        UWidgetAnimation* Animation = nullptr;
        UMovieScene* MovieScene = nullptr;
    };

    bool ResolveAnimationContext(
        const TSharedRef<FJsonObject>& Params,
        FAnimationContext& OutCtx,
        FMCPToolResult& OutError)
    {
        OutCtx.Blueprint = ResolveBlueprint(Params, OutError);
        if (!OutCtx.Blueprint) { return false; }

        FString AnimName;
        if (!ResolveAnimationName(Params, AnimName, OutError)) { return false; }

        OutCtx.Animation = AnimationCommonUtils::FindAnimationByName(OutCtx.Blueprint, AnimName);
        if (!OutCtx.Animation)
        {
            OutError = FMCPToolResult::Error(FString::Printf(TEXT("Animation '%s' not found"), *AnimName));
            return false;
        }

        OutCtx.MovieScene = OutCtx.Animation->GetMovieScene();
        if (!OutCtx.MovieScene)
        {
            OutError = FMCPToolResult::Error(TEXT("Animation has no MovieScene"));
            return false;
        }
        return true;
    }
}

// =============================================================================
//  Read operations
// =============================================================================
FMCPToolResult FMCPTool_UMGAnimation::ExecuteGetAllAnimations(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    UWidgetBlueprint* Blueprint = ResolveBlueprint(Params, Error);
    if (!Blueprint) { return Error; }

    bool bDetailed = false;
    Params->TryGetBoolField(TEXT("detailed"), bDetailed);

    TArray<TSharedPtr<FJsonValue>> Animations;
    for (UWidgetAnimation* Anim : Blueprint->Animations)
    {
        if (!Anim) { continue; }
        TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
        AnimObj->SetStringField(TEXT("name"), Anim->GetName());
        AnimObj->SetNumberField(TEXT("start_time"), Anim->GetStartTime());
        AnimObj->SetNumberField(TEXT("end_time"), Anim->GetEndTime());

        // Detailed mode: enumerate per-widget per-property tracks with key counts.
        // Adapted from UmgMcp animation_overview (UmgMcpSequencerCommands.cpp::GetAnimationOverview).
        if (bDetailed)
        {
            UMovieScene* MovieScene = Anim->GetMovieScene();
            TArray<TSharedPtr<FJsonValue>> Tracks;
            int32 TotalKeyframes = 0;

            if (MovieScene)
            {
                for (const FWidgetAnimationBinding& Binding : Anim->AnimationBindings)
                {
                    const FString WidgetName = Binding.WidgetName.ToString();
                    const FGuid ObjectGuid = Binding.AnimationGuid;

                    auto AppendTrack = [&](const FString& Property, const FString& TrackType, int32 KeyCount)
                    {
                        TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
                        TrackObj->SetStringField(TEXT("widget"), WidgetName);
                        TrackObj->SetStringField(TEXT("property"), Property);
                        TrackObj->SetStringField(TEXT("track_type"), TrackType);
                        TrackObj->SetNumberField(TEXT("keys_count"), KeyCount);
                        Tracks.Add(MakeShared<FJsonValueObject>(TrackObj));
                        TotalKeyframes += KeyCount;
                    };

                    for (const UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneFloatTrack::StaticClass(), ObjectGuid))
                    {
                        const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
                        if (!FloatTrack) { continue; }
                        int32 KeyCount = 0;
                        for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
                        {
                            if (const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
                            {
                                KeyCount += FloatSection->GetChannel().GetData().GetTimes().Num();
                            }
                        }
                        AppendTrack(FloatTrack->GetPropertyName().ToString(), TEXT("float"), KeyCount);
                    }

                    for (const UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneColorTrack::StaticClass(), ObjectGuid))
                    {
                        const UMovieSceneColorTrack* ColorTrack = Cast<UMovieSceneColorTrack>(Track);
                        if (!ColorTrack) { continue; }
                        int32 KeyCount = 0;
                        for (const UMovieSceneSection* Section : ColorTrack->GetAllSections())
                        {
                            if (const UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section))
                            {
                                KeyCount += ColorSection->GetRedChannel().GetData().GetTimes().Num();
                            }
                        }
                        AppendTrack(ColorTrack->GetPropertyName().ToString(), TEXT("color"), KeyCount);
                    }

                    for (const UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneDoubleVectorTrack::StaticClass(), ObjectGuid))
                    {
                        const UMovieSceneDoubleVectorTrack* VecTrack = Cast<UMovieSceneDoubleVectorTrack>(Track);
                        if (!VecTrack || VecTrack->GetNumChannelsUsed() < 2) { continue; }
                        int32 KeyCount = 0;
                        for (const UMovieSceneSection* Section : VecTrack->GetAllSections())
                        {
                            const UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section);
                            if (!VecSection) { continue; }
                            FMovieSceneChannelProxy& Proxy = const_cast<UMovieSceneDoubleVectorSection*>(VecSection)->GetChannelProxy();
                            TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
                            if (Channels.Num() >= 1)
                            {
                                KeyCount += Channels[0]->GetData().GetTimes().Num();
                            }
                        }
                        AppendTrack(VecTrack->GetPropertyName().ToString(), TEXT("vector2d"), KeyCount);
                    }
                }
            }

            AnimObj->SetNumberField(TEXT("track_count"), Tracks.Num());
            AnimObj->SetNumberField(TEXT("keyframe_count"), TotalKeyframes);
            AnimObj->SetArrayField(TEXT("tracks"), Tracks);
        }

        Animations.Add(MakeShared<FJsonValueObject>(AnimObj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_blueprint_path"), Blueprint->GetPathName());
    Result->SetBoolField(TEXT("detailed"), bDetailed);
    Result->SetNumberField(TEXT("animation_count"), Animations.Num());
    Result->SetArrayField(TEXT("animations"), Animations);
    return FMCPToolResult::Success(FString::Printf(TEXT("Found %d animations%s"),
        Animations.Num(), bDetailed ? TEXT(" (detailed)") : TEXT("")), Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteGetAnimationKeyframes(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    const FFrameRate TickResolution = Ctx.MovieScene->GetTickResolution();
    TArray<TSharedPtr<FJsonValue>> TracksArray;

    for (const FWidgetAnimationBinding& Binding : Ctx.Animation->AnimationBindings)
    {
        const FGuid ObjectGuid = Binding.AnimationGuid;
        const FString WidgetName = Binding.WidgetName.ToString();

        // Float tracks
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneFloatTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
            if (!FloatTrack) { continue; }
            TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
            TrackObj->SetStringField(TEXT("widget_name"), WidgetName);
            TrackObj->SetStringField(TEXT("property_name"), FloatTrack->GetPropertyName().ToString());
            TrackObj->SetStringField(TEXT("track_type"), TEXT("float"));

            TArray<TSharedPtr<FJsonValue>> KeysArray;
            for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
            {
                const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section);
                if (!FloatSection) { continue; }
                TArrayView<const FFrameNumber> Times = FloatSection->GetChannel().GetData().GetTimes();
                TArrayView<const FMovieSceneFloatValue> Values = FloatSection->GetChannel().GetData().GetValues();
                for (int32 i = 0; i < Times.Num(); ++i)
                {
                    TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
                    KeyObj->SetNumberField(TEXT("time"), TickResolution.AsSeconds(Times[i]));
                    KeyObj->SetNumberField(TEXT("value"), Values[i].Value);
                    KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
                }
            }
            TrackObj->SetArrayField(TEXT("keys"), KeysArray);
            TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
        }

        // Color tracks
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneColorTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneColorTrack* ColorTrack = Cast<UMovieSceneColorTrack>(Track);
            if (!ColorTrack) { continue; }
            TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
            TrackObj->SetStringField(TEXT("widget_name"), WidgetName);
            TrackObj->SetStringField(TEXT("property_name"), ColorTrack->GetPropertyName().ToString());
            TrackObj->SetStringField(TEXT("track_type"), TEXT("color"));

            TArray<TSharedPtr<FJsonValue>> KeysArray;
            for (const UMovieSceneSection* Section : ColorTrack->GetAllSections())
            {
                const UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section);
                if (!ColorSection) { continue; }
                TArrayView<const FFrameNumber> Times = ColorSection->GetRedChannel().GetData().GetTimes();
                TArrayView<const FMovieSceneFloatValue> RedValues = ColorSection->GetRedChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> GreenValues = ColorSection->GetGreenChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> BlueValues = ColorSection->GetBlueChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> AlphaValues = ColorSection->GetAlphaChannel().GetData().GetValues();
                const int32 N = FMath::Min(Times.Num(), FMath::Min(RedValues.Num(), FMath::Min(GreenValues.Num(), FMath::Min(BlueValues.Num(), AlphaValues.Num()))));
                for (int32 i = 0; i < N; ++i)
                {
                    TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
                    KeyObj->SetNumberField(TEXT("time"), TickResolution.AsSeconds(Times[i]));
                    TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
                    ColorObj->SetNumberField(TEXT("r"), RedValues[i].Value);
                    ColorObj->SetNumberField(TEXT("g"), GreenValues[i].Value);
                    ColorObj->SetNumberField(TEXT("b"), BlueValues[i].Value);
                    ColorObj->SetNumberField(TEXT("a"), AlphaValues[i].Value);
                    KeyObj->SetObjectField(TEXT("value"), ColorObj);
                    KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
                }
            }
            TrackObj->SetArrayField(TEXT("keys"), KeysArray);
            TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
        }

        // DoubleVector tracks (Vector2D when NumChannelsUsed==2)
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneDoubleVectorTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneDoubleVectorTrack* VecTrack = Cast<UMovieSceneDoubleVectorTrack>(Track);
            if (!VecTrack || VecTrack->GetNumChannelsUsed() < 2) { continue; }
            TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
            TrackObj->SetStringField(TEXT("widget_name"), WidgetName);
            TrackObj->SetStringField(TEXT("property_name"), VecTrack->GetPropertyName().ToString());
            TrackObj->SetStringField(TEXT("track_type"), TEXT("vector2d"));

            TArray<TSharedPtr<FJsonValue>> KeysArray;
            for (const UMovieSceneSection* Section : VecTrack->GetAllSections())
            {
                const UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section);
                if (!VecSection) { continue; }

                FMovieSceneChannelProxy& Proxy = const_cast<UMovieSceneDoubleVectorSection*>(VecSection)->GetChannelProxy();
                TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
                if (Channels.Num() < 2) { continue; }
                TArrayView<const FFrameNumber> Times = Channels[0]->GetData().GetTimes();
                TArrayView<const FMovieSceneDoubleValue> XValues = Channels[0]->GetData().GetValues();
                TArrayView<const FMovieSceneDoubleValue> YValues = Channels[1]->GetData().GetValues();
                const int32 N = FMath::Min(Times.Num(), FMath::Min(XValues.Num(), YValues.Num()));
                for (int32 i = 0; i < N; ++i)
                {
                    TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
                    KeyObj->SetNumberField(TEXT("time"), TickResolution.AsSeconds(Times[i]));
                    TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
                    VecObj->SetNumberField(TEXT("x"), XValues[i].Value);
                    VecObj->SetNumberField(TEXT("y"), YValues[i].Value);
                    KeyObj->SetObjectField(TEXT("value"), VecObj);
                    KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
                }
            }
            TrackObj->SetArrayField(TEXT("keys"), KeysArray);
            TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetNumberField(TEXT("track_count"), TracksArray.Num());
    Result->SetArrayField(TEXT("tracks"), TracksArray);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Read %d tracks for animation '%s'"), TracksArray.Num(), *Ctx.Animation->GetName()),
        Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteGetWidgetAnimationData(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    FString WidgetName;
    if (!ResolveWidgetName(Params, WidgetName, Error)) { return Error; }

    FString PropertyFilter;
    Params->TryGetStringField(TEXT("property_filter"), PropertyFilter);

    const FFrameRate TickResolution = Ctx.MovieScene->GetTickResolution();
    TArray<TSharedPtr<FJsonValue>> Changes;

    auto AddChange = [&](const FString& Property, const FString& ValueType, double TimeSeconds, const TSharedPtr<FJsonValue>& Payload)
    {
        TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
        Change->SetStringField(TEXT("widget"), WidgetName);
        Change->SetStringField(TEXT("property"), Property);
        Change->SetStringField(TEXT("value_type"), ValueType);
        Change->SetNumberField(TEXT("time"), TimeSeconds);
        Change->SetField(TEXT("value"), Payload);
        Changes.Add(MakeShared<FJsonValueObject>(Change));
    };

    for (const FWidgetAnimationBinding& Binding : Ctx.Animation->AnimationBindings)
    {
        if (Binding.WidgetName.ToString() != WidgetName) { continue; }
        const FGuid ObjectGuid = Binding.AnimationGuid;

        // Float
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneFloatTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
            if (!FloatTrack) { continue; }
            const FString PropName = FloatTrack->GetPropertyName().ToString();
            if (!PropertyFilter.IsEmpty() && PropName != PropertyFilter) { continue; }
            for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
            {
                const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section);
                if (!FloatSection) { continue; }
                TArrayView<const FFrameNumber> Times = FloatSection->GetChannel().GetData().GetTimes();
                TArrayView<const FMovieSceneFloatValue> Values = FloatSection->GetChannel().GetData().GetValues();
                for (int32 i = 0; i < Times.Num(); ++i)
                {
                    AddChange(PropName, TEXT("float"), TickResolution.AsSeconds(Times[i]), MakeShared<FJsonValueNumber>(Values[i].Value));
                }
            }
        }

        // Color
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneColorTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneColorTrack* ColorTrack = Cast<UMovieSceneColorTrack>(Track);
            if (!ColorTrack) { continue; }
            const FString PropName = ColorTrack->GetPropertyName().ToString();
            if (!PropertyFilter.IsEmpty() && PropName != PropertyFilter) { continue; }
            for (const UMovieSceneSection* Section : ColorTrack->GetAllSections())
            {
                const UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section);
                if (!ColorSection) { continue; }
                TArrayView<const FFrameNumber> Times = ColorSection->GetRedChannel().GetData().GetTimes();
                TArrayView<const FMovieSceneFloatValue> R = ColorSection->GetRedChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> G = ColorSection->GetGreenChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> B = ColorSection->GetBlueChannel().GetData().GetValues();
                TArrayView<const FMovieSceneFloatValue> A = ColorSection->GetAlphaChannel().GetData().GetValues();
                const int32 N = FMath::Min(Times.Num(), FMath::Min(R.Num(), FMath::Min(G.Num(), FMath::Min(B.Num(), A.Num()))));
                for (int32 i = 0; i < N; ++i)
                {
                    TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
                    ColorObj->SetNumberField(TEXT("r"), R[i].Value);
                    ColorObj->SetNumberField(TEXT("g"), G[i].Value);
                    ColorObj->SetNumberField(TEXT("b"), B[i].Value);
                    ColorObj->SetNumberField(TEXT("a"), A[i].Value);
                    AddChange(PropName, TEXT("color"), TickResolution.AsSeconds(Times[i]), MakeShared<FJsonValueObject>(ColorObj));
                }
            }
        }

        // Vector2D
        for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneDoubleVectorTrack::StaticClass(), ObjectGuid))
        {
            const UMovieSceneDoubleVectorTrack* VecTrack = Cast<UMovieSceneDoubleVectorTrack>(Track);
            if (!VecTrack || VecTrack->GetNumChannelsUsed() < 2) { continue; }
            const FString PropName = VecTrack->GetPropertyName().ToString();
            if (!PropertyFilter.IsEmpty() && PropName != PropertyFilter) { continue; }
            for (const UMovieSceneSection* Section : VecTrack->GetAllSections())
            {
                const UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section);
                if (!VecSection) { continue; }
                FMovieSceneChannelProxy& Proxy = const_cast<UMovieSceneDoubleVectorSection*>(VecSection)->GetChannelProxy();
                TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
                if (Channels.Num() < 2) { continue; }
                TArrayView<const FFrameNumber> Times = Channels[0]->GetData().GetTimes();
                TArrayView<const FMovieSceneDoubleValue> Xs = Channels[0]->GetData().GetValues();
                TArrayView<const FMovieSceneDoubleValue> Ys = Channels[1]->GetData().GetValues();
                const int32 N = FMath::Min(Times.Num(), FMath::Min(Xs.Num(), Ys.Num()));
                for (int32 i = 0; i < N; ++i)
                {
                    TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
                    VecObj->SetNumberField(TEXT("x"), Xs[i].Value);
                    VecObj->SetNumberField(TEXT("y"), Ys[i].Value);
                    AddChange(PropName, TEXT("vector2d"), TickResolution.AsSeconds(Times[i]), MakeShared<FJsonValueObject>(VecObj));
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetStringField(TEXT("widget"), WidgetName);
    if (!PropertyFilter.IsEmpty()) { Result->SetStringField(TEXT("property_filter"), PropertyFilter); }
    Result->SetNumberField(TEXT("change_count"), Changes.Num());
    Result->SetArrayField(TEXT("changes"), Changes);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Got %d changes for widget '%s'"), Changes.Num(), *WidgetName),
        Result);
}

// =============================================================================
//  Write operations
// =============================================================================
FMCPToolResult FMCPTool_UMGAnimation::ExecuteCreateAnimation(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    UWidgetBlueprint* Blueprint = ResolveBlueprint(Params, Error);
    if (!Blueprint) { return Error; }

    FString AnimationName;
    if (!ResolveAnimationName(Params, AnimationName, Error)) { return Error; }

    // Step 1. Reuse existing animation if it already exists.
    if (UWidgetAnimation* Existing = AnimationCommonUtils::FindAnimationByName(Blueprint, AnimationName))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), AnimationName);
        Result->SetStringField(TEXT("action"), TEXT("found"));
        Result->SetStringField(TEXT("widget_blueprint_path"), Blueprint->GetPathName());
        return FMCPToolResult::Success(FString::Printf(TEXT("Animation '%s' already exists"), *AnimationName), Result);
    }

    // Step 2. Create UWidgetAnimation + its MovieScene.
    UWidgetAnimation* NewAnimation = NewObject<UWidgetAnimation>(Blueprint, FName(*AnimationName), RF_Public | RF_Transactional);
    NewAnimation->MovieScene = NewObject<UMovieScene>(NewAnimation, FName("MovieScene"), RF_Transactional);

    Blueprint->Modify();
    Blueprint->Animations.Add(NewAnimation);

    // Step 3. Critical: register an FGuid for the animation in the WidgetVariableNameToGuidMap,
    // otherwise UMG editor compile will trip an ensure on first open.
    Blueprint->WidgetVariableNameToGuidMap.Add(NewAnimation->GetFName(), FGuid::NewGuid());

    // Step 4. Compile + refresh the editor.
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    if (GEditor)
    {
        if (UAssetEditorSubsystem* AssetSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            AssetSub->OpenEditorForAsset(Blueprint);
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), NewAnimation->GetName());
    Result->SetStringField(TEXT("action"), TEXT("created"));
    Result->SetStringField(TEXT("widget_blueprint_path"), Blueprint->GetPathName());
    return FMCPToolResult::Success(FString::Printf(TEXT("Created animation '%s'"), *AnimationName), Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteDeleteAnimation(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    UWidgetBlueprint* Blueprint = ResolveBlueprint(Params, Error);
    if (!Blueprint) { return Error; }

    FString AnimationName;
    if (!ResolveAnimationName(Params, AnimationName, Error)) { return Error; }

    bool bConfirmed = false;
    Params->TryGetBoolField(TEXT("confirm_delete"), bConfirmed);
    if (!bConfirmed)
    {
        return FMCPToolResult::Error(TEXT("delete_animation requires 'confirm_delete': true"));
    }

    const int32 RemovedCount = Blueprint->Animations.RemoveAll([&](UWidgetAnimation* Anim)
    {
        return Anim && Anim->GetName() == AnimationName;
    });

    if (RemovedCount == 0)
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Animation '%s' not found"), *AnimationName));
    }

    Blueprint->Modify();
    Blueprint->WidgetVariableNameToGuidMap.Remove(FName(*AnimationName));
    AnimationCommonUtils::RefreshAfterWriteOp(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("deleted_animation"), AnimationName);
    Result->SetNumberField(TEXT("removed_count"), RemovedCount);
    return FMCPToolResult::Success(FString::Printf(TEXT("Deleted animation '%s'"), *AnimationName), Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteSetPropertyKeys(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    FString WidgetName;
    if (!ResolveWidgetName(Params, WidgetName, Error)) { return Error; }

    FString PropertyName;
    TOptional<FMCPToolResult> ExtractError;
    if (!ExtractRequiredString(Params, TEXT("property_name"), PropertyName, ExtractError))
    {
        return ExtractError.GetValue();
    }

    const TArray<TSharedPtr<FJsonValue>>* KeysPtr = nullptr;
    if (!Params->TryGetArrayField(TEXT("keys"), KeysPtr) || !KeysPtr)
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: keys (array)"));
    }
    if (KeysPtr->Num() == 0)
    {
        TSharedPtr<FJsonObject> EmptyResult = MakeShared<FJsonObject>();
        EmptyResult->SetNumberField(TEXT("keys_count"), 0);
        return FMCPToolResult::Success(TEXT("No keys to set"), EmptyResult);
    }

    // Step 1. Detect track type from first key.
    AnimationCommonUtils::EKeyType KeyType = AnimationCommonUtils::DetectKeyType((*KeysPtr)[0]->AsObject());
    if (KeyType == AnimationCommonUtils::EKeyType::Unknown)
    {
        return FMCPToolResult::Error(TEXT("Could not detect key value type. Expected number, {r,g,b,a}, or {x,y}."));
    }

    Ctx.MovieScene->Modify();

    // Step 2. Resolve / create widget binding.
    FString BindError;
    FGuid WidgetGuid = AnimationCommonUtils::ResolveOrCreateWidgetBinding(Ctx.Blueprint, Ctx.Animation, WidgetName, BindError);
    if (!WidgetGuid.IsValid())
    {
        return FMCPToolResult::Error(BindError);
    }

    // Step 3. Range tracking (auto-extend playback range so new keys remain visible).
    const FFrameRate TickResolution = Ctx.MovieScene->GetTickResolution();
    TRange<FFrameNumber> PlaybackRange = Ctx.MovieScene->GetPlaybackRange();
    FFrameNumber RangeStart = PlaybackRange.HasLowerBound() ? PlaybackRange.GetLowerBoundValue() : FFrameNumber(0);
    FFrameNumber RangeEnd   = PlaybackRange.HasUpperBound() ? PlaybackRange.GetUpperBoundValue() : FFrameNumber(0);
    bool bRangeUpdated = false;
    bool bRangeInitialized = PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound();

    auto UpdateRange = [&](FFrameNumber Frame)
    {
        if (!bRangeInitialized) { RangeStart = Frame; RangeEnd = Frame; bRangeInitialized = true; bRangeUpdated = true; return; }
        if (Frame < RangeStart) { RangeStart = Frame; bRangeUpdated = true; }
        if (Frame > RangeEnd)   { RangeEnd = Frame;   bRangeUpdated = true; }
    };

    // Step 4. Track / section / channel writes per detected type.
    if (KeyType == AnimationCommonUtils::EKeyType::Float)
    {
        UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneFloatTrack::StaticClass(), WidgetGuid, FName(*PropertyName));
        if (!Track)
        {
            Track = Ctx.MovieScene->AddTrack(UMovieSceneFloatTrack::StaticClass(), WidgetGuid);
            Cast<UMovieSceneFloatTrack>(Track)->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);
        }
        Track->Modify();

        bool bSectionAdded = false;
        UMovieSceneSection* Section = Cast<UMovieSceneFloatTrack>(Track)->FindOrAddSection(0, bSectionAdded);
        Section->SetRange(TRange<FFrameNumber>::All());
        FMovieSceneFloatChannel& Channel = Cast<UMovieSceneFloatSection>(Section)->GetChannel();

        for (const TSharedPtr<FJsonValue>& Val : *KeysPtr)
        {
            TSharedPtr<FJsonObject> KeyObj = Val->AsObject();
            const double Time = KeyObj->GetNumberField(TEXT("time"));
            const float Value = (float)KeyObj->GetNumberField(TEXT("value"));
            const FFrameNumber Frame = AnimationCommonUtils::SecondsToFrame(Time, TickResolution);
            Channel.AddCubicKey(Frame, Value);
            UpdateRange(Frame);
        }
    }
    else if (KeyType == AnimationCommonUtils::EKeyType::Color)
    {
        UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneColorTrack::StaticClass(), WidgetGuid, FName(*PropertyName));
        if (!Track)
        {
            Track = Ctx.MovieScene->AddTrack(UMovieSceneColorTrack::StaticClass(), WidgetGuid);
            Cast<UMovieSceneColorTrack>(Track)->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);
        }
        Track->Modify();

        bool bSectionAdded = false;
        UMovieSceneSection* Section = Cast<UMovieSceneColorTrack>(Track)->FindOrAddSection(0, bSectionAdded);
        Section->SetRange(TRange<FFrameNumber>::All());
        UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section);

        for (const TSharedPtr<FJsonValue>& Val : *KeysPtr)
        {
            TSharedPtr<FJsonObject> KeyObj = Val->AsObject();
            const double Time = KeyObj->GetNumberField(TEXT("time"));
            TSharedPtr<FJsonObject> ColorObj = KeyObj->GetObjectField(TEXT("value"));

            FLinearColor Color;
            Color.R = (float)ColorObj->GetNumberField(TEXT("r"));
            Color.G = (float)ColorObj->GetNumberField(TEXT("g"));
            Color.B = (float)ColorObj->GetNumberField(TEXT("b"));
            Color.A = (float)ColorObj->GetNumberField(TEXT("a"));

            const FFrameNumber Frame = AnimationCommonUtils::SecondsToFrame(Time, TickResolution);
            ColorSection->GetRedChannel().AddLinearKey(Frame, Color.R);
            ColorSection->GetGreenChannel().AddLinearKey(Frame, Color.G);
            ColorSection->GetBlueChannel().AddLinearKey(Frame, Color.B);
            ColorSection->GetAlphaChannel().AddLinearKey(Frame, Color.A);
            UpdateRange(Frame);
        }
    }
    else // Vector2D
    {
        UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneDoubleVectorTrack::StaticClass(), WidgetGuid, FName(*PropertyName));
        if (!Track)
        {
            Track = Ctx.MovieScene->AddTrack(UMovieSceneDoubleVectorTrack::StaticClass(), WidgetGuid);
            UMovieSceneDoubleVectorTrack* VecTrack = Cast<UMovieSceneDoubleVectorTrack>(Track);
            VecTrack->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);
            VecTrack->SetNumChannelsUsed(2);
        }
        Track->Modify();

        bool bSectionAdded = false;
        UMovieSceneSection* Section = Cast<UMovieSceneDoubleVectorTrack>(Track)->FindOrAddSection(0, bSectionAdded);
        Section->SetRange(TRange<FFrameNumber>::All());

        UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section);
        if (VecSection)
        {
            VecSection->SetChannelsUsed(2);

            FMovieSceneChannelProxy& Proxy = VecSection->GetChannelProxy();
            TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
            if (Channels.Num() < 2)
            {
                return FMCPToolResult::Error(TEXT("Vector2D track has fewer than 2 channels."));
            }

            for (const TSharedPtr<FJsonValue>& Val : *KeysPtr)
            {
                TSharedPtr<FJsonObject> KeyObj = Val->AsObject();
                const double Time = KeyObj->GetNumberField(TEXT("time"));
                TSharedPtr<FJsonObject> VecObj = KeyObj->GetObjectField(TEXT("value"));
                const double X = VecObj->GetNumberField(TEXT("x"));
                const double Y = VecObj->GetNumberField(TEXT("y"));

                const FFrameNumber Frame = AnimationCommonUtils::SecondsToFrame(Time, TickResolution);
                Channels[0]->AddLinearKey(Frame, X);
                Channels[1]->AddLinearKey(Frame, Y);
                UpdateRange(Frame);
            }
        }
    }

    // Step 5. Commit playback range expansion if any new key fell outside it.
    if (bRangeUpdated)
    {
        Ctx.MovieScene->SetPlaybackRange(TRange<FFrameNumber>(RangeStart, RangeEnd));
    }

    AnimationCommonUtils::RefreshAfterWriteOp(Ctx.Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetStringField(TEXT("property"), PropertyName);
    Result->SetNumberField(TEXT("keys_count"), KeysPtr->Num());
    const TCHAR* TypeName = (KeyType == AnimationCommonUtils::EKeyType::Float) ? TEXT("float")
        : (KeyType == AnimationCommonUtils::EKeyType::Color) ? TEXT("color") : TEXT("vector2d");
    Result->SetStringField(TEXT("track_type"), TypeName);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Set %d %s keys on %s.%s"), KeysPtr->Num(), TypeName, *WidgetName, *PropertyName),
        Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteRemovePropertyTrack(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    FString WidgetName;
    if (!ResolveWidgetName(Params, WidgetName, Error)) { return Error; }

    FString PropertyName;
    TOptional<FMCPToolResult> ExtractError;
    if (!ExtractRequiredString(Params, TEXT("property_name"), PropertyName, ExtractError))
    {
        return ExtractError.GetValue();
    }

    bool bConfirmed = false;
    Params->TryGetBoolField(TEXT("confirm_delete"), bConfirmed);
    if (!bConfirmed)
    {
        return FMCPToolResult::Error(TEXT("remove_property_track requires 'confirm_delete': true"));
    }

    Ctx.MovieScene->Modify();

    // Locate binding GUID.
    FGuid WidgetGuid;
    for (int32 i = 0; i < Ctx.MovieScene->GetPossessableCount(); ++i)
    {
        if (Ctx.MovieScene->GetPossessable(i).GetName() == WidgetName)
        {
            WidgetGuid = Ctx.MovieScene->GetPossessable(i).GetGuid();
            break;
        }
    }
    if (!WidgetGuid.IsValid())
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no binding in this animation"), *WidgetName));
    }

    int32 RemovedCount = 0;
    const TArray<TSubclassOf<UMovieSceneTrack>> TrackTypes = {
        UMovieSceneFloatTrack::StaticClass(),
        UMovieSceneColorTrack::StaticClass(),
        UMovieSceneDoubleVectorTrack::StaticClass()
    };
    for (TSubclassOf<UMovieSceneTrack> Cls : TrackTypes)
    {
        if (UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(Cls, WidgetGuid, FName(*PropertyName)))
        {
            Ctx.MovieScene->RemoveTrack(*Track);
            ++RemovedCount;
        }
    }

    if (RemovedCount == 0)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("No track found for widget '%s' property '%s'"), *WidgetName, *PropertyName));
    }

    AnimationCommonUtils::RefreshAfterWriteOp(Ctx.Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetStringField(TEXT("property"), PropertyName);
    Result->SetNumberField(TEXT("removed_track_count"), RemovedCount);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Removed %d track(s) on %s.%s"), RemovedCount, *WidgetName, *PropertyName),
        Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteRemoveKeys(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    FString WidgetName;
    if (!ResolveWidgetName(Params, WidgetName, Error)) { return Error; }

    FString PropertyName;
    TOptional<FMCPToolResult> ExtractError;
    if (!ExtractRequiredString(Params, TEXT("property_name"), PropertyName, ExtractError))
    {
        return ExtractError.GetValue();
    }

    bool bConfirmed = false;
    Params->TryGetBoolField(TEXT("confirm_delete"), bConfirmed);
    if (!bConfirmed)
    {
        return FMCPToolResult::Error(TEXT("remove_keys requires 'confirm_delete': true"));
    }

    // Collect target times in seconds (accept either `times` array or single `time`).
    TArray<double> TimesSeconds;
    const TArray<TSharedPtr<FJsonValue>>* TimesArray = nullptr;
    if (Params->TryGetArrayField(TEXT("times"), TimesArray) && TimesArray)
    {
        for (const TSharedPtr<FJsonValue>& Val : *TimesArray)
        {
            if (Val->Type == EJson::Number) { TimesSeconds.Add(Val->AsNumber()); }
        }
    }
    else
    {
        double SingleTime = 0.0;
        if (Params->TryGetNumberField(TEXT("time"), SingleTime))
        {
            TimesSeconds.Add(SingleTime);
        }
    }
    if (TimesSeconds.Num() == 0)
    {
        return FMCPToolResult::Error(TEXT("Provide 'time' or 'times' (seconds) for key deletion."));
    }

    Ctx.MovieScene->Modify();

    // Locate binding GUID.
    FGuid WidgetGuid;
    for (int32 i = 0; i < Ctx.MovieScene->GetPossessableCount(); ++i)
    {
        if (Ctx.MovieScene->GetPossessable(i).GetName() == WidgetName)
        {
            WidgetGuid = Ctx.MovieScene->GetPossessable(i).GetGuid();
            break;
        }
    }
    if (!WidgetGuid.IsValid())
    {
        return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no binding in this animation"), *WidgetName));
    }

    const FFrameRate TickResolution = Ctx.MovieScene->GetTickResolution();
    auto CollectIndices = [&](TArrayView<const FFrameNumber> Times) -> TArray<int32>
    {
        TSet<int32> UniqueIndices;
        for (double TimeSeconds : TimesSeconds)
        {
            const FFrameNumber TargetFrame = AnimationCommonUtils::SecondsToFrame(TimeSeconds, TickResolution);
            for (int32 Index = 0; Index < Times.Num(); ++Index)
            {
                if (Times[Index] == TargetFrame) { UniqueIndices.Add(Index); }
            }
        }
        TArray<int32> Indices = UniqueIndices.Array();
        Algo::Sort(Indices);
        return MoveTemp(Indices);
    };

    bool bTrackFound = false;
    int32 RemovedKeys = 0;

    // Float
    if (UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneFloatTrack::StaticClass(), WidgetGuid, FName(*PropertyName)))
    {
        bTrackFound = true;
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
            {
                TArrayView<const FFrameNumber> Times = FloatSection->GetChannel().GetData().GetTimes();
                TArray<int32> Indices = CollectIndices(Times);
                if (Indices.Num() > 0)
                {
                    TArray<FKeyHandle> Handles;
                    Handles.Reserve(Indices.Num());
                    for (int32 Index : Indices)
                    {
                        Handles.Add(FloatSection->GetChannel().GetHandle(Index));
                    }
                    FloatSection->GetChannel().DeleteKeys(Handles);
                    RemovedKeys += Indices.Num();
                }
            }
        }
    }

    // Color
    if (UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneColorTrack::StaticClass(), WidgetGuid, FName(*PropertyName)))
    {
        bTrackFound = true;
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section))
            {
                TArrayView<const FFrameNumber> Times = ColorSection->GetRedChannel().GetData().GetTimes();
                TArray<int32> Indices = CollectIndices(Times);
                if (Indices.Num() > 0)
                {
                    TArray<FKeyHandle> Handles;
                    Handles.Reserve(Indices.Num());
                    for (int32 Index : Indices)
                    {
                        Handles.Add(ColorSection->GetRedChannel().GetHandle(Index));
                    }
                    ColorSection->GetRedChannel().DeleteKeys(Handles);
                    ColorSection->GetGreenChannel().DeleteKeys(Handles);
                    ColorSection->GetBlueChannel().DeleteKeys(Handles);
                    ColorSection->GetAlphaChannel().DeleteKeys(Handles);
                    RemovedKeys += Indices.Num();
                }
            }
        }
    }

    // Vector2D
    if (UMovieSceneTrack* Track = Ctx.MovieScene->FindTrack(UMovieSceneDoubleVectorTrack::StaticClass(), WidgetGuid, FName(*PropertyName)))
    {
        bTrackFound = true;
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section))
            {
                FMovieSceneChannelProxy& Proxy = VecSection->GetChannelProxy();
                TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
                if (Channels.Num() >= 2)
                {
                    TArrayView<const FFrameNumber> Times = Channels[0]->GetData().GetTimes();
                    TArray<int32> Indices = CollectIndices(Times);
                    if (Indices.Num() > 0)
                    {
                        TArray<FKeyHandle> Handles;
                        Handles.Reserve(Indices.Num());
                        for (int32 Index : Indices)
                        {
                            Handles.Add(Channels[0]->GetHandle(Index));
                        }
                        Channels[0]->DeleteKeys(Handles);
                        Channels[1]->DeleteKeys(Handles);
                        RemovedKeys += Indices.Num();
                    }
                }
            }
        }
    }

    if (!bTrackFound)
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("No track found for widget '%s' property '%s'"), *WidgetName, *PropertyName));
    }

    if (RemovedKeys > 0)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Ctx.Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Ctx.Blueprint);
        if (GEditor)
        {
            if (UAssetEditorSubsystem* AssetSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            {
                AssetSub->OpenEditorForAsset(Ctx.Blueprint);
            }
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetStringField(TEXT("property"), PropertyName);
    Result->SetNumberField(TEXT("removed_keys"), RemovedKeys);
    Result->SetNumberField(TEXT("requested_times"), TimesSeconds.Num());
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Removed %d key(s) on %s.%s"), RemovedKeys, *WidgetName, *PropertyName),
        Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteAppendWidgetTracks(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    FString WidgetName;
    if (!ResolveWidgetName(Params, WidgetName, Error)) { return Error; }

    const TArray<TSharedPtr<FJsonValue>>* TracksPtr = nullptr;
    if (!Params->TryGetArrayField(TEXT("tracks"), TracksPtr) || !TracksPtr)
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: tracks (array)"));
    }

    int32 KeysTotal = 0;
    TArray<TSharedPtr<FJsonValue>> TrackSummaries;

    // Each entry in `tracks` is { property, keys[] } - just dispatch through SetPropertyKeys.
    for (const TSharedPtr<FJsonValue>& TrackVal : *TracksPtr)
    {
        TSharedPtr<FJsonObject> TrackObj = TrackVal->AsObject();
        if (!TrackObj.IsValid()) { continue; }

        FString PropertyName;
        if (!TrackObj->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
        {
            return FMCPToolResult::Error(TEXT("Each track entry needs a 'property' string."));
        }
        const TArray<TSharedPtr<FJsonValue>>* KeysPtr = nullptr;
        if (!TrackObj->TryGetArrayField(TEXT("keys"), KeysPtr))
        {
            return FMCPToolResult::Error(TEXT("Each track entry needs a 'keys' array."));
        }

        // Build sub-params and recurse into ExecuteSetPropertyKeys.
        TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
        SubParams->SetStringField(TEXT("operation"), UMGAnimationOps::SetPropertyKeys);
        SubParams->SetStringField(TEXT("widget_blueprint_path"), Ctx.Blueprint->GetPathName());
        SubParams->SetStringField(TEXT("animation_name"), Ctx.Animation->GetName());
        SubParams->SetStringField(TEXT("widget_name"), WidgetName);
        SubParams->SetStringField(TEXT("property_name"), PropertyName);
        SubParams->SetArrayField(TEXT("keys"), *KeysPtr);

        FMCPToolResult SubResult = ExecuteSetPropertyKeys(SubParams);
        if (!SubResult.bSuccess) { return SubResult; }

        KeysTotal += KeysPtr->Num();

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("property"), PropertyName);
        Summary->SetNumberField(TEXT("keys_applied"), KeysPtr->Num());
        TrackSummaries.Add(MakeShared<FJsonValueObject>(Summary));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetNumberField(TEXT("track_count"), TrackSummaries.Num());
    Result->SetNumberField(TEXT("keys_total"), KeysTotal);
    Result->SetArrayField(TEXT("tracks"), TrackSummaries);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Appended %d track(s) / %d key(s) on '%s'"), TrackSummaries.Num(), KeysTotal, *WidgetName),
        Result);
}

FMCPToolResult FMCPTool_UMGAnimation::ExecuteSetAnimationData(const TSharedRef<FJsonObject>& Params)
{
    // Higher-level wrapper - same shape as append_widget_tracks but allows the LLM to
    // express "here is the entire animation in one go". Implementation just dispatches
    // each track through ExecuteSetPropertyKeys; we share the same input contract.
    return ExecuteAppendWidgetTracks(Params);
}

// =============================================================================
//  sample_at_time / append_time_slice (adapted from UmgMcp)
//  Source: UmgMcpSequencerCommands.cpp::GetTimeSliceProperties / AppendTimeSlice
// =============================================================================

/**
 * Evaluate every track in the animation at one or more query times.
 * Unlike get_animation_keyframes (raw key dump), this samples the *interpolated*
 * value at arbitrary times — useful for "what is widget X's RenderOpacity at t=0.5s".
 *
 * Inputs:
 *   widget_blueprint_path (required) - widget BP asset path
 *   animation_name        (required) - animation to sample
 *   time | times          (required) - single time (seconds) or array of times
 *   widget_name           (optional) - filter to a single widget binding
 *   property_filter       (optional) - filter to a single property name
 *
 * Output: { animation, slice_count, slices: [{time, values_count, values: [{widget, property, value_type, value}]}] }
 */
FMCPToolResult FMCPTool_UMGAnimation::ExecuteSampleAtTime(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    // Step 1. Collect query times (accept either `times` array or single `time`).
    TArray<double> TimesSeconds;
    const TArray<TSharedPtr<FJsonValue>>* TimesArray = nullptr;
    if (Params->TryGetArrayField(TEXT("times"), TimesArray) && TimesArray)
    {
        for (const TSharedPtr<FJsonValue>& Val : *TimesArray)
        {
            if (Val->Type == EJson::Number) { TimesSeconds.Add(Val->AsNumber()); }
        }
    }
    else
    {
        double SingleTime = 0.0;
        if (Params->TryGetNumberField(TEXT("time"), SingleTime))
        {
            TimesSeconds.Add(SingleTime);
        }
    }
    if (TimesSeconds.Num() == 0)
    {
        return FMCPToolResult::Error(TEXT("Provide 'time' or 'times' (seconds) to sample."));
    }

    // Step 2. Optional widget / property filters (both nullable).
    FString WidgetFilter;
    const bool bHasWidgetFilter = Params->TryGetStringField(TEXT("widget_name"), WidgetFilter) && !WidgetFilter.IsEmpty();
    FString PropertyFilter;
    Params->TryGetStringField(TEXT("property_filter"), PropertyFilter);

    const FFrameRate TickResolution = Ctx.MovieScene->GetTickResolution();
    TArray<TSharedPtr<FJsonValue>> Slices;

    // Step 3. For each query time, evaluate every binding/track and collect interpolated values.
    for (double QueryTime : TimesSeconds)
    {
        const FFrameNumber QueryFrame = AnimationCommonUtils::SecondsToFrame(QueryTime, TickResolution);
        TArray<TSharedPtr<FJsonValue>> ValuesAtTime;

        for (const FWidgetAnimationBinding& Binding : Ctx.Animation->AnimationBindings)
        {
            const FString BindingWidget = Binding.WidgetName.ToString();
            if (bHasWidgetFilter && BindingWidget != WidgetFilter) { continue; }
            const FGuid ObjectGuid = Binding.AnimationGuid;

            // Float tracks
            for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneFloatTrack::StaticClass(), ObjectGuid))
            {
                const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
                if (!FloatTrack) { continue; }
                const FString PropertyName = FloatTrack->GetPropertyName().ToString();
                if (!PropertyFilter.IsEmpty() && PropertyName != PropertyFilter) { continue; }

                for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
                {
                    const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section);
                    if (!FloatSection) { continue; }

                    float EvalValue = 0.f;
                    if (FloatSection->GetChannel().Evaluate(QueryFrame, EvalValue))
                    {
                        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                        Obj->SetStringField(TEXT("widget"), BindingWidget);
                        Obj->SetStringField(TEXT("property"), PropertyName);
                        Obj->SetStringField(TEXT("value_type"), TEXT("float"));
                        Obj->SetNumberField(TEXT("value"), EvalValue);
                        ValuesAtTime.Add(MakeShared<FJsonValueObject>(Obj));
                        break;
                    }
                }
            }

            // Color tracks
            for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneColorTrack::StaticClass(), ObjectGuid))
            {
                const UMovieSceneColorTrack* ColorTrack = Cast<UMovieSceneColorTrack>(Track);
                if (!ColorTrack) { continue; }
                const FString PropertyName = ColorTrack->GetPropertyName().ToString();
                if (!PropertyFilter.IsEmpty() && PropertyName != PropertyFilter) { continue; }

                for (const UMovieSceneSection* Section : ColorTrack->GetAllSections())
                {
                    const UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section);
                    if (!ColorSection) { continue; }

                    float R = 0.f, G = 0.f, B = 0.f, A = 1.f;
                    bool bHasValue = ColorSection->GetRedChannel().Evaluate(QueryFrame, R);
                    bHasValue |= ColorSection->GetGreenChannel().Evaluate(QueryFrame, G);
                    bHasValue |= ColorSection->GetBlueChannel().Evaluate(QueryFrame, B);
                    bHasValue |= ColorSection->GetAlphaChannel().Evaluate(QueryFrame, A);

                    if (bHasValue)
                    {
                        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                        Obj->SetStringField(TEXT("widget"), BindingWidget);
                        Obj->SetStringField(TEXT("property"), PropertyName);
                        Obj->SetStringField(TEXT("value_type"), TEXT("color"));

                        TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
                        ColorObj->SetNumberField(TEXT("r"), R);
                        ColorObj->SetNumberField(TEXT("g"), G);
                        ColorObj->SetNumberField(TEXT("b"), B);
                        ColorObj->SetNumberField(TEXT("a"), A);
                        Obj->SetObjectField(TEXT("value"), ColorObj);

                        ValuesAtTime.Add(MakeShared<FJsonValueObject>(Obj));
                        break;
                    }
                }
            }

            // Vector2D (DoubleVector w/ 2 channels) tracks
            for (const UMovieSceneTrack* Track : Ctx.MovieScene->FindTracks(UMovieSceneDoubleVectorTrack::StaticClass(), ObjectGuid))
            {
                const UMovieSceneDoubleVectorTrack* VecTrack = Cast<UMovieSceneDoubleVectorTrack>(Track);
                if (!VecTrack || VecTrack->GetNumChannelsUsed() < 2) { continue; }
                const FString PropertyName = VecTrack->GetPropertyName().ToString();
                if (!PropertyFilter.IsEmpty() && PropertyName != PropertyFilter) { continue; }

                for (const UMovieSceneSection* Section : VecTrack->GetAllSections())
                {
                    const UMovieSceneDoubleVectorSection* VecSection = Cast<UMovieSceneDoubleVectorSection>(Section);
                    if (!VecSection) { continue; }
                    FMovieSceneChannelProxy& Proxy = const_cast<UMovieSceneDoubleVectorSection*>(VecSection)->GetChannelProxy();
                    TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
                    if (Channels.Num() < 2) { continue; }

                    double X = 0.0, Y = 0.0;
                    const bool bHasX = Channels[0]->Evaluate(QueryFrame, X);
                    const bool bHasY = Channels[1]->Evaluate(QueryFrame, Y);
                    if (bHasX || bHasY)
                    {
                        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                        Obj->SetStringField(TEXT("widget"), BindingWidget);
                        Obj->SetStringField(TEXT("property"), PropertyName);
                        Obj->SetStringField(TEXT("value_type"), TEXT("vector2d"));

                        TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
                        VecObj->SetNumberField(TEXT("x"), X);
                        VecObj->SetNumberField(TEXT("y"), Y);
                        Obj->SetObjectField(TEXT("value"), VecObj);

                        ValuesAtTime.Add(MakeShared<FJsonValueObject>(Obj));
                        break;
                    }
                }
            }
        }

        TSharedPtr<FJsonObject> Slice = MakeShared<FJsonObject>();
        Slice->SetNumberField(TEXT("time"), QueryTime);
        Slice->SetNumberField(TEXT("values_count"), ValuesAtTime.Num());
        Slice->SetArrayField(TEXT("values"), ValuesAtTime);
        Slices.Add(MakeShared<FJsonValueObject>(Slice));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    if (bHasWidgetFilter) { Result->SetStringField(TEXT("widget"), WidgetFilter); }
    if (!PropertyFilter.IsEmpty()) { Result->SetStringField(TEXT("property_filter"), PropertyFilter); }
    Result->SetNumberField(TEXT("slice_count"), Slices.Num());
    Result->SetArrayField(TEXT("slices"), Slices);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Sampled %d slice(s) on animation '%s'"), Slices.Num(), *Ctx.Animation->GetName()),
        Result);
}

/**
 * Append a single keyframe at one timestamp across multiple widgets and properties.
 * This is the "vertical slice" inverse of append_widget_tracks (which writes many
 * times across one widget's properties). Useful for snapshot-style animation
 * authoring: "at t=1.0s, set BG.opacity=0.5 AND Title.translation=(100,0)".
 *
 * Inputs:
 *   widget_blueprint_path (required) - widget BP asset path
 *   animation_name        (required) - target animation
 *   time                  (required) - single timestamp in seconds
 *   widgets               (required) - array of {widget_name, properties:{prop:value,...}}
 *
 * Each entry in widgets[] is dispatched to ExecuteSetPropertyKeys with a single
 * key at the given time. Property value shape (number / {r,g,b,a} / {x,y}) drives
 * the same auto-detection as set_property_keys.
 */
FMCPToolResult FMCPTool_UMGAnimation::ExecuteAppendTimeSlice(const TSharedRef<FJsonObject>& Params)
{
    FMCPToolResult Error;
    FAnimationContext Ctx;
    if (!ResolveAnimationContext(Params, Ctx, Error)) { return Error; }

    double TimeSeconds = 0.0;
    if (!Params->TryGetNumberField(TEXT("time"), TimeSeconds))
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: time (seconds)"));
    }

    const TArray<TSharedPtr<FJsonValue>>* WidgetsPtr = nullptr;
    if (!Params->TryGetArrayField(TEXT("widgets"), WidgetsPtr) || !WidgetsPtr)
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: widgets (array)"));
    }

    int32 KeysTotal = 0;
    TArray<TSharedPtr<FJsonValue>> WidgetSummaries;

    // For each widget entry, iterate its `properties` map and dispatch to
    // ExecuteSetPropertyKeys with a single-key array at TimeSeconds.
    for (const TSharedPtr<FJsonValue>& WidgetVal : *WidgetsPtr)
    {
        TSharedPtr<FJsonObject> WidgetObj = WidgetVal.IsValid() ? WidgetVal->AsObject() : nullptr;
        if (!WidgetObj.IsValid()) { continue; }

        FString WidgetName;
        if (!WidgetObj->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
        {
            return FMCPToolResult::Error(TEXT("Each entry of 'widgets' needs a 'widget_name' string."));
        }

        const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
        if (!WidgetObj->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) || !PropertiesObjPtr || !PropertiesObjPtr->IsValid())
        {
            return FMCPToolResult::Error(TEXT("Each widget entry needs a 'properties' object."));
        }
        TSharedPtr<FJsonObject> PropertiesObj = *PropertiesObjPtr;

        int32 WidgetKeys = 0;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
        {
            const FString& PropertyName = Pair.Key;

            // Build the single-key array {time, value: <whatever the user passed>}.
            TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
            KeyObj->SetNumberField(TEXT("time"), TimeSeconds);
            KeyObj->SetField(TEXT("value"), Pair.Value);

            TArray<TSharedPtr<FJsonValue>> KeysArray;
            KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));

            // Dispatch through the existing set_property_keys handler.
            TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
            SubParams->SetStringField(TEXT("operation"), UMGAnimationOps::SetPropertyKeys);
            SubParams->SetStringField(TEXT("widget_blueprint_path"), Ctx.Blueprint->GetPathName());
            SubParams->SetStringField(TEXT("animation_name"), Ctx.Animation->GetName());
            SubParams->SetStringField(TEXT("widget_name"), WidgetName);
            SubParams->SetStringField(TEXT("property_name"), PropertyName);
            SubParams->SetArrayField(TEXT("keys"), KeysArray);

            FMCPToolResult SubResult = ExecuteSetPropertyKeys(SubParams);
            if (!SubResult.bSuccess) { return SubResult; }

            ++WidgetKeys;
            ++KeysTotal;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("widget"), WidgetName);
        Summary->SetNumberField(TEXT("keys_applied"), WidgetKeys);
        WidgetSummaries.Add(MakeShared<FJsonValueObject>(Summary));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("animation"), Ctx.Animation->GetName());
    Result->SetNumberField(TEXT("time"), TimeSeconds);
    Result->SetNumberField(TEXT("widgets_updated"), WidgetSummaries.Num());
    Result->SetNumberField(TEXT("keys_total"), KeysTotal);
    Result->SetArrayField(TEXT("widgets"), WidgetSummaries);
    return FMCPToolResult::Success(
        FString::Printf(TEXT("Appended slice @ %.3fs across %d widget(s) (%d key(s))"),
            TimeSeconds, WidgetSummaries.Num(), KeysTotal),
        Result);
}
