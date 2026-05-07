// Copyright Ban Ming. All Rights Reserved.
// 
// 

#include "MCPTool_GASModify.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

// ---------------------------------------------------------------------------
// Local helpers (file-scope anonymous namespace)
// ---------------------------------------------------------------------------
namespace
{
    /**
     * Convert a string to EGameplayEffectDurationType enum value.
     * Accepts "Instant", "Infinite", "HasDuration" (case-insensitive).
     * Returns EGameplayEffectDurationType::Instant on unrecognised input.
     *
     * @param In - Input string
     * @return Matching enum value
     */
    EGameplayEffectDurationType ParseDurationPolicy(const FString& In)
    {
        if (In.Equals(TEXT("Infinite"),    ESearchCase::IgnoreCase)) return EGameplayEffectDurationType::Infinite;
        if (In.Equals(TEXT("HasDuration"), ESearchCase::IgnoreCase)) return EGameplayEffectDurationType::HasDuration;
        return EGameplayEffectDurationType::Instant;
    }

    /**
     * Convert a string to EGameplayModOp::Type.
     * Accepts "Add" / "Additive", "Multiply" / "Multiplicative", "Override" / "Division".
     * Returns EGameplayModOp::Additive on unrecognised input.
     *
     * @param In - Input string
     * @return Matching enum value
     */
    EGameplayModOp::Type ParseModOp(const FString& In)
    {
        if (In.Equals(TEXT("Multiply"),       ESearchCase::IgnoreCase)) return EGameplayModOp::Multiplicitive;
        if (In.Equals(TEXT("Multiplicative"), ESearchCase::IgnoreCase)) return EGameplayModOp::Multiplicitive;
        if (In.Equals(TEXT("Override"),       ESearchCase::IgnoreCase)) return EGameplayModOp::Override;
        if (In.Equals(TEXT("Division"),       ESearchCase::IgnoreCase)) return EGameplayModOp::Division;
        return EGameplayModOp::Additive; // covers "Add" and "Additive"
    }

    /**
     * Convert EGameplayEffectDurationType to a human-readable string for JSON output.
     *
     * @param Policy - Duration policy enum value
     * @return String representation
     */
    FString DurationPolicyToString(EGameplayEffectDurationType Policy)
    {
        switch (Policy)
        {
            case EGameplayEffectDurationType::Instant:     return TEXT("Instant");
            case EGameplayEffectDurationType::Infinite:    return TEXT("Infinite");
            case EGameplayEffectDurationType::HasDuration: return TEXT("HasDuration");
            default:                                       return TEXT("Unknown");
        }
    }

    /**
     * Extract an array of strings from a JSON parameter.
     * Returns an empty array if the field is absent or not an array.
     *
     * @param Params - JSON object to read from
     * @param FieldName - Name of the JSON array field
     * @return Extracted strings (may be empty)
     */
    TArray<FString> ExtractStringArray(
        const TSharedRef<FJsonObject>& Params,
        const FString& FieldName)
    {
        TArray<FString> Result;
        const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
        if (Params->TryGetArrayField(FieldName, JsonArray) && JsonArray)
        {
            for (const TSharedPtr<FJsonValue>& Val : *JsonArray)
            {
                FString Str;
                if (Val.IsValid() && Val->TryGetString(Str) && !Str.IsEmpty())
                {
                    Result.Add(Str);
                }
            }
        }
        return Result;
    }
} // namespace

