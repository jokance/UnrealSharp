# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

UnrealSharp is a C# integration plugin for Unreal Engine 5 that enables developers to write game logic in C# using .NET 9. The plugin provides seamless UE5 compatibility, hot reload capabilities, and automatic C# API generation from UE5 reflection data.

## Architecture

The codebase consists of two main architectural layers:

### C++ Plugin Layer (`Source/`)
- **UnrealSharpCore**: Core runtime module handling C#/UE5 bridge
- **UnrealSharpEditor**: Editor-time tools and compilation support
- **UnrealSharpCompiler**: Build system integration and compilation pipeline
- **UnrealSharpBlueprint**: Blueprint integration layer
- **UnrealSharpBinds**: Automatic binding generation from UE5 reflection
- **UnrealSharpAsync**: Async/await support and task management
- **UnrealSharpProcHelper**: Process management utilities
- **UnrealSharpRuntimeGlue**: Editor runtime bridging

### Managed C# Layer (`Managed/`)
- **UnrealSharp/**: Core C# libraries and UE5 API bindings
- **UnrealSharpPrograms/**: Build tools (BuildTool, Weaver)
- **Shared/**: Shared utilities and common code
- **DotNetRuntime/**: .NET runtime integration components

## Solution Structure

The project contains multiple solutions for different development scenarios:

1. **Main Solution** (`UnrealSharp.sln`): Complete solution including C++ and C# projects
2. **Managed Solution** (`Managed/UnrealSharp/UnrealSharp.sln`): Core C# libraries only
3. **Programs Solution** (`Managed/UnrealSharpPrograms/UnrealSharpPrograms.sln`): Build tools

## Development Commands

### Building C# Components

```bash
# Build core C# libraries (most common during development)
dotnet build Managed/UnrealSharp/UnrealSharp.sln

# Build build tools and utilities
dotnet build Managed/UnrealSharpPrograms/UnrealSharpPrograms.sln

# Build specific configuration
dotnet build --configuration Release
dotnet build --configuration Debug
```

### .NET Configuration
- **Target Framework**: .NET 9.0 (specified in `Managed/global.json`)
- **Package Management**: Centralized via `Directory.Packages.props`
- **Key Dependencies**: Mono.Cecil, Microsoft.CodeAnalysis.CSharp, CommandLineParser

## Plugin Integration

- **Plugin Definition**: `UnrealSharp.uplugin` defines all C++ modules and dependencies
- **Supported Platforms**: Windows, macOS, Linux, Android, iOS
- **UE5 Compatibility**: 5.3 - 5.6
- **Required UE5 Plugins**: EnhancedInput, StateTree, PluginBrowser

## Key Development Workflows

### Hot Reload Development
The plugin supports hot reloading of C# code without engine restart. When modifying C# code:
1. Make changes to C# files in `Managed/UnrealSharp/`
2. Build with `dotnet build`
3. UE5 editor automatically detects and reloads changes

### API Binding Generation
The plugin automatically generates C# bindings from UE5 reflection data through:
- **Source Generators** (`UnrealSharp.SourceGenerators/`)
- **Editor Source Generators** (`UnrealSharp.EditorSourceGenerators/`)
- **Extension Source Generators** (`UnrealSharp.ExtensionSourceGenerators/`)

### Build System Integration
The plugin integrates with UE5's build system via:
- C++ `.Build.cs` files in each `Source/` module
- Custom build tools in `UnrealSharpPrograms/`
- UBT plugin system (`Source/UnrealSharpManagedGlue/UnrealSharpManagedGlue.ubtplugin.csproj`)

## Important Notes

- **Plugin Placement**: Designed to be placed in UE5 project's `Plugins/` directory
- **Multiple Solutions**: Use appropriate solution file based on development focus
- **Thread Safety**: Recent improvements include comprehensive thread safety analysis and GC safety enhancements
- **Platform-Specific**: Contains platform-specific hot reload guides for Android and iOS