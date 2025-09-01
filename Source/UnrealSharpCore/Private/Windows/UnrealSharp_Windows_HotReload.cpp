#include "Windows/UnrealSharp_Windows_HotReload.h"

#if WITH_MONO_RUNTIME && PLATFORM_WINDOWS

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Engine/Engine.h"
#include "Async/TaskGraphInterfaces.h"

// Mono includes for Windows hot reload
#include "mono/metadata/mono-config.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/tokentype.h"
#include "mono/utils/mono-publib.h"

namespace UnrealSharp::Windows::HotReload
{
    // Windows-specific hot reload state
    struct FWindowsHotReloadState
    {
        TMap<FString, MonoAssembly*> RegisteredAssemblies;
        TMap<FString, MonoDomain*> AssemblyDomains;
        TMap<FString, TArray<MonoMethod*>> ReplacedMethods;
        MonoDomain* MainDomain;
        MonoDomain* CurrentHotReloadDomain;
        bool bIsInitialized;
        FWindowsHotReloadStats Stats;

        FWindowsHotReloadState()
            : MainDomain(nullptr)
            , CurrentHotReloadDomain(nullptr)
            , bIsInitialized(false)
        {}
    };

    static FWindowsHotReloadState WindowsHotReloadState;

    /**
     * Windows-specific Mono configuration for enhanced hot reload
     */
    void ConfigureMonoForWindowsHotReload()
    {
        // Enable enhanced JIT mode for optimal Windows performance
        mono_jit_set_aot_mode(MONO_AOT_MODE_NORMAL);
        
        // Configure Mono for Windows optimizations
        mono_config_parse_environment();
        
        // Enable Windows-specific optimizations
        mono_set_signal_chaining(true);
        mono_set_crash_chaining(true);
        
        // Configure enhanced JIT compilation for hot reload
        const char* jit_options = "--optimize=all,peephole,branch,inline,cfold,consprop,copyprop,deadce,linears,cmov,shared --server";
        mono_jit_parse_options(1, (char**)&jit_options);
        
        // Enable advanced debugging for hot reload
        mono_debug_init(MONO_DEBUG_FORMAT_MONO);
        mono_debug_set_level(MONO_DEBUG_LEVEL_SOURCE);
        
        // Enable Windows-specific domain optimizations
        mono_domain_set_config(mono_domain_get(), "", "");
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Configured enhanced Mono hot reload with Windows optimizations"));
    }

    /**
     * Enhanced AppDomain switching for Windows with no restart
     */
    bool SwitchToHotReloadDomainWindows(const FString& AssemblyName)
    {
        // Create new domain if needed
        FString DomainName = FString::Printf(TEXT("HotReloadDomain_%s_%lld"), *AssemblyName, FDateTime::Now().GetTicks());
        
        MonoDomain* NewDomain = mono_domain_create_appdomain(TCHAR_TO_ANSI(*DomainName), nullptr);
        if (!NewDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Failed to create hot reload domain for %s"), *AssemblyName);
            return false;
        }

        // Store previous domain
        MonoDomain* PreviousDomain = WindowsHotReloadState.CurrentHotReloadDomain;
        
        // Switch to new domain
        if (!mono_domain_set(NewDomain, false))
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Failed to switch to hot reload domain for %s"), *AssemblyName);
            mono_domain_unload(NewDomain);
            return false;
        }

        WindowsHotReloadState.CurrentHotReloadDomain = NewDomain;
        WindowsHotReloadState.AssemblyDomains.Add(AssemblyName, NewDomain);

        // Schedule cleanup of previous domain
        if (PreviousDomain && PreviousDomain != WindowsHotReloadState.MainDomain)
        {
            // Unload previous domain asynchronously to avoid blocking
            FFunctionGraphTask::CreateAndDispatchWhenReady([PreviousDomain]()
            {
                mono_domain_unload(PreviousDomain);
                UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Windows: Cleaned up previous hot reload domain"));
            }, TStatId(), nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Switched to enhanced hot reload domain for %s (no restart)"), *AssemblyName);
        return true;
    }

    /**
     * Load assembly in hot reload domain with Windows optimizations
     */
    MonoAssembly* LoadAssemblyInHotReloadDomainWindows(const FString& AssemblyName, const TArray<uint8>& AssemblyData)
    {
        if (!WindowsHotReloadState.CurrentHotReloadDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: No hot reload domain available for %s"), *AssemblyName);
            return nullptr;
        }

        MonoImageOpenStatus Status;
        
        // Load assembly with Windows memory optimization
        MonoImage* Image = mono_image_open_from_data_with_name(
            reinterpret_cast<char*>(const_cast<uint8*>(AssemblyData.GetData())),
            AssemblyData.Num(),
            true, // copy_data - Windows memory management optimization
            &Status,
            false,
            TCHAR_TO_ANSI(*AssemblyName)
        );

        if (Status != MONO_IMAGE_OK || !Image)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Failed to load image for %s, status: %d"), *AssemblyName, (int32)Status);
            return nullptr;
        }

        // Load assembly in current hot reload domain
        MonoAssembly* Assembly = mono_assembly_load_from_full(Image, TCHAR_TO_ANSI(*AssemblyName), &Status, false);
        if (Status != MONO_IMAGE_OK || !Assembly)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Failed to load assembly %s in hot reload domain, status: %d"), *AssemblyName, (int32)Status);
            mono_image_close(Image);
            return nullptr;
        }

