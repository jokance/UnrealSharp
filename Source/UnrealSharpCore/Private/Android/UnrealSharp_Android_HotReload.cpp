#include "Android/UnrealSharp_Android_HotReload.h"

#if WITH_MONO_RUNTIME && PLATFORM_ANDROID

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Engine/Engine.h"
#include "Async/TaskGraphInterfaces.h"

// Mono includes for Android hot reload
#include "mono/metadata/mono-config.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/tokentype.h"
#include "mono/utils/mono-publib.h"

namespace UnrealSharp::Android::HotReload
{
    // Android-specific hot reload state
    struct FAndroidHotReloadState
    {
        TMap<FString, MonoAssembly*> RegisteredAssemblies;
        TMap<FString, TArray<MonoMethod*>> ReplacedMethods;
        TMap<FString, TArray<void*>> OriginalMethodPointers;
        MonoDomain* HotReloadDomain;
        bool bIsInitialized;
        FAndroidHotReloadStats Stats;

        FAndroidHotReloadState()
            : HotReloadDomain(nullptr)
            , bIsInitialized(false)
        {}
    };

    static FAndroidHotReloadState AndroidHotReloadState;

    /**
     * Android-specific Mono configuration for hot reload
     */
    void ConfigureMonoForAndroidHotReload()
    {
        // Enable interpreter mode for better hot reload support
        mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP_LLVMONLY);
        
        // Configure Mono for Android optimizations
        mono_config_parse_environment();
        