// ---------------------------------------------------------------------------
// GetInfo
// ---------------------------------------------------------------------------
FMCPToolInfo FMCPTool_GASModify::GetInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("gas_modify");
    Info.Description = TEXT(
        "Read and write Gameplay Ability System (GAS) assets: AttributeSets, "
        "GameplayAbilities, and GameplayEffects.\n\n"
        "IMPORTANT: For create_* operations, prefer supplying a Paoge* project "
        "subclass as parent_class (e.g., /Game/Abilities/PaogeGameplayAbility."
        "PaogeGameplayAbility_C) per project ADR-0009/0011. Using engine base "
        "classes directly violates the project's subclass-first architecture.\n\n"
        "Operations:\n"
        "- list_attribute_sets:          Optional path, recursive, include_native. "
        "Lists AttributeSet Blueprints AND native C++ subclasses (the project uses "
        "native UPaogeHealthAttributeSet etc per ADR-0009/0011); each entry has a "
        "'source' field (\"native\" | \"blueprint\").\n"
        "- list_abilities:               Optional path, recursive, include_native. "
        "Lists GameplayAbility Blueprints AND native C++ subclasses with tag containers.\n"
        "- list_effects:                 Optional path, recursive, include_native. "
        "Lists GameplayEffect Blueprints AND native C++ subclasses with duration "
        "policy and modifier count.\n"
        "- create_ability_blueprint:     Required class_name + output_path. "
        "Optional parent_class (default: /Script/GameplayAbilities.GameplayAbility). "
        "Refuses if asset already exists.\n"
        "- create_attribute_set_blueprint: Required class_name + output_path. "
        "Optional parent_class (default: /Script/GameplayAbilities.AttributeSet). "
        "Refuses if asset already exists.\n"
        "- create_effect_blueprint:      Required class_name + output_path. "
        "Optional parent_class, duration_policy (Instant|Infinite|HasDuration). "
        "Refuses if asset already exists.\n"
        "- set_ability_tags:             Required ability_path. Optional arrays "
        "ability_tags, cancel_tags, block_tags (replaces existing containers).\n"
        "- set_effect_modifier:          Required effect_path, attribute "
        "(\"ClassName.PropertyName\"), op (Add|Multiply|Override), magnitude.");

    Info.Parameters = {
        FMCPToolParameter(TEXT("operation"), TEXT("string"),
            TEXT("Operation: list_attribute_sets | list_abilities | list_effects | "
                 "create_ability_blueprint | create_attribute_set_blueprint | "
                 "create_effect_blueprint | set_ability_tags | set_effect_modifier"),
            true),

        // ---- list_* shared params ----
        FMCPToolParameter(TEXT("path"), TEXT("string"),
            TEXT("Content path to search (default: /Game). Applies only to Blueprint assets — "
                 "native C++ classes are enumerated globally regardless of this path."),
            false, TEXT("/Game")),
        FMCPToolParameter(TEXT("recursive"), TEXT("boolean"),
            TEXT("Search subdirectories recursively for Blueprint assets (default: true)"),
            false, TEXT("true")),
        FMCPToolParameter(TEXT("include_native"), TEXT("boolean"),
            TEXT("Include native C++ subclasses in addition to Blueprint assets (default: true). "
                 "Each returned entry carries a 'source' field: \"native\" | \"blueprint\". "
                 "Native enumeration uses TObjectIterator<UClass> across all loaded modules; "
                 "abstract / deprecated / superseded classes are filtered out."),
            false, TEXT("true")),

        // ---- create_* shared params ----
        FMCPToolParameter(TEXT("class_name"), TEXT("string"),
            TEXT("create_*: short asset name for the new Blueprint"), false),
        FMCPToolParameter(TEXT("output_path"), TEXT("string"),
            TEXT("create_*: content folder path, e.g. /Game/Abilities"), false),
        FMCPToolParameter(TEXT("parent_class"), TEXT("string"),
            TEXT("create_*: parent class path. PREFER a Paoge* subclass per ADR-0009/0011. "
                 "Fallback defaults: GameplayAbility | AttributeSet | GameplayEffect"),
            false),

        // ---- create_effect_blueprint only ----
        FMCPToolParameter(TEXT("duration_policy"), TEXT("string"),
            TEXT("create_effect_blueprint: Instant | Infinite | HasDuration (default: Instant)"),
            false, TEXT("Instant")),

        // ---- set_ability_tags params ----
        FMCPToolParameter(TEXT("ability_path"), TEXT("string"),
            TEXT("set_ability_tags: content path to the ability Blueprint asset"), false),
        FMCPToolParameter(TEXT("ability_tags"), TEXT("array"),
            TEXT("set_ability_tags: array of tag name strings for AbilityTags"), false),
        FMCPToolParameter(TEXT("cancel_tags"), TEXT("array"),
            TEXT("set_ability_tags: array of tag name strings for CancelAbilitiesWithTag"), false),
        FMCPToolParameter(TEXT("block_tags"), TEXT("array"),
            TEXT("set_ability_tags: array of tag name strings for BlockAbilitiesWithTag"), false),

        // ---- set_effect_modifier params ----
        FMCPToolParameter(TEXT("effect_path"), TEXT("string"),
            TEXT("set_effect_modifier: content path to the GameplayEffect Blueprint"), false),
        FMCPToolParameter(TEXT("attribute"), TEXT("string"),
            TEXT("set_effect_modifier: attribute in \"ClassName.PropertyName\" format, "
                 "e.g. \"PaogeHealthAttributeSet.Health\""),
            false),
        FMCPToolParameter(TEXT("op"), TEXT("string"),
            TEXT("set_effect_modifier: Add | Multiply | Override"), false),
        FMCPToolParameter(TEXT("magnitude"), TEXT("number"),
            TEXT("set_effect_modifier: scalar magnitude for the modifier"), false),
    };

    Info.Annotations = FMCPToolAnnotations::Modifying();
    return Info;
}

// ---------------------------------------------------------------------------
// Execute — dispatch
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Read required "operation" parameter.
    FString Operation;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 2: Route to the matching handler.
    if (Operation == TEXT("list_attribute_sets"))          return ExecuteListAttributeSets(Params);
    if (Operation == TEXT("list_abilities"))               return ExecuteListAbilities(Params);
    if (Operation == TEXT("list_effects"))                 return ExecuteListEffects(Params);
    if (Operation == TEXT("create_ability_blueprint"))     return ExecuteCreateAbilityBlueprint(Params);
    if (Operation == TEXT("create_attribute_set_blueprint")) return ExecuteCreateAttributeSetBlueprint(Params);
    if (Operation == TEXT("create_effect_blueprint"))      return ExecuteCreateEffectBlueprint(Params);
    if (Operation == TEXT("set_ability_tags"))             return ExecuteSetAbilityTags(Params);
    if (Operation == TEXT("set_effect_modifier"))          return ExecuteSetEffectModifier(Params);

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation '%s'. Expected: list_attribute_sets | list_abilities | "
             "list_effects | create_ability_blueprint | create_attribute_set_blueprint | "
             "create_effect_blueprint | set_ability_tags | set_effect_modifier"),
        *Operation));
}

