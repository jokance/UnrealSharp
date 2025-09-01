#pragma once

#include "CoreMinimal.h"
#include "../CSManagedCallbacksCache.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>

/**
 * 线程安全的托管回调管理系统
 * 提供并发控制、超时机制和回调统计功能
 */
class UNREALSHARPCORE_API FCSThreadSafeManagedCallbacks
{
public:
    // 回调执行结果
    enum class ECallbackResult : uint8
    {
        Success,
        Failed,
        Timeout,
        TooManyConcurrentCalls,
        SystemNotReady
    };

    // 回调统计信息
    struct FCallbackStats
    {
        std::atomic<int32> TotalCallbacksExecuted{0};
        std::atomic<int32> SuccessfulCallbacks{0};
        std::atomic<int32> FailedCallbacks{0};
        std::atomic<int32> TimeoutCallbacks{0};
        std::atomic<int32> RejectedCallbacks{0};
        std::atomic<int32> CurrentActiveCalls{0};
        std::atomic<int32> MaxConcurrentCalls{0};
        std::atomic<double> AverageExecutionTime{0.0};
        std::atomic<double> MaxExecutionTime{0.0};
        
        void RecordExecution(ECallbackResult Result, double ExecutionTimeMs)
        {
            TotalCallbacksExecuted.fetch_add(1, std::memory_order_relaxed);
            
            switch (Result)
            {
                case ECallbackResult::Success:
                    SuccessfulCallbacks.fetch_add(1, std::memory_order_relaxed);
                    break;
                case ECallbackResult::Failed:
                    FailedCallbacks.fetch_add(1, std::memory_order_relaxed);
                    break;
                case ECallbackResult::Timeout:
                    TimeoutCallbacks.fetch_add(1, std::memory_order_relaxed);
                    break;
                case ECallbackResult::TooManyConcurrentCalls:
                    RejectedCallbacks.fetch_add(1, std::memory_order_relaxed);
                    break;
                default:
                    break;
            }
            
            // 更新执行时间统计
            if (Result == ECallbackResult::Success)
            {
                double CurrentAvg = AverageExecutionTime.load(std::memory_order_relaxed);
                double UpdatedAvg = (CurrentAvg * 0.9) + (ExecutionTimeMs * 0.1);
                AverageExecutionTime.store(UpdatedAvg, std::memory_order_relaxed);
                
                double CurrentMax = MaxExecutionTime.load(std::memory_order_relaxed);
                if (ExecutionTimeMs > CurrentMax)
                {
                    MaxExecutionTime.store(ExecutionTimeMs, std::memory_order_relaxed);
                }
            }
        }
        
        void RecordConcurrentCall(int32 ActiveCalls)
        {
            int32 CurrentMax = MaxConcurrentCalls.load(std::memory_order_relaxed);
            if (ActiveCalls > CurrentMax)
            {
                MaxConcurrentCalls.store(ActiveCalls, std::memory_order_relaxed);
            }
        }
        
        double GetSuccessRate() const
        {
            int32 Total = TotalCallbacksExecuted.load(std::memory_order_relaxed);
            if (Total == 0) return 0.0;
            return (double)SuccessfulCallbacks.load(std::memory_order_relaxed) / Total;
        }
    };

    // 并发控制配置
    struct FConcurrencyConfig
    {
        int32 MaxConcurrentCallbacks = 64;     // 最大并发回调数量
        double CallbackTimeoutSeconds = 30.0;  // 回调超时时间
        int32 CallbackQueueSize = 256;         // 等待队列大小
        bool bEnableStatistics = true;         // 启用统计功能
        bool bEnableTimeout = true;             // 启用超时检测
        bool bLogSlowCallbacks = true;          // 记录慢回调
        double SlowCallbackThresholdMs = 100.0; // 慢回调阈值
    };

private:
    // 回调控制状态
    mutable std::mutex CallbackMutex;
    mutable std::condition_variable CallbackCondition;
    std::atomic<bool> bIsInitialized{false};
    std::atomic<bool> bIsShuttingDown{false};
    
    // 统计和配置
    FCallbackStats Stats;
    FConcurrencyConfig Config;
    
    // 活跃回调跟踪
    TSet<uint64> ActiveCallbackIds;
    mutable FCriticalSection ActiveCallbacksMutex;
    std::atomic<uint64> NextCallbackId{1};

public:
    /**
     * 初始化回调管理系统
     */
    bool Initialize(const FConcurrencyConfig& InConfig = FConcurrencyConfig());

    /**
     * 关闭回调管理系统
     */
    void Shutdown();

    /**
     * 线程安全的托管对象创建
     */
    ECallbackResult SafeCreateNewManagedObject(const void* Object, void* TypeHandle, TCHAR** Error, FGCHandleIntPtr& OutResult);

    /**
     * 线程安全的托管对象包装器创建
     */
    ECallbackResult SafeCreateNewManagedObjectWrapper(void* Object, void* TypeHandle, FGCHandleIntPtr& OutResult);

    /**
     * 线程安全的托管事件调用
     */
    ECallbackResult SafeInvokeManagedEvent(void* EventPtr, void* Params, void* Result, void* Exception, void* World, int& OutResult);

    /**
     * 线程安全的委托调用
     */
    ECallbackResult SafeInvokeDelegate(FGCHandleIntPtr DelegateHandle, int& OutResult);

    /**
     * 线程安全的方法查找
     */
    ECallbackResult SafeLookupMethod(void* Assembly, const TCHAR* MethodName, uint8*& OutResult);

