#pragma once

#include "CoreMinimal.h"

#if PLATFORM_IOS && WITH_MONO_RUNTIME

/**
 * iOS Hot Reload System for UnrealSharp
 * 
 * Provides hot reload capabilities on iOS using Mono's hybrid AOT+Interpreter mode.
 * Due to iOS limitations, true runtime hot reload is limited, but this system provides:
 * 
 * 1. Assembly caching for next app restart
 * 2. Bytecode loading from memory
 * 3. File-based assembly loading
 * 4. Hot reload preparation for development builds
 * 
 * Limitations on iOS:
 * - Core system assemblies cannot be hot reloaded (AOT compiled)
 * - True runtime hot reload requires app restart in most cases
 * - Only game logic assemblies can be hot reloaded
 * - Development builds only (not for App Store distribution)
 */

namespace UnrealSharp::iOS::HotReload
{
    /**
     * Initialize the iOS Hot Reload system
     * Must be called during Mono domain initialization
     */
    UNREALSHARPCORE_API void InitializeHotReload();
    
    /**
     * Load or reload an assembly from bytecode data
     * @param AssemblyName Name of the assembly
     * @param BytecodeData Compiled assembly bytecode
     * @return True if loaded successfully
     */
    UNREALSHARPCORE_API bool LoadAssemblyFromBytecode(const FString& AssemblyName, const TArray<uint8>& BytecodeData);
    
    /**
     * Load assembly from file path
     * @param AssemblyPath Full path to the assembly file
     * @return True if loaded successfully
     */
    UNREALSHARPCORE_API bool LoadAssemblyFromFile(const FString& AssemblyPath);
    
    /**
     * Hot reload an assembly with new bytecode
     * On iOS, this caches the new bytecode for next app restart
     * @param AssemblyName Name of the assembly to reload
     * @param NewBytecodeData New compiled bytecode
     * @return True if hot reload was prepared successfully
     */
    UNREALSHARPCORE_API bool HotReloadAssembly(const FString& AssemblyName, const TArray<uint8>& NewBytecodeData);
    
    /**
     * Load cached hot reload assemblies on app startup
     * This automatically loads assemblies that were hot reloaded in previous sessions
     */
    UNREALSHARPCORE_API void LoadCachedHotReloadAssemblies();
    
    /**
     * Check if an assembly can be hot reloaded on iOS
     * @param AssemblyName Name of the assembly to check
     * @return True if the assembly supports hot reload
     */
    UNREALSHARPCORE_API bool CanAssemblyBeHotReloaded(const FString& AssemblyName);
    
    /**
     * Get list of currently loaded assemblies
     * @return Array of loaded assembly names
     */
    UNREALSHARPCORE_API TArray<FString> GetLoadedAssemblies();
    
    /**
     * Shutdown the hot reload system
     * Cleans up resources and caches
     */
    UNREALSHARPCORE_API void ShutdownHotReload();
}

/**
 * Convenience macros for iOS hot reload
 */
#define UNREALSHARP_IOS_HOTRELOAD_INIT() UnrealSharp::iOS::HotReload::InitializeHotReload()
#define UNREALSHARP_IOS_HOTRELOAD_SHUTDOWN() UnrealSharp::iOS::HotReload::ShutdownHotReload()
#define UNREALSHARP_IOS_HOTRELOAD_LOAD_CACHED() UnrealSharp::iOS::HotReload::LoadCachedHotReloadAssemblies()

#else

// Empty macros for non-iOS platforms
#define UNREALSHARP_IOS_HOTRELOAD_INIT()
#define UNREALSHARP_IOS_HOTRELOAD_SHUTDOWN()
#define UNREALSHARP_IOS_HOTRELOAD_LOAD_CACHED()

#endif // PLATFORM_IOS && WITH_MONO_RUNTIME