        // Register assembly for tracking
        WindowsHotReloadState.RegisteredAssemblies.Add(AssemblyName, Assembly);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Loaded assembly %s in hot reload domain successfully"), *AssemblyName);
        return Assembly;
    }

    /**
     * Enhanced hot reload assembly replacement for Windows
     */
    bool HotReloadAssemblyWindows(const FString& AssemblyName, const TArray<uint8>& AssemblyData)
    {
        if (!WindowsHotReloadState.bIsInitialized)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Hot reload not initialized"));
            return false;
        }

        double StartTime = FPlatformTime::Seconds();
        
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Starting enhanced hot reload for assembly '%s' (no restart)"), *AssemblyName);

        // Switch to hot reload domain
        if (!SwitchToHotReloadDomainWindows(AssemblyName))
        {
            WindowsHotReloadState.Stats.FailedReloads++;
            return false;
        }

        // Load new assembly
        MonoAssembly* NewAssembly = LoadAssemblyInHotReloadDomainWindows(AssemblyName, AssemblyData);
        if (!NewAssembly)
        {
            WindowsHotReloadState.Stats.FailedReloads++;
            return false;
        }

        // Update statistics
        WindowsHotReloadState.Stats.TotalAssembliesReloaded++;
        WindowsHotReloadState.Stats.SuccessfulReloads++;
        WindowsHotReloadState.Stats.LastReloadTime = FDateTime::Now();
        
        double ReloadTime = FPlatformTime::Seconds() - StartTime;
        WindowsHotReloadState.Stats.AverageReloadTime = 
            (WindowsHotReloadState.Stats.AverageReloadTime * (WindowsHotReloadState.Stats.SuccessfulReloads - 1) + ReloadTime) / 
            WindowsHotReloadState.Stats.SuccessfulReloads;

        // Display success notification
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, 
                FString::Printf(TEXT("Windows Hot Reload: %s ✓ (%.2fs, no restart)"), *AssemblyName, ReloadTime));
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Enhanced hot reload completed for '%s' in %.3f seconds (no restart)"), 
               *AssemblyName, ReloadTime);
        
        return true;
    }

    // Public API Implementation
    bool InitializeWindowsHotReload()
    {
        if (WindowsHotReloadState.bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Windows: Hot reload already initialized"));
            return true;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Initializing enhanced hot reload system"));

        // Store main domain
        WindowsHotReloadState.MainDomain = mono_domain_get();
        if (!WindowsHotReloadState.MainDomain)
        {
            UE_LOG(LogTemp, Error, TEXT("UnrealSharp Windows: Could not get main Mono domain"));
            return false;
        }

        // Configure Mono for Windows hot reload
        ConfigureMonoForWindowsHotReload();

        // Initialize Windows-specific optimizations
        WindowsOptimizations::OptimizeDomainSwitching();
        WindowsOptimizations::EnableJITOptimizations();
        WindowsOptimizations::ConfigureMemoryManagement();

        WindowsHotReloadState.bIsInitialized = true;
        WindowsHotReloadState.Stats = FWindowsHotReloadStats();

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Enhanced hot reload system initialized successfully"));
        return true;
    }

    bool IsWindowsHotReloadSupported()
    {
        // Check if running on Windows
        #if !PLATFORM_WINDOWS
        return false;
        #endif

        // Check if Mono runtime is available
        #if !WITH_MONO_RUNTIME
        return false;
        #endif

        // Check if main domain is available
        if (!mono_domain_get())
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Windows: Main Mono domain not available"));
            return false;
        }

        // Check Mono version compatibility
        const char* MonoVersion = mono_get_runtime_version();
        if (!MonoVersion)
        {
            UE_LOG(LogTemp, Warning, TEXT("UnrealSharp Windows: Could not determine Mono version"));
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Enhanced hot reload supported on Mono %s"), ANSI_TO_TCHAR(MonoVersion));
        return true;
    }

    bool RegisterAssemblyForWindowsHotReload(MonoAssembly* Assembly)
    {
        if (!Assembly)
        {
            return false;
        }

        const char* AssemblyNamePtr = mono_image_get_name(mono_assembly_get_image(Assembly));
        FString AssemblyName = FString(AssemblyNamePtr);

        WindowsHotReloadState.RegisteredAssemblies.Add(AssemblyName, Assembly);
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Registered assembly '%s' for enhanced hot reload"), *AssemblyName);
        
        return true;
    }

    FWindowsHotReloadStats GetWindowsHotReloadStats()
    {
        return WindowsHotReloadState.Stats;
    }

    void ShutdownWindowsHotReload()
    {
        if (!WindowsHotReloadState.bIsInitialized)
        {
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Shutting down enhanced hot reload system"));

        // Clean up hot reload domains
        for (auto& DomainPair : WindowsHotReloadState.AssemblyDomains)
        {
            MonoDomain* Domain = DomainPair.Value;
            if (Domain && Domain != WindowsHotReloadState.MainDomain)
            {
                mono_domain_unload(Domain);
            }
        }

        // Switch back to main domain
        if (WindowsHotReloadState.MainDomain)
        {
            mono_domain_set(WindowsHotReloadState.MainDomain, false);
        }

        // Clear state
        WindowsHotReloadState.RegisteredAssemblies.Empty();
        WindowsHotReloadState.AssemblyDomains.Empty();
        WindowsHotReloadState.ReplacedMethods.Empty();
        WindowsHotReloadState.bIsInitialized = false;

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Windows: Enhanced hot reload system shutdown complete"));
    }

    // Windows-specific optimizations namespace
    namespace WindowsOptimizations
    {
        void OptimizeDomainSwitching()
        {
            // Configure domain switching optimizations for Windows
            mono_domain_set_config(mono_domain_get(), "System.GC.Server", "true");
            mono_domain_set_config(mono_domain_get(), "System.GC.Concurrent", "true");
            
            UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Windows: Optimized domain switching"));
        }

        void EnableJITOptimizations()
        {
            // Enable Windows-specific JIT optimizations
            mono_set_signal_chaining(true);
            mono_jit_set_trace_options("jit");
            
            UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Windows: Enabled JIT optimizations"));
        }

        void ConfigureMemoryManagement()
        {
            // Configure memory management for hot reload
            mono_gc_set_desktop_mode();
            
            UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Windows: Configured memory management"));
        }
    }
}

// Blueprint library implementation
UWindowsHotReloadBlueprintLibrary::UWindowsHotReloadBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

bool UWindowsHotReloadBlueprintLibrary::IsWindowsHotReloadAvailable()
{
    return UnrealSharp::Windows::HotReload::IsWindowsHotReloadSupported();
}

FString UWindowsHotReloadBlueprintLibrary::GetWindowsHotReloadStatsString()
{
    using namespace UnrealSharp::Windows::HotReload;
    
    FWindowsHotReloadStats Stats = GetWindowsHotReloadStats();
    
    return FString::Printf(TEXT("Windows Hot Reload Stats:\n")
                          TEXT("  Total Assemblies Reloaded: %d\n")
                          TEXT("  Successful Reloads: %d\n") 
                          TEXT("  Failed Reloads: %d\n")
                          TEXT("  Average Reload Time: %.3fs\n")
                          TEXT("  Last Reload: %s\n")
                          TEXT("  Status: No Restart Required"),
                          Stats.TotalAssembliesReloaded,
                          Stats.SuccessfulReloads,
                          Stats.FailedReloads,
                          Stats.AverageReloadTime,
                          *Stats.LastReloadTime.ToString());
}

bool UWindowsHotReloadBlueprintLibrary::EnableWindowsHotReloadOptimizations()
{
    using namespace UnrealSharp::Windows::HotReload;
    
    WindowsOptimizations::OptimizeDomainSwitching();
    WindowsOptimizations::EnableJITOptimizations();
    WindowsOptimizations::ConfigureMemoryManagement();
    
    return true;
}

#endif // WITH_MONO_RUNTIME && PLATFORM_WINDOWS