// ---------------------------------------------------------------------------
// Read: list_attribute_sets
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteListAttributeSets(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse path / recursive / include_native params.
    const FString SearchPath     = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));
    const bool    bRecursive     = ExtractOptionalBool(Params, TEXT("recursive"),      true);
    const bool    bIncludeNative = ExtractOptionalBool(Params, TEXT("include_native"), true);

    // Cache the FGameplayAttributeData script struct once — used by both BP and native paths.
    UScriptStruct* AttrDataStruct = TBaseStructure<FGameplayAttributeData>::Get();

    // Step 2: Build the result array.
    TArray<TSharedPtr<FJsonValue>> SetsArray;
    int32 BlueprintCount = 0;
    int32 NativeCount    = 0;

    // Step 2a: Enumerate Blueprint assets via Asset Registry.
    TArray<FAssetData> Assets = FindBlueprintsByParentClass(SearchPath, TEXT("AttributeSet"), bRecursive);
    for (const FAssetData& AssetData : Assets)
    {
        TSharedPtr<FJsonObject> SetObj = MakeShared<FJsonObject>();
        SetObj->SetStringField(TEXT("source"),          TEXT("blueprint"));
        SetObj->SetStringField(TEXT("class_path"),      AssetData.GetObjectPathString());
        SetObj->SetStringField(TEXT("blueprint_name"),  AssetData.AssetName.ToString());

        TArray<TSharedPtr<FJsonValue>> AttributesArray;
        UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
        if (BP && BP->GeneratedClass)
        {
            for (TFieldIterator<FStructProperty> It(BP->GeneratedClass); It; ++It)
            {
                FStructProperty* Prop = *It;
                if (Prop && Prop->Struct == AttrDataStruct)
                {
                    TSharedPtr<FJsonObject> AttrObj = MakeShared<FJsonObject>();
                    AttrObj->SetStringField(TEXT("name"), Prop->GetName());
                    AttrObj->SetStringField(TEXT("type"), TEXT("FGameplayAttributeData"));
                    AttributesArray.Add(MakeShared<FJsonValueObject>(AttrObj));
                }
            }
        }
        SetObj->SetArrayField(TEXT("attributes"), AttributesArray);

        SetsArray.Add(MakeShared<FJsonValueObject>(SetObj));
        ++BlueprintCount;
    }

    // Step 2b: Enumerate native C++ subclasses via TObjectIterator<UClass>.
    if (bIncludeNative)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Cls = *It;
            if (!Cls) continue;
            if (Cls == UAttributeSet::StaticClass()) continue;
            if (!Cls->IsChildOf(UAttributeSet::StaticClass())) continue;

            // Skip abstract / deprecated / superseded classes.
            if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;

            // Skip BP-generated classes (already handled by the Asset Registry pass).
            if (Cls->ClassGeneratedBy != nullptr) continue;

            TSharedPtr<FJsonObject> SetObj = MakeShared<FJsonObject>();
            SetObj->SetStringField(TEXT("source"),     TEXT("native"));
            SetObj->SetStringField(TEXT("class_path"), Cls->GetClassPathName().ToString());
            SetObj->SetStringField(TEXT("class_name"), Cls->GetName());

            // Module / package name (e.g., "/Script/Paoge").
            if (UPackage* OuterPkg = Cls->GetOuterUPackage())
            {
                SetObj->SetStringField(TEXT("module"), OuterPkg->GetName());
            }

            // Enumerate FGameplayAttributeData fields the same way as the BP path.
            TArray<TSharedPtr<FJsonValue>> AttributesArray;
            for (TFieldIterator<FStructProperty> FieldIt(Cls); FieldIt; ++FieldIt)
            {
                FStructProperty* Prop = *FieldIt;
                if (Prop && Prop->Struct == AttrDataStruct)
                {
                    TSharedPtr<FJsonObject> AttrObj = MakeShared<FJsonObject>();
                    AttrObj->SetStringField(TEXT("name"), Prop->GetName());
                    AttrObj->SetStringField(TEXT("type"), TEXT("FGameplayAttributeData"));
                    AttributesArray.Add(MakeShared<FJsonValueObject>(AttrObj));
                }
            }
            SetObj->SetArrayField(TEXT("attributes"), AttributesArray);

            SetsArray.Add(MakeShared<FJsonValueObject>(SetObj));
            ++NativeCount;
        }
    }

    // Step 3: Build top-level result.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("sets"),             SetsArray);
    ResultData->SetNumberField(TEXT("count"),           SetsArray.Num());
    ResultData->SetNumberField(TEXT("blueprint_count"), BlueprintCount);
    ResultData->SetNumberField(TEXT("native_count"),    NativeCount);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d AttributeSet(s) (%d blueprint, %d native) under %s"),
            SetsArray.Num(), BlueprintCount, NativeCount, *SearchPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Read: list_abilities
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteListAbilities(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse path / recursive / include_native params.
    const FString SearchPath     = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));
    const bool    bRecursive     = ExtractOptionalBool(Params, TEXT("recursive"),      true);
    const bool    bIncludeNative = ExtractOptionalBool(Params, TEXT("include_native"), true);

    // Cache reflected property handles for the protected tag containers — used by both paths.
    FStructProperty* CancelProp = FindFProperty<FStructProperty>(
        UGameplayAbility::StaticClass(), TEXT("CancelAbilitiesWithTag"));
    FStructProperty* BlockProp = FindFProperty<FStructProperty>(
        UGameplayAbility::StaticClass(), TEXT("BlockAbilitiesWithTag"));

    // Lambda: populate ability_tags / cancel_tags / block_tags fields from a CDO.
    auto PopulateTagContainers = [this, CancelProp, BlockProp]
        (UGameplayAbility* CDO, TSharedPtr<FJsonObject>& AbilityObj)
    {
        if (!CDO) return;
        AbilityObj->SetArrayField(TEXT("ability_tags"), TagContainerToJsonArray(CDO->GetAssetTags()));
        if (CancelProp)
        {
            const FGameplayTagContainer* CancelTags =
                CancelProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            AbilityObj->SetArrayField(TEXT("cancel_tags"), TagContainerToJsonArray(*CancelTags));
        }
        if (BlockProp)
        {
            const FGameplayTagContainer* BlockTags =
                BlockProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            AbilityObj->SetArrayField(TEXT("block_tags"), TagContainerToJsonArray(*BlockTags));
        }
    };

    // Step 2: Build the result array.
    TArray<TSharedPtr<FJsonValue>> AbilitiesArray;
    int32 BlueprintCount = 0;
    int32 NativeCount    = 0;

    // Step 2a: Enumerate Blueprint assets.
    TArray<FAssetData> Assets = FindBlueprintsByParentClass(SearchPath, TEXT("GameplayAbility"), bRecursive);
    for (const FAssetData& AssetData : Assets)
    {
        TSharedPtr<FJsonObject> AbilityObj = MakeShared<FJsonObject>();
        AbilityObj->SetStringField(TEXT("source"),     TEXT("blueprint"));
        AbilityObj->SetStringField(TEXT("class_path"), AssetData.GetObjectPathString());

        FString ParentClassTag;
        AssetData.GetTagValue(FName(TEXT("ParentClass")), ParentClassTag);
        AbilityObj->SetStringField(TEXT("parent_class"), ParentClassTag);

        UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
        if (BP && BP->GeneratedClass)
        {
            UGameplayAbility* CDO = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
            PopulateTagContainers(CDO, AbilityObj);
        }

        AbilitiesArray.Add(MakeShared<FJsonValueObject>(AbilityObj));
        ++BlueprintCount;
    }

    // Step 2b: Enumerate native C++ subclasses.
    if (bIncludeNative)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Cls = *It;
            if (!Cls) continue;
            if (Cls == UGameplayAbility::StaticClass()) continue;
            if (!Cls->IsChildOf(UGameplayAbility::StaticClass())) continue;
            if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
            if (Cls->ClassGeneratedBy != nullptr) continue;

            TSharedPtr<FJsonObject> AbilityObj = MakeShared<FJsonObject>();
            AbilityObj->SetStringField(TEXT("source"),     TEXT("native"));
            AbilityObj->SetStringField(TEXT("class_path"), Cls->GetClassPathName().ToString());
            AbilityObj->SetStringField(TEXT("class_name"), Cls->GetName());

            if (UClass* SuperCls = Cls->GetSuperClass())
            {
                AbilityObj->SetStringField(TEXT("parent_class"), SuperCls->GetClassPathName().ToString());
            }
            if (UPackage* OuterPkg = Cls->GetOuterUPackage())
            {
                AbilityObj->SetStringField(TEXT("module"), OuterPkg->GetName());
            }

            UGameplayAbility* CDO = Cast<UGameplayAbility>(Cls->GetDefaultObject());
            PopulateTagContainers(CDO, AbilityObj);

            AbilitiesArray.Add(MakeShared<FJsonValueObject>(AbilityObj));
            ++NativeCount;
        }
    }

    // Step 3: Build top-level result.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("abilities"),        AbilitiesArray);
    ResultData->SetNumberField(TEXT("count"),           AbilitiesArray.Num());
    ResultData->SetNumberField(TEXT("blueprint_count"), BlueprintCount);
    ResultData->SetNumberField(TEXT("native_count"),    NativeCount);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d GameplayAbility(ies) (%d blueprint, %d native) under %s"),
            AbilitiesArray.Num(), BlueprintCount, NativeCount, *SearchPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Read: list_effects
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteListEffects(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse path / recursive / include_native params.
    const FString SearchPath     = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));
    const bool    bRecursive     = ExtractOptionalBool(Params, TEXT("recursive"),      true);
    const bool    bIncludeNative = ExtractOptionalBool(Params, TEXT("include_native"), true);

    // Step 2: Build the result array.
    TArray<TSharedPtr<FJsonValue>> EffectsArray;
    int32 BlueprintCount = 0;
    int32 NativeCount    = 0;

    // Step 2a: Enumerate Blueprint assets.
    TArray<FAssetData> Assets = FindBlueprintsByParentClass(SearchPath, TEXT("GameplayEffect"), bRecursive);
    for (const FAssetData& AssetData : Assets)
    {
        TSharedPtr<FJsonObject> EffectObj = MakeShared<FJsonObject>();
        EffectObj->SetStringField(TEXT("source"),     TEXT("blueprint"));
        EffectObj->SetStringField(TEXT("class_path"), AssetData.GetObjectPathString());

        UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
        if (BP && BP->GeneratedClass)
        {
            UGameplayEffect* CDO = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
            if (CDO)
            {
                EffectObj->SetStringField(TEXT("duration_policy"),
                    DurationPolicyToString(CDO->DurationPolicy));
                EffectObj->SetNumberField(TEXT("modifier_count"), CDO->Modifiers.Num());
            }
        }
        else
        {
            EffectObj->SetStringField(TEXT("duration_policy"), TEXT("Unknown"));
            EffectObj->SetNumberField(TEXT("modifier_count"), 0);
        }

        EffectsArray.Add(MakeShared<FJsonValueObject>(EffectObj));
        ++BlueprintCount;
    }

    // Step 2b: Enumerate native C++ subclasses.
    if (bIncludeNative)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Cls = *It;
            if (!Cls) continue;
            if (Cls == UGameplayEffect::StaticClass()) continue;
            if (!Cls->IsChildOf(UGameplayEffect::StaticClass())) continue;
            if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
            if (Cls->ClassGeneratedBy != nullptr) continue;

            TSharedPtr<FJsonObject> EffectObj = MakeShared<FJsonObject>();
            EffectObj->SetStringField(TEXT("source"),     TEXT("native"));
            EffectObj->SetStringField(TEXT("class_path"), Cls->GetClassPathName().ToString());
            EffectObj->SetStringField(TEXT("class_name"), Cls->GetName());

            if (UClass* SuperCls = Cls->GetSuperClass())
            {
                EffectObj->SetStringField(TEXT("parent_class"), SuperCls->GetClassPathName().ToString());
            }
            if (UPackage* OuterPkg = Cls->GetOuterUPackage())
            {
                EffectObj->SetStringField(TEXT("module"), OuterPkg->GetName());
            }

            UGameplayEffect* CDO = Cast<UGameplayEffect>(Cls->GetDefaultObject());
            if (CDO)
            {
                EffectObj->SetStringField(TEXT("duration_policy"),
                    DurationPolicyToString(CDO->DurationPolicy));
                EffectObj->SetNumberField(TEXT("modifier_count"), CDO->Modifiers.Num());
            }
            else
            {
                EffectObj->SetStringField(TEXT("duration_policy"), TEXT("Unknown"));
                EffectObj->SetNumberField(TEXT("modifier_count"), 0);
            }

            EffectsArray.Add(MakeShared<FJsonValueObject>(EffectObj));
            ++NativeCount;
        }
    }

    // Step 3: Build top-level result.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("effects"),          EffectsArray);
    ResultData->SetNumberField(TEXT("count"),           EffectsArray.Num());
    ResultData->SetNumberField(TEXT("blueprint_count"), BlueprintCount);
    ResultData->SetNumberField(TEXT("native_count"),    NativeCount);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d GameplayEffect(s) (%d blueprint, %d native) under %s"),
            EffectsArray.Num(), BlueprintCount, NativeCount, *SearchPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Write: create_ability_blueprint
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteCreateAbilityBlueprint(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse required params.
    FString ClassName, OutputPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("class_name"),  ClassName,   ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("output_path"), OutputPath,  ParamError)) return ParamError.GetValue();

    const FString ParentClassPath = ExtractOptionalString(
        Params, TEXT("parent_class"),
        TEXT("/Script/GameplayAbilities.GameplayAbility"));

    // Step 2: Resolve parent class and validate it descends from UGameplayAbility.
    UClass* ParentClass = LoadUClass(ParentClassPath, ParamError);
    if (!ParentClass) return ParamError.GetValue();

    if (!ParentClass->IsChildOf(UGameplayAbility::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("parent_class '%s' does not descend from UGameplayAbility. "
                 "Use a UGameplayAbility-derived class (preferably a Paoge* subclass per ADR-0009)."),
            *ParentClassPath));
    }

    // Step 3: Create the Blueprint asset.
    UBlueprint* NewBP = nullptr;
    if (!CreateBlueprintAsset(ClassName, OutputPath, ParentClass, NewBP, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 4: Build response.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("class_path"),     NewBP->GeneratedClass
        ? NewBP->GeneratedClass->GetClassPathName().ToString()
        : FString::Printf(TEXT("%s.%s_C"), *OutputPath, *ClassName));
    ResultData->SetStringField(TEXT("blueprint_path"),
        FString::Printf(TEXT("%s/%s"), *OutputPath, *ClassName));

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Created ability Blueprint '%s' at %s"), *ClassName, *OutputPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Write: create_attribute_set_blueprint
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteCreateAttributeSetBlueprint(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse required params.
    FString ClassName, OutputPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("class_name"),  ClassName,  ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("output_path"), OutputPath, ParamError)) return ParamError.GetValue();

    const FString ParentClassPath = ExtractOptionalString(
        Params, TEXT("parent_class"),
        TEXT("/Script/GameplayAbilities.AttributeSet"));

    // Step 2: Resolve parent class and validate it descends from UAttributeSet.
    UClass* ParentClass = LoadUClass(ParentClassPath, ParamError);
    if (!ParentClass) return ParamError.GetValue();

    if (!ParentClass->IsChildOf(UAttributeSet::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("parent_class '%s' does not descend from UAttributeSet. "
                 "Use a UAttributeSet-derived class (preferably a Paoge* subclass per ADR-0009)."),
            *ParentClassPath));
    }

    // Step 3: Create the Blueprint asset.
    UBlueprint* NewBP = nullptr;
    if (!CreateBlueprintAsset(ClassName, OutputPath, ParentClass, NewBP, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 4: Build response.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("class_path"), NewBP->GeneratedClass
        ? NewBP->GeneratedClass->GetClassPathName().ToString()
        : FString::Printf(TEXT("%s.%s_C"), *OutputPath, *ClassName));

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Created AttributeSet Blueprint '%s' at %s"), *ClassName, *OutputPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Write: create_effect_blueprint
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteCreateEffectBlueprint(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse required params.
    FString ClassName, OutputPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("class_name"),  ClassName,  ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("output_path"), OutputPath, ParamError)) return ParamError.GetValue();

    const FString ParentClassPath  = ExtractOptionalString(
        Params, TEXT("parent_class"),
        TEXT("/Script/GameplayAbilities.GameplayEffect"));
    const FString DurationPolicyStr = ExtractOptionalString(Params, TEXT("duration_policy"), TEXT("Instant"));

    // Step 2: Resolve parent class and validate it descends from UGameplayEffect.
    UClass* ParentClass = LoadUClass(ParentClassPath, ParamError);
    if (!ParentClass) return ParamError.GetValue();

    if (!ParentClass->IsChildOf(UGameplayEffect::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("parent_class '%s' does not descend from UGameplayEffect. "
                 "Use a UGameplayEffect-derived class (preferably a Paoge* subclass per ADR-0011)."),
            *ParentClassPath));
    }

    // Step 3: Create the Blueprint asset.
    UBlueprint* NewBP = nullptr;
    if (!CreateBlueprintAsset(ClassName, OutputPath, ParentClass, NewBP, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 4: Apply duration policy to CDO if the Blueprint was created successfully.
    if (NewBP && NewBP->GeneratedClass)
    {
        UGameplayEffect* CDO = Cast<UGameplayEffect>(NewBP->GeneratedClass->GetDefaultObject());
        if (CDO)
        {
            FScopedTransaction Transaction(FText::FromString(
                FString::Printf(TEXT("MCP: Set GE Duration Policy '%s'"), *ClassName)));
            NewBP->Modify();
            CDO->DurationPolicy = ParseDurationPolicy(DurationPolicyStr);
            FKismetEditorUtilities::CompileBlueprint(NewBP);
            NewBP->MarkPackageDirty();
        }
    }

    // Step 5: Build response.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("class_path"), NewBP->GeneratedClass
        ? NewBP->GeneratedClass->GetClassPathName().ToString()
        : FString::Printf(TEXT("%s.%s_C"), *OutputPath, *ClassName));
    ResultData->SetStringField(TEXT("duration_policy"), DurationPolicyStr);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Created GameplayEffect Blueprint '%s' (%s) at %s"),
            *ClassName, *DurationPolicyStr, *OutputPath),
        ResultData);
}