    /**
     * 线程安全的类型查找
     */
    ECallbackResult SafeLookupType(uint8* Assembly, const TCHAR* TypeName, uint8*& OutResult);

    /**
     * 线程安全的句柄释放
     */
    ECallbackResult SafeDispose(FGCHandleIntPtr Handle, FGCHandleIntPtr AssemblyHandle = FGCHandleIntPtr());

    /**
     * 线程安全的句柄释放
     */
    ECallbackResult SafeFreeHandle(FGCHandleIntPtr Handle);

    /**
     * 获取回调统计信息
     */
    const FCallbackStats& GetCallbackStatistics() const { return Stats; }

    /**
     * 获取配置信息
     */
    const FConcurrencyConfig& GetConfiguration() const { return Config; }

    /**
     * 更新配置
     */
    void UpdateConfiguration(const FConcurrencyConfig& NewConfig);

    /**
     * 重置统计信息
     */
    void ResetStatistics();

    /**
     * 导出诊断报告
     */
    FString ExportDiagnosticsReport();

    /**
     * 检查系统是否健康
     */
    bool IsSystemHealthy() const;

    /**
     * 获取当前活跃回调数量
     */
    int32 GetActiveCallbackCount() const { return Stats.CurrentActiveCalls.load(std::memory_order_relaxed); }

    /**
     * 强制终止所有活跃回调（紧急情况使用）
     */
    void ForceTerminateAllCallbacks();

private:
    /**
     * 活跃回调管理
     */
    class FScopedCallbackTracker
    {
        FCSThreadSafeManagedCallbacks& Manager;
        uint64 CallbackId;
        double StartTime;

    public:
        FScopedCallbackTracker(FCSThreadSafeManagedCallbacks& InManager)
            : Manager(InManager)
            , CallbackId(InManager.NextCallbackId.fetch_add(1, std::memory_order_relaxed))
            , StartTime(FPlatformTime::Seconds())
        {
            int32 ActiveCount = Manager.Stats.CurrentActiveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
            Manager.Stats.RecordConcurrentCall(ActiveCount);
            
            {
                FScopeLock Lock(&Manager.ActiveCallbacksMutex);
                Manager.ActiveCallbackIds.Add(CallbackId);
            }
        }

        ~FScopedCallbackTracker()
        {
            Manager.Stats.CurrentActiveCalls.fetch_sub(1, std::memory_order_relaxed);
            
            {
                FScopeLock Lock(&Manager.ActiveCallbacksMutex);
                Manager.ActiveCallbackIds.Remove(CallbackId);
            }
            
            double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            if (Manager.Config.bLogSlowCallbacks && ElapsedTime > Manager.Config.SlowCallbackThresholdMs)
            {
                UE_LOG(LogTemp, Warning, TEXT("CSThreadSafeManagedCallbacks: Slow callback detected (%.2fms, ID: %llu)"), 
                       ElapsedTime, CallbackId);
            }
        }

        double GetElapsedTimeMs() const
        {
            return (FPlatformTime::Seconds() - StartTime) * 1000.0;
        }

        uint64 GetCallbackId() const { return CallbackId; }
    };

    /**
     * 等待回调槽位可用
     */
    bool WaitForCallbackSlot(double TimeoutSeconds);

    /**
     * 执行带超时的回调操作
     */
    template<typename FunctionType>
    ECallbackResult ExecuteWithTimeout(FunctionType&& Function, double TimeoutSeconds);

    /**
     * 检查系统是否可以接受新回调
     */
    bool CanAcceptNewCallback() const;

    /**
     * 记录回调执行结果
     */
    void RecordCallbackResult(ECallbackResult Result, double ExecutionTimeMs);
};

/**
 * 全局线程安全托管回调管理器
 */
UNREALSHARPCORE_API FCSThreadSafeManagedCallbacks& GetGlobalThreadSafeManagedCallbacks();

/**
 * 线程安全回调助手宏
 */
#define SAFE_MANAGED_CALLBACK(CallbackName, ...) \
    GetGlobalThreadSafeManagedCallbacks().Safe##CallbackName(__VA_ARGS__)

/**
 * 线程安全回调执行包装器
 */
template<typename ReturnType, typename... Args>
class TThreadSafeCallbackWrapper
{
public:
    using CallbackFunction = ReturnType(*)(Args...);
    
private:
    CallbackFunction Callback;
    FCSThreadSafeManagedCallbacks& Manager;

public:
    TThreadSafeCallbackWrapper(CallbackFunction InCallback, FCSThreadSafeManagedCallbacks& InManager)
        : Callback(InCallback), Manager(InManager) {}

    ReturnType operator()(Args... Arguments)
    {
        if (!Manager.CanAcceptNewCallback())
        {
            UE_LOG(LogTemp, Error, TEXT("ThreadSafeCallbackWrapper: Cannot accept new callback"));
            return ReturnType{};
        }

        return Manager.ExecuteWithTimeout([&]() { return Callback(Arguments...); }, 
                                        Manager.GetConfiguration().CallbackTimeoutSeconds);
    }
};

/**
 * 创建线程安全回调包装器的助手函数
 */
template<typename ReturnType, typename... Args>
TThreadSafeCallbackWrapper<ReturnType, Args...> MakeThreadSafeCallback(ReturnType(*Callback)(Args...))
{
    return TThreadSafeCallbackWrapper<ReturnType, Args...>(Callback, GetGlobalThreadSafeManagedCallbacks());
}