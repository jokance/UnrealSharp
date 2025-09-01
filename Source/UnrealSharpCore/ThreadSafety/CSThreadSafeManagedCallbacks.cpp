#include "CSThreadSafeManagedCallbacks.h"
#include "Engine/Engine.h"

// 全局实例
static FCSThreadSafeManagedCallbacks GlobalThreadSafeManagedCallbacks;

FCSThreadSafeManagedCallbacks& GetGlobalThreadSafeManagedCallbacks()
{
    return GlobalThreadSafeManagedCallbacks;
}

bool FCSThreadSafeManagedCallbacks::Initialize(const FConcurrencyConfig& InConfig)
{
    std::unique_lock<std::mutex> Lock(CallbackMutex);
    
    if (bIsInitialized.load(std::memory_order_relaxed))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Already initialized"));
        return true;
    }

    Config = InConfig;
    
    // 重置统计信息
    ResetStatistics();
    
    // 清空活跃回调集合
    {
        FScopeLock ActiveLock(&ActiveCallbacksMutex);
        ActiveCallbackIds.Empty();
    }
    
    bIsShuttingDown.store(false, std::memory_order_release);
    bIsInitialized.store(true, std::memory_order_release);
    
    UE_LOG(LogTemp, Log, TEXT("CSThreadSafeManagedCallbacks: Initialized with max %d concurrent callbacks"), 
           Config.MaxConcurrentCallbacks);
    
    return true;
}

void FCSThreadSafeManagedCallbacks::Shutdown()
{
    {
        std::unique_lock<std::mutex> Lock(CallbackMutex);
        
        if (!bIsInitialized.load(std::memory_order_relaxed))
        {
            return;
        }
        
        bIsShuttingDown.store(true, std::memory_order_release);
    }
    
    UE_LOG(LogTemp, Log, TEXT("CSThreadSafeManagedCallbacks: Shutting down callback system"));
    
    // 等待所有活跃回调完成（最多等待10秒）
    double StartTime = FPlatformTime::Seconds();
    const double MaxWaitTime = 10.0; // 10秒超时
    
    while (Stats.CurrentActiveCalls.load(std::memory_order_relaxed) > 0 && 
           (FPlatformTime::Seconds() - StartTime) < MaxWaitTime)
    {
        FPlatformProcess::Sleep(0.1f);
    }
    
    int32 RemainingCalls = Stats.CurrentActiveCalls.load(std::memory_order_relaxed);
    if (RemainingCalls > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: %d callbacks still active during shutdown"), 
               RemainingCalls);
        ForceTerminateAllCallbacks();
    }
    
    // 导出最终统计信息
    FString FinalReport = ExportDiagnosticsReport();
    UE_LOG(LogTemp, Log, TEXT("CSThreadSafeManagedCallbacks: Final Statistics:\n%s"), *FinalReport);
    
    bIsInitialized.store(false, std::memory_order_release);
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeCreateNewManagedObject(const void* Object, void* TypeHandle, TCHAR** Error, FGCHandleIntPtr& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.CreateNewManagedObject(Object, TypeHandle, Error);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        ECallbackResult Result = OutResult.IntPtr ? ECallbackResult::Success : ECallbackResult::Failed;
        
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in CreateNewManagedObject"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeCreateNewManagedObjectWrapper(void* Object, void* TypeHandle, FGCHandleIntPtr& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.CreateNewManagedObjectWrapper(Object, TypeHandle);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        ECallbackResult Result = OutResult.IntPtr ? ECallbackResult::Success : ECallbackResult::Failed;
        
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in CreateNewManagedObjectWrapper"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeInvokeManagedEvent(void* EventPtr, void* Params, void* Result, void* Exception, void* World, int& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.InvokeManagedMethod(EventPtr, Params, Result, Exception, World);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        
        // 检查超时
        if (Config.bEnableTimeout && ElapsedTime > Config.CallbackTimeoutSeconds * 1000.0)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Event invocation timeout (%.2fms)"), ElapsedTime);
            RecordCallbackResult(ECallbackResult::Timeout, ElapsedTime);
            return ECallbackResult::Timeout;
        }
        
        ECallbackResult Result = (OutResult == 0) ? ECallbackResult::Success : ECallbackResult::Failed;
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in InvokeManagedEvent"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeInvokeDelegate(FGCHandleIntPtr DelegateHandle, int& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.InvokeDelegate(DelegateHandle);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        
        // 检查超时
        if (Config.bEnableTimeout && ElapsedTime > Config.CallbackTimeoutSeconds * 1000.0)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Delegate invocation timeout (%.2fms)"), ElapsedTime);
            RecordCallbackResult(ECallbackResult::Timeout, ElapsedTime);
            return ECallbackResult::Timeout;
        }
        
        ECallbackResult Result = (OutResult == 0) ? ECallbackResult::Success : ECallbackResult::Failed;
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in InvokeDelegate"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeLookupMethod(void* Assembly, const TCHAR* MethodName, uint8*& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.LookupManagedMethod(Assembly, MethodName);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        ECallbackResult Result = OutResult ? ECallbackResult::Success : ECallbackResult::Failed;
        
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in LookupMethod"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeLookupType(uint8* Assembly, const TCHAR* TypeName, uint8*& OutResult)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        OutResult = FCSManagedCallbacks::ManagedCallbacks.LookupManagedType(Assembly, TypeName);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        ECallbackResult Result = OutResult ? ECallbackResult::Success : ECallbackResult::Failed;
        
        RecordCallbackResult(Result, ElapsedTime);
        return Result;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in LookupType"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeDispose(FGCHandleIntPtr Handle, FGCHandleIntPtr AssemblyHandle)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        FCSManagedCallbacks::ManagedCallbacks.Dispose(Handle, AssemblyHandle);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Success, ElapsedTime);
        return ECallbackResult::Success;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in Dispose"));
        return ECallbackResult::Failed;
    }
}

