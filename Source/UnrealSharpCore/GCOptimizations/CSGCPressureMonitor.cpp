#include "CSGCPressureMonitor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMemory.h"
#include "Stats/Stats.h"

void FCSGCPressureMonitor::Initialize()
{
    UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Initializing GC pressure monitoring system"));
    
    // 重置所有计数器
    TotalManagedObjects.store(0);
    StrongHandles.store(0);
    WeakHandles.store(0);
    PinnedHandles.store(0);
    OrphanedHandles.store(0);
    
    // 清理历史数据
    {
        FScopeLock Lock(&CountersMutex);
        ObjectTypeCounters.Empty();
        StatsHistory.Empty();
    }
    
    LastMonitoringTime = FDateTime::UtcNow();
    LastCleanupTime = FDateTime::UtcNow();
    
    UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Initialization completed"));
}

void FCSGCPressureMonitor::Shutdown()
{
    UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Shutting down GC pressure monitoring system"));
    
    // 导出最终报告
    FString FinalReport = ExportDiagnosticsReport();
    UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Final Report:\n%s"), *FinalReport);
    
    // 清理资源
    {
        FScopeLock Lock(&CountersMutex);
        ObjectTypeCounters.Empty();
        StatsHistory.Empty();
    }
}

void FCSGCPressureMonitor::IncrementManagedObject(const FString& ObjectType, GCHandleType HandleType)
{
    TotalManagedObjects.fetch_add(1, std::memory_order_relaxed);
    
    switch (HandleType)
    {
        case GCHandleType::StrongHandle:
            StrongHandles.fetch_add(1, std::memory_order_relaxed);
            break;
        case GCHandleType::WeakHandle:
            WeakHandles.fetch_add(1, std::memory_order_relaxed);
            break;
        case GCHandleType::PinnedHandle:
            PinnedHandles.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }
    
    // 更新对象类型计数
    {
        FScopeLock Lock(&CountersMutex);
        ObjectTypeCounters.FindOrAdd(ObjectType)++;
    }
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSGCPressureMonitor: Object created - Type: %s, Handle: %d, Total: %d"), 
           *ObjectType, (int32)HandleType, TotalManagedObjects.load());
}

void FCSGCPressureMonitor::DecrementManagedObject(const FString& ObjectType, GCHandleType HandleType)
{
    TotalManagedObjects.fetch_sub(1, std::memory_order_relaxed);
    
    switch (HandleType)
    {
        case GCHandleType::StrongHandle:
            StrongHandles.fetch_sub(1, std::memory_order_relaxed);
            break;
        case GCHandleType::WeakHandle:
            WeakHandles.fetch_sub(1, std::memory_order_relaxed);
            break;
        case GCHandleType::PinnedHandle:
            PinnedHandles.fetch_sub(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }
    
    // 更新对象类型计数
    {
        FScopeLock Lock(&CountersMutex);
        if (int32* Count = ObjectTypeCounters.Find(ObjectType))
        {
            (*Count)--;
            if (*Count <= 0)
            {
                ObjectTypeCounters.Remove(ObjectType);
            }
        }
    }
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSGCPressureMonitor: Object destroyed - Type: %s, Handle: %d, Total: %d"), 
           *ObjectType, (int32)HandleType, TotalManagedObjects.load());
}

void FCSGCPressureMonitor::MarkOrphanedHandle()
{
    OrphanedHandles.fetch_add(1, std::memory_order_relaxed);
    UE_LOG(LogTemp, Warning, TEXT("CSGCPressureMonitor: Orphaned handle detected, Total: %d"), 
           OrphanedHandles.load());
}

FCSGCPressureMonitor::EGCPressureLevel FCSGCPressureMonitor::MonitorGCPressure()
{
    FGCStats CurrentStats = GetCurrentGCStatistics();
    EGCPressureLevel PressureLevel = CalculatePressureLevel(CurrentStats);
    
    // 更新统计历史
    UpdateStatsHistory(CurrentStats);
    
    // 根据压力等级执行相应操作
    PerformCleanupOperations(PressureLevel);
    
    // 记录性能指标
    LogPerformanceMetrics(CurrentStats);
    
    LastMonitoringTime = FDateTime::UtcNow();
    
    return PressureLevel;
}

