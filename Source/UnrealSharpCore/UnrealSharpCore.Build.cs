using System;
using System.Diagnostics;
using System.IO;
using UnrealBuildTool;

public class UnrealSharpCore : ModuleRules
{
	private readonly string _managedPath;
	private readonly string _managedBinariesPath;
	private readonly string _engineGluePath;
	
	// Runtime selection switch - Set to true to use native .NET runtime on desktop platforms
	// Default: false (use Mono runtime on all platforms for consistency)
	// Can be overridden by environment variable UNREAL_SHARP_USE_DOTNET_RUNTIME=true
	private static readonly bool UseDesktopDotNetRuntime = GetRuntimePreference();
	
	public UnrealSharpCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		_managedPath = Path.Combine(PluginDirectory, "Managed");
		_managedBinariesPath = Path.Combine(PluginDirectory, "Binaries", "Managed");
		_engineGluePath = Path.Combine(_managedPath, "UnrealSharp", "UnrealSharp", "Generated");
		
		PublicDefinitions.Add("GENERATED_GLUE_PATH=" + _engineGluePath.Replace("\\","/"));
		PublicDefinitions.Add("PLUGIN_PATH=" + PluginDirectory.Replace("\\","/"));
		PublicDefinitions.Add("BUILDING_EDITOR=" + (Target.bBuildEditor ? "1" : "0"));
		
		// Platform-specific runtime configuration
		bool isMobilePlatform = Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS;
		bool useMonoRuntime = isMobilePlatform || !UseDesktopDotNetRuntime;
		
		if (isMobilePlatform)
		{
			PublicDefinitions.Add("MOBILE_PLATFORM=1");
		}
		
		if (useMonoRuntime)
		{
			PublicDefinitions.Add("USE_MONO_RUNTIME=1");
			Console.WriteLine($"UnrealSharp: Using Mono runtime for {Target.Platform}");
		}
		else
		{
			PublicDefinitions.Add("USE_DOTNET_NATIVE_RUNTIME=1");
			Console.WriteLine($"UnrealSharp: Using .NET 9 native runtime for {Target.Platform}");
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

		// Add Mono runtime support when using Mono
		if (useMonoRuntime)
		{
			PrivateDependencyModuleNames.Add("Mono");
			
			// Add Mono include paths
			string MonoPath = Path.Combine(PluginDirectory, "Source", "ThirdParty", "Mono");
			PublicIncludePaths.Add(Path.Combine(MonoPath, "src"));
			
			// Add Mono-specific definitions
			PublicDefinitions.Add("WITH_MONO_RUNTIME=1");
			
			// Add iOS-specific hot reload support
			if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				PublicDefinitions.Add("UNREALSHARP_IOS_HOTRELOAD_ENABLED=1");
				// Add iOS-specific include paths
				PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public", "iOS"));
			}
		}
		else
		{
			PublicDefinitions.Add("WITH_MONO_RUNTIME=0");
		}

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

	private static bool GetRuntimePreference()
	{
		// Check environment variable first
		string envValue = Environment.GetEnvironmentVariable("UNREAL_SHARP_USE_DOTNET_RUNTIME");
		if (!string.IsNullOrEmpty(envValue))
		{
			bool.TryParse(envValue, out bool envPreference);
			Console.WriteLine($"UnrealSharp: Runtime preference from environment variable: {(envPreference ? ".NET 9" : "Mono")}");
			return envPreference;
		}

		// Default to Mono for consistency with UnrealCSharp approach
		Console.WriteLine("UnrealSharp: Using default runtime preference: Mono (set UNREAL_SHARP_USE_DOTNET_RUNTIME=true to use .NET 9 on desktop)");
		return false;
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
		
		// Add platform specific publish configurations
		bool isMobilePlatform = Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS;
		bool useMonoRuntime = isMobilePlatform || !UseDesktopDotNetRuntime;
		
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("android-arm64");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("ios-arm64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("win-x64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("osx-x64");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			process.StartInfo.ArgumentList.Add("-r");
			process.StartInfo.ArgumentList.Add("linux-x64");
		}
		
		// Set runtime type
		if (useMonoRuntime)
		{
			process.StartInfo.ArgumentList.Add("-p:UseMonoRuntime=true");
		}
		else
		{
			process.StartInfo.ArgumentList.Add("-p:UseMonoRuntime=false");
		}
		
		process.Start();
		process.WaitForExit();
		
		if (process.ExitCode != 0)
		{
			Console.WriteLine($"Failed to publish solution: {projectRootDirectory}");
		}
	}
}


