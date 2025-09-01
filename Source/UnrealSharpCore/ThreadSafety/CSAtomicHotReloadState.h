#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include <mutex>
#include <unordered_set>

/**
 * 原子化的热重载状态管理系统
 * 确保所有热重载状态变更的原子性和线程安全
 */
class UNREALSHARPCORE_API FCSAtomicHotReloadState
{
public:
    // 热重载状态枚举
    enum class EHotReloadState : int32
    {
        Idle = 0,           // 空闲状态
        Preparing,          // 准备中
        InProgress,         // 进行中
        Finalizing,         // 完成中
        Failed,             // 失败状态
        Cancelled           // 已取消
    };

    // 热重载类型
    enum class EHotReloadType : uint8
    {
        Full,               // 完整热重载
        Incremental,        // 增量热重载
        Assembly,           // 程序集热重载
        Method              // 方法热重载
    };

    // 平台特定状态
    enum class EPlatformHotReloadState : uint8
    {
        Ready,              // 准备就绪
        PlatformSpecific,   // 平台特定操作中
        DomainSwitching,    // 域切换中
        MethodReplacing     // 方法替换中
    };

    // 热重载统计信息
    struct FHotReloadStats
    {
        std::atomic<int32> TotalHotReloads{0};
        std::atomic<int32> SuccessfulHotReloads{0};
        std::atomic<int32> FailedHotReloads{0};
        std::atomic<int32> CancelledHotReloads{0};
        std::atomic<double> AverageHotReloadTime{0.0};
        std::atomic<double> MaxHotReloadTime{0.0};
        std::atomic<int32> ConcurrentHotReloadAttempts{0};
        std::atomic<int32> QueuedHotReloads{0};
        
