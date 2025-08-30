#include "CoreMinimal.h"

#if PLATFORM_IOS && WITH_MONO_RUNTIME

#include "mono/jit/jit.h"
#include "mono/metadata/assembly.h" 
#include "mono/metadata/appdomain.h"
#include "mono/metadata/class.h"
#include "mono/metadata/object.h"
#include "mono/metadata/reflection.h"
#include "mono/metadata/loader.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

/**
 * iOS Assembly Load Context Switching for Hot Reload
 * 
 * This system manages multiple assembly contexts to enable seamless hot reload:
 * 1. Primary Context - Original assemblies (AOT compiled)
 * 2. Hot Reload Context - New assembly versions (Interpreter)
 * 3. Context Switching - Redirect type resolution to new contexts
 * 4. Isolation - Keep different versions isolated
 */

namespace UnrealSharp::iOS::AssemblyContext
{
    // Context management structures
    struct FAssemblyLoadContext
    {
        FString ContextName;
        MonoDomain* Domain;
        TMap<FString, MonoAssembly*> LoadedAssemblies;
        TMap<FString, MonoImage*> LoadedImages;
        bool bIsHotReloadContext;
        int32 Version;
    };

    // Global context management
    static TMap<FString, FAssemblyLoadContext> LoadContexts;
    static FString ActiveContextName = TEXT("Primary");
    static MonoDomain* PrimaryDomain = nullptr;
    static bool bContextSwitchingEnabled = false;

    /**
     * Initialize Assembly Load Context system for hot reload
     */
    bool InitializeAssemblyContextSystem()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Initializing Assembly Load Context system for iOS"));

        // Get current primary domain
        PrimaryDomain = mono_domain_get();
        if (!PrimaryDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to get primary domain"));
            return false;
        }

        // Create primary context
        FAssemblyLoadContext PrimaryContext;
        PrimaryContext.ContextName = TEXT("Primary");
        PrimaryContext.Domain = PrimaryDomain;
        PrimaryContext.bIsHotReloadContext = false;
        PrimaryContext.Version = 1;