FCSThreadSafeManagedCallbacks::ECallbackResult FCSThreadSafeManagedCallbacks::SafeFreeHandle(FGCHandleIntPtr Handle)
{
    if (!CanAcceptNewCallback())
    {
        return ECallbackResult::TooManyConcurrentCalls;
    }

    FScopedCallbackTracker Tracker(*this);
    double StartTime = FPlatformTime::Seconds();

    try
    {
        FCSManagedCallbacks::ManagedCallbacks.FreeHandle(Handle);
        
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Success, ElapsedTime);
        return ECallbackResult::Success;
    }
    catch (...)
    {
        double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        RecordCallbackResult(ECallbackResult::Failed, ElapsedTime);
        
        UE_LOG(LogTemp, Error, TEXT("CSThreadSafeManagedCallbacks: Exception in FreeHandle"));
        return ECallbackResult::Failed;
    }
}

void FCSThreadSafeManagedCallbacks::UpdateConfiguration(const FConcurrencyConfig& NewConfig)
{
    std::lock_guard<std::mutex> Lock(CallbackMutex);
    Config = NewConfig;
    
    UE_LOG(LogTemp, Log, TEXT("CSThreadSafeManagedCallbacks: Configuration updated"));
}

void FCSThreadSafeManagedCallbacks::ResetStatistics()
{
    Stats = FCallbackStats{};
    
    UE_LOG(LogTemp, Log, TEXT("CSThreadSafeManagedCallbacks: Statistics reset"));
}

