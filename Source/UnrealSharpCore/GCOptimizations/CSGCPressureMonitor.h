#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Containers/Map.h"
#include "Misc/DateTime.h"
#include <atomic>

/**
 * GC压力监控系统
 * 主动监控和管理内存压力，防止GC性能问题
 */
class UNREALSHARPCORE_API FCSGCPressureMonitor
{
public:
    // GC压力等级
    enum class EGCPressureLevel : uint8
    {
        Low,        // 低压力 - 正常运行
        Moderate,   // 中等压力 - 需要注意
        High,       // 高压力 - 需要主动GC
        Critical    // 临界压力 - 紧急清理
    };

    // GC统计信息
    struct FGCStats
    {
        int32 StrongHandleCount = 0;
        int32 WeakHandleCount = 0;
        int32 PinnedHandleCount = 0;
        int32 OrphanedHandleCount = 0;
        double AverageObjectLifetime = 0.0;
        double MemoryPressureMB = 0.0;
        EGCPressureLevel PressureLevel = EGCPressureLevel::Low;
        FDateTime LastUpdateTime = FDateTime::UtcNow();
    };

private:
    // 监控配置常量
    static constexpr int32 GC_PRESSURE_THRESHOLD_LOW = 1000;
    static constexpr int32 GC_PRESSURE_THRESHOLD_MODERATE = 2500;  
    static constexpr int32 GC_PRESSURE_THRESHOLD_HIGH = 5000;
    static constexpr int32 GC_PRESSURE_THRESHOLD_CRITICAL = 10000;
    
    static constexpr double MONITORING_INTERVAL_SECONDS = 5.0;
    static constexpr double CLEANUP_INTERVAL_SECONDS = 30.0;

    // 静态监控数据
    static std::atomic<int32> TotalManagedObjects;
    static std::atomic<int32> StrongHandles;
    static std::atomic<int32> WeakHandles;
    static std::atomic<int32> PinnedHandles;
    static std::atomic<int32> OrphanedHandles;
    
    static FDateTime LastMonitoringTime;
    static FDateTime LastCleanupTime;
    static TMap<FString, int32> ObjectTypeCounters;
    static FCriticalSection CountersMutex;

    // 历史统计
    static TArray<FGCStats> StatsHistory;
    static constexpr int32 MAX_HISTORY_SIZE = 100;

public:
    /**
     * 初始化GC压力监控系统
     */
    static void Initialize();

    /**
     * 关闭监控系统
     */
    static void Shutdown();

    /**
     * 增加托管对象计数
     */
    static void IncrementManagedObject(const FString& ObjectType, GCHandleType HandleType);

    /**
     * 减少托管对象计数
     */
    static void DecrementManagedObject(const FString& ObjectType, GCHandleType HandleType);

    /**
     * 标记孤立句柄
     */
    static void MarkOrphanedHandle();

    /**
     * 执行定期监控检查
     * @return 当前压力等级
     */
    static EGCPressureLevel MonitorGCPressure();

    /**
     * 获取当前GC统计信息
     */
    static FGCStats GetCurrentGCStatistics();

    /**
     * 获取压力等级描述
     */
    static FString GetPressureLevelDescription(EGCPressureLevel Level);

    /**
     * 请求垃圾回收（根据压力等级）
     */
    static void RequestGarbageCollectionIfNeeded();

    /**
     * 强制执行垃圾回收
     */
    static void ForceGarbageCollection();

    /**
     * 清理孤立句柄
     */
    static int32 CleanupOrphanedHandles();

    /**
     * 获取建议的操作
     */
    static TArray<FString> GetRecommendedActions(EGCPressureLevel PressureLevel);

    /**
     * 导出诊断报告
     */
    static FString ExportDiagnosticsReport();

    /**
     * 获取历史统计数据
     */
    static const TArray<FGCStats>& GetStatsHistory() { return StatsHistory; }

    /**
     * 获取对象类型分布
     */
    static TMap<FString, int32> GetObjectTypeDistribution();

    /**
     * 验证句柄完整性
     */
    static void ValidateHandleIntegrity();

    /**
     * 报告可疑模式
     */
    static TArray<FString> ReportSuspiciousPatterns();

    /**
     * 定期维护任务（应由定时器调用）
     */
    static void PerformPeriodicMaintenance();

private:
    /**
     * 计算压力等级
     */
    static EGCPressureLevel CalculatePressureLevel(const FGCStats& Stats);

    /**
     * 更新统计历史
     */
    static void UpdateStatsHistory(const FGCStats& NewStats);

    /**
     * 执行清理操作
     */
    static void PerformCleanupOperations(EGCPressureLevel PressureLevel);

    /**
     * 记录性能指标
     */
    static void LogPerformanceMetrics(const FGCStats& Stats);
};

// 静态成员初始化
std::atomic<int32> FCSGCPressureMonitor::TotalManagedObjects{0};
std::atomic<int32> FCSGCPressureMonitor::StrongHandles{0};
std::atomic<int32> FCSGCPressureMonitor::WeakHandles{0};
std::atomic<int32> FCSGCPressureMonitor::PinnedHandles{0};
std::atomic<int32> FCSGCPressureMonitor::OrphanedHandles{0};

FDateTime FCSGCPressureMonitor::LastMonitoringTime = FDateTime::UtcNow();
FDateTime FCSGCPressureMonitor::LastCleanupTime = FDateTime::UtcNow();
TMap<FString, int32> FCSGCPressureMonitor::ObjectTypeCounters;
FCriticalSection FCSGCPressureMonitor::CountersMutex;
TArray<FCSGCPressureMonitor::FGCStats> FCSGCPressureMonitor::StatsHistory;