#include "CSAtomicHotReloadState.h"
#include "Engine/Engine.h"

// 全局实例
static FCSAtomicHotReloadState GlobalAtomicHotReloadState;

FCSAtomicHotReloadState& GetGlobalAtomicHotReloadState()
{
    return GlobalAtomicHotReloadState;
}

void FCSAtomicHotReloadState::Initialize()
{
    std::lock_guard<std::mutex> Lock(StateMutex);
    
    // 重置所有状态
    CurrentState.store(EHotReloadState::Idle, std::memory_order_release);
    CurrentType.store(EHotReloadType::Full, std::memory_order_release);
    PlatformState.store(EPlatformHotReloadState::Ready, std::memory_order_release);
    ActiveHotReloads.store(0, std::memory_order_release);
    PendingHotReloads.store(0, std::memory_order_release);
    CurrentHotReloadId.store(0, std::memory_order_release);
    bIsSystemReady.store(true, std::memory_order_release);
    bEmergencyStop.store(false, std::memory_order_release);
    
    // 清空容器
    {
        std::lock_guard<std::mutex> ActiveLock(ActiveIdsMutex);
        ActiveHotReloadIds.clear();
    }
    
    {
        FScopeLock AssemblyLock(&AssemblyMutex);
        RegisteredAssemblies.Empty();
        MethodReplacementMap.Empty();
    }
    
    // 重置统计信息
    Stats = FHotReloadStats{};
    
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Initialized"));
}

void FCSAtomicHotReloadState::Shutdown()
{
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Shutting down"));
    
    // 标记紧急停止
    bEmergencyStop.store(true, std::memory_order_release);
    
    // 等待所有活跃热重载完成
    double StartTime = FPlatformTime::Seconds();
    const double MaxWaitTime = 30.0; // 30秒超时
    
    while (ActiveHotReloads.load(std::memory_order_relaxed) > 0 && 
           (FPlatformTime::Seconds() - StartTime) < MaxWaitTime)
    {
        FPlatformProcess::Sleep(0.1f);
    }
    
    int32 RemainingHotReloads = ActiveHotReloads.load(std::memory_order_relaxed);
    if (RemainingHotReloads > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: %d hot reloads still active during shutdown"), 
               RemainingHotReloads);
        EmergencyStopAllHotReloads();
    }
    
    // 导出最终统计信息
    FString FinalReport = ExportDiagnosticsReport();
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Final Statistics:\n%s"), *FinalReport);
    
    bIsSystemReady.store(false, std::memory_order_release);
}

bool FCSAtomicHotReloadState::AtomicBeginHotReload(EHotReloadType Type, uint64& OutHotReloadId)
{
    // 检查系统状态
    if (!IsSystemReady())
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: System not ready for hot reload"));
        return false;
    }
    
    std::lock_guard<std::mutex> Lock(StateMutex);
    
    EHotReloadState CurrentStateValue = CurrentState.load(std::memory_order_relaxed);
    
    // 检查是否可以开始新的热重载
    if (CurrentStateValue != EHotReloadState::Idle)
    {
        if (bEnableHotReloadQueue && PendingHotReloads.load(std::memory_order_relaxed) < 10)
        {
            PendingHotReloads.fetch_add(1, std::memory_order_relaxed);
            UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Hot reload queued"));
            return false; // 需要等待
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Cannot start hot reload, current state: %s"), 
                   *GetStateDescription(CurrentStateValue));
            return false;
        }
    }
    
    // 检查并发限制
    if (ActiveHotReloads.load(std::memory_order_relaxed) >= MaxConcurrentHotReloads)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Too many concurrent hot reloads"));
        return false;
    }
    
    // 原子化状态转换
    EHotReloadState ExpectedState = EHotReloadState::Idle;
    if (!CurrentState.compare_exchange_strong(ExpectedState, EHotReloadState::Preparing, std::memory_order_acq_rel))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: State changed during begin attempt"));
        return false;
    }
    
    // 设置类型和生成ID
    CurrentType.store(Type, std::memory_order_release);
    OutHotReloadId = GenerateNewHotReloadId();
    ActiveHotReloads.fetch_add(1, std::memory_order_relaxed);
    
    // 转换到进行中状态
    CurrentState.store(EHotReloadState::InProgress, std::memory_order_release);
    
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Started hot reload (Type: %s, ID: %llu)"), 
           *GetTypeDescription(Type), OutHotReloadId);
    
    return true;
}