// ---------------------------------------------------------------------------
// Write: set_ability_tags
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteSetAbilityTags(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse required ability_path.
    FString AbilityPath;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("ability_path"), AbilityPath, ParamError))
    {
        return ParamError.GetValue();
    }

    // Step 2: Extract optional tag string arrays.
    const TArray<FString> AbilityTagStrings = ExtractStringArray(Params, TEXT("ability_tags"));
    const TArray<FString> CancelTagStrings  = ExtractStringArray(Params, TEXT("cancel_tags"));
    const TArray<FString> BlockTagStrings   = ExtractStringArray(Params, TEXT("block_tags"));

    // Step 3: Load the Blueprint.
    UBlueprint* BP = LoadBlueprint(AbilityPath, ParamError);
    if (!BP) return ParamError.GetValue();

    // Step 4: Validate the Blueprint generates a UGameplayAbility subclass.
    if (!BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(UGameplayAbility::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Asset at '%s' is not a GameplayAbility Blueprint."), *AbilityPath));
    }

    UGameplayAbility* CDO = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
    if (!CDO)
    {
        return FMCPToolResult::Error(
            TEXT("Failed to obtain GameplayAbility CDO. The Blueprint may need to be compiled first."));
    }

    // Step 5: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(
        FString::Printf(TEXT("MCP: Set GA Tags on '%s'"), *AbilityPath)));
    BP->Modify();

    // Only overwrite containers that were actually supplied in the call.
    // AbilityTags: deprecated in 5.5 — use SetAssetTags().
    // CancelAbilitiesWithTag / BlockAbilitiesWithTag: protected — write via FProperty reflection.
    int32 NumAbility = 0, NumCancel = 0, NumBlock = 0;
    if (!AbilityTagStrings.IsEmpty())
    {
        // SetAssetTags is protected — write via FProperty reflection on the
        // backing AssetTags field (the underlying storage for AbilityTags).
        FStructProperty* AssetTagsProp = FindFProperty<FStructProperty>(
            UGameplayAbility::StaticClass(), TEXT("AssetTags"));
        if (!AssetTagsProp)
        {
            AssetTagsProp = FindFProperty<FStructProperty>(
                UGameplayAbility::StaticClass(), TEXT("AbilityTags"));
        }
        if (AssetTagsProp)
        {
            const FGameplayTagContainer NewAbilityTags = BuildTagContainer(AbilityTagStrings);
            FGameplayTagContainer* Slot =
                AssetTagsProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            *Slot      = NewAbilityTags;
            NumAbility = Slot->Num();
        }
    }
    if (!CancelTagStrings.IsEmpty())
    {
        FStructProperty* CancelProp = FindFProperty<FStructProperty>(
            UGameplayAbility::StaticClass(), TEXT("CancelAbilitiesWithTag"));
        if (CancelProp)
        {
            FGameplayTagContainer* Slot =
                CancelProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            *Slot     = BuildTagContainer(CancelTagStrings);
            NumCancel = Slot->Num();
        }
    }
    if (!BlockTagStrings.IsEmpty())
    {
        FStructProperty* BlockProp = FindFProperty<FStructProperty>(
            UGameplayAbility::StaticClass(), TEXT("BlockAbilitiesWithTag"));
        if (BlockProp)
        {
            FGameplayTagContainer* Slot =
                BlockProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            *Slot    = BuildTagContainer(BlockTagStrings);
            NumBlock = Slot->Num();
        }
    }

    FKismetEditorUtilities::CompileBlueprint(BP);
    BP->MarkPackageDirty();

    // Step 6: Build response.
    TSharedPtr<FJsonObject> TagsSet = MakeShared<FJsonObject>();
    TagsSet->SetNumberField(TEXT("ability"), NumAbility);
    TagsSet->SetNumberField(TEXT("cancel"),  NumCancel);
    TagsSet->SetNumberField(TEXT("block"),   NumBlock);

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("ability_path"), AbilityPath);
    ResultData->SetObjectField(TEXT("tags_set"),     TagsSet);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Tags set on '%s' (ability:%d cancel:%d block:%d)"),
            *AbilityPath, NumAbility, NumCancel, NumBlock),
        ResultData);
}

