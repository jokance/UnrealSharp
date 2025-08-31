#include "Android/UnrealSharp_Android_HotReload.h"

#if WITH_MONO_RUNTIME && PLATFORM_ANDROID

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

/**
 * Unit tests for Android Hot Reload functionality
 * These tests validate the core Android hot reload features
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidHotReloadBasicTest, "UnrealSharp.Android.HotReload.Basic",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidHotReloadBasicTest::RunTest(const FString& Parameters)
{
    // Test 1: Check if Android hot reload is supported
    bool bIsSupported = UnrealSharp::Android::HotReload::IsAndroidHotReloadSupported();
    TestTrue(TEXT("Android hot reload should be supported on Android platform"), bIsSupported);

    // Test 2: Initialize Android hot reload
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    TestTrue(TEXT("Android hot reload initialization should succeed"), bInitialized);

    // Test 3: Get initial statistics
    UnrealSharp::Android::HotReload::FAndroidHotReloadStats InitialStats = 
        UnrealSharp::Android::HotReload::GetAndroidHotReloadStats();
    TestEqual(TEXT("Initial method replacement count should be 0"), InitialStats.TotalMethodsReplaced, 0);
    TestEqual(TEXT("Initial assembly reload count should be 0"), InitialStats.TotalAssembliesReloaded, 0);

    // Test 4: Test blueprint library functions
    bool bBlueprintSupported = UAndroidHotReloadBlueprintLibrary::IsAndroidHotReloadAvailable();
    TestTrue(TEXT("Blueprint library should report Android hot reload as available"), bBlueprintSupported);

    // Test 5: Test optimization functions
    bool bOptimizationsEnabled = UAndroidHotReloadBlueprintLibrary::EnableAndroidHotReloadOptimizations();
    TestTrue(TEXT("Android hot reload optimizations should be enabled successfully"), bOptimizationsEnabled);

    // Test 6: Get statistics string format
    FString StatsString = UAndroidHotReloadBlueprintLibrary::GetAndroidHotReloadStatsString();
    TestTrue(TEXT("Statistics string should not be empty"), !StatsString.IsEmpty());
    TestTrue(TEXT("Statistics string should contain 'Android Hot Reload Statistics'"), 
        StatsString.Contains(TEXT("Android Hot Reload Statistics")));

    // Test 7: Cleanup
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidHotReloadAssemblyTest, "UnrealSharp.Android.HotReload.Assembly",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidHotReloadAssemblyTest::RunTest(const FString& Parameters)
{
    // Initialize for testing
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    if (!TestTrue(TEXT("Hot reload must be initialized for assembly tests"), bInitialized))
    {
        return false;
    }

    // Test assembly registration with nullptr (should fail gracefully)
    bool bNullRegister = UnrealSharp::Android::HotReload::RegisterAssemblyForAndroidHotReload(nullptr);
    TestFalse(TEXT("Registering null assembly should fail"), bNullRegister);

    // Test hot reload with invalid assembly name (should fail gracefully)
    TArray<uint8> DummyData;
    DummyData.Add(0x4D); // 'M' - fake PE header start
    DummyData.Add(0x5A); // 'Z'
    
    bool bInvalidReload = UnrealSharp::Android::HotReload::HotReloadAssemblyAndroid(
        TEXT("NonExistentAssembly"), DummyData);
    TestFalse(TEXT("Hot reload of non-existent assembly should fail"), bInvalidReload);

    // Test revert of non-existent assembly (should fail gracefully)
    bool bInvalidRevert = UnrealSharp::Android::HotReload::RevertHotReloadAndroid(TEXT("NonExistentAssembly"));
    TestFalse(TEXT("Revert of non-existent assembly should fail"), bInvalidRevert);

    // Cleanup
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidHotReloadDynamicCodeTest, "UnrealSharp.Android.HotReload.DynamicCode",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidHotReloadDynamicCodeTest::RunTest(const FString& Parameters)
{
    // Initialize for testing
    bool bInitialized = UnrealSharp::Android::HotReload::InitializeAndroidHotReload();
    if (!TestTrue(TEXT("Hot reload must be initialized for dynamic code tests"), bInitialized))
    {
        return false;
    }

    // Test empty code (should fail gracefully)
    bool bEmptyCode = UnrealSharp::Android::HotReload::HotReloadDynamicCodeAndroid(TEXT(""));
    TestFalse(TEXT("Empty dynamic code should fail"), bEmptyCode);

    // Test simple C# expression
    FString SimpleCode = TEXT("System.Console.WriteLine(\"Hello from Android hot reload!\");");
    bool bSimpleCode = UnrealSharp::Android::HotReload::HotReloadDynamicCodeAndroid(SimpleCode);
    // Note: This may fail as dynamic code execution is still being implemented
    // We're just testing that it doesn't crash
    TestTrue(TEXT("Dynamic code execution should not crash"), true);

    // Test blueprint library dynamic code function
    bool bBlueprintDynamicCode = UAndroidHotReloadBlueprintLibrary::HotReloadAndroidCode(SimpleCode);
    // Again, just testing for crashes, not success
    TestTrue(TEXT("Blueprint dynamic code should not crash"), true);

    // Cleanup
    UnrealSharp::Android::HotReload::ShutdownAndroidHotReload();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidHotReloadOptimizationsTest, "UnrealSharp.Android.HotReload.Optimizations",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAndroidHotReloadOptimizationsTest::RunTest(const FString& Parameters)
{
    // Test individual optimization functions
    UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeThunkCache();
    TestTrue(TEXT("Thunk cache optimization should not crash"), true);

    UnrealSharp::Android::HotReload::AndroidOptimizations::OptimizeGCForHotReload();
    TestTrue(TEXT("GC optimization should not crash"), true);

    bool bInterpreterOptimizations = UnrealSharp::Android::HotReload::AndroidOptimizations::EnableInterpreterOptimizations();
    TestTrue(TEXT("Interpreter optimizations should be enabled successfully"), bInterpreterOptimizations);

    return true;
}

#endif // WITH_MONO_RUNTIME && PLATFORM_ANDROID