// Copyright Natali Caggiano. All Rights Reserved.
// Portions adapted from UmgMcp (MIT) (c) 2025-2026 Winyunq.
// https://github.com/winyunq/UnrealMotionGraphicsMCP

#include "UMGCommonUtils.h"
#include "PropertyNameMappings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Styling/SlateBrush.h"

#include "JsonObjectConverter.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace UMGCommonUtils
{
    // -------------------------------------------------------------------
    // LoadWidgetBlueprint
    // -------------------------------------------------------------------
    UWidgetBlueprint* LoadWidgetBlueprint(const FString& BlueprintPath, FString& OutError)
    {
        // Step 1. Sanity check the path string.
        if (BlueprintPath.IsEmpty())
        {
            OutError = TEXT("widget_blueprint_path is empty");
            return nullptr;
        }

        // Step 2. Try a direct LoadObject first (handles "/Game/UI/WBP_Foo" cleanly).
        UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *BlueprintPath);

        // Step 3. Fallback: strip a possible "_C" suffix and retry. Some MCP callers
        //         pass the generated class path instead of the asset path.
        if (!WBP && BlueprintPath.EndsWith(TEXT("_C")))
        {
            FString Stripped = BlueprintPath.LeftChop(2);
            WBP = LoadObject<UWidgetBlueprint>(nullptr, *Stripped);
        }

        if (!WBP)
        {
            OutError = FString::Printf(TEXT("Failed to load WidgetBlueprint at path: %s"), *BlueprintPath);
            return nullptr;
        }

        return WBP;
    }

    // -------------------------------------------------------------------
    // ResolveWidgetClass — 4-tier fallback resolution
    // -------------------------------------------------------------------
    UClass* ResolveWidgetClass(const FString& WidgetType)
    {
        if (WidgetType.IsEmpty())
        {
            return nullptr;
        }

        // Tier 1. Looks like a full asset path (contains '/').
        if (WidgetType.Contains(TEXT("/")))
        {
            // 1a. Direct FindObject (already-loaded class).
            if (UClass* Found = FindObject<UClass>(nullptr, *WidgetType))
            {
                return Found;
            }

            // 1b. LoadObject<UClass> — covers /Script/* native classes.
            if (UClass* Loaded = LoadObject<UClass>(nullptr, *WidgetType))
            {
                return Loaded;
            }

            // 1c. /Game/...: try with "_C" suffix to grab the generated class.
            if (WidgetType.StartsWith(TEXT("/Game/")) && !WidgetType.EndsWith(TEXT("_C")))
            {
                FString WithSuffix = WidgetType + TEXT("_C");
                if (UClass* GenClass = LoadObject<UClass>(nullptr, *WithSuffix))
                {
                    return GenClass;
                }
            }
        }

        // Tier 2. /Game-only path without leading slash — uncommon but cheap to try.
        if (WidgetType.StartsWith(TEXT("Game/")))
        {
            FString Prefixed = TEXT("/") + WidgetType;
            if (UClass* C = LoadObject<UClass>(nullptr, *Prefixed))
            {
                return C;
            }
        }

        // Tier 3. /Script/UMG.<Type> — unmodified type name.
        {
            FString Native = FString::Printf(TEXT("/Script/UMG.%s"), *WidgetType);
            if (UClass* C = LoadObject<UClass>(nullptr, *Native))
            {
                return C;
            }
        }

        // Tier 4. /Script/UMG.U<Type> — auto-prefix the canonical UMG U-prefix.
        {
            FString WithU = FString::Printf(TEXT("/Script/UMG.U%s"), *WidgetType);
            if (UClass* C = LoadObject<UClass>(nullptr, *WithU))
            {
                return C;
            }
        }

        return nullptr;
    }

    // -------------------------------------------------------------------
    // FindWidgetByName
    // -------------------------------------------------------------------
    UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FName& WidgetName)
    {
        if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
        {
            return nullptr;
        }
        return WidgetBlueprint->WidgetTree->FindWidget(WidgetName);
    }

    // -------------------------------------------------------------------
    // ExportWidgetTreeToJson — recursive shallow-tree export
    // -------------------------------------------------------------------
    TSharedPtr<FJsonObject> ExportWidgetTreeToJson(UWidget* Widget)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        if (!Widget)
        {
            return Out;
        }

        // Step 1. Basic identity.
        Out->SetStringField(TEXT("name"), Widget->GetName());
        Out->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
        Out->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

        // Step 2. Recurse into children if this is a panel.
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            TArray<TSharedPtr<FJsonValue>> ChildArr;
            const int32 ChildCount = Panel->GetChildrenCount();
            ChildArr.Reserve(ChildCount);
            for (int32 i = 0; i < ChildCount; ++i)
            {
                if (UWidget* Child = Panel->GetChildAt(i))
                {
                    ChildArr.Add(MakeShared<FJsonValueObject>(ExportWidgetTreeToJson(Child)));
                }
            }
            Out->SetArrayField(TEXT("children"), ChildArr);
        }

        return Out;
    }

    // -------------------------------------------------------------------
    // ExportWidgetPropertiesToJson — full reflection dump
    // -------------------------------------------------------------------
    void ExportWidgetPropertiesToJson(UWidget* Widget, const TSharedRef<FJsonObject>& OutObject)
    {
        if (!Widget)
        {
            return;
        }

        // Step 1. Identity fields.
        OutObject->SetStringField(TEXT("name"), Widget->GetName());
        OutObject->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

        // Step 2. Widget-level UPROPERTYs via FJsonObjectConverter.
        TSharedRef<FJsonObject> WidgetProps = MakeShared<FJsonObject>();
        FJsonObjectConverter::UStructToJsonObject(
            Widget->GetClass(),
            Widget,
            WidgetProps,
            /*CheckFlags*/ 0,
            /*SkipFlags*/ 0);
        OutObject->SetObjectField(TEXT("properties"), WidgetProps);

        // Step 3. Slot props (separate sub-object — slot data lives on the slot UObject).
        if (Widget->Slot)
        {
            TSharedRef<FJsonObject> SlotProps = MakeShared<FJsonObject>();
            FJsonObjectConverter::UStructToJsonObject(
                Widget->Slot->GetClass(),
                Widget->Slot,
                SlotProps,
                0, 0);
            OutObject->SetObjectField(TEXT("slot"), SlotProps);
            OutObject->SetStringField(TEXT("slot_class"), Widget->Slot->GetClass()->GetName());
        }
    }

    // -------------------------------------------------------------------
    // NormalizeJsonKeysToPascalCase — recursive in-place rewrite
    // -------------------------------------------------------------------
    static TSharedPtr<FJsonValue> NormalizeJsonValueKeys(const TSharedPtr<FJsonValue>& InValue);

    static TSharedPtr<FJsonObject> NormalizeJsonObjectKeys(const TSharedPtr<FJsonObject>& InObject)
    {
        if (!InObject.IsValid())
        {
            return InObject;
        }

        TSharedPtr<FJsonObject> NewObject = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InObject->Values)
        {
            const FString NormalizedKey = UMGPropertyNameMappings::NormalizePropertyName(Pair.Key);
            NewObject->SetField(NormalizedKey, NormalizeJsonValueKeys(Pair.Value));
        }
        return NewObject;
    }

    static TSharedPtr<FJsonValue> NormalizeJsonValueKeys(const TSharedPtr<FJsonValue>& InValue)
    {
        if (!InValue.IsValid())
        {
            return InValue;
        }

        switch (InValue->Type)
        {
            case EJson::Object:
            {
                TSharedPtr<FJsonObject> NestedObj = InValue->AsObject();
                return MakeShared<FJsonValueObject>(NormalizeJsonObjectKeys(NestedObj));
            }
            case EJson::Array:
            {
                TArray<TSharedPtr<FJsonValue>> NewArr;
                for (const TSharedPtr<FJsonValue>& Element : InValue->AsArray())
                {
                    NewArr.Add(NormalizeJsonValueKeys(Element));
                }
                return MakeShared<FJsonValueArray>(NewArr);
            }
            default:
                return InValue;
        }
    }

    void NormalizeJsonKeysToPascalCase(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return;
        }

        // Build a normalized copy first, then swap field-by-field on the original.
        TSharedPtr<FJsonObject> Normalized = NormalizeJsonObjectKeys(JsonObject);
        JsonObject->Values.Empty();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Normalized->Values)
        {
            JsonObject->SetField(Pair.Key, Pair.Value);
        }
    }

    // -------------------------------------------------------------------
    // ExpandCanvasSlotAliases — convenience aliases for CanvasPanelSlot.
    // -------------------------------------------------------------------
    /**
     * Helper: read a 2-element JSON array (or x/y object) into Out0/Out1.
     * Returns true if the value was an object/array of size >= 2.
     */
    static bool ReadVec2(const TSharedPtr<FJsonValue>& Val, double& Out0, double& Out1)
    {
        if (!Val.IsValid())
        {
            return false;
        }
        if (Val->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
            if (Arr.Num() >= 2 && Arr[0].IsValid() && Arr[1].IsValid())
            {
                Out0 = Arr[0]->AsNumber();
                Out1 = Arr[1]->AsNumber();
                return true;
            }
        }
        else if (Val->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
            if (Obj.IsValid())
            {
                double X = 0, Y = 0;
                bool bHasX = Obj->TryGetNumberField(TEXT("X"), X) || Obj->TryGetNumberField(TEXT("x"), X);
                bool bHasY = Obj->TryGetNumberField(TEXT("Y"), Y) || Obj->TryGetNumberField(TEXT("y"), Y);
                if (bHasX && bHasY)
                {
                    Out0 = X;
                    Out1 = Y;
                    return true;
                }
            }
        }
        return false;
    }

    void ExpandCanvasSlotAliases(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return;
        }

        // We only act when there is a "Slot" sub-object.
        const TSharedPtr<FJsonObject>* SlotPtr = nullptr;
        if (!JsonObject->TryGetObjectField(TEXT("Slot"), SlotPtr) || !SlotPtr || !SlotPtr->IsValid())
        {
            return;
        }
        TSharedPtr<FJsonObject> Slot = *SlotPtr;

        // Lazily produce the LayoutData sub-object.
        auto GetLayoutData = [&]() -> TSharedPtr<FJsonObject>
        {
            const TSharedPtr<FJsonObject>* LP = nullptr;
            if (Slot->TryGetObjectField(TEXT("LayoutData"), LP) && LP && LP->IsValid())
            {
                return *LP;
            }
            TSharedPtr<FJsonObject> NewLayout = MakeShared<FJsonObject>();
            Slot->SetObjectField(TEXT("LayoutData"), NewLayout);
            return NewLayout;
        };

        auto GetLayoutChild = [&](const FString& Name) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Layout = GetLayoutData();
            const TSharedPtr<FJsonObject>* Sub = nullptr;
            if (Layout->TryGetObjectField(Name, Sub) && Sub && Sub->IsValid())
            {
                return *Sub;
            }
            TSharedPtr<FJsonObject> NewSub = MakeShared<FJsonObject>();
            Layout->SetObjectField(Name, NewSub);
            return NewSub;
        };

        // Alias 1. Slot.Position [x,y] -> Slot.LayoutData.Offsets.{Left, Top}
        if (TSharedPtr<FJsonValue> PosVal = Slot->TryGetField(TEXT("Position")))
        {
            double X = 0, Y = 0;
            if (ReadVec2(PosVal, X, Y))
            {
                TSharedPtr<FJsonObject> Offsets = GetLayoutChild(TEXT("Offsets"));
                Offsets->SetNumberField(TEXT("Left"), X);
                Offsets->SetNumberField(TEXT("Top"), Y);
                Slot->RemoveField(TEXT("Position"));
            }
        }

        // Alias 2. Slot.Size [w,h] -> Slot.LayoutData.Offsets.{Right, Bottom}
        if (TSharedPtr<FJsonValue> SizeVal = Slot->TryGetField(TEXT("Size")))
        {
            double W = 0, H = 0;
            if (ReadVec2(SizeVal, W, H))
            {
                TSharedPtr<FJsonObject> Offsets = GetLayoutChild(TEXT("Offsets"));
                Offsets->SetNumberField(TEXT("Right"), W);
                Offsets->SetNumberField(TEXT("Bottom"), H);
                Slot->RemoveField(TEXT("Size"));
            }
        }

        // Alias 3. Slot.Anchors -> Slot.LayoutData.Anchors (verbatim move).
        if (TSharedPtr<FJsonValue> AnchorsVal = Slot->TryGetField(TEXT("Anchors")))
        {
            TSharedPtr<FJsonObject> Layout = GetLayoutData();
            Layout->SetField(TEXT("Anchors"), AnchorsVal);
            Slot->RemoveField(TEXT("Anchors"));
        }

        // Alias 4. Slot.Alignment [x,y] -> Slot.LayoutData.Alignment.{X, Y}
        if (TSharedPtr<FJsonValue> AlignVal = Slot->TryGetField(TEXT("Alignment")))
        {
            double X = 0, Y = 0;
            if (ReadVec2(AlignVal, X, Y))
            {
                TSharedPtr<FJsonObject> Alignment = GetLayoutChild(TEXT("Alignment"));
                Alignment->SetNumberField(TEXT("X"), X);
                Alignment->SetNumberField(TEXT("Y"), Y);
                Slot->RemoveField(TEXT("Alignment"));
            }
        }
    }

    // -------------------------------------------------------------------
    // ApplyJsonToObject — reflect JSON into a UObject's UPROPERTY tree.
    // -------------------------------------------------------------------

    /**
     * Apply a single FSlateBrush JSON sub-object onto a target struct field.
     *
     * Why this exists:
     * FJsonObjectConverter cannot resolve FSlateBrush::ResourceObject (UObject*)
     * from a string. We intercept that field, LoadObject the asset, and write it
     * via FObjectProperty. Other brush fields (TintColor, ImageSize, DrawAs ...) we
     * still let the converter handle.
     *
     * @param BrushJson      JSON object describing the brush.
     * @param TargetBrush    Pointer to the FSlateBrush to populate.
     * @return               true if the field applied without error.
     */
    static bool ApplyBrushJson(const TSharedPtr<FJsonObject>& BrushJson, FSlateBrush* TargetBrush)
    {
        if (!BrushJson.IsValid() || !TargetBrush)
        {
            return false;
        }

        // Step 1. Pull out and load the ResourceObject (if specified).
        FString ResourcePath;
        if (BrushJson->TryGetStringField(TEXT("ResourceObject"), ResourcePath) && !ResourcePath.IsEmpty())
        {
            UObject* ResourceObj = LoadObject<UObject>(nullptr, *ResourcePath);
            TargetBrush->SetResourceObject(ResourceObj);
        }

        // Step 2. Apply the rest via the standard converter (skip the field we just handled).
        TSharedPtr<FJsonObject> Reduced = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BrushJson->Values)
        {
            if (Pair.Key != TEXT("ResourceObject"))
            {
                Reduced->SetField(Pair.Key, Pair.Value);
            }
        }
        if (Reduced->Values.Num() > 0)
        {
            return FJsonObjectConverter::JsonObjectToUStruct(
                Reduced.ToSharedRef(),
                FSlateBrush::StaticStruct(),
                TargetBrush,
                0, 0);
        }
        return true;
    }

    bool ApplyJsonToObject(
        const TSharedPtr<FJsonObject>& JsonObject,
        UObject* TargetObject,
        TArray<FString>& OutErrors)
    {
        if (!JsonObject.IsValid() || !TargetObject)
        {
            OutErrors.Add(TEXT("ApplyJsonToObject: null input"));
            return false;
        }

        // Step 1. Normalize keys to PascalCase first.
        NormalizeJsonKeysToPascalCase(JsonObject);

        UClass* Class = TargetObject->GetClass();
        bool bAnyApplied = false;

        // Step 2. Walk top-level keys; intercept Brush properties; defer the rest.
        for (TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObject->Values)
        {
            const FString& Key = Pair.Key;
            FProperty* Prop = Class->FindPropertyByName(*Key);
            if (!Prop)
            {
                OutErrors.Add(FString::Printf(TEXT("Property not found: %s on %s"),
                    *Key, *Class->GetName()));
                continue;
            }

            // Step 2a. Intercept FSlateBrush UPROPERTYs.
            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                if (StructProp->Struct == TBaseStructure<FSlateBrush>::Get()
                    || StructProp->Struct == FSlateBrush::StaticStruct())
                {
                    if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
                    {
                        FSlateBrush* BrushPtr = StructProp->ContainerPtrToValuePtr<FSlateBrush>(TargetObject);
                        if (ApplyBrushJson(Pair.Value->AsObject(), BrushPtr))
                        {
                            bAnyApplied = true;
                        }
                        else
                        {
                            OutErrors.Add(FString::Printf(TEXT("Failed to apply FSlateBrush: %s"), *Key));
                        }
                    }
                    continue;
                }
            }

            // Step 2b. Default path: per-field JSON->UProperty using the converter.
            void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
            if (FJsonObjectConverter::JsonValueToUProperty(Pair.Value, Prop, PropAddr, 0, 0))
            {
                bAnyApplied = true;
            }
            else
            {
                OutErrors.Add(FString::Printf(TEXT("Failed to apply property: %s"), *Key));
            }
        }

        return bAnyApplied;
    }
}