bool FCSAtomicHotReloadState::AtomicEndHotReload(uint64 HotReloadId, bool bSuccess, double ElapsedTimeMs)
{
    std::lock_guard<std::mutex> Lock(StateMutex);
    
    EHotReloadState CurrentStateValue = CurrentState.load(std::memory_order_relaxed);
    
    if (CurrentStateValue != EHotReloadState::InProgress && 
        CurrentStateValue != EHotReloadState::Finalizing)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Invalid state for ending hot reload: %s"), 
               *GetStateDescription(CurrentStateValue));
        return false;
    }
    
    // 转换到完成状态
    CurrentState.store(EHotReloadState::Finalizing, std::memory_order_release);
    
    // 记录统计信息
    Stats.RecordHotReload(bSuccess, ElapsedTimeMs);
    
    // 减少活跃计数
    ActiveHotReloads.fetch_sub(1, std::memory_order_relaxed);
    
    // 处理等待队列
    if (PendingHotReloads.load(std::memory_order_relaxed) > 0)
    {
        PendingHotReloads.fetch_sub(1, std::memory_order_relaxed);
        UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Processing queued hot reload"));
    }
    
    // 返回空闲状态
    CurrentState.store(EHotReloadState::Idle, std::memory_order_release);
    
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Ended hot reload (ID: %llu, Success: %s, Time: %.2fms)"), 
           HotReloadId, bSuccess ? TEXT("Yes") : TEXT("No"), ElapsedTimeMs);
    
    return true;
}

bool FCSAtomicHotReloadState::AtomicCancelHotReload(uint64 HotReloadId)
{
    std::lock_guard<std::mutex> Lock(StateMutex);
    
    EHotReloadState CurrentStateValue = CurrentState.load(std::memory_order_relaxed);
    
    if (CurrentStateValue == EHotReloadState::Idle)
    {
        return true; // 已经完成或未开始
    }
    
    // 设置取消状态
    CurrentState.store(EHotReloadState::Cancelled, std::memory_order_release);
    
    // 更新统计
    Stats.CancelledHotReloads.fetch_add(1, std::memory_order_relaxed);
    
    // 减少活跃计数
    if (ActiveHotReloads.load(std::memory_order_relaxed) > 0)
    {
        ActiveHotReloads.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // 返回空闲状态
    CurrentState.store(EHotReloadState::Idle, std::memory_order_release);
    
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Cancelled hot reload (ID: %llu)"), HotReloadId);
    
    return true;
}

bool FCSAtomicHotReloadState::WaitForHotReloadCompletion(double TimeoutSeconds)
{
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + TimeoutSeconds;
    
    while (IsHotReloading() && FPlatformTime::Seconds() < EndTime)
    {
        FPlatformProcess::Sleep(0.01f); // 10ms
    }
    
    bool bCompleted = !IsHotReloading();
    if (!bCompleted)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Timeout waiting for hot reload completion"));
    }
    
    return bCompleted;
}

bool FCSAtomicHotReloadState::AtomicSetPlatformState(EPlatformHotReloadState NewState)
{
    EPlatformHotReloadState CurrentPlatformState = PlatformState.load(std::memory_order_relaxed);
    
    if (PlatformState.compare_exchange_strong(CurrentPlatformState, NewState, std::memory_order_acq_rel))
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("CSAtomicHotReloadState: Platform state changed: %s -> %s"), 
               *GetPlatformStateDescription(CurrentPlatformState), *GetPlatformStateDescription(NewState));
        return true;
    }
    
    return false;
}

bool FCSAtomicHotReloadState::RegisterAssemblyThreadSafe(const FString& AssemblyName, MonoAssembly* Assembly)
{
    if (!Assembly)
    {
        return false;
    }
    
    FScopeLock Lock(&AssemblyMutex);
    RegisteredAssemblies.Add(AssemblyName, Assembly);
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSAtomicHotReloadState: Registered assembly: %s"), *AssemblyName);
    return true;
}

bool FCSAtomicHotReloadState::UnregisterAssemblyThreadSafe(const FString& AssemblyName)
{
    FScopeLock Lock(&AssemblyMutex);
    bool bRemoved = RegisteredAssemblies.Remove(AssemblyName) > 0;
    
    if (bRemoved)
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("CSAtomicHotReloadState: Unregistered assembly: %s"), *AssemblyName);
    }
    
    return bRemoved;
}

MonoAssembly* FCSAtomicHotReloadState::GetAssemblyThreadSafe(const FString& AssemblyName) const
{
    FScopeLock Lock(&AssemblyMutex);
    
    if (MonoAssembly* const* Found = RegisteredAssemblies.Find(AssemblyName))
    {
        return *Found;
    }
    
    return nullptr;
}

