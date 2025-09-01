#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Async/TaskGraphInterfaces.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <thread>

/**
 * 并发监控系统
 * 实时检测和报告线程安全违规行为
 */
class UNREALSHARPCORE_API FCSConcurrencyMonitor
{
public:
    // 违规类型枚举
    enum class EViolationType : uint8
    {
        RaceCondition,          // 竞态条件
        DeadlockPotential,      // 死锁潜在风险
        UnsafeConcurrentAccess, // 不安全并发访问
        ExcessiveLocking,       // 过度加锁
        LockOrderViolation,     // 加锁顺序违规
        ThreadUnsafeUsage,      // 线程不安全使用
        MemoryOrdering,         // 内存顺序问题
        ResourceLeak            // 资源泄露
    };

    // 线程访问模式
    enum class EAccessPattern : uint8
    {
        Read,                   // 读取
        Write,                  // 写入
        ReadWrite,              // 读写
        Atomic                  // 原子操作
    };

    // 违规严重程度
    enum class ESeverity : uint8
    {
        Info = 0,               // 信息
        Warning = 1,            // 警告
        Error = 2,              // 错误
        Critical = 3            // 严重
    };

    // 资源访问记录
    struct FResourceAccess
    {
        FString ResourceName;               // 资源名称
        uint32 ThreadId;                    // 线程ID
        EAccessPattern AccessPattern;       // 访问模式
        std::chrono::high_resolution_clock::time_point Timestamp; // 时间戳
        FString CallStack;                  // 调用栈
        void* ResourceAddress;              // 资源地址
        int32 AccessCount;                  // 访问计数
        
        FResourceAccess()
            : ThreadId(0)
            , AccessPattern(EAccessPattern::Read)
            , Timestamp(std::chrono::high_resolution_clock::now())
            , ResourceAddress(nullptr)
            , AccessCount(1)
        {}
    };

    // 违规报告
    struct FViolationReport
    {
        EViolationType Type;                // 违规类型
        ESeverity Severity;                 // 严重程度
        FString Description;                // 描述
        FString ResourceName;               // 涉及资源
        TArray<uint32> InvolvedThreads;     // 涉及线程
        std::chrono::high_resolution_clock::time_point DetectionTime; // 检测时间
        FString CallStack;                  // 相关调用栈
        TMap<FString, FString> AdditionalInfo; // 附加信息
        
        FViolationReport()
            : Type(EViolationType::RaceCondition)
            , Severity(ESeverity::Warning)
            , DetectionTime(std::chrono::high_resolution_clock::now())
        {}
    };

    // 监控配置
    struct FMonitoringConfig
    {
        bool bEnableRaceConditionDetection = true;     // 启用竞态条件检测
        bool bEnableDeadlockDetection = true;          // 启用死锁检测
        bool bEnableLockOrderValidation = true;        // 启用加锁顺序验证
        bool bEnableResourceTracking = true;           // 启用资源跟踪
        bool bEnablePerformanceMonitoring = true;      // 启用性能监控
        bool bEnableCallStackCapture = false;          // 启用调用栈捕获（性能影响较大）
        
        double DetectionIntervalSeconds = 1.0;         // 检测间隔
        int32 MaxViolationReports = 1000;              // 最大违规报告数
        int32 MaxResourceHistorySize = 10000;          // 最大资源历史大小
        double ResourceAccessTimeoutSeconds = 5.0;     // 资源访问超时
        ESeverity MinReportSeverity = ESeverity::Warning; // 最小报告严重程度
        
        bool bLogViolationsToConsole = true;           // 记录违规到控制台
        bool bLogViolationsToFile = false;             // 记录违规到文件
        FString LogFilePath = TEXT("Logs/ConcurrencyViolations.log"); // 日志文件路径
    };

    // 统计信息
    struct FMonitoringStats
    {
        std::atomic<int32> TotalViolationsDetected{0};         // 检测到的总违规数
        std::atomic<int32> RaceConditionViolations{0};         // 竞态条件违规数
        std::atomic<int32> DeadlockViolations{0};              // 死锁违规数
        std::atomic<int32> LockOrderViolations{0};             // 加锁顺序违规数
        std::atomic<int32> ResourceLeakViolations{0};          // 资源泄露违规数
        std::atomic<int32> ActiveResourceTracking{0};          // 活跃资源跟踪数
        std::atomic<int32> MonitoredThreads{0};                // 监控线程数
        std::atomic<double> AverageDetectionTimeMs{0.0};       // 平均检测时间
        std::atomic<double> MaxDetectionTimeMs{0.0};           // 最大检测时间
        