// ---------------------------------------------------------------------------
// Write: set_effect_modifier
// ---------------------------------------------------------------------------
FMCPToolResult FMCPTool_GASModify::ExecuteSetEffectModifier(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: Parse required params.
    FString EffectPath, AttributeString, OpString;
    TOptional<FMCPToolResult> ParamError;
    if (!ExtractRequiredString(Params, TEXT("effect_path"), EffectPath,        ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("attribute"),   AttributeString,   ParamError)) return ParamError.GetValue();
    if (!ExtractRequiredString(Params, TEXT("op"),          OpString,          ParamError)) return ParamError.GetValue();

    double Magnitude = 0.0;
    if (!Params->TryGetNumberField(TEXT("magnitude"), Magnitude))
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: magnitude"));
    }

    // Step 2: Load the Blueprint.
    UBlueprint* BP = LoadBlueprint(EffectPath, ParamError);
    if (!BP) return ParamError.GetValue();

    // Step 3: Validate it generates a UGameplayEffect subclass.
    if (!BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
    {
        return FMCPToolResult::Error(FString::Printf(
            TEXT("Asset at '%s' is not a GameplayEffect Blueprint."), *EffectPath));
    }

    UGameplayEffect* CDO = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
    if (!CDO)
    {
        return FMCPToolResult::Error(
            TEXT("Failed to obtain GameplayEffect CDO. The Blueprint may need to be compiled first."));
    }

    // Step 4: Resolve the attribute string to a FGameplayAttribute.
    FGameplayAttribute ResolvedAttr = ResolveAttribute(AttributeString, ParamError);
    if (!ResolvedAttr.IsValid())
    {
        return ParamError.IsSet()
            ? ParamError.GetValue()
            : FMCPToolResult::Error(FString::Printf(
                TEXT("Could not resolve attribute '%s'. Use \"ClassName.PropertyName\" format."),
                *AttributeString));
    }

    // Step 5: Build FGameplayModifierInfo.
    FGameplayModifierInfo ModInfo;
    ModInfo.Attribute      = ResolvedAttr;
    ModInfo.ModifierOp     = ParseModOp(OpString);
    ModInfo.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Magnitude)));

    // Step 6: Transaction-wrapped mutation.
    FScopedTransaction Transaction(FText::FromString(
        FString::Printf(TEXT("MCP: Add GE Modifier '%s' to '%s'"), *AttributeString, *EffectPath)));
    BP->Modify();

    const int32 AddedIndex = CDO->Modifiers.Add(ModInfo);

    FKismetEditorUtilities::CompileBlueprint(BP);
    BP->MarkPackageDirty();

    // Step 7: Build response.
    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetNumberField(TEXT("modifier_index_added"), AddedIndex);
    ResultData->SetStringField(TEXT("attribute"),            AttributeString);
    ResultData->SetStringField(TEXT("op"),                   OpString);
    ResultData->SetNumberField(TEXT("magnitude"),            Magnitude);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Modifier added to '%s': %s %s %.4g (index %d)"),
            *EffectPath, *AttributeString, *OpString, Magnitude, AddedIndex),
        ResultData);
}