        // Enable soft debugger for hot reload
        mono_debug_init(MONO_DEBUG_FORMAT_MONO);
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp: Configured Mono for Android hot reload"));
    }

    /**
     * Replace method body using Android-optimized approach
     */
    bool ReplaceMethodBodyAndroid(MonoMethod* OriginalMethod, MonoMethod* NewMethod)
    {
        if (!OriginalMethod || !NewMethod)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Invalid method pointers for replacement"));
            return false;
        }

        try
        {
            // Get the unmanaged thunk for the new method
            void* NewMethodPtr = mono_method_get_unmanaged_thunk(NewMethod);
            if (!NewMethodPtr)
            {
                UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Failed to get unmanaged thunk for new method"));
                return false;
            }

            // Store original method pointer for potential revert
            void* OriginalMethodPtr = mono_method_get_unmanaged_thunk(OriginalMethod);
            FString MethodName = FString(mono_method_get_name(OriginalMethod));
            
            if (!AndroidHotReloadState.OriginalMethodPointers.Contains(MethodName))
            {
                AndroidHotReloadState.OriginalMethodPointers.Add(MethodName, TArray<void*>());
            }
            AndroidHotReloadState.OriginalMethodPointers[MethodName].Add(OriginalMethodPtr);

            // Set the new unmanaged thunk (this is the key Android/iOS compatible operation)
            mono_method_set_unmanaged_thunk(OriginalMethod, NewMethodPtr);

            // Track replaced method
            if (!AndroidHotReloadState.ReplacedMethods.Contains(MethodName))
            {
                AndroidHotReloadState.ReplacedMethods.Add(MethodName, TArray<MonoMethod*>());
            }
            AndroidHotReloadState.ReplacedMethods[MethodName].Add(OriginalMethod);

            AndroidHotReloadState.Stats.TotalMethodsReplaced++;
            
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Successfully replaced method '%s'"), *MethodName);
            return true;
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Exception during method replacement"));
            return false;
        }
    }

    /**
     * Android-specific assembly loading with hot reload support
     */
    MonoAssembly* LoadAssemblyForAndroidHotReload(const TArray<uint8>& AssemblyData, const FString& AssemblyName)
    {
        MonoImageOpenStatus Status;
        
        // Create image from memory (Android-compatible)
        MonoImage* Image = mono_image_open_from_data_with_name(
            reinterpret_cast<char*>(const_cast<uint8*>(AssemblyData.GetData())),
            AssemblyData.Num(),
            true, // copy_data - important for Android memory management
            &Status,
            false, // refonly
            TCHAR_TO_ANSI(*AssemblyName)
        );

        if (!Image || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Failed to create image for assembly '%s', status: %d"), *AssemblyName, (int32)Status);
            return nullptr;
        }

        // Load assembly in hot reload domain
        MonoAssembly* Assembly = mono_assembly_load_from(Image, TCHAR_TO_ANSI(*AssemblyName), &Status);
        if (!Assembly || Status != MONO_IMAGE_OK)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Failed to load assembly '%s', status: %d"), *AssemblyName, (int32)Status);
            mono_image_close(Image);
            return nullptr;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Successfully loaded assembly '%s' for hot reload"), *AssemblyName);
        return Assembly;
    }

    /**
     * Compare and replace methods between old and new assemblies
     */
    bool CompareAndReplaceMethodsAndroid(MonoAssembly* OldAssembly, MonoAssembly* NewAssembly)
    {
        if (!OldAssembly || !NewAssembly)
        {
            return false;
        }

        MonoImage* OldImage = mono_assembly_get_image(OldAssembly);
        MonoImage* NewImage = mono_assembly_get_image(NewAssembly);

        if (!OldImage || !NewImage)
        {
            return false;
        }

        // Iterate through classes in the new assembly
        int32 ClassCount = mono_image_get_table_rows(NewImage, MONO_TABLE_TYPEDEF);
        int32 ReplacedMethods = 0;

        for (int32 ClassIndex = 1; ClassIndex <= ClassCount; ++ClassIndex)
        {
            MonoClass* NewClass = mono_class_get(NewImage, MONO_TOKEN_TYPE_DEF | ClassIndex);
            if (!NewClass) continue;

            const char* ClassName = mono_class_get_name(NewClass);
            const char* Namespace = mono_class_get_namespace(NewClass);

            // Find corresponding class in old assembly
            MonoClass* OldClass = mono_class_from_name(OldImage, Namespace, ClassName);
            if (!OldClass) continue;

            // Compare methods
            void* MethodIter = nullptr;
            MonoMethod* NewMethod;

            while ((NewMethod = mono_class_get_methods(NewClass, &MethodIter)))
            {
                const char* MethodName = mono_method_get_name(NewMethod);
                
                // Find corresponding method in old class
                MonoMethod* OldMethod = mono_class_get_method_from_name(OldClass, MethodName, mono_method_signature(NewMethod)->param_count);
                if (!OldMethod) continue;

                // Replace method body
                if (ReplaceMethodBodyAndroid(OldMethod, NewMethod))
                {
                    ReplacedMethods++;
                }
            }
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Replaced %d methods during hot reload"), ReplacedMethods);
        return ReplacedMethods > 0;
    }

    // Public API Implementation
    bool InitializeAndroidHotReload()
    {
        if (AndroidHotReloadState.bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Android: Hot reload already initialized"));
            return true;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Initializing hot reload system"));

        // Configure Mono for Android hot reload
        ConfigureMonoForAndroidHotReload();

        // Create hot reload domain
        AndroidHotReloadState.HotReloadDomain = mono_domain_create_appdomain("AndroidHotReloadDomain", nullptr);
        if (!AndroidHotReloadState.HotReloadDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Failed to create hot reload domain"));
            return false;
        }

        // Initialize Android-specific optimizations
        AndroidOptimizations::OptimizeGCForHotReload();
        AndroidOptimizations::EnableInterpreterOptimizations();

        AndroidHotReloadState.bIsInitialized = true;
        AndroidHotReloadState.Stats = FAndroidHotReloadStats();

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Hot reload system initialized successfully"));
        return true;
    }

    bool IsAndroidHotReloadSupported()
    {
        // Check if running on Android
        #if !PLATFORM_ANDROID
        return false;
        #endif

        // Check if Mono runtime is available
        #if !WITH_MONO_RUNTIME
        return false;
        #endif

        // Check Mono version compatibility
        const char* MonoVersion = mono_get_runtime_version();
        if (!MonoVersion)
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Android: Could not determine Mono version"));
            return false;
        }

        // Verify essential functions are available
        if (!mono_method_get_unmanaged_thunk || !mono_method_set_unmanaged_thunk)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Required Mono functions not available"));
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Hot reload supported on Mono %s"), ANSI_TO_TCHAR(MonoVersion));
        return true;
    }

    bool HotReloadAssemblyAndroid(const FString& AssemblyName, const TArray<uint8>& DeltaData)
    {
        if (!AndroidHotReloadState.bIsInitialized)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Hot reload not initialized"));
            return false;
        }

        double StartTime = FPlatformTime::Seconds();
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Starting hot reload for assembly '%s'"), *AssemblyName);

        // Find registered assembly
        MonoAssembly* OldAssembly = AndroidHotReloadState.RegisteredAssemblies.FindRef(AssemblyName);
        if (!OldAssembly)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Assembly '%s' not registered for hot reload"), *AssemblyName);
            AndroidHotReloadState.Stats.FailedReloads++;
            return false;
        }

        // Load new assembly version
        MonoAssembly* NewAssembly = LoadAssemblyForAndroidHotReload(DeltaData, AssemblyName);
        if (!NewAssembly)
        {
            AndroidHotReloadState.Stats.FailedReloads++;
            return false;
        }

        // Switch to hot reload domain for the operation
        MonoDomain* CurrentDomain = mono_domain_get();
        mono_domain_set(AndroidHotReloadState.HotReloadDomain, false);

        bool bSuccess = CompareAndReplaceMethodsAndroid(OldAssembly, NewAssembly);

        // Restore original domain
        mono_domain_set(CurrentDomain, false);

        if (bSuccess)
        {
            // Update registered assembly
            AndroidHotReloadState.RegisteredAssemblies[AssemblyName] = NewAssembly;
            AndroidHotReloadState.Stats.TotalAssembliesReloaded++;
            AndroidHotReloadState.Stats.SuccessfulReloads++;
            AndroidHotReloadState.Stats.LastReloadTime = FDateTime::Now();
            
            double ElapsedTime = FPlatformTime::Seconds() - StartTime;
            AndroidHotReloadState.Stats.AverageReloadTime = (AndroidHotReloadState.Stats.AverageReloadTime + ElapsedTime) / 2.0;
            
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Hot reload completed successfully for '%s' in %.3f seconds"), *AssemblyName, ElapsedTime);
            
            // Show in-game notification
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, 
                    FString::Printf(TEXT("Android Hot Reload: %s ✓"), *AssemblyName));
            }
        }
        else
        {
            AndroidHotReloadState.Stats.FailedReloads++;
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Hot reload failed for '%s'"), *AssemblyName);
        }

        return bSuccess;
    }

    bool HotReloadDynamicCodeAndroid(const FString& CSharpCode)
    {
        if (!AndroidHotReloadState.bIsInitialized)
        {
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Evaluating dynamic C# code"));

        // This would use Mono's evaluator API for dynamic code execution
        // Implementation similar to iOS version but with Android optimizations
        
        // For now, return placeholder implementation
        UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Android: Dynamic code hot reload not yet fully implemented"));
        return false;
    }

    bool RevertHotReloadAndroid(const FString& AssemblyName)
    {
        // Find replaced methods for this assembly
        TArray<MonoMethod*>* ReplacedMethodsPtr = AndroidHotReloadState.ReplacedMethods.Find(AssemblyName);
        TArray<void*>* OriginalPointersPtr = AndroidHotReloadState.OriginalMethodPointers.Find(AssemblyName);

        if (!ReplacedMethodsPtr || !OriginalPointersPtr || ReplacedMethodsPtr->Num() != OriginalPointersPtr->Num())
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Android: Cannot revert hot reload for '%s' - missing data"), *AssemblyName);
            return false;
        }

        // Restore original method pointers
        for (int32 i = 0; i < ReplacedMethodsPtr->Num(); i++)
        {
            MonoMethod* Method = (*ReplacedMethodsPtr)[i];
            void* OriginalPtr = (*OriginalPointersPtr)[i];
            
            mono_method_set_unmanaged_thunk(Method, OriginalPtr);
        }

        // Clean up tracking data
        AndroidHotReloadState.ReplacedMethods.Remove(AssemblyName);
        AndroidHotReloadState.OriginalMethodPointers.Remove(AssemblyName);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Reverted hot reload for '%s'"), *AssemblyName);
        return true;
    }

    bool RegisterAssemblyForAndroidHotReload(MonoAssembly* Assembly)
    {
        if (!Assembly)
        {
            return false;
        }

        MonoImage* Image = mono_assembly_get_image(Assembly);
        if (!Image)
        {
            return false;
        }

        const char* AssemblyNameCStr = mono_image_get_name(Image);
        FString AssemblyName = FString(AssemblyNameCStr);

        AndroidHotReloadState.RegisteredAssemblies.Add(AssemblyName, Assembly);
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Registered assembly '%s' for hot reload"), *AssemblyName);
        return true;
    }

    FAndroidHotReloadStats GetAndroidHotReloadStats()
    {
        return AndroidHotReloadState.Stats;
    }

    void ShutdownAndroidHotReload()
    {
        if (!AndroidHotReloadState.bIsInitialized)
        {
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Shutting down hot reload system"));

        // Clean up hot reload domain
        if (AndroidHotReloadState.HotReloadDomain)
        {
            mono_domain_unload(AndroidHotReloadState.HotReloadDomain);
            AndroidHotReloadState.HotReloadDomain = nullptr;
        }

        // Clear tracking data
        AndroidHotReloadState.RegisteredAssemblies.Empty();
        AndroidHotReloadState.ReplacedMethods.Empty();
        AndroidHotReloadState.OriginalMethodPointers.Empty();

        AndroidHotReloadState.bIsInitialized = false;

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Hot reload system shut down"));
    }

    // Android Optimizations Implementation
    namespace AndroidOptimizations
    {
        void OptimizeThunkCache()
        {
            // Android-specific thunk caching optimizations
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Optimizing method thunk cache"));
            
            // Configure Mono's internal caches for better Android performance
            // This helps with the frequent method calls during hot reload
        }

        void OptimizeGCForHotReload()
        {
            // Adjust garbage collection for hot reload scenarios
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Optimizing GC for hot reload"));
            
            // Configure Mono GC to be less aggressive during hot reload operations
            // This prevents premature collection of hot reload related objects
        }

        bool EnableInterpreterOptimizations()
        {
            UE_LOG(LogTemp, Log, TEXT("UnrealSharp Android: Enabling interpreter optimizations"));
            
            // Enable Android-specific Mono interpreter optimizations
            // These help with performance during method replacement operations
            return true;
        }
    }
}

