// Copyright Ban Ming. All Rights Reserved.
// 
// 
// 

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"

/**
 * MCP Tool: read and write operations against Gameplay Ability System (GAS) assets.
 *
 * This is a compound tool — the required "operation" parameter routes to a
 * dedicated handler method for each of the 8 supported operations:
 *
 * Read operations:
 *   list_attribute_sets   - Find AttributeSet Blueprint assets AND native C++
 *                           subclasses (UPaogeHealthAttributeSet etc per
 *                           ADR-0009/0011). Each entry has a 'source' field
 *                           ("blueprint" | "native"). Returns class_path,
 *                           blueprint_name (BP only), class_name (native only),
 *                           module (native only), and attribute list.
 *   list_abilities        - Find GameplayAbility Blueprint assets AND native C++
 *                           subclasses. Each entry has 'source', class_path,
 *                           parent_class, and tag containers (ability/cancel/block).
 *   list_effects          - Find GameplayEffect Blueprint assets AND native C++
 *                           subclasses. Each entry has 'source', class_path,
 *                           duration_policy, modifier_count.
 *
 * Write operations (all require editor running; wrap in FScopedTransaction):
 *   create_ability_blueprint    - Create a new Blueprint subclassing a GameplayAbility class.
 *                                 Prefer passing a Paoge* subclass (ADR-0009/0011).
 *   create_attribute_set_blueprint - Create a new Blueprint subclassing an AttributeSet class.
 *   create_effect_blueprint     - Create a new Blueprint subclassing a GameplayEffect class.
 *   set_ability_tags            - Set AbilityTags / CancelAbilitiesWithTag / BlockAbilitiesWithTag
 *                                 on an existing ability Blueprint CDO.
 *   set_effect_modifier         - Append a modifier to an existing GameplayEffect Blueprint CDO.
 *
 * All write operations call Modify() + MarkPackageDirty() + CompileBlueprint() on
 * the target Blueprint asset after mutation.
 *
 * NOTE: Build.cs module dependencies for GameplayAbilities and GameplayTags are
 * declared separately (task #10). The includes compile only when those modules
 * are present in the build graph.
 */
class FMCPTool_GASModify : public FMCPToolBase
{
public:
    /**
     * Returns tool metadata: name, description, all parameter definitions,
     * and modifying annotations (non-destructive write).
     */
    virtual FMCPToolInfo GetInfo() const override;

    /**
     * Main dispatch entry point.
     * Reads the required "operation" parameter and routes to the appropriate
     * private Execute* method. Returns an error result for unknown operations.
     *
     * @param Params - JSON parameter object from the MCP caller
     * @return Tool result (success with data, or error)
     */
    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    // ===== Read Operations =====

    /**
     * List AttributeSet Blueprint assets AND native C++ subclasses.
     *
     * Two enumeration passes are merged:
     *  - Blueprint pass: Asset Registry query for BP assets whose
     *    NativeParentClass / ParentClass tag contains "AttributeSet". Each is
     *    loaded to enumerate FGameplayAttributeData struct properties.
     *  - Native pass (when include_native=true, default): TObjectIterator<UClass>
     *    over loaded modules, filtering for UAttributeSet subclasses, skipping
     *    abstract / deprecated / superseded / BP-generated classes.
     *
     * Each entry carries a 'source' field ("blueprint" | "native"). Native
     * entries also include 'class_name' and 'module'. Both attribute lists
     * are produced via the same FStructProperty / FGameplayAttributeData
     * inspection logic.
     *
     * Required because the project's AttributeSets are native (UPaogeHealthAttributeSet
     * etc per ADR-0009/0011) — a Blueprint-only enumeration returns 0 entries.
     *
     * @param Params - Optional "path" (default /Game), "recursive" (default true),
     *                 "include_native" (default true)
     * @return Success with {sets:[...], count, blueprint_count, native_count}
     */
    FMCPToolResult ExecuteListAttributeSets(const TSharedRef<FJsonObject>& Params);

