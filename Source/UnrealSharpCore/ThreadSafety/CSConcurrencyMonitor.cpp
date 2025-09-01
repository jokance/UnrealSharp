#include "CSConcurrencyMonitor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"

// 全局实例
static FCSConcurrencyMonitor GlobalConcurrencyMonitor;

FCSConcurrencyMonitor& GetGlobalConcurrencyMonitor()
{
    return GlobalConcurrencyMonitor;
}

bool FCSConcurrencyMonitor::Initialize(const FMonitoringConfig& InConfig)
{
    if (bIsInitialized.load(std::memory_order_relaxed))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSConcurrencyMonitor: Already initialized"));
        return false;
    }

    Config = InConfig;
    
    // 重置所有状态
    bIsMonitoring.store(false, std::memory_order_release);
    bShouldStop.store(false, std::memory_order_release);
    
    // 清空数据结构
    {
        std::lock_guard<std::mutex> ResourceLock(ResourcesMutex);
        ResourceAccessHistory.clear();
        ResourceThreadMap.clear();
    }
    
    {
        std::lock_guard<std::mutex> ThreadLock(ThreadsMutex);
        ThreadNames.clear();
        ThreadLastActivity.clear();
    }
    
    {
        std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
        ThreadLockOrder.clear();
    }
    
    {
        std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
        ViolationReports.clear();
    }
    
    // 重置统计信息
    Stats = FMonitoringStats{};
    
    // 注册主线程
    RegisterThread(FPlatformTLS::GetCurrentThreadId(), TEXT("MainThread"));
    
    bIsInitialized.store(true, std::memory_order_release);
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Initialized with detection interval %.2fs"), Config.DetectionIntervalSeconds);
    
    return true;
}

bool FCSConcurrencyMonitor::StartMonitoring()
{
    if (!bIsInitialized.load(std::memory_order_relaxed))
    {
        UE_LOG(LogTemp, Error, TEXT("CSConcurrencyMonitor: Not initialized"));
        return false;
    }
    
    if (bIsMonitoring.load(std::memory_order_relaxed))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSConcurrencyMonitor: Already monitoring"));
        return true;
    }
    
    bShouldStop.store(false, std::memory_order_release);
    bIsMonitoring.store(true, std::memory_order_release);
    
    // 启动监控线程
    MonitoringThread = std::make_unique<std::thread>(&FCSConcurrencyMonitor::MonitoringThreadLoop, this);
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Monitoring started"));
    
    return true;
}