        LoadContexts.Add(TEXT("Primary"), PrimaryContext);
        bContextSwitchingEnabled = true;

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Assembly Load Context system initialized"));
        return true;
    }

    /**
     * Create new assembly load context for hot reload
     */
    FString CreateHotReloadContext(const FString& ContextName)
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Creating hot reload context '%s'"), *ContextName);

        if (LoadContexts.Contains(ContextName))
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp: Context '%s' already exists"), *ContextName);
            return ContextName;
        }

        // Create new domain for isolation
        FString DomainName = FString::Printf(TEXT("%s_Domain"), *ContextName);
        MonoDomain* NewDomain = mono_domain_create_appdomain(TCHAR_TO_ANSI(*DomainName), NULL);
        
        if (!NewDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to create domain for context '%s'"), *ContextName);
            return TEXT("");
        }

        // Configure domain for interpreter mode (required for hot reload)
        MonoDomain* OriginalDomain = mono_domain_get();
        mono_domain_set(NewDomain, false);
        
        // Set interpreter mode for this domain
        mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
        
        mono_domain_set(OriginalDomain, false);

        // Create context
        FAssemblyLoadContext NewContext;
        NewContext.ContextName = ContextName;
        NewContext.Domain = NewDomain;
        NewContext.bIsHotReloadContext = true;
        NewContext.Version = 1;

        LoadContexts.Add(ContextName, NewContext);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Created hot reload context '%s'"), *ContextName);
        return ContextName;
    }

    /**
     * Load assembly into specific context
     */
    bool LoadAssemblyIntoContext(const FString& ContextName, const FString& AssemblyName, const TArray<uint8>& AssemblyData)
    {
        FAssemblyLoadContext* Context = LoadContexts.Find(ContextName);
        if (!Context)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Context '%s' not found"), *ContextName);
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Loading assembly '%s' into context '%s'"), *AssemblyName, *ContextName);

        // Switch to context domain
        MonoDomain* OriginalDomain = mono_domain_get();
        mono_domain_set(Context->Domain, false);

        // Load assembly from data
        MonoImageOpenStatus Status;
        MonoImage* Image = mono_image_open_from_data_with_name(
            reinterpret_cast<char*>(const_cast<uint8*>(AssemblyData.GetData())),
            AssemblyData.Num(),
            true, // need_copy
            &Status,
            false, // refonly
            TCHAR_TO_ANSI(*AssemblyName)
        );

        if (!Image || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to open image for assembly '%s', status: %d"), 
                   *AssemblyName, static_cast<int32>(Status));
            mono_domain_set(OriginalDomain, false);
            return false;
        }

        // Load assembly from image
        MonoAssembly* Assembly = mono_assembly_load_from(Image, TCHAR_TO_ANSI(*AssemblyName), &Status);
        if (!Assembly || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Failed to load assembly '%s', status: %d"), 
                   *AssemblyName, static_cast<int32>(Status));
            mono_image_close(Image);
            mono_domain_set(OriginalDomain, false);
            return false;
        }

        // Store in context
        Context->LoadedAssemblies.Add(AssemblyName, Assembly);
        Context->LoadedImages.Add(AssemblyName, Image);

        mono_domain_set(OriginalDomain, false);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Successfully loaded assembly '%s' into context '%s'"), 
               *AssemblyName, *ContextName);
        return true;
    }

    /**
     * Switch active context for type resolution
     */
    bool SwitchActiveContext(const FString& ContextName)
    {
        if (!LoadContexts.Contains(ContextName))
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Cannot switch to non-existent context '%s'"), *ContextName);
            return false;
        }

        FString OldContext = ActiveContextName;
        ActiveContextName = ContextName;

        // Get the target context
        FAssemblyLoadContext* Context = LoadContexts.Find(ContextName);
        if (Context && Context->Domain)
        {
            // Switch domain if it's a hot reload context
            if (Context->bIsHotReloadContext)
            {
                mono_domain_set(Context->Domain, false);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Switched active context from '%s' to '%s'"), 
               *OldContext, *ContextName);
        return true;
    }

    /**
     * Resolve type from active context first, fallback to primary
     */
    MonoClass* ResolveTypeInActiveContext(const FString& NamespaceName, const FString& TypeName)
    {
        // Try active context first
        FAssemblyLoadContext* ActiveContext = LoadContexts.Find(ActiveContextName);
        if (ActiveContext)
        {
            for (const auto& ImagePair : ActiveContext->LoadedImages)
            {
                MonoClass* FoundClass = mono_class_from_name(
                    ImagePair.Value, 
                    TCHAR_TO_ANSI(*NamespaceName), 
                    TCHAR_TO_ANSI(*TypeName)
                );
                if (FoundClass)
                {
                    UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp: Resolved type %s.%s from context '%s'"), 
                           *NamespaceName, *TypeName, *ActiveContextName);
                    return FoundClass;
                }
            }
        }

        // Fallback to primary context
        if (ActiveContextName != TEXT("Primary"))
        {
            FAssemblyLoadContext* PrimaryContext = LoadContexts.Find(TEXT("Primary"));
            if (PrimaryContext)
            {
                for (const auto& ImagePair : PrimaryContext->LoadedImages)
                {
                    MonoClass* FoundClass = mono_class_from_name(
                        ImagePair.Value, 
                        TCHAR_TO_ANSI(*NamespaceName), 
                        TCHAR_TO_ANSI(*TypeName)
                    );
                    if (FoundClass)
                    {
                        UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp: Resolved type %s.%s from primary context"), 
                               *NamespaceName, *TypeName);
                        return FoundClass;
                    }
                }
            }
        }

        return nullptr;
    }

    /**
     * Hot reload assembly by switching contexts
     */
    bool HotReloadAssemblyWithContextSwitch(const FString& AssemblyName, const TArray<uint8>& NewAssemblyData)
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Hot reloading assembly '%s' with context switching"), *AssemblyName);

        // Create new context for this hot reload
        FString HotReloadContextName = FString::Printf(TEXT("HotReload_%s_%d"), 
                                                      *AssemblyName, 
                                                      FDateTime::Now().GetTicks());

        FString NewContextName = CreateHotReloadContext(HotReloadContextName);
        if (NewContextName.IsEmpty())
        {
            return false;
        }

        // Load new assembly version into hot reload context
        if (!LoadAssemblyIntoContext(NewContextName, AssemblyName, NewAssemblyData))
        {
            // Cleanup failed context
            UnloadContext(NewContextName);
            return false;
        }

        // Switch to new context for future type resolutions
        if (!SwitchActiveContext(NewContextName))
        {
            UnloadContext(NewContextName);
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Successfully hot reloaded assembly '%s' with context '%s'"), 
               *AssemblyName, *NewContextName);
        return true;
    }

    /**
     * Unload context and cleanup resources
     */
    bool UnloadContext(const FString& ContextName)
    {
        if (ContextName == TEXT("Primary"))
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp: Cannot unload primary context"));
            return false;
        }

        FAssemblyLoadContext* Context = LoadContexts.Find(ContextName);
        if (!Context)
        {
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Unloading context '%s'"), *ContextName);

        // If this is the active context, switch back to primary
        if (ActiveContextName == ContextName)
        {
            SwitchActiveContext(TEXT("Primary"));
        }

        // Unload domain if it's a hot reload context
        if (Context->bIsHotReloadContext && Context->Domain)
        {
            mono_domain_unload(Context->Domain);
        }

        // Remove from contexts
        LoadContexts.Remove(ContextName);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Successfully unloaded context '%s'"), *ContextName);
        return true;
    }

    /**
     * Get context information for debugging
     */
    struct FContextInfo
    {
        FString Name;
        int32 LoadedAssembliesCount;
        bool bIsActive;
        bool bIsHotReloadContext;
        int32 Version;
    };

    TArray<FContextInfo> GetContextsInfo()
    {
        TArray<FContextInfo> ContextsInfo;

        for (const auto& ContextPair : LoadContexts)
        {
            FContextInfo Info;
            Info.Name = ContextPair.Key;
            Info.LoadedAssembliesCount = ContextPair.Value.LoadedAssemblies.Num();
            Info.bIsActive = (ContextPair.Key == ActiveContextName);
            Info.bIsHotReloadContext = ContextPair.Value.bIsHotReloadContext;
            Info.Version = ContextPair.Value.Version;

            ContextsInfo.Add(Info);
        }

        return ContextsInfo;
    }

    /**
     * Cleanup all contexts except primary
     */
    void ShutdownAssemblyContextSystem()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Shutting down Assembly Load Context system"));

        // Unload all hot reload contexts
        TArray<FString> ContextsToUnload;
        for (const auto& ContextPair : LoadContexts)
        {
            if (ContextPair.Value.bIsHotReloadContext)
            {
                ContextsToUnload.Add(ContextPair.Key);
            }
        }

        for (const FString& ContextName : ContextsToUnload)
        {
            UnloadContext(ContextName);
        }

        // Switch back to primary
        if (ActiveContextName != TEXT("Primary"))
        {
            SwitchActiveContext(TEXT("Primary"));
        }

        bContextSwitchingEnabled = false;
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Assembly Load Context system shut down"));
    }
}

#endif // PLATFORM_IOS && WITH_MONO_RUNTIME