bool FCSAtomicHotReloadState::MapMethodReplacementThreadSafe(const FString& MethodName, const TArray<void*>& MethodPointers)
{
    FScopeLock Lock(&AssemblyMutex);
    MethodReplacementMap.Add(MethodName, MethodPointers);
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSAtomicHotReloadState: Mapped method replacement: %s (%d pointers)"), 
           *MethodName, MethodPointers.Num());
    
    return true;
}

void FCSAtomicHotReloadState::ResetStatistics()
{
    Stats = FHotReloadStats{};
    UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Statistics reset"));
}

FString FCSAtomicHotReloadState::ExportDiagnosticsReport() const
{
    FString Report;
    
    Report += TEXT("=== Atomic Hot Reload State Diagnostics ===\n");
    Report += FString::Printf(TEXT("Current State: %s\n"), *GetStateDescription(GetCurrentState()));
    Report += FString::Printf(TEXT("Current Type: %s\n"), *GetTypeDescription(GetCurrentType()));
    Report += FString::Printf(TEXT("Platform State: %s\n"), *GetPlatformStateDescription(GetPlatformState()));
    Report += FString::Printf(TEXT("System Ready: %s\n"), IsSystemReady() ? TEXT("Yes") : TEXT("No"));
    Report += FString::Printf(TEXT("Emergency Stop: %s\n"), bEmergencyStop.load() ? TEXT("Yes") : TEXT("No"));
    Report += FString::Printf(TEXT("Active Hot Reloads: %d\n"), GetActiveHotReloadCount());
    Report += FString::Printf(TEXT("Pending Hot Reloads: %d\n"), GetPendingHotReloadCount());
    
    Report += TEXT("\nStatistics:\n");
    Report += FString::Printf(TEXT("  Total Hot Reloads: %d\n"), Stats.TotalHotReloads.load());
    Report += FString::Printf(TEXT("  Successful: %d\n"), Stats.SuccessfulHotReloads.load());
    Report += FString::Printf(TEXT("  Failed: %d\n"), Stats.FailedHotReloads.load());
    Report += FString::Printf(TEXT("  Cancelled: %d\n"), Stats.CancelledHotReloads.load());
    Report += FString::Printf(TEXT("  Success Rate: %.2f%%\n"), Stats.GetSuccessRate() * 100.0);
    Report += FString::Printf(TEXT("  Average Time: %.2f ms\n"), Stats.AverageHotReloadTime.load());
    Report += FString::Printf(TEXT("  Max Time: %.2f ms\n"), Stats.MaxHotReloadTime.load());
    
    {
        FScopeLock Lock(&AssemblyMutex);
        Report += FString::Printf(TEXT("  Registered Assemblies: %d\n"), RegisteredAssemblies.Num());
        Report += FString::Printf(TEXT("  Method Replacements: %d\n"), MethodReplacementMap.Num());
    }
    
    {
        std::lock_guard<std::mutex> ActiveLock(ActiveIdsMutex);
        Report += FString::Printf(TEXT("  Active Hot Reload IDs: %d\n"), (int32)ActiveHotReloadIds.size());
    }
    
    return Report;
}

void FCSAtomicHotReloadState::EmergencyStopAllHotReloads()
{
    UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Emergency stop triggered"));
    
    bEmergencyStop.store(true, std::memory_order_release);
    CurrentState.store(EHotReloadState::Cancelled, std::memory_order_release);
    ActiveHotReloads.store(0, std::memory_order_release);
    PendingHotReloads.store(0, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> ActiveLock(ActiveIdsMutex);
        int32 StoppedCount = (int32)ActiveHotReloadIds.size();
        ActiveHotReloadIds.clear();
        
        if (StoppedCount > 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSAtomicHotReloadState: Emergency stopped %d hot reload operations"), StoppedCount);
        }
    }
    
    // 一段时间后重置紧急停止标志
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
    {
        FPlatformProcess::Sleep(5.0f); // 等待5秒
        bEmergencyStop.store(false, std::memory_order_release);
        CurrentState.store(EHotReloadState::Idle, std::memory_order_release);
        UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Emergency stop cleared"));
    });
}

