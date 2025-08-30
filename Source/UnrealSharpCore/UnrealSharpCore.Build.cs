using System;
using System.Diagnostics;
using System.IO;
using UnrealBuildTool;

public class UnrealSharpCore : ModuleRules
{
	private readonly string _managedPath;
	private readonly string _managedBinariesPath;
	private readonly string _engineGluePath;
	
	public UnrealSharpCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		_managedPath = Path.Combine(PluginDirectory, "Managed");
		_managedBinariesPath = Path.Combine(PluginDirectory, "Binaries", "Managed");
		_engineGluePath = Path.Combine(_managedPath, "UnrealSharp", "UnrealSharp", "Generated");
		
		PublicDefinitions.Add("GENERATED_GLUE_PATH=" + _engineGluePath.Replace("\\","/"));
		PublicDefinitions.Add("PLUGIN_PATH=" + PluginDirectory.Replace("\\","/"));
		PublicDefinitions.Add("BUILDING_EDITOR=" + (Target.bBuildEditor ? "1" : "0"));
		
		// Mobile platform support
		if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicDefinitions.Add("MOBILE_PLATFORM=1");
			PublicDefinitions.Add("USE_MONO_RUNTIME=1");
		}
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore", 
				"Boost", 
				"XmlParser",
				"Json", 
				"Projects",
				"UMG", 
				"DeveloperSettings", 
				"UnrealSharpProcHelper", 
				"EnhancedInput", 
				"UnrealSharpUtilities",
				"GameplayTags", 
				"AIModule",
				"UnrealSharpBinds",
				"FieldNotification",
				"InputCore",
			}
			);

		// Add mobile platform specific modules
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("AndroidPermission");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PrivateDependencyModuleNames.Add("IOSRuntimeSettings");
		}

        PublicIncludePaths.AddRange(new string[] { ModuleDirectory });
        PublicDefinitions.Add("ForceAsEngineGlue=1");

        IncludeDotNetHeaders();

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd", 
				"EditorSubsystem",
				"BlueprintGraph",
				"BlueprintEditorLibrary"
			});
			
			PublishSolution(Path.Combine(_managedPath, "UnrealSharpPrograms"));
		}
		
		if (Target.bGenerateProjectFiles && Directory.Exists(_engineGluePath))
		{
			PublishSolution(Path.Combine(_managedPath, "UnrealSharp"));
		}
	}
	
	private void IncludeDotNetHeaders()
	{
		PublicSystemIncludePaths.Add(Path.Combine(_managedPath, "DotNetRuntime", "inc"));
	}
	
	public static string FindDotNetExecutable()
	{
		const string DOTNET_WIN = "dotnet.exe";
		const string DOTNET_UNIX = "dotnet";

		string dotnetExe = OperatingSystem.IsWindows() ? DOTNET_WIN : DOTNET_UNIX;

		string pathVariable = Environment.GetEnvironmentVariable("PATH");
    
		if (pathVariable == null)
		{
			return null;
		}
    
		string[] paths = pathVariable.Split(Path.PathSeparator);
    
		foreach (string path in paths)
		{
			// This is a hack to avoid using the dotnet.exe from the Unreal Engine installation directory.
			// Can't use the dotnet.exe from the Unreal Engine installation directory because it's .NET 6.0
			if (!path.Contains(@"\dotnet\"))
			{
				continue;
			}
			
			var dotnetExePath = Path.Combine(path, dotnetExe);
			
			if (File.Exists(dotnetExePath))
			{
				return dotnetExePath;
			}
		}

		if ( OperatingSystem.IsMacOS() ) 
		{
			if (File.Exists("/usr/local/share/dotnet/dotnet")) 
			{
				return "/usr/local/share/dotnet/dotnet";
			}
			if (File.Exists("/opt/homebrew/bin/dotnet")) 
			{
				return "/opt/homebrew/bin/dotnet";
			}
		}

		throw new Exception($"Couldn't find {dotnetExe} in PATH!");
	}

	void PublishSolution(string projectRootDirectory)
	{
		if (!Directory.Exists(projectRootDirectory))
		{
			throw new Exception($"Couldn't find project root directory: {projectRootDirectory}");
		}

        Console.WriteLine($"Start publish solution: {projectRootDirectory}");

        string dotnetPath = FindDotNetExecutable();
		
		Process process = new Process();
		process.StartInfo.FileName = dotnetPath;
		process.StartInfo.WorkingDirectory = projectRootDirectory;
		
		process.StartInfo.ArgumentList.Add("publish");
		process.StartInfo.ArgumentList.Add($"\"{projectRootDirectory}\"");
		
		process.StartInfo.ArgumentList.Add($"-p:PublishDir=\"{_managedBinariesPath}\"");
		
		// Add mobile platform specific publish configurations
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("android-arm64");
			process.StartInfo.ArgumentList.Add("-p:UseMonoRuntime=true");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("ios-arm64");
			process.StartInfo.ArgumentList.Add("-p:UseMonoRuntime=true");
		}
		
		process.Start();
		process.WaitForExit();
		
		if (process.ExitCode != 0)
		{
			Console.WriteLine($"Failed to publish solution: {projectRootDirectory}");
		}
	}
}


