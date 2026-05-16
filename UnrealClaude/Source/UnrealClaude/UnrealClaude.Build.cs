// Copyright Natali Caggiano. All Rights Reserved.

using UnrealBuildTool;

public class UnrealClaude : ModuleRules
{
	public UnrealClaude(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"ToolMenus",
				"Projects",
				"EditorFramework",
				"WorkspaceMenuStructure"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Project Settings -> Plugins -> Unreal Claude (UUnrealClaudeSettings)
				"DeveloperSettings",
				"Json",
				"JsonUtilities",
				"HTTP",
				"HTTPServer",
				"Sockets",
				"Networking",
				"ImageWrapper",
				// Blueprint manipulation
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"AssetRegistry",
				"AssetTools",
				// Animation Blueprint manipulation
				"AnimGraph",
				"AnimGraphRuntime",
				// Asset saving
				"EditorScriptingUtilities",
				// Enhanced Input
				"EnhancedInput",
				// UMG widget editing (Story 1: UMG CRUD)
				"UMG",
				"UMGEditor",
				// Material graph editing (Story 2: Material Graph + HLSL)
				"MaterialEditor",
				// UMG animation / Sequencer (Story 3)
				"MovieScene",
				"MovieSceneTracks",
				// PR-B: UMG session anchor subsystem
				"EditorSubsystem",
				// PR-E: StateTree tool family (yes-ue-mcp MIT, adapted)
				"StateTreeModule",
				"StateTreeEditorModule",
				// PR-G: Niagara tool family ()
				"Niagara",
				"NiagaraEditor",
				// PR-G: GAS tool family ()
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks"
			}
		);

		// Clipboard support (FPlatformApplicationMisc) on all platforms
		PrivateDependencyModuleNames.Add("ApplicationCore");

		// Windows only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// LiveCoding is only available in editor builds on Windows
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("LiveCoding");
			}
		}
	}
}