// ---------------------------------------------------------------------------
// Shared Helpers
// ---------------------------------------------------------------------------

TArray<FAssetData> FMCPTool_GASModify::FindBlueprintsByParentClass(
    const FString& SearchPath,
    const FString& ParentClassFragment,
    bool bRecursive) const
{
    // Step 1: Get asset registry.
    FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& Registry = Module.Get();

    // Step 2: Build filter for Blueprint assets under the search path.
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*SearchPath));
    Filter.bRecursivePaths   = bRecursive;
    Filter.bRecursiveClasses = false;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));

    TArray<FAssetData> AllBlueprints;
    Registry.GetAssets(Filter, AllBlueprints);

    // Step 3: Filter by parent class tags — cheaper than loading each asset.
    TArray<FAssetData> Matched;
    for (const FAssetData& AssetData : AllBlueprints)
    {
        // Check both NativeParentClass and ParentClass tags.
        FString NativeParent, ParentClass;
        AssetData.GetTagValue(FName(TEXT("NativeParentClass")), NativeParent);
        AssetData.GetTagValue(FName(TEXT("ParentClass")),       ParentClass);

        if (NativeParent.Contains(ParentClassFragment, ESearchCase::IgnoreCase) ||
            ParentClass.Contains( ParentClassFragment, ESearchCase::IgnoreCase))
        {
            Matched.Add(AssetData);
        }
    }

    return Matched;
}