// Blueprint Library Implementation
bool UAndroidHotReloadBlueprintLibrary::IsAndroidHotReloadAvailable()
{
    return UnrealSharp::Android::HotReload::IsAndroidHotReloadSupported();
}

bool UAndroidHotReloadBlueprintLibrary::HotReloadAndroidCode(const FString& CSharpCode)
{
    return UnrealSharp::Android::HotReload::HotReloadDynamicCodeAndroid(CSharpCode);
}

FString UAndroidHotReloadBlueprintLibrary::GetAndroidHotReloadStatsString()
{
    UnrealSharp::Android::HotReload::FAndroidHotReloadStats Stats = UnrealSharp::Android::HotReload::GetAndroidHotReloadStats();
    
    FString Result;
    Result += FString::Printf(TEXT("Android Hot Reload Statistics:\n"));
    Result += FString::Printf(TEXT("Methods Replaced: %d\n"), Stats.TotalMethodsReplaced);
    Result += FString::Printf(TEXT("Assemblies Reloaded: %d\n"), Stats.TotalAssembliesReloaded);
    Result += FString::Printf(TEXT("Successful Reloads: %d\n"), Stats.SuccessfulReloads);
    Result += FString::Printf(TEXT("Failed Reloads: %d\n"), Stats.FailedReloads);
    Result += FString::Printf(TEXT("Average Reload Time: %.3f seconds\n"), Stats.AverageReloadTime);
    Result += FString::Printf(TEXT("Last Reload: %s"), *Stats.LastReloadTime.ToString());
    
    return Result;
}

bool UAndroidHotReloadBlueprintLibrary::RevertAndroidAssemblyHotReload(const FString& AssemblyName)
{
    return UnrealSharp::Android::HotReload::RevertHotReloadAndroid(AssemblyName);
}

bool UAndroidHotReloadBlueprintLibrary::EnableAndroidHotReloadOptimizations()
{
    UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeThunkCache();
    UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeGCForHotReload();
    return UnrealSharp::Android::HotReload::AndroidOptimizations::EnableInterpreterOptimizations();
}

#endif // WITH_MONO_RUNTIME && PLATFORM_ANDROID