#pragma once

#include "CoreMinimal.h"

#if WITH_MONO_RUNTIME

/**
 * Platform-specific initialization for UnrealSharp
 * Handles platform-specific features like hot reload, runtime configuration, and platform limitations
 */
namespace UnrealSharp::Platform
{
    /**
     * Initialize platform-specific features during UnrealSharp module startup
     * This includes hot reload systems, runtime configuration, and platform optimizations
     */
    UNREALSHARPCORE_API void InitializePlatformFeatures();

    /**
     * Shutdown platform-specific features during UnrealSharp module shutdown
     * Cleans up resources and saves any necessary state
     */
    UNREALSHARPCORE_API void ShutdownPlatformFeatures();

    /**
     * Check if hot reload is supported on the current platform
     * @return True if hot reload is supported, false otherwise
     */
    UNREALSHARPCORE_API bool IsHotReloadSupported();

    /**
     * Get a description of hot reload limitations on the current platform
     * @return String describing platform-specific hot reload limitations
     */
    UNREALSHARPCORE_API FString GetHotReloadLimitations();
}

#endif // WITH_MONO_RUNTIME