FCSGCPressureMonitor::FGCStats FCSGCPressureMonitor::GetCurrentGCStatistics()
{
    FGCStats Stats;
    
    Stats.StrongHandleCount = StrongHandles.load(std::memory_order_relaxed);
    Stats.WeakHandleCount = WeakHandles.load(std::memory_order_relaxed);
    Stats.PinnedHandleCount = PinnedHandles.load(std::memory_order_relaxed);
    Stats.OrphanedHandleCount = OrphanedHandles.load(std::memory_order_relaxed);
    Stats.LastUpdateTime = FDateTime::UtcNow();
    
    // 计算内存压力
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
    Stats.MemoryPressureMB = (MemStats.UsedPhysical - MemStats.AvailablePhysical) / (1024.0 * 1024.0);
    
    // 计算平均对象生命周期（简化版）
    int32 TotalObjects = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    if (StatsHistory.Num() > 1 && TotalObjects > 0)
    {
        const FGCStats& PrevStats = StatsHistory.Last();
        double TimeDiff = (Stats.LastUpdateTime - PrevStats.LastUpdateTime).GetTotalSeconds();
        int32 ObjectDiff = TotalObjects - (PrevStats.StrongHandleCount + PrevStats.WeakHandleCount + PrevStats.PinnedHandleCount);
        
        if (ObjectDiff != 0)
        {
            Stats.AverageObjectLifetime = TimeDiff / FMath::Abs(ObjectDiff);
        }
    }
    
    Stats.PressureLevel = CalculatePressureLevel(Stats);
    
    return Stats;
}

FString FCSGCPressureMonitor::GetPressureLevelDescription(EGCPressureLevel Level)
{
    switch (Level)
    {
        case EGCPressureLevel::Low: return TEXT("Low - Normal operation");
        case EGCPressureLevel::Moderate: return TEXT("Moderate - Monitor closely");
        case EGCPressureLevel::High: return TEXT("High - Consider cleanup");
        case EGCPressureLevel::Critical: return TEXT("Critical - Immediate action required");
        default: return TEXT("Unknown");
    }
}

void FCSGCPressureMonitor::RequestGarbageCollectionIfNeeded()
{
    EGCPressureLevel CurrentLevel = MonitorGCPressure();
    
    switch (CurrentLevel)
    {
        case EGCPressureLevel::High:
            UE_LOG(LogTemp, Warning, TEXT("CSGCPressureMonitor: High GC pressure detected, requesting garbage collection"));
            if (GEngine)
            {
                GEngine->ForceGarbageCollection(true);
            }
            break;
            
        case EGCPressureLevel::Critical:
            UE_LOG(LogTemp, Error, TEXT("CSGCPressureMonitor: Critical GC pressure detected, forcing immediate cleanup"));
            ForceGarbageCollection();
            CleanupOrphanedHandles();
            break;
            
        default:
            break;
    }
}

void FCSGCPressureMonitor::ForceGarbageCollection()
{
    UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Forcing garbage collection"));
    
    if (GEngine)
    {
        GEngine->ForceGarbageCollection(true);
    }
    
    // 调用.NET垃圾回收（如果可用）
    // 这里需要通过托管回调实现
    // FCSManagedCallbacks::ManagedCallbacks.ForceGarbageCollection();
}

int32 FCSGCPressureMonitor::CleanupOrphanedHandles()
{
    int32 CleanedCount = OrphanedHandles.exchange(0, std::memory_order_relaxed);
    
    if (CleanedCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("CSGCPressureMonitor: Cleaned up %d orphaned handles"), CleanedCount);
    }
    
    return CleanedCount;
}

