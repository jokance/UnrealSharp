#include "CoreMinimal.h"

#if PLATFORM_IOS && WITH_MONO_RUNTIME

#include "mono/jit/jit.h"
#include "mono/metadata/assembly.h"
#include "mono/metadata/class.h"
#include "mono/metadata/object.h"
#include "mono/metadata/mono-config.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * iOS Hot Reload Implementation for UnrealSharp
 * 
 * Strategy:
 * 1. Use Mono's AOT+Interpreter hybrid mode (MONO_AOT_MODE_INTERP)
 * 2. Core assemblies are AOT-compiled and included in the app bundle
 * 3. Game logic assemblies can be interpreted for hot reload
 * 4. Assembly replacement through bytecode/IL replacement
 * 5. Use Assembly.Load with byte arrays for dynamic loading
 */

namespace UnrealSharp::iOS::HotReload
{
    static TMap<FString, MonoAssembly*> LoadedAssemblies;
    static TMap<FString, TArray<uint8>> AssemblyBytecodeCache;
    
    /**
     * Initialize iOS Hot Reload system
     * Must be called during Mono domain initialization
     */
    void InitializeHotReload()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Initializing iOS Hot Reload system"));
        
        // Set hybrid AOT+Interpreter mode for iOS
        mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
        
        // Register core AOT modules that are pre-compiled
        // These cannot be hot-reloaded but provide base functionality
        #ifdef MONO_AOT_MODULE_SYSTEM_PRIVATE_CORELIB
        extern void* mono_aot_module_System_Private_CoreLib_info;
        mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));
        #endif
        
        // Configure native library mappings for iOS
        mono_dllmap_insert(nullptr, "System.Native", nullptr, "__Internal", nullptr);
        mono_dllmap_insert(nullptr, "System.Net.Security.Native", nullptr, "__Internal", nullptr);
        mono_dllmap_insert(nullptr, "System.IO.Compression.Native", nullptr, "__Internal", nullptr);
        mono_dllmap_insert(nullptr, "System.Security.Cryptography.Native.Apple", nullptr, "__Internal", nullptr);
        mono_dllmap_insert(nullptr, "System.Globalization.Native", nullptr, "__Internal", nullptr);
        
        // Set invariant globalization for iOS compliance
        setenv("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1", 1);
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: iOS Hot Reload system initialized"));
    }
    
    /**
     * Load or reload an assembly from bytecode
     * This is the core hot reload mechanism for iOS
     */
    bool LoadAssemblyFromBytecode(const FString& AssemblyName, const TArray<uint8>& BytecodeData)
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Loading assembly '%s' from bytecode (%d bytes)"), 
               *AssemblyName, BytecodeData.Num());
        
        // Check if assembly was previously loaded
        if (MonoAssembly** ExistingAssembly = LoadedAssemblies.Find(AssemblyName))
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp: Assembly '%s' already loaded, attempting reload"), *AssemblyName);
            // Note: True hot reload on iOS requires advanced techniques like method body replacement
            // For now, we cache the new bytecode for next app restart
            AssemblyBytecodeCache.Add(AssemblyName, BytecodeData);
            return false;
        }
        
        // Load assembly from memory
        MonoImageOpenStatus Status;
        MonoImage* Image = mono_image_open_from_data_with_name(
            reinterpret_cast<char*>(const_cast<uint8*>(BytecodeData.GetData())),
            BytecodeData.Num(),
            true, // need_copy
            &Status,
            false, // refonly
            TCHAR_TO_ANSI(*AssemblyName)
        );
        
        if (!Image || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to open image for assembly '%s', status: %d"), 
                   *AssemblyName, static_cast<int32>(Status));
            return false;
        }
        
        // Load the assembly from the image
        MonoAssembly* Assembly = mono_assembly_load_from(Image, TCHAR_TO_ANSI(*AssemblyName), &Status);
        if (!Assembly || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to load assembly '%s', status: %d"), 
                   *AssemblyName, static_cast<int32>(Status));
            mono_image_close(Image);
            return false;
        }
        
        // Cache the loaded assembly
        LoadedAssemblies.Add(AssemblyName, Assembly);
        AssemblyBytecodeCache.Add(AssemblyName, BytecodeData);
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Successfully loaded assembly '%s'"), *AssemblyName);
        return true;
    }
    
    /**
     * Load assembly from file in the iOS app bundle or Documents directory
     * Supports loading from writable directories for hot reload
     */
    bool LoadAssemblyFromFile(const FString& AssemblyPath)
    {
        TArray<uint8> AssemblyData;
        if (!FFileHelper::LoadFileToArray(AssemblyData, *AssemblyPath))
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to read assembly file: %s"), *AssemblyPath);
            return false;
        }
        
        FString AssemblyName = FPaths::GetBaseFilename(AssemblyPath);
        return LoadAssemblyFromBytecode(AssemblyName, AssemblyData);
    }
    
    /**
     * Hot reload mechanism: Replace assembly bytecode
     * On iOS, this stores new bytecode for next app restart due to AOT limitations
     */
    bool HotReloadAssembly(const FString& AssemblyName, const TArray<uint8>& NewBytecodeData)
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Hot reloading assembly '%s'"), *AssemblyName);
        
        // Save new bytecode to Documents directory for persistence
        FString DocumentsDir = FPaths::ConvertRelativePathToFull(FPlatformProcess::UserSettingsDir());
        FString HotReloadCachePath = FPaths::Combine(DocumentsDir, TEXT("UnrealSharp"), TEXT("HotReloadCache"));
        FString AssemblyFilePath = FPaths::Combine(HotReloadCachePath, AssemblyName + TEXT(".dll"));
        
        // Ensure directory exists
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*HotReloadCachePath))
        {
            PlatformFile.CreateDirectoryTree(*HotReloadCachePath);
        }
        
        // Save new assembly for next app launch
        if (FFileHelper::SaveArrayToFile(NewBytecodeData, *AssemblyFilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Cached hot reload assembly at: %s"), *AssemblyFilePath);
            
            // Update cache
            AssemblyBytecodeCache.Add(AssemblyName, NewBytecodeData);
            
            // Note: On iOS, true hot reload requires app restart due to AOT constraints
            // Advanced implementations could use method body replacement for limited scenarios
            return true;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to cache hot reload assembly"));
            return false;
        }
    }
    
    /**
     * Load cached hot reload assemblies on app startup
     */
    void LoadCachedHotReloadAssemblies()
    {
        FString DocumentsDir = FPaths::ConvertRelativePathToFull(FPlatformProcess::UserSettingsDir());
        FString HotReloadCachePath = FPaths::Combine(DocumentsDir, TEXT("UnrealSharp"), TEXT("HotReloadCache"));
        
        TArray<FString> CachedAssemblies;
        IFileManager::Get().FindFiles(CachedAssemblies, *(HotReloadCachePath / TEXT("*.dll")), true, false);
        
        for (const FString& AssemblyFile : CachedAssemblies)
        {
            FString FullPath = FPaths::Combine(HotReloadCachePath, AssemblyFile);
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Loading cached hot reload assembly: %s"), *FullPath);
            LoadAssemblyFromFile(FullPath);
        }
    }
    
    /**
     * Check if an assembly supports hot reload on iOS
     * Core system assemblies cannot be hot reloaded
     */
    bool CanAssemblyBeHotReloaded(const FString& AssemblyName)
    {
        // Core system assemblies that are AOT-compiled cannot be hot reloaded
        static const TArray<FString> NonHotReloadableAssemblies = {
            TEXT("System.Private.CoreLib"),
            TEXT("System.Runtime"),
            TEXT("System.Collections"),
            TEXT("UnrealSharp.Core"), // Our core runtime bindings
            TEXT("UnrealSharp.Binds")  // Engine bindings
        };
        
        for (const FString& NonReloadable : NonHotReloadableAssemblies)
        {
            if (AssemblyName.Contains(NonReloadable))
            {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Get information about loaded assemblies
     */
    TArray<FString> GetLoadedAssemblies()
    {
        TArray<FString> AssemblyNames;
        LoadedAssemblies.GetKeys(AssemblyNames);
        return AssemblyNames;
    }
    
    /**
     * Clean up hot reload system
     */
    void ShutdownHotReload()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Shutting down iOS Hot Reload system"));
        LoadedAssemblies.Empty();
        AssemblyBytecodeCache.Empty();
    }
}

#endif // PLATFORM_IOS && WITH_MONO_RUNTIME