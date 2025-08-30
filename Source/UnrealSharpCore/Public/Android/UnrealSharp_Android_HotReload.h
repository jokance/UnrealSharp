#pragma once

#include "CoreMinimal.h"

#if WITH_MONO_RUNTIME && PLATFORM_ANDROID

#include "mono/jit/jit.h"
#include "mono/metadata/assembly.h"
#include "mono/metadata/class.h"
#include "mono/metadata/object.h"
#include "mono/metadata/appdomain.h"

/**
 * UnrealSharp Android Hot Reload System
 * 
 * Based on the same Mono method replacement approach as iOS, but optimized for Android
 * This implementation provides true runtime hot reload without app restart on Android devices.
 * 
 * Key features:
 * - Method body replacement using mono_method_set_unmanaged_thunk
 * - Assembly Load Context switching with MonoDomain isolation
 * - Interpreter-based hot reload with Mono evaluator
 * - Android-specific optimizations
 */

namespace UnrealSharp::Android::HotReload
{
    /**
     * Hot reload statistics for monitoring performance
     */
    struct FAndroidHotReloadStats
    {
        int32 TotalMethodsReplaced;
        int32 TotalAssembliesReloaded;
        int32 SuccessfulReloads;
        int32 FailedReloads;
        double AverageReloadTime;
        FDateTime LastReloadTime;

        FAndroidHotReloadStats()
            : TotalMethodsReplaced(0)
            , TotalAssembliesReloaded(0)
            , SuccessfulReloads(0)
            , FailedReloads(0)
            , AverageReloadTime(0.0)
        {}
    };

    /**
     * Initialize Android-specific hot reload system
     */
    UNREALSHARPCORE_API bool InitializeAndroidHotReload();

    /**
     * Check if Android hot reload is available on this device
     * Verifies Mono runtime capabilities and Android version compatibility
     */
    UNREALSHARPCORE_API bool IsAndroidHotReloadSupported();

    /**
     * Perform hot reload for a specific assembly on Android
     * Uses method replacement and domain switching for optimal performance
     * 
     * @param AssemblyName Name of the assembly to hot reload
     * @param DeltaData Binary delta data containing changes
     * @return true if hot reload succeeded, false otherwise
     */
    UNREALSHARPCORE_API bool HotReloadAssemblyAndroid(const FString& AssemblyName, const TArray<uint8>& DeltaData);

    /**
     * Hot reload dynamic C# code using Mono evaluator
     * Allows on-the-fly evaluation of C# code snippets
     * 
     * @param CSharpCode C# code to evaluate and execute
     * @return true if evaluation and hot reload succeeded
     */
    UNREALSHARPCORE_API bool HotReloadDynamicCodeAndroid(const FString& CSharpCode);

    /**
     * Revert a hot reload operation
     * Restores the original method implementations
     * 
     * @param AssemblyName Assembly to revert
     * @return true if revert succeeded
     */
    UNREALSHARPCORE_API bool RevertHotReloadAndroid(const FString& AssemblyName);

    /**
     * Register an assembly for Android hot reload tracking
     * Must be called before hot reload can be performed on an assembly
     * 
     * @param Assembly Mono assembly to register
     * @return true if registration succeeded
     */
    UNREALSHARPCORE_API bool RegisterAssemblyForAndroidHotReload(MonoAssembly* Assembly);

    /**
     * Get hot reload statistics
     */
    UNREALSHARPCORE_API FAndroidHotReloadStats GetAndroidHotReloadStats();

    /**
     * Shutdown Android hot reload system and cleanup resources
     */
    UNREALSHARPCORE_API void ShutdownAndroidHotReload();

    /**
     * Android-specific optimizations for hot reload
     */
    namespace AndroidOptimizations
    {
        /**
         * Optimize method thunk caching for Android
         */
        UNREALSHARPCORE_API void OptimizeThunkCache();

        /**
         * Adjust garbage collection settings for hot reload
         */
        UNREALSHARPCORE_API void OptimizeGCForHotReload();

        /**
         * Enable Android-specific Mono interpreter optimizations
         */
        UNREALSHARPCORE_API bool EnableInterpreterOptimizations();
    }
}

/**
 * Convenience macros for Android hot reload
 */
#define UNREALSHARP_ANDROID_HOTRELOAD_INIT() UnrealSharp::Android::HotReload::InitializeAndroidHotReload()
#define UNREALSHARP_ANDROID_HOTRELOAD_SHUTDOWN() UnrealSharp::Android::HotReload::ShutdownAndroidHotReload()
#define UNREALSHARP_ANDROID_HOTRELOAD_OPTIMIZE() UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeThunkCache()

/**
 * Blueprint integration for Android hot reload
 */
UCLASS()
class UNREALSHARPCORE_API UAndroidHotReloadBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Check if Android hot reload is available
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Android Hot Reload")
    static bool IsAndroidHotReloadAvailable();

    /**
     * Trigger hot reload for Android-specific C# code
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Android Hot Reload")
    static bool HotReloadAndroidCode(const FString& CSharpCode);

    /**
     * Get Android hot reload statistics as formatted string
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Android Hot Reload")
    static FString GetAndroidHotReloadStatsString();

    /**
     * Revert Android assembly hot reload
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Android Hot Reload")
    static bool RevertAndroidAssemblyHotReload(const FString& AssemblyName);

    /**
     * Enable Android hot reload optimizations
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Android Hot Reload")
    static bool EnableAndroidHotReloadOptimizations();
};

#endif // WITH_MONO_RUNTIME && PLATFORM_ANDROID