void FCSConcurrencyMonitor::StopMonitoring()
{
    if (!bIsMonitoring.load(std::memory_order_relaxed))
    {
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Stopping monitoring"));
    
    bShouldStop.store(true, std::memory_order_release);
    bIsMonitoring.store(false, std::memory_order_release);
    
    // 等待监控线程结束
    if (MonitoringThread && MonitoringThread->joinable())
    {
        MonitoringThread->join();
        MonitoringThread.reset();
    }
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Monitoring stopped"));
}

void FCSConcurrencyMonitor::Shutdown()
{
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Shutting down"));
    
    StopMonitoring();
    
    // 导出最终报告
    FString FinalReport = ExportDiagnosticsReport();
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Final Report:\n%s"), *FinalReport);
    
    // 如果配置了文件日志，导出违规报告
    if (Config.bLogViolationsToFile)
    {
        FString ViolationReport = ExportViolationReport();
        FFileHelper::SaveStringToFile(ViolationReport, *Config.LogFilePath);
    }
    
    bIsInitialized.store(false, std::memory_order_release);
}

void FCSConcurrencyMonitor::RecordResourceAccess(void* Resource, const FString& ResourceName, EAccessPattern AccessPattern)
{
    if (!bIsMonitoring.load(std::memory_order_relaxed) || !Resource)
    {
        return;
    }
    
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    auto Now = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> Lock(ResourcesMutex);
    
    // 创建访问记录
    FResourceAccess Access;
    Access.ResourceName = ResourceName;
    Access.ThreadId = CurrentThreadId;
    Access.AccessPattern = AccessPattern;
    Access.Timestamp = Now;
    Access.ResourceAddress = Resource;
    Access.CallStack = Config.bEnableCallStackCapture ? GetCurrentCallStack() : TEXT("");
    
    // 检查是否已有该资源的记录
    auto& AccessHistory = ResourceAccessHistory[Resource];
    
    // 检查最近的访问记录，如果是同一线程的连续访问，更新计数
    if (!AccessHistory.empty())
    {
        auto& LastAccess = AccessHistory.back();
        if (LastAccess.ThreadId == CurrentThreadId && 
            LastAccess.AccessPattern == AccessPattern &&
            std::chrono::duration_cast<std::chrono::milliseconds>(Now - LastAccess.Timestamp).count() < 100)
        {
            LastAccess.AccessCount++;
            LastAccess.Timestamp = Now;
            return;
        }
    }
    
    // 添加新的访问记录
    AccessHistory.push_back(Access);
    
    // 更新资源线程映射
    ResourceThreadMap[ResourceName].insert(CurrentThreadId);
    
    // 限制历史记录大小
    if (AccessHistory.size() > (size_t)Config.MaxResourceHistorySize / 10)
    {
        AccessHistory.erase(AccessHistory.begin(), AccessHistory.begin() + AccessHistory.size() / 2);
    }
    
    // 实时检测竞态条件
    if (Config.bEnableRaceConditionDetection && AccessHistory.size() >= 2)
    {
        AnalyzeResourceAccessPatterns(Resource, AccessHistory);
    }
}

void FCSConcurrencyMonitor::RecordLockAcquisition(void* LockObject, const FString& LockName)
{
    if (!bIsMonitoring.load(std::memory_order_relaxed) || !LockObject)
    {
        return;
    }
    
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    {
        std::lock_guard<std::mutex> ThreadLock(ThreadsMutex);
        ThreadLastActivity[CurrentThreadId] = std::chrono::high_resolution_clock::now();
    }
    
    if (Config.bEnableLockOrderValidation)
    {
        std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
        ThreadLockOrder[CurrentThreadId].push_back(LockObject);
        
        // 检查锁顺序
        if (ThreadLockOrder[CurrentThreadId].size() > 1)
        {
            // 简单的锁顺序检查：检查是否按地址排序获取锁
            auto& LockOrder = ThreadLockOrder[CurrentThreadId];
            if (LockOrder.size() >= 2)
            {
                void* PrevLock = LockOrder[LockOrder.size() - 2];
                void* CurrLock = LockOrder[LockOrder.size() - 1];
                
                // 如果新锁的地址小于前一个锁，可能违反了锁顺序
                if (reinterpret_cast<uintptr_t>(CurrLock) < reinterpret_cast<uintptr_t>(PrevLock))
                {
                    FViolationReport Report;
                    Report.Type = EViolationType::LockOrderViolation;
                    Report.Severity = ESeverity::Warning;
                    Report.Description = FString::Printf(TEXT("Potential lock order violation in thread %d: acquired lock %s (0x%p) after lock (0x%p)"), 
                                                        CurrentThreadId, *LockName, CurrLock, PrevLock);
                    Report.ResourceName = LockName;
                    Report.InvolvedThreads.Add(CurrentThreadId);
                    
                    ReportViolation(Report);
                }
            }
        }
    }
}

void FCSConcurrencyMonitor::RecordLockRelease(void* LockObject, const FString& LockName)
{
    if (!bIsMonitoring.load(std::memory_order_relaxed) || !LockObject)
    {
        return;
    }
    
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    if (Config.bEnableLockOrderValidation)
    {
        std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
        auto& LockOrder = ThreadLockOrder[CurrentThreadId];
        
        // 移除释放的锁
        auto It = std::find(LockOrder.begin(), LockOrder.end(), LockObject);
        if (It != LockOrder.end())
        {
            LockOrder.erase(It);
        }
    }
}

void FCSConcurrencyMonitor::RegisterThread(uint32 ThreadId, const FString& ThreadName)
{
    std::lock_guard<std::mutex> ThreadLock(ThreadsMutex);
    ThreadNames[ThreadId] = ThreadName;
    ThreadLastActivity[ThreadId] = std::chrono::high_resolution_clock::now();
    Stats.MonitoredThreads.store((int32)ThreadNames.size(), std::memory_order_relaxed);
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSConcurrencyMonitor: Registered thread %d (%s)"), ThreadId, *ThreadName);
}

void FCSConcurrencyMonitor::UnregisterThread(uint32 ThreadId)
{
    std::lock_guard<std::mutex> ThreadLock(ThreadsMutex);
    ThreadNames.erase(ThreadId);
    ThreadLastActivity.erase(ThreadId);
    
    // 清理该线程的锁顺序记录
    {
        std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
        ThreadLockOrder.erase(ThreadId);
    }
    
    Stats.MonitoredThreads.store((int32)ThreadNames.size(), std::memory_order_relaxed);
    
    UE_LOG(LogTemp, VeryVerbose, TEXT("CSConcurrencyMonitor: Unregistered thread %d"), ThreadId);
}

bool FCSConcurrencyMonitor::DetectRaceConditions()
{
    if (!Config.bEnableRaceConditionDetection)
    {
        return true;
    }
    
    auto StartTime = std::chrono::high_resolution_clock::now();
    bool FoundViolations = false;
    
    std::lock_guard<std::mutex> ResourceLock(ResourcesMutex);
    
    for (const auto& ResourcePair : ResourceAccessHistory)
    {
        void* Resource = ResourcePair.first;
        const auto& AccessHistory = ResourcePair.second;
        
        if (AnalyzeResourceAccessPatterns(Resource, AccessHistory))
        {
            FoundViolations = true;
        }
    }
    
    auto EndTime = std::chrono::high_resolution_clock::now();
    double ElapsedMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
    Stats.RecordViolation(EViolationType::RaceCondition, ElapsedMs);
    
    return !FoundViolations;
}

bool FCSConcurrencyMonitor::DetectDeadlockPotential()
{
    if (!Config.bEnableDeadlockDetection)
    {
        return true;
    }
    
    auto StartTime = std::chrono::high_resolution_clock::now();
    bool FoundPotentialDeadlocks = false;
    
    std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
    
    // 检查线程间的锁依赖关系
    for (const auto& Thread1Pair : ThreadLockOrder)
    {
        uint32 Thread1 = Thread1Pair.first;
        for (const auto& Thread2Pair : ThreadLockOrder)
        {
            uint32 Thread2 = Thread2Pair.first;
            if (Thread1 != Thread2 && CheckDeadlockBetweenThreads(Thread1, Thread2))
            {
                FViolationReport Report;
                Report.Type = EViolationType::DeadlockPotential;
                Report.Severity = ESeverity::Error;
                Report.Description = FString::Printf(TEXT("Potential deadlock detected between threads %d and %d"), Thread1, Thread2);
                Report.InvolvedThreads.Add(Thread1);
                Report.InvolvedThreads.Add(Thread2);
                
                ReportViolation(Report);
                FoundPotentialDeadlocks = true;
            }
        }
    }
    
    auto EndTime = std::chrono::high_resolution_clock::now();
    double ElapsedMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
    
    if (FoundPotentialDeadlocks)
    {
        Stats.RecordViolation(EViolationType::DeadlockPotential, ElapsedMs);
    }
    
    return !FoundPotentialDeadlocks;
}

bool FCSConcurrencyMonitor::ValidateLockOrder()
{
    // 锁顺序验证在RecordLockAcquisition中实时进行
    return true;
}

bool FCSConcurrencyMonitor::DetectResourceLeaks()
{
    if (!Config.bEnableResourceTracking)
    {
        return true;
    }
    
    auto StartTime = std::chrono::high_resolution_clock::now();
    bool FoundLeaks = false;
    auto Now = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> ResourceLock(ResourcesMutex);
    
    for (const auto& ResourcePair : ResourceAccessHistory)
    {
        const auto& AccessHistory = ResourcePair.second;
        
        if (!AccessHistory.empty())
        {
            const auto& LastAccess = AccessHistory.back();
            auto TimeSinceLastAccess = std::chrono::duration<double>(Now - LastAccess.Timestamp).count();
            
            // 如果资源很久没有被访问，可能是泄露
            if (TimeSinceLastAccess > Config.ResourceAccessTimeoutSeconds)
            {
                FViolationReport Report;
                Report.Type = EViolationType::ResourceLeak;
                Report.Severity = ESeverity::Warning;
                Report.Description = FString::Printf(TEXT("Potential resource leak: %s has not been accessed for %.2f seconds"), 
                                                   *LastAccess.ResourceName, TimeSinceLastAccess);
                Report.ResourceName = LastAccess.ResourceName;
                
                ReportViolation(Report);
                FoundLeaks = true;
            }
        }
    }
    
    auto EndTime = std::chrono::high_resolution_clock::now();
    double ElapsedMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
    
    if (FoundLeaks)
    {
        Stats.RecordViolation(EViolationType::ResourceLeak, ElapsedMs);
    }
    
    return !FoundLeaks;
}

void FCSConcurrencyMonitor::UpdateConfiguration(const FMonitoringConfig& NewConfig)
{
    Config = NewConfig;
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Configuration updated"));
}

TArray<FViolationReport> FCSConcurrencyMonitor::GetViolationReports(ESeverity MinSeverity) const
{
    TArray<FViolationReport> FilteredReports;
    
    std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
    
    for (const auto& Report : ViolationReports)
    {
        if (Report.Severity >= MinSeverity)
        {
            FilteredReports.Add(Report);
        }
    }
    
    return FilteredReports;
}

void FCSConcurrencyMonitor::ClearViolationReports()
{
    std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
    ViolationReports.clear();
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Violation reports cleared"));
}

FString FCSConcurrencyMonitor::ExportDiagnosticsReport() const
{
    FString Report;
    
    Report += TEXT("=== Concurrency Monitor Diagnostics ===\n");
    Report += FString::Printf(TEXT("Monitoring Active: %s\n"), IsMonitoring() ? TEXT("Yes") : TEXT("No"));
    Report += FString::Printf(TEXT("Monitored Threads: %d\n"), Stats.MonitoredThreads.load());
    
    Report += TEXT("\nDetection Statistics:\n");
    Report += FString::Printf(TEXT("  Total Violations: %d\n"), Stats.TotalViolationsDetected.load());
    Report += FString::Printf(TEXT("  Race Conditions: %d\n"), Stats.RaceConditionViolations.load());
    Report += FString::Printf(TEXT("  Deadlock Potential: %d\n"), Stats.DeadlockViolations.load());
    Report += FString::Printf(TEXT("  Lock Order Violations: %d\n"), Stats.LockOrderViolations.load());
    Report += FString::Printf(TEXT("  Resource Leaks: %d\n"), Stats.ResourceLeakViolations.load());
    Report += FString::Printf(TEXT("  Average Detection Time: %.2f ms\n"), Stats.AverageDetectionTimeMs.load());
    Report += FString::Printf(TEXT("  Max Detection Time: %.2f ms\n"), Stats.MaxDetectionTimeMs.load());
    
    {
        std::lock_guard<std::mutex> ResourceLock(ResourcesMutex);
        Report += FString::Printf(TEXT("  Tracked Resources: %d\n"), (int32)ResourceAccessHistory.size());
        Stats.ActiveResourceTracking.store((int32)ResourceAccessHistory.size(), std::memory_order_relaxed);
    }
    
    {
        std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
        Report += FString::Printf(TEXT("  Stored Violation Reports: %d\n"), (int32)ViolationReports.size());
    }
    
    Report += TEXT("\nConfiguration:\n");
    Report += FString::Printf(TEXT("  Race Condition Detection: %s\n"), Config.bEnableRaceConditionDetection ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("  Deadlock Detection: %s\n"), Config.bEnableDeadlockDetection ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("  Lock Order Validation: %s\n"), Config.bEnableLockOrderValidation ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("  Resource Tracking: %s\n"), Config.bEnableResourceTracking ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("  Call Stack Capture: %s\n"), Config.bEnableCallStackCapture ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("  Detection Interval: %.2f seconds\n"), Config.DetectionIntervalSeconds);
    
    return Report;
}

FString FCSConcurrencyMonitor::ExportViolationReport() const
{
    FString Report;
    
    Report += TEXT("=== Concurrency Violation Report ===\n");
    Report += FString::Printf(TEXT("Generated: %s\n\n"), *FDateTime::Now().ToString());
    
    std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
    
    if (ViolationReports.empty())
    {
        Report += TEXT("No violations detected.\n");
        return Report;
    }
    
    // 按严重程度分组报告
    TMap<ESeverity, TArray<FViolationReport>> ViolationsBySeverity;
    
    for (const auto& Violation : ViolationReports)
    {
        ViolationsBySeverity.FindOrAdd(Violation.Severity).Add(Violation);
    }
    
    // 按严重程度顺序输出
    TArray<ESeverity> SeverityOrder = {ESeverity::Critical, ESeverity::Error, ESeverity::Warning, ESeverity::Info};
    
    for (ESeverity Severity : SeverityOrder)
    {
        if (const TArray<FViolationReport>* Violations = ViolationsBySeverity.Find(Severity))
        {
            Report += FString::Printf(TEXT("\n%s Violations (%d):\n"), *GetSeverityDescription(Severity), Violations->Num());
            Report += TEXT("----------------------------------------\n");
            
            for (int32 i = 0; i < Violations->Num(); ++i)
            {
                const auto& Violation = (*Violations)[i];
                
                Report += FString::Printf(TEXT("%d. [%s] %s\n"), i + 1, *GetViolationTypeDescription(Violation.Type), *Violation.Description);
                
                if (!Violation.ResourceName.IsEmpty())
                {
                    Report += FString::Printf(TEXT("   Resource: %s\n"), *Violation.ResourceName);
                }
                
                if (Violation.InvolvedThreads.Num() > 0)
                {
                    Report += TEXT("   Involved Threads: ");
                    for (int32 j = 0; j < Violation.InvolvedThreads.Num(); ++j)
                    {
                        if (j > 0) Report += TEXT(", ");
                        Report += FString::Printf(TEXT("%d"), Violation.InvolvedThreads[j]);
                    }
                    Report += TEXT("\n");
                }
                
                auto DetectionTime = std::chrono::system_clock::from_time_t(
                    std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(Violation.DetectionTime))
                );
                auto TimeT = std::chrono::system_clock::to_time_t(DetectionTime);
                Report += FString::Printf(TEXT("   Detection Time: %s"), UTF8_TO_TCHAR(std::ctime(&TimeT)));
                
                if (!Violation.CallStack.IsEmpty())
                {
                    Report += FString::Printf(TEXT("   Call Stack:\n%s\n"), *Violation.CallStack);
                }
                
                Report += TEXT("\n");
            }
        }
    }
    
    return Report;
}

