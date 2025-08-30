#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealSharp_UnifiedHotReload.generated.h"

/**
 * UnrealSharp Unified Hot Reload System
 * 
 * Provides a unified API for hot reload that automatically adapts to the runtime:
 * - .NET 9 Native Runtime: Uses modern .NET hot reload APIs
 * - Mono Runtime: Uses AppDomain switching or method replacement techniques
 * 
 * This system is designed to answer the user's question about whether UnrealSharp's
 * existing .NET desktop hot reload can work with Mono, or if UnrealCSharp's approach
 * should be used directly.
 */

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnUnifiedHotReloadCompleted, const FString& /*AssemblyName*/, bool /*bSuccess*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnHotReloadStrategyChanged, const FString& /*NewStrategy*/);

namespace UnrealSharp::HotReload
{
    // Hot reload implementation strategy
    enum class EHotReloadStrategy : uint8
    {
        DotNetNative,           // Use .NET 9 native hot reload
        MonoAppDomain,          // Use Mono AppDomain switching
        MonoMethodReplacement,  // Use Mono method body replacement
        Disabled                // Hot reload not available
    };

    // Runtime information
    struct UNREALSHARPCORE_API FRuntimeInfo
    {
        bool bIsMonoRuntime;
        bool bIsDotNetNative;
        bool bIsDesktop;
        bool bIsMobile;
        EHotReloadStrategy PreferredStrategy;
        FString RuntimeVersion;

        FRuntimeInfo()
            : bIsMonoRuntime(false)
            , bIsDotNetNative(false)
            , bIsDesktop(false)
            , bIsMobile(false)
            , PreferredStrategy(EHotReloadStrategy::Disabled)
        {}
    };

    // Hot reload capabilities
    struct UNREALSHARPCORE_API FHotReloadCapabilities
    {
        bool bSupportsMethodBodyReplacement;
        bool bSupportsAssemblyReplacement;
        bool bSupportsNewTypeAddition;
        bool bRequiresRestart;
        FString StrategyName;

        FHotReloadCapabilities()
            : bSupportsMethodBodyReplacement(false)
            , bSupportsAssemblyReplacement(false)
            , bSupportsNewTypeAddition(false)
            , bRequiresRestart(true)
        {}
    };

    /**
     * Main API functions
     */

    // Initialize the unified hot reload system
    UNREALSHARPCORE_API bool InitializeUnifiedHotReload();

    // Shutdown the unified hot reload system
    UNREALSHARPCORE_API void ShutdownUnifiedHotReload();

    // Get current runtime information
    UNREALSHARPCORE_API FRuntimeInfo GetCurrentRuntimeInfo();

    // Get current hot reload capabilities
    UNREALSHARPCORE_API FHotReloadCapabilities GetHotReloadCapabilities();

    // Unified hot reload function - automatically chooses best method
    UNREALSHARPCORE_API bool HotReloadAssembly(const FString& AssemblyName, const TArray<uint8>& AssemblyData);

    // Check if hot reload is supported and ready
    UNREALSHARPCORE_API bool IsHotReloadSupported();

    // Get human-readable name of current strategy
    UNREALSHARPCORE_API FString GetCurrentStrategyName();

    // Events
    UNREALSHARPCORE_API FOnUnifiedHotReloadCompleted& OnHotReloadCompleted();
    UNREALSHARPCORE_API FOnHotReloadStrategyChanged& OnStrategyChanged();

    /**
     * Strategy-specific implementations (internal)
     */
    bool InitializeDotNetHotReload();
    bool InitializeMonoAppDomainHotReload();
    bool InitializeMonoMethodReplacementHotReload();
    bool InitializeFileWatchingHotReload();

    bool HotReloadAssemblyDotNet(const FString& AssemblyName, const TArray<uint8>& AssemblyData);
    bool HotReloadAssemblyMonoAppDomain(const FString& AssemblyName, const TArray<uint8>& AssemblyData);
    bool HotReloadAssemblyMonoMethodReplacement(const FString& AssemblyName, const TArray<uint8>& AssemblyData);

    const TCHAR* GetStrategyName(EHotReloadStrategy Strategy);

    /**
     * Detect current runtime and determine best hot reload strategy
     */
    FRuntimeInfo DetectRuntime();
}

/**
 * Blueprint integration for unified hot reload
 */
UCLASS()
class UNREALSHARPCORE_API UUnifiedHotReloadBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Check if hot reload is available
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Hot Reload")
    static bool IsHotReloadAvailable();

    // Get current hot reload strategy name
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Hot Reload")
    static FString GetHotReloadStrategy();

    // Get hot reload capabilities as a formatted string
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Hot Reload")
    static FString GetHotReloadCapabilities();

    // Trigger hot reload for a specific assembly (if available)
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Hot Reload")
    static bool TriggerHotReload(const FString& AssemblyName);

    // Get runtime information as a formatted string
    UFUNCTION(BlueprintCallable, Category = "UnrealSharp | Hot Reload")
    static FString GetRuntimeInfo();
};

/**
 * Integration with UnrealSharp platform initialization
 */
namespace UnrealSharp::Platform
{
    // Called during platform initialization
    UNREALSHARPCORE_API void InitializeHotReloadSystem();

    // Called during platform shutdown
    UNREALSHARPCORE_API void ShutdownHotReloadSystem();
}