UBlueprint* FMCPTool_GASModify::LoadBlueprint(
    const FString& BlueprintPath,
    TOptional<FMCPToolResult>& OutError) const
{
    // Step 1: Attempt load — try with and without _C suffix.
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!BP)
    {
        // Some callers pass the generated class path (with _C); strip it.
        FString CleanPath = BlueprintPath;
        if (CleanPath.EndsWith(TEXT("_C")))
        {
            CleanPath.LeftChopInline(2, EAllowShrinking::No);
            BP = LoadObject<UBlueprint>(nullptr, *CleanPath);
        }
    }

    if (!BP)
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Failed to load Blueprint at '%s'. Verify the asset path is correct."),
            *BlueprintPath));
        return nullptr;
    }
    return BP;
}

UClass* FMCPTool_GASModify::LoadUClass(
    const FString& ClassPath,
    TOptional<FMCPToolResult>& OutError) const
{
    // Step 1: Try direct load (works for native paths like /Script/GameplayAbilities.GameplayAbility).
    UClass* Class = LoadClass<UObject>(nullptr, *ClassPath);

    // Step 2: For Blueprint classes, try with _C suffix.
    if (!Class && ClassPath.StartsWith(TEXT("/Game/")) && !ClassPath.EndsWith(TEXT("_C")))
    {
        FString BlueprintClassPath = ClassPath + TEXT("_C");
        Class = LoadClass<UObject>(nullptr, *BlueprintClassPath);
    }

    // Step 3: Last-resort FindObject fallback.
    if (!Class)
    {
        Class = FindObject<UClass>(nullptr, *ClassPath);
    }

    if (!Class)
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Could not resolve class '%s'. Check the path is correct."), *ClassPath));
        return nullptr;
    }
    return Class;
}