bool FCSConcurrencyMonitor::IsSystemHealthy() const
{
    int32 TotalViolations = Stats.TotalViolationsDetected.load(std::memory_order_relaxed);
    int32 CriticalViolations = 0;
    
    {
        std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
        for (const auto& Report : ViolationReports)
        {
            if (Report.Severity == ESeverity::Critical)
            {
                CriticalViolations++;
            }
        }
    }
    
    // 如果有严重违规或违规总数过多，认为系统不健康
    return CriticalViolations == 0 && TotalViolations < 100;
}

void FCSConcurrencyMonitor::MonitoringThreadLoop()
{
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Monitoring thread started"));
    
    while (!bShouldStop.load(std::memory_order_relaxed))
    {
        RunDetectionCycle();
        
        // 等待下一个检测周期
        double SleepTime = Config.DetectionIntervalSeconds;
        int32 SleepMs = (int32)(SleepTime * 1000);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(SleepMs));
    }
    
    UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: Monitoring thread stopped"));
}

void FCSConcurrencyMonitor::RunDetectionCycle()
{
    auto CycleStartTime = std::chrono::high_resolution_clock::now();
    
    try
    {
        // 运行各种检测
        DetectRaceConditions();
        DetectDeadlockPotential();
        DetectResourceLeaks();
        
        // 清理过期数据
        CleanupExpiredData();
    }
    catch (const std::exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("CSConcurrencyMonitor: Exception in detection cycle: %s"), UTF8_TO_TCHAR(e.what()));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("CSConcurrencyMonitor: Unknown exception in detection cycle"));
    }
    
    auto CycleEndTime = std::chrono::high_resolution_clock::now();
    double CycleTimeMs = std::chrono::duration<double, std::milli>(CycleEndTime - CycleStartTime).count();
    
    if (CycleTimeMs > Config.DetectionIntervalSeconds * 1000.0 * 0.8) // 如果检测时间超过间隔的80%
    {
        UE_LOG(LogTemp, Warning, TEXT("CSConcurrencyMonitor: Detection cycle took %.2fms (%.1f%% of interval)"), 
               CycleTimeMs, (CycleTimeMs / (Config.DetectionIntervalSeconds * 1000.0)) * 100.0);
    }
}