    /**
     * List GameplayAbility Blueprint assets AND native C++ subclasses.
     *
     * Same dual-pass strategy as ExecuteListAttributeSets. For each entry the
     * CDO is inspected for asset tags (AbilityTags) and the protected
     * CancelAbilitiesWithTag / BlockAbilitiesWithTag containers (read via
     * FStructProperty reflection because the fields are protected since 5.5).
     *
     * @param Params - Optional "path" (default /Game), "recursive" (default true),
     *                 "include_native" (default true)
     * @return Success with {abilities:[...], count, blueprint_count, native_count}
     */
    FMCPToolResult ExecuteListAbilities(const TSharedRef<FJsonObject>& Params);

    /**
     * List GameplayEffect Blueprint assets AND native C++ subclasses.
     *
     * Same dual-pass strategy. Each entry is annotated with the CDO's
     * DurationPolicy (Instant | Infinite | HasDuration) and Modifiers count.
     *
     * @param Params - Optional "path" (default /Game), "recursive" (default true),
     *                 "include_native" (default true)
     * @return Success with {effects:[...], count, blueprint_count, native_count}
     */
    FMCPToolResult ExecuteListEffects(const TSharedRef<FJsonObject>& Params);

    // ===== Write Operations =====

    /**
     * Create a new Blueprint asset subclassing a GameplayAbility-derived class.
     * Validates that parent_class actually descends from UGameplayAbility.
     * Refuses if an asset already exists at the computed target path.
     * After creation, compiles the Blueprint and marks the package dirty.
     *
     * Prefer supplying a Paoge* subclass (e.g., /Game/Abilities/PaogeGameplayAbility)
     * as parent_class per ADR-0009/0011.
     *
     * @param Params - Required: class_name, output_path. Optional: parent_class
     * @return Success with {class_path, blueprint_path}
     */
    FMCPToolResult ExecuteCreateAbilityBlueprint(const TSharedRef<FJsonObject>& Params);

    /**
     * Create a new Blueprint asset subclassing an AttributeSet-derived class.
     * Validates that parent_class actually descends from UAttributeSet.
     * Refuses if an asset already exists at the computed target path.
     *
     * @param Params - Required: class_name, output_path. Optional: parent_class
     * @return Success with {class_path}
     */
    FMCPToolResult ExecuteCreateAttributeSetBlueprint(const TSharedRef<FJsonObject>& Params);

    /**
     * Create a new Blueprint asset subclassing a GameplayEffect-derived class.
     * Validates that parent_class actually descends from UGameplayEffect.
     * Refuses if an asset already exists at the computed target path.
     * Sets the duration_policy on the CDO if provided (Instant/Infinite/HasDuration).
     *
     * @param Params - Required: class_name, output_path. Optional: parent_class, duration_policy
     * @return Success with {class_path}
     */
    FMCPToolResult ExecuteCreateEffectBlueprint(const TSharedRef<FJsonObject>& Params);

    /**
     * Set GameplayTag containers on an existing GameplayAbility Blueprint CDO.
     * Loads the Blueprint by path, modifies AbilityTags / CancelAbilitiesWithTag /
     * BlockAbilitiesWithTag, recompiles, and marks the package dirty.
     * Wraps mutation in FScopedTransaction so undo works in editor.
     *
     * @param Params - Required: ability_path. Optional arrays: ability_tags, cancel_tags, block_tags
     * @return Success with {ability_path, tags_set:{ability:N, cancel:N, block:N}}
     */
    FMCPToolResult ExecuteSetAbilityTags(const TSharedRef<FJsonObject>& Params);

    /**
     * Append a modifier to an existing GameplayEffect Blueprint CDO.
     * Parses the attribute string ("ClassName.PropertyName"), resolves it to a
     * FGameplayAttribute, and appends a FGameplayModifierInfo to Modifiers[].
     * Recompiles the Blueprint and marks the package dirty.
     * Wraps mutation in FScopedTransaction.
     *
     * @param Params - Required: effect_path, attribute ("Class.Property"), op (Add|Multiply|Override), magnitude
     * @return Success with {modifier_index_added}
     */
    FMCPToolResult ExecuteSetEffectModifier(const TSharedRef<FJsonObject>& Params);

