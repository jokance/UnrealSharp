#pragma once

#include "CoreMinimal.h"

#if PLATFORM_IOS && WITH_MONO_RUNTIME

struct MonoMethod;
struct MonoAssembly;

/**
 * iOS Runtime Hot Reload System - NO RESTART REQUIRED
 * 
 * This system provides true runtime hot reload capabilities on iOS using advanced Mono features:
 * 
 * 1. Method Body Replacement - Replace method implementations at runtime
 * 2. Mono Interpreter Integration - Execute new code through interpreter
 * 3. Delta Application - Apply incremental changes using EnC delta files
 * 4. Dynamic Code Compilation - Compile and execute C# code at runtime
 * 
 * Key Advantages:
 * - No app restart required
 * - Immediate code changes visible
 * - Preserves application state
 * - Supports method body modifications
 * - Enables dynamic feature development
 * 
 * Limitations:
 * - Cannot modify core system assemblies
 * - Limited to method body changes (some structural changes supported)
 * - Performance impact from interpreter usage
 * - iOS development builds only
 */

namespace UnrealSharp::iOS::RuntimeHotReload
{
    /**
     * Hot reload statistics for monitoring and debugging
     */
    struct FHotReloadStats
    {
        int32 TotalAssemblies;
        int32 ActiveMethodReplacements;
        int32 TotalMethodReplacements;
        bool bInterpreterActive;
    };

    /**
     * Initialize Runtime Hot Reload system with advanced Mono features
     * This sets up method body replacement and interpreter integration
     * @return True if initialization successful
     */
    UNREALSHARPCORE_API bool InitializeRuntimeHotReload();

    /**
     * Replace method body at runtime without restarting the app
     * This is the core functionality for no-restart hot reload
     * @param OriginalMethod The method to replace
     * @param NewMethodBytecode Compiled bytecode for the new method implementation
     * @return True if method replacement successful
     */
    UNREALSHARPCORE_API bool ReplaceMethodBody(MonoMethod* OriginalMethod, const TArray<uint8>& NewMethodBytecode);

    /**
     * Hot reload entire assembly using Method Body Replacement technique
     * Processes delta files for incremental updates without restart
     * @param AssemblyName Name of the assembly to hot reload
     * @param DeltaData Delta file containing method changes
     * @return True if hot reload successful
     */
    UNREALSHARPCORE_API bool HotReloadAssemblyRuntime(const FString& AssemblyName, const TArray<uint8>& DeltaData);

    /**
     * Hot reload using Mono Evaluator for dynamic code compilation
     * This allows adding new classes and methods at runtime
     * @param CSharpCode C# source code to compile and execute
     * @return True if dynamic compilation successful
     */
    UNREALSHARPCORE_API bool HotReloadDynamicCode(const FString& CSharpCode);

    /**
     * Revert hot reload changes and restore original method implementations
     * Useful for testing and debugging hot reload changes
     * @param AssemblyName Assembly to revert changes for
     * @return True if revert successful
     */
    UNREALSHARPCORE_API bool RevertHotReload(const FString& AssemblyName);

    /**
     * Register assembly for hot reload tracking
     * Must be called for assemblies that should support hot reload
     * @param Assembly Assembly to register
     * @return True if registration successful
     */
    UNREALSHARPCORE_API bool RegisterAssemblyForHotReload(MonoAssembly* Assembly);

    /**
     * Get hot reload statistics for debugging and monitoring
     * @return Current hot reload statistics
     */
    UNREALSHARPCORE_API FHotReloadStats GetHotReloadStats();

    /**
     * Cleanup runtime hot reload system
     * Reverts all changes and releases resources
     */
    UNREALSHARPCORE_API void ShutdownRuntimeHotReload();
}

/**
 * Convenience macros for iOS runtime hot reload
 */
#define UNREALSHARP_IOS_RUNTIME_HOTRELOAD_INIT() UnrealSharp::iOS::RuntimeHotReload::InitializeRuntimeHotReload()
#define UNREALSHARP_IOS_RUNTIME_HOTRELOAD_SHUTDOWN() UnrealSharp::iOS::RuntimeHotReload::ShutdownRuntimeHotReload()

/**
 * Blueprint-friendly wrapper functions for hot reload
 */
UCLASS()
class UNREALSHARPCORE_API UiOSHotReloadBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Check if runtime hot reload is available on iOS
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp|iOS|Hot Reload", CallInEditor = true)
    static bool IsRuntimeHotReloadAvailable();

    /**
     * Hot reload C# code from string (for testing purposes)
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp|iOS|Hot Reload", CallInEditor = true)
    static bool HotReloadCSharpCode(const FString& CSharpCode);

    /**
     * Get hot reload statistics as a formatted string
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp|iOS|Hot Reload", CallInEditor = true)
    static FString GetHotReloadStatsString();

    /**
     * Revert all hot reload changes for an assembly
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp|iOS|Hot Reload", CallInEditor = true)
    static bool RevertAssemblyHotReload(const FString& AssemblyName);
};

#else

// Empty macros for non-iOS platforms
#define UNREALSHARP_IOS_RUNTIME_HOTRELOAD_INIT() (false)
#define UNREALSHARP_IOS_RUNTIME_HOTRELOAD_SHUTDOWN()

#endif // PLATFORM_IOS && WITH_MONO_RUNTIME