bool FCSConcurrencyMonitor::AnalyzeResourceAccessPatterns(void* Resource, const std::vector<FResourceAccess>& AccessHistory)
{
    if (AccessHistory.size() < 2)
    {
        return false;
    }
    
    bool FoundViolation = false;
    auto Now = std::chrono::high_resolution_clock::now();
    
    // 检查最近的访问模式
    const int32 RecentAccessWindow = FMath::Min((int32)AccessHistory.size(), 10);
    
    for (int32 i = AccessHistory.size() - RecentAccessWindow; i < (int32)AccessHistory.size() - 1; ++i)
    {
        const auto& Access1 = AccessHistory[i];
        const auto& Access2 = AccessHistory[i + 1];
        
        // 检查不同线程的并发写入
        if (Access1.ThreadId != Access2.ThreadId && 
            (Access1.AccessPattern == EAccessPattern::Write || Access1.AccessPattern == EAccessPattern::ReadWrite) &&
            (Access2.AccessPattern == EAccessPattern::Write || Access2.AccessPattern == EAccessPattern::ReadWrite))
        {
            auto TimeDiff = std::chrono::duration<double, std::milli>(Access2.Timestamp - Access1.Timestamp).count();
            
            // 如果两次写入时间很接近，可能存在竞态条件
            if (TimeDiff < 50.0) // 50ms以内
            {
                FViolationReport Report;
                Report.Type = EViolationType::RaceCondition;
                Report.Severity = ESeverity::Error;
                Report.Description = FString::Printf(TEXT("Potential race condition: Concurrent write access to resource %s by threads %d and %d within %.2fms"), 
                                                   *Access1.ResourceName, Access1.ThreadId, Access2.ThreadId, TimeDiff);
                Report.ResourceName = Access1.ResourceName;
                Report.InvolvedThreads.Add(Access1.ThreadId);
                Report.InvolvedThreads.Add(Access2.ThreadId);
                
                ReportViolation(Report);
                FoundViolation = true;
            }
        }
        
        // 检查读写冲突
        if (Access1.ThreadId != Access2.ThreadId)
        {
            bool IsConflict = (Access1.AccessPattern == EAccessPattern::Write && Access2.AccessPattern != EAccessPattern::Write) ||
                             (Access1.AccessPattern != EAccessPattern::Write && Access2.AccessPattern == EAccessPattern::Write);
            
            if (IsConflict)
            {
                auto TimeDiff = std::chrono::duration<double, std::milli>(Access2.Timestamp - Access1.Timestamp).count();
                
                if (TimeDiff < 100.0) // 100ms以内
                {
                    FViolationReport Report;
                    Report.Type = EViolationType::UnsafeConcurrentAccess;
                    Report.Severity = ESeverity::Warning;
                    Report.Description = FString::Printf(TEXT("Unsafe concurrent access: Read-Write conflict on resource %s between threads %d and %d within %.2fms"), 
                                                       *Access1.ResourceName, Access1.ThreadId, Access2.ThreadId, TimeDiff);
                    Report.ResourceName = Access1.ResourceName;
                    Report.InvolvedThreads.Add(Access1.ThreadId);
                    Report.InvolvedThreads.Add(Access2.ThreadId);
                    
                    ReportViolation(Report);
                    FoundViolation = true;
                }
            }
        }
    }
    
    return FoundViolation;
}