TArray<FString> FCSGCPressureMonitor::GetRecommendedActions(EGCPressureLevel PressureLevel)
{
    TArray<FString> Actions;
    
    switch (PressureLevel)
    {
        case EGCPressureLevel::Moderate:
            Actions.Add(TEXT("Monitor object creation patterns"));
            Actions.Add(TEXT("Review strong handle usage"));
            break;
            
        case EGCPressureLevel::High:
            Actions.Add(TEXT("Execute garbage collection"));
            Actions.Add(TEXT("Convert strong handles to weak handles where appropriate"));
            Actions.Add(TEXT("Review object lifetime management"));
            break;
            
        case EGCPressureLevel::Critical:
            Actions.Add(TEXT("Emergency garbage collection"));
            Actions.Add(TEXT("Clean up orphaned handles"));
            Actions.Add(TEXT("Audit memory usage patterns"));
            Actions.Add(TEXT("Consider reducing object creation rate"));
            break;
            
        default:
            Actions.Add(TEXT("Continue normal operation"));
            break;
    }
    
    return Actions;
}

FString FCSGCPressureMonitor::ExportDiagnosticsReport()
{
    FString Report;
    FGCStats CurrentStats = GetCurrentGCStatistics();
    
    Report += TEXT("=== UnrealSharp GC Pressure Monitor Report ===\n");
    Report += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::UtcNow().ToString());
    Report += TEXT("\n--- Current Statistics ---\n");
    Report += FString::Printf(TEXT("Strong Handles: %d\n"), CurrentStats.StrongHandleCount);
    Report += FString::Printf(TEXT("Weak Handles: %d\n"), CurrentStats.WeakHandleCount);
    Report += FString::Printf(TEXT("Pinned Handles: %d\n"), CurrentStats.PinnedHandleCount);
    Report += FString::Printf(TEXT("Orphaned Handles: %d\n"), CurrentStats.OrphanedHandleCount);
    Report += FString::Printf(TEXT("Memory Pressure: %.2f MB\n"), CurrentStats.MemoryPressureMB);
    Report += FString::Printf(TEXT("Pressure Level: %s\n"), *GetPressureLevelDescription(CurrentStats.PressureLevel));
    
    Report += TEXT("\n--- Object Type Distribution ---\n");
    {
        FScopeLock Lock(&CountersMutex);
        for (const auto& Pair : ObjectTypeCounters)
        {
            Report += FString::Printf(TEXT("%s: %d\n"), *Pair.Key, Pair.Value);
        }
    }
    
    Report += TEXT("\n--- Recommended Actions ---\n");
    TArray<FString> Actions = GetRecommendedActions(CurrentStats.PressureLevel);
    for (const FString& Action : Actions)
    {
        Report += FString::Printf(TEXT("- %s\n"), *Action);
    }
    
    Report += TEXT("\n--- Suspicious Patterns ---\n");
    TArray<FString> SuspiciousPatterns = ReportSuspiciousPatterns();
    for (const FString& Pattern : SuspiciousPatterns)
    {
        Report += FString::Printf(TEXT("WARNING: %s\n"), *Pattern);
    }
    
    return Report;
}

TMap<FString, int32> FCSGCPressureMonitor::GetObjectTypeDistribution()
{
    FScopeLock Lock(&CountersMutex);
    return ObjectTypeCounters;
}

void FCSGCPressureMonitor::ValidateHandleIntegrity()
{
    FGCStats Stats = GetCurrentGCStatistics();
    
    // 检查是否有异常的句柄数量
    int32 TotalHandles = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    
    if (Stats.OrphanedHandleCount > TotalHandles * 0.1) // 孤立句柄超过10%
    {
        UE_LOG(LogTemp, Error, TEXT("CSGCPressureMonitor: High orphaned handle ratio detected: %d/%d"), 
               Stats.OrphanedHandleCount, TotalHandles);
    }
    
    if (Stats.StrongHandleCount > TotalHandles * 0.8) // 强引用超过80%
    {
        UE_LOG(LogTemp, Warning, TEXT("CSGCPressureMonitor: Very high strong handle ratio: %d/%d"), 
               Stats.StrongHandleCount, TotalHandles);
    }
}