FString FCSThreadSafeManagedCallbacks::ExportDiagnosticsReport()
{
    FString Report;
    
    Report += TEXT("=== ThreadSafe Managed Callbacks Diagnostics ===\n");
    Report += FString::Printf(TEXT("Total Callbacks Executed: %d\n"), Stats.TotalCallbacksExecuted.load());
    Report += FString::Printf(TEXT("Successful Callbacks: %d\n"), Stats.SuccessfulCallbacks.load());
    Report += FString::Printf(TEXT("Failed Callbacks: %d\n"), Stats.FailedCallbacks.load());
    Report += FString::Printf(TEXT("Timeout Callbacks: %d\n"), Stats.TimeoutCallbacks.load());
    Report += FString::Printf(TEXT("Rejected Callbacks: %d\n"), Stats.RejectedCallbacks.load());
    Report += FString::Printf(TEXT("Success Rate: %.2f%%\n"), Stats.GetSuccessRate() * 100.0);
    Report += FString::Printf(TEXT("Current Active Calls: %d\n"), Stats.CurrentActiveCalls.load());
    Report += FString::Printf(TEXT("Max Concurrent Calls: %d\n"), Stats.MaxConcurrentCalls.load());
    Report += FString::Printf(TEXT("Average Execution Time: %.2f ms\n"), Stats.AverageExecutionTime.load());
    Report += FString::Printf(TEXT("Max Execution Time: %.2f ms\n"), Stats.MaxExecutionTime.load());
    
    Report += TEXT("\nConfiguration:\n");
    Report += FString::Printf(TEXT("  Max Concurrent Callbacks: %d\n"), Config.MaxConcurrentCallbacks);
    Report += FString::Printf(TEXT("  Callback Timeout: %.2f seconds\n"), Config.CallbackTimeoutSeconds);
    Report += FString::Printf(TEXT("  Queue Size: %d\n"), Config.CallbackQueueSize);
    Report += FString::Printf(TEXT("  Statistics Enabled: %s\n"), Config.bEnableStatistics ? TEXT("Yes") : TEXT("No"));
    Report += FString::Printf(TEXT("  Timeout Enabled: %s\n"), Config.bEnableTimeout ? TEXT("Yes") : TEXT("No"));
    Report += FString::Printf(TEXT("  Slow Callback Threshold: %.2f ms\n"), Config.SlowCallbackThresholdMs);
    
    {
        FScopeLock ActiveLock(&ActiveCallbacksMutex);
        Report += FString::Printf(TEXT("  Active Callback IDs Count: %d\n"), ActiveCallbackIds.Num());
    }
    
    return Report;
}

bool FCSThreadSafeManagedCallbacks::IsSystemHealthy() const
{
    if (!bIsInitialized.load(std::memory_order_relaxed) || bIsShuttingDown.load(std::memory_order_relaxed))
    {
        return false;
    }
    
    // 检查成功率
    if (Stats.GetSuccessRate() < 0.95) // 成功率低于95%
    {
        return false;
    }
    
    // 检查当前活跃回调数量
    if (Stats.CurrentActiveCalls.load(std::memory_order_relaxed) >= Config.MaxConcurrentCallbacks)
    {
        return false;
    }
    
    // 检查超时回调比例
    int32 TotalCallbacks = Stats.TotalCallbacksExecuted.load(std::memory_order_relaxed);
    if (TotalCallbacks > 100) // 至少有100个回调样本
    {
        double TimeoutRatio = (double)Stats.TimeoutCallbacks.load(std::memory_order_relaxed) / TotalCallbacks;
        if (TimeoutRatio > 0.1) // 超时率超过10%
        {
            return false;
        }
    }
    
    return true;
}

void FCSThreadSafeManagedCallbacks::ForceTerminateAllCallbacks()
{
    UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Force terminating all active callbacks"));
    
    {
        FScopeLock ActiveLock(&ActiveCallbacksMutex);
        for (uint64 CallbackId : ActiveCallbackIds)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Forcibly terminated callback ID: %llu"), CallbackId);
        }
        ActiveCallbackIds.Empty();
    }
    
    // 重置活跃计数器
    Stats.CurrentActiveCalls.store(0, std::memory_order_release);
}

bool FCSThreadSafeManagedCallbacks::WaitForCallbackSlot(double TimeoutSeconds)
{
    std::unique_lock<std::mutex> Lock(CallbackMutex);
    
    auto StartTime = std::chrono::steady_clock::now();
    auto TimeoutTime = StartTime + std::chrono::duration<double>(TimeoutSeconds);
    
    return CallbackCondition.wait_until(Lock, TimeoutTime, [this]()
    {
        return Stats.CurrentActiveCalls.load(std::memory_order_relaxed) < Config.MaxConcurrentCallbacks ||
               bIsShuttingDown.load(std::memory_order_relaxed);
    });
}

bool FCSThreadSafeManagedCallbacks::CanAcceptNewCallback() const
{
    if (!bIsInitialized.load(std::memory_order_acquire) || bIsShuttingDown.load(std::memory_order_acquire))
    {
        return false;
    }
    
    return Stats.CurrentActiveCalls.load(std::memory_order_relaxed) < Config.MaxConcurrentCallbacks;
}

void FCSThreadSafeManagedCallbacks::RecordCallbackResult(ECallbackResult Result, double ExecutionTimeMs)
{
    if (Config.bEnableStatistics)
    {
        Stats.RecordExecution(Result, ExecutionTimeMs);
    }
    
    // 唤醒等待的线程
    if (Result != ECallbackResult::TooManyConcurrentCalls)
    {
        CallbackCondition.notify_one();
    }
}