        void RecordViolation(EViolationType Type, double DetectionTimeMs)
        {
            TotalViolationsDetected.fetch_add(1, std::memory_order_relaxed);
            
            switch (Type)
            {
                case EViolationType::RaceCondition:
                    RaceConditionViolations.fetch_add(1, std::memory_order_relaxed);
                    break;
                case EViolationType::DeadlockPotential:
                    DeadlockViolations.fetch_add(1, std::memory_order_relaxed);
                    break;
                case EViolationType::LockOrderViolation:
                    LockOrderViolations.fetch_add(1, std::memory_order_relaxed);
                    break;
                case EViolationType::ResourceLeak:
                    ResourceLeakViolations.fetch_add(1, std::memory_order_relaxed);
                    break;
                default:
                    break;
            }
            
            // 更新检测时间统计
            double CurrentAvg = AverageDetectionTimeMs.load(std::memory_order_relaxed);
            double NewAvg = (CurrentAvg * 0.95) + (DetectionTimeMs * 0.05);
            AverageDetectionTimeMs.store(NewAvg, std::memory_order_relaxed);
            
            double CurrentMax = MaxDetectionTimeMs.load(std::memory_order_relaxed);
            if (DetectionTimeMs > CurrentMax)
            {
                MaxDetectionTimeMs.store(DetectionTimeMs, std::memory_order_relaxed);
            }
        }
    };

private:
    // 监控状态
    std::atomic<bool> bIsMonitoring{false};
    std::atomic<bool> bIsInitialized{false};
    std::atomic<bool> bShouldStop{false};
    
    // 配置和统计
    FMonitoringConfig Config;
    FMonitoringStats Stats;
    
    // 资源跟踪
    mutable std::mutex ResourcesMutex;
    std::unordered_map<void*, std::vector<FResourceAccess>> ResourceAccessHistory;
    std::unordered_map<FString, std::unordered_set<uint32>> ResourceThreadMap;
    
    // 线程跟踪
    mutable std::mutex ThreadsMutex;
    std::unordered_map<uint32, FString> ThreadNames;
    std::unordered_map<uint32, std::chrono::high_resolution_clock::time_point> ThreadLastActivity;
    
    // 锁顺序跟踪
    mutable std::mutex LockOrderMutex;
    std::unordered_map<uint32, std::vector<void*>> ThreadLockOrder;
    
    // 违规报告
    mutable std::mutex ViolationsMutex;
    std::vector<FViolationReport> ViolationReports;
    
    // 监控线程
    std::unique_ptr<std::thread> MonitoringThread;
    mutable FCriticalSection MonitoringMutex;

public:
    /**
     * 初始化并发监控系统
     */
    bool Initialize(const FMonitoringConfig& InConfig = FMonitoringConfig());

    /**
     * 启动监控
     */
    bool StartMonitoring();

    /**
     * 停止监控
     */
    void StopMonitoring();

    /**
     * 关闭监控系统
     */
    void Shutdown();

    /**
     * 记录资源访问
     */
    void RecordResourceAccess(void* Resource, const FString& ResourceName, EAccessPattern AccessPattern);

    /**
     * 记录锁获取
     */
    void RecordLockAcquisition(void* LockObject, const FString& LockName);

    /**
     * 记录锁释放
     */
    void RecordLockRelease(void* LockObject, const FString& LockName);

    /**
     * 记录线程信息
     */
    void RegisterThread(uint32 ThreadId, const FString& ThreadName);

    /**
     * 注销线程
     */
    void UnregisterThread(uint32 ThreadId);

    /**
     * 检测竞态条件
     */
    bool DetectRaceConditions();

    /**
     * 检测死锁潜在风险
     */
    bool DetectDeadlockPotential();

    /**
     * 验证加锁顺序
     */
    bool ValidateLockOrder();

    /**
     * 检测资源泄露
     */
    bool DetectResourceLeaks();

    /**
     * 获取监控统计
     */
    const FMonitoringStats& GetMonitoringStatistics() const { return Stats; }

    /**
     * 获取配置
     */
    const FMonitoringConfig& GetConfiguration() const { return Config; }

    /**
     * 更新配置
     */
    void UpdateConfiguration(const FMonitoringConfig& NewConfig);

    /**
     * 获取违规报告
     */
    TArray<FViolationReport> GetViolationReports(ESeverity MinSeverity = ESeverity::Warning) const;

    /**
     * 清除违规报告
     */
    void ClearViolationReports();

    /**
     * 导出诊断报告
     */
    FString ExportDiagnosticsReport() const;

    /**
     * 导出详细的违规报告
     */
    FString ExportViolationReport() const;

    /**
     * 检查系统健康状态
     */
    bool IsSystemHealthy() const;

    /**
     * 获取当前监控状态
     */
    bool IsMonitoring() const { return bIsMonitoring.load(std::memory_order_relaxed); }

private:
    /**
     * 监控线程主循环
     */
    void MonitoringThreadLoop();

    /**
     * 运行检测周期
     */
    void RunDetectionCycle();

    /**
     * 分析资源访问模式
     */
    bool AnalyzeResourceAccessPatterns(void* Resource, const std::vector<FResourceAccess>& AccessHistory);