TArray<FString> FCSGCPressureMonitor::ReportSuspiciousPatterns()
{
    TArray<FString> Patterns;
    FGCStats Stats = GetCurrentGCStatistics();
    
    // 检查各种可疑模式
    int32 TotalHandles = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    
    if (Stats.OrphanedHandleCount > 100)
    {
        Patterns.Add(FString::Printf(TEXT("High number of orphaned handles: %d"), Stats.OrphanedHandleCount));
    }
    
    if (TotalHandles > GC_PRESSURE_THRESHOLD_HIGH)
    {
        Patterns.Add(FString::Printf(TEXT("Total handle count exceeds threshold: %d > %d"), TotalHandles, GC_PRESSURE_THRESHOLD_HIGH));
    }
    
    if (Stats.StrongHandleCount > Stats.WeakHandleCount * 2)
    {
        Patterns.Add(TEXT("Strong handles significantly outnumber weak handles"));
    }
    
    // 检查对象类型分布异常
    {
        FScopeLock Lock(&CountersMutex);
        for (const auto& Pair : ObjectTypeCounters)
        {
            if (Pair.Value > 1000) // 单一类型对象过多
            {
                Patterns.Add(FString::Printf(TEXT("High count for object type '%s': %d"), *Pair.Key, Pair.Value));
            }
        }
    }
    
    return Patterns;
}

void FCSGCPressureMonitor::PerformPeriodicMaintenance()
{
    FDateTime Now = FDateTime::UtcNow();
    
    // 定期监控
    if ((Now - LastMonitoringTime).GetTotalSeconds() >= MONITORING_INTERVAL_SECONDS)
    {
        MonitorGCPressure();
    }
    
    // 定期清理
    if ((Now - LastCleanupTime).GetTotalSeconds() >= CLEANUP_INTERVAL_SECONDS)
    {
        ValidateHandleIntegrity();
        CleanupOrphanedHandles();
        LastCleanupTime = Now;
    }
}

FCSGCPressureMonitor::EGCPressureLevel FCSGCPressureMonitor::CalculatePressureLevel(const FGCStats& Stats)
{
    int32 TotalHandles = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    
    if (TotalHandles >= GC_PRESSURE_THRESHOLD_CRITICAL || Stats.OrphanedHandleCount >= 1000)
    {
        return EGCPressureLevel::Critical;
    }
    else if (TotalHandles >= GC_PRESSURE_THRESHOLD_HIGH || Stats.OrphanedHandleCount >= 500)
    {
        return EGCPressureLevel::High;
    }
    else if (TotalHandles >= GC_PRESSURE_THRESHOLD_MODERATE || Stats.OrphanedHandleCount >= 100)
    {
        return EGCPressureLevel::Moderate;
    }
    else
    {
        return EGCPressureLevel::Low;
    }
}

void FCSGCPressureMonitor::UpdateStatsHistory(const FGCStats& NewStats)
{
    FScopeLock Lock(&CountersMutex);
    
    StatsHistory.Add(NewStats);
    
    if (StatsHistory.Num() > MAX_HISTORY_SIZE)
    {
        StatsHistory.RemoveAt(0);
    }
}

void FCSGCPressureMonitor::PerformCleanupOperations(EGCPressureLevel PressureLevel)
{
    switch (PressureLevel)
    {
        case EGCPressureLevel::High:
            // 执行常规清理
            CleanupOrphanedHandles();
            break;
            
        case EGCPressureLevel::Critical:
            // 执行紧急清理
            CleanupOrphanedHandles();
            ForceGarbageCollection();
            break;
            
        default:
            break;
    }
}

void FCSGCPressureMonitor::LogPerformanceMetrics(const FGCStats& Stats)
{
    UE_LOG(LogTemp, Verbose, TEXT("CSGCPressureMonitor: Stats - Strong:%d, Weak:%d, Pinned:%d, Orphaned:%d, Pressure:%s"), 
           Stats.StrongHandleCount, Stats.WeakHandleCount, Stats.PinnedHandleCount, Stats.OrphanedHandleCount,
           *GetPressureLevelDescription(Stats.PressureLevel));
}