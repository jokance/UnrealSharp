#include "CoreMinimal.h"

#if WITH_MONO_RUNTIME

#if PLATFORM_IOS
#include "iOS/UnrealSharp_iOS_HotReload.h"
#endif

#if PLATFORM_ANDROID
#include "Android/UnrealSharp_Android_HotReload.h"
#endif

/**
 * Platform-specific initialization for UnrealSharp
 * Handles platform-specific setup for hot reload and runtime features
 */
namespace UnrealSharp::Platform
{
    /**
     * Initialize platform-specific features
     * Called during UnrealSharp module startup
     */
    void InitializePlatformFeatures()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Initializing platform-specific features"));

#if PLATFORM_IOS && UNREALSHARP_IOS_HOTRELOAD_ENABLED
        // Initialize iOS hot reload system
        UNREALSHARP_IOS_HOTRELOAD_INIT();
        
        // Load any cached hot reload assemblies from previous sessions
        UNREALSHARP_IOS_HOTRELOAD_LOAD_CACHED();
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: iOS hot reload system initialized"));
#endif

#if PLATFORM_ANDROID
        // Initialize Android hot reload system
        UNREALSHARP_ANDROID_HOTRELOAD_INIT();
        
        // Enable Android-specific optimizations
        UNREALSHARP_ANDROID_HOTRELOAD_OPTIMIZE();
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Android hot reload system initialized"));
#endif

#if PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX
        // Desktop platforms with optional .NET runtime
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Desktop platform initialization"));
#endif
    }

    /**
     * Shutdown platform-specific features
     * Called during UnrealSharp module shutdown
     */
    void ShutdownPlatformFeatures()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Shutting down platform-specific features"));

#if PLATFORM_IOS && UNREALSHARP_IOS_HOTRELOAD_ENABLED
        // Shutdown iOS hot reload system
        UNREALSHARP_IOS_HOTRELOAD_SHUTDOWN();
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: iOS hot reload system shut down"));
#endif

#if PLATFORM_ANDROID
        // Shutdown Android hot reload system
        UNREALSHARP_ANDROID_HOTRELOAD_SHUTDOWN();
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Android hot reload system shut down"));
#endif
    }

    /**
     * Check if hot reload is supported on the current platform
     */
    bool IsHotReloadSupported()
    {
#if PLATFORM_IOS && UNREALSHARP_IOS_HOTRELOAD_ENABLED
        return true; // Limited hot reload support on iOS
#elif PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX
        return true; // Full hot reload support on desktop platforms
#elif PLATFORM_ANDROID
        return true; // Full hot reload support on Android
#else
        return false; // Unknown platform
#endif
    }

    /**
     * Get platform-specific hot reload limitations
     */
    FString GetHotReloadLimitations()
    {
#if PLATFORM_IOS
        return TEXT("iOS: Limited to game logic assemblies, requires app restart for full reload");
#elif PLATFORM_ANDROID
        return TEXT("Android: Full hot reload support with method replacement and domain switching");
#elif PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX
        return TEXT("Desktop: Full hot reload support available");
#else
        return TEXT("Unknown platform: Hot reload status unknown");
#endif
    }
}

#endif // WITH_MONO_RUNTIME