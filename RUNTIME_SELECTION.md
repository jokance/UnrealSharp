# UnrealSharp Runtime Selection Guide

UnrealSharp now supports flexible runtime selection, allowing you to choose between Mono runtime (default) and .NET 9 native runtime for different platforms.

## Default Behavior

**All platforms use Mono runtime by default** - consistent with UnrealCSharp approach:
- ✅ **Windows**: Mono Runtime
- ✅ **macOS**: Mono Runtime  
- ✅ **Linux**: Mono Runtime
- ✅ **Android**: Mono Runtime (forced)
- ✅ **iOS**: Mono Runtime (forced)

## Runtime Selection Options

### Option 1: Environment Variable (Recommended)

Set the environment variable to enable .NET 9 native runtime on desktop platforms:

```bash
# Windows (Command Prompt)
set UNREAL_SHARP_USE_DOTNET_RUNTIME=true

# Windows (PowerShell)
$env:UNREAL_SHARP_USE_DOTNET_RUNTIME="true"

# macOS/Linux
export UNREAL_SHARP_USE_DOTNET_RUNTIME=true
```

### Option 2: Build.cs Modification

Edit `/Source/UnrealSharpCore/UnrealSharpCore.Build.cs`:

```csharp
// Change this line from false to true
private static readonly bool UseDesktopDotNetRuntime = true;
```

## Runtime Behavior by Platform

| Platform | Default Runtime | Can Use .NET 9? | Performance |
|----------|----------------|------------------|-------------|
| Windows | Mono | ✅ Yes | .NET 9 > Mono |
| macOS | Mono | ✅ Yes | .NET 9 > Mono |
| Linux | Mono | ✅ Yes | .NET 9 > Mono |
| Android | Mono | ❌ No | Mono only |
| iOS | Mono | ❌ No | Mono only |

## When to Use Each Runtime

### Use Mono Runtime (Default) When:
- 🎯 You want **consistent behavior** across all platforms
- 📱 You're targeting **mobile platforms** (required)
- 🔧 You want **simpler debugging** (single runtime)
- ✅ You want **proven stability** (UnrealCSharp-tested approach)

### Use .NET 9 Native Runtime When:
- ⚡ You want **maximum performance** on desktop platforms
- 🆕 You need **latest C# features** 
- 🔍 You want **better debugging tools** on desktop
- 🖥️ You're **desktop-only** development

## Build Output Examples

When building, you'll see console output indicating the runtime choice:

```
UnrealSharp: Runtime preference from environment variable: Mono
UnrealSharp: Using Mono runtime for Win64

# OR

UnrealSharp: Runtime preference from environment variable: .NET 9  
UnrealSharp: Using .NET 9 native runtime for Win64
```

## Mobile Platform Notes

- **Android** and **iOS** always use Mono runtime regardless of settings
- Mobile platforms require AOT compilation which Mono handles better
- .NET 9 mobile support is not yet mature enough for production use

## Troubleshooting

### Build Errors
If you encounter build errors after changing runtime:
1. Clean and rebuild the project
2. Delete `Binaries/Managed` folder
3. Regenerate project files

### Performance Issues
If experiencing performance issues:
- Desktop: Try enabling .NET 9 runtime
- Mobile: Ensure Mono runtime is being used (default)

### Debugging Issues
- Mono: Use traditional debugging tools
- .NET 9: Leverage modern .NET debugging capabilities

## Migration from Previous Versions

If you were using the mixed runtime approach from earlier modifications:
1. The new default is Mono for all platforms
2. Use the environment variable to opt-in to .NET 9 on desktop
3. Your mobile builds will automatically use Mono (no change needed)

---

This approach provides the stability of UnrealCSharp's proven Mono-first strategy while offering the flexibility to use modern .NET performance when desired.