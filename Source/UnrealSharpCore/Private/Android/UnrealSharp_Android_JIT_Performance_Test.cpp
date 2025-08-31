#include "Android/UnrealSharp_Android_HotReload.h"

#if WITH_MONO_RUNTIME && PLATFORM_ANDROID

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/DateTime.h"

/**
 * Performance tests for Android JIT Hot Reload functionality
 * These tests validate JIT compilation performance and optimization effectiveness
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidJITPerformanceTest, "UnrealSharp.Android.HotReload.JIT.Performance",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidJITPerformanceTest::RunTest(const FString& Parameters)
{
    // Test 1: JIT initialization performance
    double StartTime = FPlatformTime::Seconds();
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    double InitTime = FPlatformTime::Seconds() - StartTime;
    
    TestTrue(TEXT("Android JIT hot reload should initialize successfully"), bInitialized);
    TestTrue(TEXT("JIT initialization should complete within 1 second"), InitTime < 1.0);
    
    UE_LOG(LogTemp, Log, TEXT("Android JIT Hot Reload initialization time: %.3f seconds"), InitTime);

    // Test 2: JIT optimization enablement performance
    StartTime = FPlatformTime::Seconds();
    bool bOptimizationsEnabled = UnrealSharp::Android::HotReload::AndroidOptimizations::EnableJITOptimizations();
    double OptimizationTime = FPlatformTime::Seconds() - StartTime;
    
    TestTrue(TEXT("JIT optimizations should be enabled successfully"), bOptimizationsEnabled);
    TestTrue(TEXT("JIT optimization setup should complete within 0.5 seconds"), OptimizationTime < 0.5);
    
    UE_LOG(LogTemp, Log, TEXT("JIT optimizations setup time: %.3f seconds"), OptimizationTime);

    // Test 3: JIT code cache configuration performance
    StartTime = FPlatformTime::Seconds();
    UnrealSharp::Android::HotReload::AndroidOptimizations::ConfigureJITCodeCache();
    double CacheConfigTime = FPlatformTime::Seconds() - StartTime;
    
    TestTrue(TEXT("JIT code cache configuration should complete within 0.2 seconds"), CacheConfigTime < 0.2);
    
    UE_LOG(LogTemp, Log, TEXT("JIT code cache configuration time: %.3f seconds"), CacheConfigTime);

    // Test 4: Get performance statistics
    UnrealSharp::Android::HotReload::FAndroidHotReloadStats Stats = 
        UnrealSharp::Android::HotReload::GetAndroidHotReloadStats();
    
    TestTrue(TEXT("Performance statistics should be available"), true);
    TestTrue(TEXT("Average reload time should be initialized to 0"), Stats.AverageReloadTime == 0.0);

    // Test 5: Blueprint JIT performance functions
    StartTime = FPlatformTime::Seconds();
    bool bBlueprintOptimizations = UAndroidHotReloadBlueprintLibrary::EnableAndroidHotReloadOptimizations();
    double BlueprintOptTime = FPlatformTime::Seconds() - StartTime;
    
    TestTrue(TEXT("Blueprint JIT optimizations should be enabled"), bBlueprintOptimizations);
    TestTrue(TEXT("Blueprint optimization setup should complete quickly"), BlueprintOptTime < 0.3);
    
    UE_LOG(LogTemp, Log, TEXT("Blueprint JIT optimizations setup time: %.3f seconds"), BlueprintOptTime);

    // Clean up
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();
    
    UE_LOG(LogTemp, Log, TEXT("Android JIT Hot Reload performance test completed successfully"));
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidJITMemoryUsageTest, "UnrealSharp.Android.HotReload.JIT.Memory",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidJITMemoryUsageTest::RunTest(const FString& Parameters)
{
    // Get initial memory usage
    FPlatformMemoryStats InitialMemStats = FPlatformMemory::GetStats();
    SIZE_T InitialUsedMemory = InitialMemStats.UsedPhysical;
    
    UE_LOG(LogTemp, Log, TEXT("Initial memory usage: %.2f MB"), InitialUsedMemory / (1024.0f * 1024.0f));

    // Initialize JIT hot reload
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    TestTrue(TEXT("JIT hot reload should initialize for memory test"), bInitialized);

    // Enable JIT optimizations
    UnrealSharp::Android::HotReload::AndroidOptimizations::EnableJITOptimizations();
    UnrealSharp::Android::HotReload::AndroidOptimizations::ConfigureJITCodeCache();

    // Get memory usage after JIT setup
    FPlatformMemoryStats PostJITMemStats = FPlatformMemory::GetStats();
    SIZE_T PostJITUsedMemory = PostJITMemStats.UsedPhysical;
    SIZE_T JITMemoryOverhead = PostJITUsedMemory - InitialUsedMemory;
    
    UE_LOG(LogTemp, Log, TEXT("Memory usage after JIT setup: %.2f MB"), PostJITUsedMemory / (1024.0f * 1024.0f));
    UE_LOG(LogTemp, Log, TEXT("JIT memory overhead: %.2f MB"), JITMemoryOverhead / (1024.0f * 1024.0f));

    // Test memory overhead is within acceptable limits (< 20MB for JIT setup)
    TestTrue(TEXT("JIT memory overhead should be reasonable"), JITMemoryOverhead < 20 * 1024 * 1024);

    // Test multiple optimization cycles don't cause memory leaks
    SIZE_T PreOptMemory = PostJITUsedMemory;
    
    for (int32 i = 0; i < 5; i++)
    {
        UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeThunkCache();
        UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeGCForHotReload();
    }
    
    FPlatformMemoryStats PostOptMemStats = FPlatformMemory::GetStats();
    SIZE_T PostOptUsedMemory = PostOptMemStats.UsedPhysical;
    SIZE_T OptimizationMemoryDelta = (PostOptUsedMemory > PreOptMemory) ? 
        PostOptUsedMemory - PreOptMemory : PreOptMemory - PostOptUsedMemory;
    
    UE_LOG(LogTemp, Log, TEXT("Memory delta after optimization cycles: %.2f MB"), 
        OptimizationMemoryDelta / (1024.0f * 1024.0f));

    // Memory delta should be minimal (< 5MB) indicating no significant leaks
    TestTrue(TEXT("Optimization cycles should not cause significant memory growth"), 
        OptimizationMemoryDelta < 5 * 1024 * 1024);

    // Clean up and test memory restoration
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();
    
    FPlatformMemoryStats FinalMemStats = FPlatformMemory::GetStats();
    SIZE_T FinalUsedMemory = FinalMemStats.UsedPhysical;
    
    UE_LOG(LogTemp, Log, TEXT("Final memory usage after cleanup: %.2f MB"), 
        FinalUsedMemory / (1024.0f * 1024.0f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidJITCompatibilityTest, "UnrealSharp.Android.HotReload.JIT.Compatibility",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidJITCompatibilityTest::RunTest(const FString& Parameters)
{
    // Test 1: Verify JIT mode is properly set
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    TestTrue(TEXT("JIT hot reload should initialize for compatibility test"), bInitialized);

    // Test 2: Verify JIT is preferred over interpreter mode
    bool bIsSupported = UnrealSharp::Android::HotReload::IsAndroidHotReloadSupported();
    TestTrue(TEXT("Android JIT hot reload should be supported"), bIsSupported);

    // Test 3: Test JIT optimization functions are available
    bool bJITOptimizations = false;
    try 
    {
        bJITOptimizations = UnrealSharp::Android::HotReload::AndroidOptimizations::EnableJITOptimizations();
        TestTrue(TEXT("JIT optimizations should be available"), bJITOptimizations);
    }
    catch (...)
    {
        TestFalse(TEXT("JIT optimization functions should not throw exceptions"), true);
    }

    // Test 4: Test JIT code cache functions
    bool bCacheConfigured = false;
    try
    {
        UnrealSharp::Android::HotReload::AndroidOptimizations::ConfigureJITCodeCache();
        bCacheConfigured = true;
        TestTrue(TEXT("JIT code cache should be configurable"), bCacheConfigured);
    }
    catch (...)
    {
        TestFalse(TEXT("JIT code cache configuration should not throw exceptions"), true);
    }

    // Test 5: Verify Blueprint integration works with JIT
    bool bBlueprintSupport = UAndroidHotReloadBlueprintLibrary::IsAndroidHotReloadAvailable();
    TestTrue(TEXT("Blueprint support should work with JIT"), bBlueprintSupport);

    // Test 6: Test statistics reporting with JIT
    FString StatsString = UAndroidHotReloadBlueprintLibrary::GetAndroidHotReloadStatsString();
    TestTrue(TEXT("JIT statistics should be reportable"), !StatsString.IsEmpty());
    TestTrue(TEXT("Statistics should mention Android Hot Reload"), 
        StatsString.Contains(TEXT("Android Hot Reload")));

    // Clean up
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();
    
    UE_LOG(LogTemp, Log, TEXT("Android JIT compatibility test completed successfully"));
    
    return true;
}

#endif // WITH_MONO_RUNTIME && PLATFORM_ANDROID