void FCSConcurrencyMonitor::ReportViolation(const FViolationReport& Report)
{
    if (Report.Severity < Config.MinReportSeverity)
    {
        return;
    }
    
    {
        std::lock_guard<std::mutex> ViolationsLock(ViolationsMutex);
        
        // 限制报告数量
        if ((int32)ViolationReports.size() >= Config.MaxViolationReports)
        {
            // 移除最旧的报告
            ViolationReports.erase(ViolationReports.begin(), ViolationReports.begin() + ViolationReports.size() / 2);
        }
        
        ViolationReports.push_back(Report);
    }
    
    // 记录到日志
    if (Config.bLogViolationsToConsole)
    {
        LogViolation(Report);
    }
    
    Stats.RecordViolation(Report.Type, 0.0);
}

void FCSConcurrencyMonitor::LogViolation(const FViolationReport& Report)
{
    FString LogMessage = FString::Printf(TEXT("[%s] %s: %s"), 
                                       *GetSeverityDescription(Report.Severity),
                                       *GetViolationTypeDescription(Report.Type),
                                       *Report.Description);
    
    switch (Report.Severity)
    {
        case ESeverity::Critical:
        case ESeverity::Error:
            UE_LOG(LogTemp, Error, TEXT("CSConcurrencyMonitor: %s"), *LogMessage);
            break;
        case ESeverity::Warning:
            UE_LOG(LogTemp, Warning, TEXT("CSConcurrencyMonitor: %s"), *LogMessage);
            break;
        case ESeverity::Info:
        default:
            UE_LOG(LogTemp, Log, TEXT("CSConcurrencyMonitor: %s"), *LogMessage);
            break;
    }
}