        void RecordHotReload(bool bSuccess, double TimeMs)
        {
            TotalHotReloads.fetch_add(1, std::memory_order_relaxed);
            
            if (bSuccess)
            {
                SuccessfulHotReloads.fetch_add(1, std::memory_order_relaxed);
                
                // 更新平均时间
                double CurrentAvg = AverageHotReloadTime.load(std::memory_order_relaxed);
                double NewAvg = (CurrentAvg * 0.9) + (TimeMs * 0.1);
                AverageHotReloadTime.store(NewAvg, std::memory_order_relaxed);
                
                // 更新最大时间
                double CurrentMax = MaxHotReloadTime.load(std::memory_order_relaxed);
                if (TimeMs > CurrentMax)
                {
                    MaxHotReloadTime.store(TimeMs, std::memory_order_relaxed);
                }
            }
            else
            {
                FailedHotReloads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        double GetSuccessRate() const
        {
            int32 Total = TotalHotReloads.load(std::memory_order_relaxed);
            if (Total == 0) return 0.0;
            return (double)SuccessfulHotReloads.load(std::memory_order_relaxed) / Total;
        }
    };

private:
    // 原子状态变量
    std::atomic<EHotReloadState> CurrentState{EHotReloadState::Idle};
    std::atomic<EHotReloadType> CurrentType{EHotReloadType::Full};
    std::atomic<EPlatformHotReloadState> PlatformState{EPlatformHotReloadState::Ready};
    std::atomic<int32> ActiveHotReloads{0};
    std::atomic<int32> PendingHotReloads{0};
    std::atomic<uint64> CurrentHotReloadId{0};
    std::atomic<bool> bIsSystemReady{true};
    std::atomic<bool> bEmergencyStop{false};
    
    // 线程同步
    mutable std::mutex StateMutex;
    mutable FCriticalSection AssemblyMutex;
    
    // 活跃热重载跟踪
    std::unordered_set<uint64> ActiveHotReloadIds;
    mutable std::mutex ActiveIdsMutex;
    
    // 统计信息
    FHotReloadStats Stats;
    
    // 平台特定状态映射
    TMap<FString, MonoAssembly*> RegisteredAssemblies;
    TMap<FString, TArray<void*>> MethodReplacementMap;
    
    // 系统配置
    int32 MaxConcurrentHotReloads = 1;  // 大多数情况下热重载应该是串行的
    double HotReloadTimeoutSeconds = 60.0;
    bool bEnableHotReloadQueue = true;

public:
    /**
     * 初始化热重载状态管理
     */
    void Initialize();

    /**
     * 关闭热重载状态管理
     */
    void Shutdown();

    /**
     * 原子化开始热重载
     * @param Type 热重载类型
     * @param OutHotReloadId 输出热重载ID
     * @return 是否成功开始
     */
    bool AtomicBeginHotReload(EHotReloadType Type, uint64& OutHotReloadId);

    /**
     * 原子化结束热重载
     * @param HotReloadId 热重载ID
     * @param bSuccess 是否成功
     * @param ElapsedTimeMs 执行时间
     * @return 是否成功结束
     */
    bool AtomicEndHotReload(uint64 HotReloadId, bool bSuccess, double ElapsedTimeMs);

    /**
     * 原子化取消热重载
     * @param HotReloadId 热重载ID
     * @return 是否成功取消
     */
    bool AtomicCancelHotReload(uint64 HotReloadId);

    /**
     * 获取当前热重载状态
     */
    EHotReloadState GetCurrentState() const 
    { 
        return CurrentState.load(std::memory_order_acquire); 
    }

    /**
     * 获取当前热重载类型
     */
    EHotReloadType GetCurrentType() const 
    { 
        return CurrentType.load(std::memory_order_acquire); 
    }

    /**
     * 获取平台状态
     */
    EPlatformHotReloadState GetPlatformState() const 
    { 
        return PlatformState.load(std::memory_order_acquire); 
    }

    /**
     * 检查是否正在热重载
     */
    bool IsHotReloading() const 
    { 
        return GetCurrentState() != EHotReloadState::Idle; 
    }

    /**
     * 检查系统是否准备就绪
     */
    bool IsSystemReady() const 
    { 
        return bIsSystemReady.load(std::memory_order_acquire) && !bEmergencyStop.load(std::memory_order_acquire); 
    }

    /**
     * 获取活跃热重载数量
     */
    int32 GetActiveHotReloadCount() const 
    { 
        return ActiveHotReloads.load(std::memory_order_relaxed); 
    }

    /**
     * 获取等待热重载数量
     */
    int32 GetPendingHotReloadCount() const 
    { 
        return PendingHotReloads.load(std::memory_order_relaxed); 
    }

    /**
     * 等待热重载完成
     * @param TimeoutSeconds 超时时间
     * @return 是否在超时前完成
     */
    bool WaitForHotReloadCompletion(double TimeoutSeconds = 60.0);

    /**
     * 原子化设置平台状态
     * @param NewState 新的平台状态
     * @return 是否设置成功
     */
    bool AtomicSetPlatformState(EPlatformHotReloadState NewState);

    /**
     * 线程安全的程序集注册
     * @param AssemblyName 程序集名称
     * @param Assembly 程序集指针
     * @return 是否注册成功
     */
    bool RegisterAssemblyThreadSafe(const FString& AssemblyName, MonoAssembly* Assembly);

    /**
     * 线程安全的程序集注销
     * @param AssemblyName 程序集名称
     * @return 是否注销成功
     */
    bool UnregisterAssemblyThreadSafe(const FString& AssemblyName);

    /**
     * 线程安全获取程序集
     * @param AssemblyName 程序集名称
     * @return 程序集指针，未找到返回nullptr
     */
    MonoAssembly* GetAssemblyThreadSafe(const FString& AssemblyName) const;

    /**
     * 线程安全的方法替换映射
     * @param MethodName 方法名称
     * @param MethodPointers 方法指针数组
     * @return 是否映射成功
     */
    bool MapMethodReplacementThreadSafe(const FString& MethodName, const TArray<void*>& MethodPointers);

    /**
     * 获取热重载统计信息
     */
    const FHotReloadStats& GetStatistics() const { return Stats; }

    /**
     * 重置统计信息
     */
    void ResetStatistics();

    /**
     * 导出诊断报告
     */
    FString ExportDiagnosticsReport() const;

    /**
     * 紧急停止所有热重载操作
     */
    void EmergencyStopAllHotReloads();

    /**
     * 验证状态一致性
     * @return 发现的问题数量
     */
    int32 ValidateStateConsistency() const;

    /**
     * 获取状态描述字符串
     */
    static FString GetStateDescription(EHotReloadState State);
    static FString GetTypeDescription(EHotReloadType Type);
    static FString GetPlatformStateDescription(EPlatformHotReloadState State);

private:
    /**
     * 内部状态转换验证
     */
    bool ValidateStateTransition(EHotReloadState FromState, EHotReloadState ToState) const;

    /**
     * 清理过期的热重载ID
     */
    void CleanupExpiredHotReloadIds();

    /**
     * 生成新的热重载ID
     */
    uint64 GenerateNewHotReloadId();

    /**
     * RAII热重载追踪器
     */
    class FScopedHotReloadTracker
    {
        FCSAtomicHotReloadState& State;
        uint64 HotReloadId;
        double StartTime;
        bool bIsActive;

    public:
        FScopedHotReloadTracker(FCSAtomicHotReloadState& InState, uint64 InHotReloadId)
            : State(InState), HotReloadId(InHotReloadId), StartTime(FPlatformTime::Seconds()), bIsActive(true)
        {
            std::lock_guard<std::mutex> Lock(State.ActiveIdsMutex);
            State.ActiveHotReloadIds.insert(HotReloadId);
        }

        ~FScopedHotReloadTracker()
        {
            if (bIsActive)
            {
                double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                State.AtomicEndHotReload(HotReloadId, false, ElapsedTime); // 默认为失败，需要显式调用成功
                
                std::lock_guard<std::mutex> Lock(State.ActiveIdsMutex);
                State.ActiveHotReloadIds.erase(HotReloadId);
            }
        }

        void MarkSuccess()
        {
            if (bIsActive)
            {
                double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                State.AtomicEndHotReload(HotReloadId, true, ElapsedTime);
                bIsActive = false;
            }
        }

        void MarkFailed()
        {
            if (bIsActive)
            {
                double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                State.AtomicEndHotReload(HotReloadId, false, ElapsedTime);
                bIsActive = false;
            }
        }

        double GetElapsedTimeMs() const
        {
            return (FPlatformTime::Seconds() - StartTime) * 1000.0;
        }
    };
};

/**
 * 全局原子化热重载状态管理器
 */
UNREALSHARPCORE_API FCSAtomicHotReloadState& GetGlobalAtomicHotReloadState();

/**
 * 热重载操作助手宏
 */
#define ATOMIC_HOT_RELOAD_OPERATION(Type, Operation) \
    do { \
        uint64 HotReloadId; \
        if (GetGlobalAtomicHotReloadState().AtomicBeginHotReload(Type, HotReloadId)) { \
            FCSAtomicHotReloadState::FScopedHotReloadTracker Tracker(GetGlobalAtomicHotReloadState(), HotReloadId); \
            try { \
                Operation; \
                Tracker.MarkSuccess(); \
            } catch (...) { \
                Tracker.MarkFailed(); \
                throw; \
            } \
        } \
    } while(0)