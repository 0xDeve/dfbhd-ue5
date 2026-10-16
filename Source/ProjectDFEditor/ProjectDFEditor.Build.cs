// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectDFEditor : ModuleRules
{
	public ProjectDFEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Json"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"AudioEditor",
			"AssetRegistry"
		});
	}
}