void FCSConcurrencyMonitor::CleanupExpiredData()
{
    auto Now = std::chrono::high_resolution_clock::now();
    
    // 清理过期的资源访问记录
    {
        std::lock_guard<std::mutex> ResourceLock(ResourcesMutex);
        
        for (auto It = ResourceAccessHistory.begin(); It != ResourceAccessHistory.end();)
        {
            auto& AccessHistory = It->second;
            
            // 移除超过一定时间的访问记录
            AccessHistory.erase(
                std::remove_if(AccessHistory.begin(), AccessHistory.end(),
                    [Now](const FResourceAccess& Access) {
                        auto Age = std::chrono::duration<double>(Now - Access.Timestamp).count();
                        return Age > 300.0; // 5分钟
                    }),
                AccessHistory.end()
            );
            
            // 如果没有访问记录了，删除整个资源条目
            if (AccessHistory.empty())
            {
                It = ResourceAccessHistory.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }
    
    // 清理过期的线程活动记录
    {
        std::lock_guard<std::mutex> ThreadLock(ThreadsMutex);
        
        for (auto It = ThreadLastActivity.begin(); It != ThreadLastActivity.end();)
        {
            auto Age = std::chrono::duration<double>(Now - It->second).count();
            if (Age > 600.0) // 10分钟
            {
                uint32 ThreadId = It->first;
                It = ThreadLastActivity.erase(It);
                
                // 同时清理线程名称和锁顺序记录
                ThreadNames.erase(ThreadId);
                
                {
                    std::lock_guard<std::mutex> LockOrderLock(LockOrderMutex);
                    ThreadLockOrder.erase(ThreadId);
                }
            }
            else
            {
                ++It;
            }
        }
        
        Stats.MonitoredThreads.store((int32)ThreadNames.size(), std::memory_order_relaxed);
    }
}

FString FCSConcurrencyMonitor::GetCurrentCallStack() const
{
    // 简化的调用栈实现
    // 实际实现可能需要平台特定的调用栈获取代码
    return TEXT("[Call stack capture not implemented]");
}

bool FCSConcurrencyMonitor::CheckDeadlockBetweenThreads(uint32 Thread1, uint32 Thread2)
{
    // 简化的死锁检测
    const auto& LockOrder1 = ThreadLockOrder.at(Thread1);
    const auto& LockOrder2 = ThreadLockOrder.at(Thread2);
    
    if (LockOrder1.empty() || LockOrder2.empty())
    {
        return false;
    }
    
    // 检查是否存在循环等待
    for (void* Lock1 : LockOrder1)
    {
        for (void* Lock2 : LockOrder2)
        {
            // 如果两个线程持有相同的锁，可能存在死锁风险
            if (Lock1 == Lock2)
            {
                return true;
            }
        }
    }
    
    return false;
}

FString FCSConcurrencyMonitor::GetViolationTypeDescription(EViolationType Type)
{
    switch (Type)
    {
        case EViolationType::RaceCondition: return TEXT("Race Condition");
        case EViolationType::DeadlockPotential: return TEXT("Deadlock Potential");
        case EViolationType::UnsafeConcurrentAccess: return TEXT("Unsafe Concurrent Access");
        case EViolationType::ExcessiveLocking: return TEXT("Excessive Locking");
        case EViolationType::LockOrderViolation: return TEXT("Lock Order Violation");
        case EViolationType::ThreadUnsafeUsage: return TEXT("Thread Unsafe Usage");
        case EViolationType::MemoryOrdering: return TEXT("Memory Ordering");
        case EViolationType::ResourceLeak: return TEXT("Resource Leak");
        default: return TEXT("Unknown");
    }
}

FString FCSConcurrencyMonitor::GetSeverityDescription(ESeverity Severity)
{
    switch (Severity)
    {
        case ESeverity::Info: return TEXT("INFO");
        case ESeverity::Warning: return TEXT("WARNING");
        case ESeverity::Error: return TEXT("ERROR");
        case ESeverity::Critical: return TEXT("CRITICAL");
        default: return TEXT("UNKNOWN");
    }
}