int32 FCSAtomicHotReloadState::ValidateStateConsistency() const
{
    int32 Issues = 0;
    
    // 检查状态一致性
    EHotReloadState State = GetCurrentState();
    int32 ActiveCount = GetActiveHotReloadCount();
    
    if (State == EHotReloadState::Idle && ActiveCount > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("CSAtomicHotReloadState: Inconsistent state - Idle with %d active reloads"), ActiveCount);
        Issues++;
    }
    
    if (State != EHotReloadState::Idle && ActiveCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("CSAtomicHotReloadState: Inconsistent state - %s with 0 active reloads"), 
               *GetStateDescription(State));
        Issues++;
    }
    
    // 检查活跃ID集合
    {
        std::lock_guard<std::mutex> ActiveLock(ActiveIdsMutex);
        if ((int32)ActiveHotReloadIds.size() != ActiveCount)
        {
            UE_LOG(LogTemp, Error, TEXT("CSAtomicHotReloadState: Active ID count mismatch - Set: %d, Counter: %d"), 
                   (int32)ActiveHotReloadIds.size(), ActiveCount);
            Issues++;
        }
    }
    
    if (Issues == 0)
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("CSAtomicHotReloadState: State consistency validated"));
    }
    
    return Issues;
}

FString FCSAtomicHotReloadState::GetStateDescription(EHotReloadState State)
{
    switch (State)
    {
        case EHotReloadState::Idle: return TEXT("Idle");
        case EHotReloadState::Preparing: return TEXT("Preparing");
        case EHotReloadState::InProgress: return TEXT("In Progress");
        case EHotReloadState::Finalizing: return TEXT("Finalizing");
        case EHotReloadState::Failed: return TEXT("Failed");
        case EHotReloadState::Cancelled: return TEXT("Cancelled");
        default: return TEXT("Unknown");
    }
}

FString FCSAtomicHotReloadState::GetTypeDescription(EHotReloadType Type)
{
    switch (Type)
    {
        case EHotReloadType::Full: return TEXT("Full");
        case EHotReloadType::Incremental: return TEXT("Incremental");
        case EHotReloadType::Assembly: return TEXT("Assembly");
        case EHotReloadType::Method: return TEXT("Method");
        default: return TEXT("Unknown");
    }
}

FString FCSAtomicHotReloadState::GetPlatformStateDescription(EPlatformHotReloadState State)
{
    switch (State)
    {
        case EPlatformHotReloadState::Ready: return TEXT("Ready");
        case EPlatformHotReloadState::PlatformSpecific: return TEXT("Platform Specific");
        case EPlatformHotReloadState::DomainSwitching: return TEXT("Domain Switching");
        case EPlatformHotReloadState::MethodReplacing: return TEXT("Method Replacing");
        default: return TEXT("Unknown");
    }
}

bool FCSAtomicHotReloadState::ValidateStateTransition(EHotReloadState FromState, EHotReloadState ToState) const
{
    // 定义有效的状态转换
    switch (FromState)
    {
        case EHotReloadState::Idle:
            return ToState == EHotReloadState::Preparing;
            
        case EHotReloadState::Preparing:
            return ToState == EHotReloadState::InProgress || ToState == EHotReloadState::Failed || ToState == EHotReloadState::Cancelled;
            
        case EHotReloadState::InProgress:
            return ToState == EHotReloadState::Finalizing || ToState == EHotReloadState::Failed || ToState == EHotReloadState::Cancelled;
            
        case EHotReloadState::Finalizing:
            return ToState == EHotReloadState::Idle || ToState == EHotReloadState::Failed;
            
        case EHotReloadState::Failed:
        case EHotReloadState::Cancelled:
            return ToState == EHotReloadState::Idle;
            
        default:
            return false;
    }
}

void FCSAtomicHotReloadState::CleanupExpiredHotReloadIds()
{
    std::lock_guard<std::mutex> ActiveLock(ActiveIdsMutex);
    
    // 简单清理：如果活跃ID集合过大，清理最旧的ID
    // 实际实现中可能需要时间戳跟踪
    if (ActiveHotReloadIds.size() > 100)
    {
        int32 ToRemove = (int32)ActiveHotReloadIds.size() - 50;
        auto it = ActiveHotReloadIds.begin();
        for (int32 i = 0; i < ToRemove && it != ActiveHotReloadIds.end(); ++i)
        {
            it = ActiveHotReloadIds.erase(it);
        }
        
        UE_LOG(LogTemp, Log, TEXT("CSAtomicHotReloadState: Cleaned up %d expired hot reload IDs"), ToRemove);
    }
}

uint64 FCSAtomicHotReloadState::GenerateNewHotReloadId()
{
    return CurrentHotReloadId.fetch_add(1, std::memory_order_relaxed) + 1;
}