    // ===== Shared Helpers =====

    /**
     * Query the asset registry for Blueprint assets under SearchPath whose
     * NativeParentClass or ParentClass asset tag string contains ParentClassFragment.
     * bRecursive controls whether the path search descends into subdirectories.
     *
     * @param SearchPath - Content path to search (e.g., /Game)
     * @param ParentClassFragment - Substring to match against tag values (e.g., "GameplayAbility")
     * @param bRecursive - Whether to recurse into subdirectories
     * @return Array of matching FAssetData entries
     */
    TArray<FAssetData> FindBlueprintsByParentClass(
        const FString& SearchPath,
        const FString& ParentClassFragment,
        bool bRecursive) const;

    /**
     * Load a UBlueprint from the given asset path.
     * Returns nullptr and sets OutError if the load fails or the asset is
     * not a Blueprint.
     *
     * @param BlueprintPath - Full content path (e.g., /Game/Abilities/GA_Attack)
     * @param OutError - Populated with an error result on failure
     * @return Loaded UBlueprint, or nullptr
     */
    UBlueprint* LoadBlueprint(
        const FString& BlueprintPath,
        TOptional<FMCPToolResult>& OutError) const;

    /**
     * Load a UClass by path, trying with and without "_C" suffix for Blueprint classes.
     * Returns nullptr and sets OutError if the class cannot be resolved.
     *
     * @param ClassPath - Native or Blueprint class path (e.g., /Script/GameplayAbilities.GameplayAbility
     *                    or /Game/Abilities/PaogeGameplayAbility.PaogeGameplayAbility_C)
     * @param OutError - Populated with an error result on failure
     * @return Resolved UClass, or nullptr
     */
    UClass* LoadUClass(
        const FString& ClassPath,
        TOptional<FMCPToolResult>& OutError) const;

    /**
     * Create a Blueprint asset using AssetTools + BlueprintFactory.
     * Checks for existing asset at the target path before creation; refuses with
     * an error if one is found. Compiles the Blueprint and marks dirty on success.
     *
     * @param ClassName - Short asset name for the new Blueprint
     * @param OutputPath - Content folder path (e.g., /Game/Abilities)
     * @param ParentClass - Parent UClass for the factory (must already be validated)
     * @param OutBlueprint - Receives the newly created Blueprint on success
     * @param OutError - Populated with an error result on failure
     * @return true on success
     */
    bool CreateBlueprintAsset(
        const FString& ClassName,
        const FString& OutputPath,
        UClass* ParentClass,
        UBlueprint*& OutBlueprint,
        TOptional<FMCPToolResult>& OutError) const;

    /**
     * Build a FGameplayTagContainer from an array of tag name strings.
     * Each string is passed to FGameplayTag::RequestGameplayTag; tags that
     * cannot be resolved are silently skipped (logged as warnings).
     *
     * @param TagStrings - Array of tag name strings (e.g., "Ability.Attack")
     * @return Populated FGameplayTagContainer
     */
    FGameplayTagContainer BuildTagContainer(const TArray<FString>& TagStrings) const;

    /**
     * Resolve a "ClassName.PropertyName" attribute string to a FGameplayAttribute.
     * Iterates loaded UAttributeSet-derived UClass objects, matches the short class
     * name, then iterates FStructProperty fields for matching FGameplayAttributeData.
     *
     * @param AttributeString - Attribute in "ClassName.PropertyName" format
     * @param OutError - Populated with an error result if the attribute cannot be resolved
     * @return Resolved FGameplayAttribute (IsValid() == false on error)
     */
    FGameplayAttribute ResolveAttribute(
        const FString& AttributeString,
        TOptional<FMCPToolResult>& OutError) const;

    /**
     * Convert a GameplayTagContainer to a JSON array of tag name strings.
     *
     * @param Container - Source tag container
     * @return JSON value array (array of FJsonValueString)
     */
    TArray<TSharedPtr<FJsonValue>> TagContainerToJsonArray(
        const FGameplayTagContainer& Container) const;
};
