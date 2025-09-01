#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "UObject/NoExportTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#if WITH_MONO_RUNTIME && PLATFORM_WINDOWS
#include "mono/jit/jit.h"
#include "mono/metadata/assembly.h"
#include "mono/metadata/appdomain.h"
#endif

#include "UnrealSharp_Windows_HotReload.generated.h"

/**
 * UnrealSharp Windows Hot Reload System
 * 
 * Enhanced hot reload implementation specifically for Windows platform using
 * advanced Mono AppDomain switching techniques for true no-restart hot reload.
 * 
 * Features:
 * - Enhanced AppDomain switching with Windows optimizations
 * - No application restart required
 * - Advanced memory management and JIT optimizations
 * - Real-time performance monitoring
 * - Blueprint integration for testing and monitoring
 */

#if WITH_MONO_RUNTIME && PLATFORM_WINDOWS

namespace UnrealSharp::Windows::HotReload
{
    /**
     * Windows hot reload performance statistics
     */
    struct FWindowsHotReloadStats
    {
        int32 TotalAssembliesReloaded;     // Total assemblies hot reloaded
        int32 SuccessfulReloads;           // Successful hot reload operations
        int32 FailedReloads;               // Failed hot reload attempts
        double AverageReloadTime;          // Average time per hot reload
        FDateTime LastReloadTime;          // Timestamp of last hot reload

        FWindowsHotReloadStats()
            : TotalAssembliesReloaded(0)
            , SuccessfulReloads(0)
            , FailedReloads(0)
            , AverageReloadTime(0.0)
            , LastReloadTime(FDateTime::MinValue())
        {}
    };

    /**
     * Initialize Windows hot reload system with enhanced features
     */
    bool InitializeWindowsHotReload();

    /**
     * Check if Windows hot reload is supported on this system
     */
    bool IsWindowsHotReloadSupported();

    /**
     * Register an assembly for Windows hot reload tracking
     */
    bool RegisterAssemblyForWindowsHotReload(MonoAssembly* Assembly);

    /**
     * Perform enhanced hot reload of an assembly on Windows (no restart)
     */
    bool HotReloadAssemblyWindows(const FString& AssemblyName, const TArray<uint8>& AssemblyData);

    /**
     * Get Windows hot reload performance statistics
     */
    FWindowsHotReloadStats GetWindowsHotReloadStats();

    /**
     * Shutdown Windows hot reload system and cleanup resources
     */
    void ShutdownWindowsHotReload();

    /**
     * Windows-specific optimization functions
     */
    namespace WindowsOptimizations
    {
        void OptimizeDomainSwitching();
        void EnableJITOptimizations(); 
        void ConfigureMemoryManagement();
    }
}

#endif // WITH_MONO_RUNTIME && PLATFORM_WINDOWS

/**
 * Blueprint function library for Windows Hot Reload functionality
 */
UCLASS()
class UNREALSHARPCORE_API UWindowsHotReloadBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UWindowsHotReloadBlueprintLibrary(const FObjectInitializer& ObjectInitializer);

    /**
     * Check if Windows hot reload is available on this system
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Windows Hot Reload")
    static bool IsWindowsHotReloadAvailable();

    /**
     * Get Windows hot reload statistics as a formatted string
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Windows Hot Reload")
    static FString GetWindowsHotReloadStatsString();

    /**
     * Enable Windows-specific hot reload optimizations
     */
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Windows Hot Reload")
    static bool EnableWindowsHotReloadOptimizations();
};