bool FMCPTool_GASModify::CreateBlueprintAsset(
    const FString& ClassName,
    const FString& OutputPath,
    UClass* ParentClass,
    UBlueprint*& OutBlueprint,
    TOptional<FMCPToolResult>& OutError) const
{
    // Step 1: Refuse if an asset already exists at the target path.
    const FString FullAssetPath = FString::Printf(TEXT("%s/%s"), *OutputPath, *ClassName);
    if (FindObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *FullAssetPath, *ClassName)))
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Asset already exists at '%s'. Use a different class_name or output_path."),
            *FullAssetPath));
        return false;
    }

    // Step 2: Set up BlueprintFactory with the given parent class.
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    // Step 3: Create the asset via AssetTools module.
    FAssetToolsModule& AssetToolsModule =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

    UObject* NewAsset = AssetToolsModule.Get().CreateAsset(
        ClassName, OutputPath, UBlueprint::StaticClass(), Factory);

    OutBlueprint = Cast<UBlueprint>(NewAsset);
    if (!OutBlueprint)
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("AssetTools failed to create Blueprint '%s' in '%s'. "
                 "The output_path folder may not exist in the Content Browser."),
            *ClassName, *OutputPath));
        return false;
    }

    // Step 4: Compile and mark dirty so the asset is ready for use.
    FKismetEditorUtilities::CompileBlueprint(OutBlueprint);
    OutBlueprint->MarkPackageDirty();

    return true;
}

FGameplayTagContainer FMCPTool_GASModify::BuildTagContainer(const TArray<FString>& TagStrings) const
{
    FGameplayTagContainer Container;
    for (const FString& TagName : TagStrings)
    {
        // RequestGameplayTag returns an invalid tag (IsValid() == false) if the
        // tag is not registered; we skip those silently to avoid hard failures.
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(*TagName), /*bErrorIfNotFound=*/false);
        if (Tag.IsValid())
        {
            Container.AddTag(Tag);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("FMCPTool_GASModify: tag '%s' is not registered; skipping."), *TagName);
        }
    }
    return Container;
}

FGameplayAttribute FMCPTool_GASModify::ResolveAttribute(
    const FString& AttributeString,
    TOptional<FMCPToolResult>& OutError) const
{
    // Step 1: Split "ClassName.PropertyName" on the first dot.
    FString ClassName, PropertyName;
    if (!AttributeString.Split(TEXT("."), &ClassName, &PropertyName))
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Attribute '%s' must be in \"ClassName.PropertyName\" format, "
                 "e.g. \"PaogeHealthAttributeSet.Health\"."),
            *AttributeString));
        return FGameplayAttribute();
    }

    // Step 2: Find the UAttributeSet-derived class matching ClassName.
    UClass* FoundAttrClass = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Cls = *It;
        if (Cls && Cls->IsChildOf(UAttributeSet::StaticClass()) &&
            !Cls->HasAnyClassFlags(CLASS_Abstract) &&
            Cls->GetName().Equals(ClassName, ESearchCase::IgnoreCase))
        {
            FoundAttrClass = Cls;
            break;
        }
    }

    if (!FoundAttrClass)
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("AttributeSet class '%s' not found. Ensure the module defining it is loaded."),
            *ClassName));
        return FGameplayAttribute();
    }

    // Step 3: Iterate FStructProperty fields of type FGameplayAttributeData to find PropertyName.
    UScriptStruct* AttrDataStruct = TBaseStructure<FGameplayAttributeData>::Get();
    for (TFieldIterator<FStructProperty> It(FoundAttrClass); It; ++It)
    {
        FStructProperty* Prop = *It;
        if (Prop && Prop->Struct == AttrDataStruct &&
            Prop->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
        {
            return FGameplayAttribute(Prop);
        }
    }

    OutError = FMCPToolResult::Error(FString::Printf(
        TEXT("Property '%s' not found on AttributeSet class '%s'. "
             "Verify the property exists and is of type FGameplayAttributeData."),
        *PropertyName, *ClassName));
    return FGameplayAttribute();
}

TArray<TSharedPtr<FJsonValue>> FMCPTool_GASModify::TagContainerToJsonArray(
    const FGameplayTagContainer& Container) const
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FGameplayTag& Tag : Container)
    {
        Result.Add(MakeShared<FJsonValueString>(Tag.GetTagName().ToString()));
    }
    return Result;
}