    /**
     * 报告违规
     */
    void ReportViolation(const FViolationReport& Report);

    /**
     * 记录违规到日志
     */
    void LogViolation(const FViolationReport& Report);

    /**
     * 清理过期数据
     */
    void CleanupExpiredData();

    /**
     * 获取当前调用栈（如果启用）
     */
    FString GetCurrentCallStack() const;

    /**
     * 检查两个线程是否可能发生死锁
     */
    bool CheckDeadlockBetweenThreads(uint32 Thread1, uint32 Thread2);

    /**
     * 获取违规类型描述
     */
    static FString GetViolationTypeDescription(EViolationType Type);

    /**
     * 获取严重程度描述
     */
    static FString GetSeverityDescription(ESeverity Severity);

    /**
     * RAII资源访问追踪器
     */
    class FScopedResourceTracker
    {
        FCSConcurrencyMonitor& Monitor;
        void* Resource;
        FString ResourceName;
        EAccessPattern AccessPattern;

    public:
        FScopedResourceTracker(FCSConcurrencyMonitor& InMonitor, void* InResource, const FString& InResourceName, EAccessPattern InAccessPattern)
            : Monitor(InMonitor), Resource(InResource), ResourceName(InResourceName), AccessPattern(InAccessPattern)
        {
            if (Monitor.IsMonitoring())
            {
                Monitor.RecordResourceAccess(Resource, ResourceName, AccessPattern);
            }
        }

        ~FScopedResourceTracker()
        {
            // 访问结束时的清理可以在这里进行
        }
    };
};

/**
 * 全局并发监控器
 */
UNREALSHARPCORE_API FCSConcurrencyMonitor& GetGlobalConcurrencyMonitor();

/**
 * 监控助手宏
 */
#define MONITOR_RESOURCE_ACCESS(Resource, Name, Pattern) \
    FCSConcurrencyMonitor::FScopedResourceTracker ANONYMOUS_VARIABLE(ResourceTracker)(GetGlobalConcurrencyMonitor(), Resource, Name, Pattern)

#define MONITOR_LOCK_ACQUISITION(LockObject, LockName) \
    do { \
        if (GetGlobalConcurrencyMonitor().IsMonitoring()) \
        { \
            GetGlobalConcurrencyMonitor().RecordLockAcquisition(LockObject, LockName); \
        } \
    } while(0)

#define MONITOR_LOCK_RELEASE(LockObject, LockName) \
    do { \
        if (GetGlobalConcurrencyMonitor().IsMonitoring()) \
        { \
            GetGlobalConcurrencyMonitor().RecordLockRelease(LockObject, LockName); \
        } \
    } while(0)

/**
 * 线程安全监控包装器模板
 */
template<typename T>
class TMonitoredResource
{
private:
    mutable T Resource;
    FString ResourceName;
    mutable FCSConcurrencyMonitor* Monitor;

public:
    TMonitoredResource(const T& InResource, const FString& InName)
        : Resource(InResource), ResourceName(InName), Monitor(&GetGlobalConcurrencyMonitor()) {}

    // 读取访问
    const T& Get() const
    {
        if (Monitor->IsMonitoring())
        {
            Monitor->RecordResourceAccess((void*)&Resource, ResourceName, FCSConcurrencyMonitor::EAccessPattern::Read);
        }
        return Resource;
    }

    // 写入访问
    T& GetMutable()
    {
        if (Monitor->IsMonitoring())
        {
            Monitor->RecordResourceAccess((void*)&Resource, ResourceName, FCSConcurrencyMonitor::EAccessPattern::Write);
        }
        return Resource;
    }

    // 原子访问（读写）
    T& GetAtomic()
    {
        if (Monitor->IsMonitoring())
        {
            Monitor->RecordResourceAccess((void*)&Resource, ResourceName, FCSConcurrencyMonitor::EAccessPattern::ReadWrite);
        }
        return Resource;
    }

    // 赋值操作
    TMonitoredResource& operator=(const T& Other)
    {
        if (Monitor->IsMonitoring())
        {
            Monitor->RecordResourceAccess((void*)&Resource, ResourceName, FCSConcurrencyMonitor::EAccessPattern::Write);
        }
        Resource = Other;
        return *this;
    }

    // 比较操作
    bool operator==(const T& Other) const
    {
        if (Monitor->IsMonitoring())
        {
            Monitor->RecordResourceAccess((void*)&Resource, ResourceName, FCSConcurrencyMonitor::EAccessPattern::Read);
        }
        return Resource == Other;
    }

    // 转换操作
    operator const T&() const
    {
        return Get();
    }
};

/**
 * 创建监控资源的助手函数
 */
template<typename T>
TMonitoredResource<T> MakeMonitoredResource(const T& Resource, const FString& Name)
{
    return TMonitoredResource<T>(Resource, Name);
}