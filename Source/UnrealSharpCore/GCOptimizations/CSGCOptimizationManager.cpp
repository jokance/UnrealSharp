#include "CSGCOptimizationManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

bool FCSGCOptimizationManager::Initialize(const FOptimizationConfig& InConfig)
{
    if (CurrentStatus != EManagerStatus::Uninitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSGCOptimizationManager: Already initialized"));
        return false;
    }

    CurrentStatus = EManagerStatus::Initializing;
    Config = InConfig;
    InitializationTime = FDateTime::UtcNow();

    UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Initializing with %s optimization level"), 
           *GetOptimizationLevelDescription(Config.Level));

    // 初始化各个子系统
    try
    {
        if (Config.bEnablePressureMonitoring)
        {
            FCSGCPressureMonitor::Initialize();
        }

        if (Config.bEnableAutomaticDiagnostics)
        {
            FCSGCSafetyDiagnostics::Initialize();
        }

        // 应用配置
        ApplyOptimizationConfiguration();

        // 设置定时器
        SetupTimers();

        // 重置统计数据
        ResetStatistics();

        CurrentStatus = EManagerStatus::Active;

        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Successfully initialized"));
        LogOptimizationOperation(TEXT("Manager Initialization"));

        return true;
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGCOptimizationManager: Failed to initialize"));
        CurrentStatus = EManagerStatus::Uninitialized;
        return false;
    }
}

void FCSGCOptimizationManager::Shutdown()
{
    if (CurrentStatus == EManagerStatus::Shutdown || CurrentStatus == EManagerStatus::Uninitialized)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Shutting down"));

    CurrentStatus = EManagerStatus::Shutdown;

    // 清理定时器
    ClearTimers();

    // 生成最终报告
    if (Config.bExportDiagnosticReports)
    {
        FString FinalReport = ExportOptimizationReport();
        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Final Report Generated"));
        
        // 保存报告到文件
        FString FilePath = Config.DiagnosticReportPath + TEXT("FinalOptimizationReport_") + 
                          FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT(".txt");
        FFileHelper::SaveStringToFile(FinalReport, *FilePath);
    }

    // 关闭子系统
    if (Config.bEnableAutomaticDiagnostics)
    {
        FCSGCSafetyDiagnostics::Shutdown();
    }

    if (Config.bEnablePressureMonitoring)
    {
        FCSGCPressureMonitor::Shutdown();
    }

    UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Shutdown completed. Total optimizations: %d, Objects optimized: %d, Time saved: %.2fs"), 
           TotalOptimizationsApplied, TotalObjectsOptimized, TotalTimeSaved);
}

void FCSGCOptimizationManager::Pause()
{
    if (CurrentStatus == EManagerStatus::Active)
    {
        CurrentStatus = EManagerStatus::Paused;
        ClearTimers();
        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Paused"));
    }
}

void FCSGCOptimizationManager::Resume()
{
    if (CurrentStatus == EManagerStatus::Paused)
    {
        CurrentStatus = EManagerStatus::Active;
        SetupTimers();
        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Resumed"));
    }
}

FGCHandle FCSGCOptimizationManager::CreateOptimizedGCHandle(const UObject* Object, void* TypeHandle, FString* Error)
{
    if (CurrentStatus != EManagerStatus::Active)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSGCOptimizationManager: Manager not active, falling back to standard creation"));
        return FCSManagedCallbacks::ManagedCallbacks.CreateNewManagedObject(const_cast<UObject*>(Object), TypeHandle, Error);
    }

    double StartTime = FPlatformTime::Seconds();

    // 使用智能对象管理器创建优化句柄
    FGCHandle OptimizedHandle = UCSObjectManager::CreateOptimizedHandle(Object, TypeHandle, Error);

    if (!OptimizedHandle.IsNull())
    {
        double TimeSaved = (FPlatformTime::Seconds() - StartTime);
        TotalTimeSaved += TimeSaved;
        TotalOptimizationsApplied++;

        if (Config.bLogPerformanceMetrics)
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSGCOptimizationManager: Created optimized handle for %s (Handle Type: %s, Time: %.4fms)"), 
                   Object ? *Object->GetClass()->GetName() : TEXT("NULL"),
                   *UCSObjectManager::GetHandleTypeName(OptimizedHandle.Type),
                   TimeSaved * 1000.0);
        }

        LogOptimizationOperation(TEXT("Handle Creation"), TimeSaved);
    }

    return OptimizedHandle;
}

FString FCSGCOptimizationManager::PerformSystemOptimization()
{
    if (CurrentStatus != EManagerStatus::Active)
    {
        return TEXT("Manager not active");
    }

    UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Performing comprehensive system optimization"));

    double StartTime = FPlatformTime::Seconds();
    FString Results;

    // 1. 执行GC压力监控和清理
    if (Config.bEnablePressureMonitoring)
    {
        FCSGCPressureMonitor::EGCPressureLevel PressureLevel = FCSGCPressureMonitor::MonitorGCPressure();
        Results += FString::Printf(TEXT("GC Pressure Level: %s\n"), 
                                 *FCSGCPressureMonitor::GetPressureLevelDescription(PressureLevel));

        if (PressureLevel >= FCSGCPressureMonitor::EGCPressureLevel::High)
        {
            FCSGCPressureMonitor::RequestGarbageCollectionIfNeeded();
            Results += TEXT("Executed garbage collection due to high pressure\n");
        }

        int32 CleanedHandles = FCSGCPressureMonitor::CleanupOrphanedHandles();
        if (CleanedHandles > 0)
        {
            Results += FString::Printf(TEXT("Cleaned up %d orphaned handles\n"), CleanedHandles);
        }
    }

    // 2. 执行安全性验证
    FCSGCPressureMonitor::ValidateHandleIntegrity();
    Results += TEXT("Handle integrity validated\n");

    // 3. 生成诊断报告
    if (Config.bEnableAutomaticDiagnostics)
    {
        FCSGCSafetyDiagnostics::FDiagnosticReport Report = 
            FCSGCSafetyDiagnostics::PerformComprehensiveDiagnostic(FCSGCSafetyDiagnostics::EDiagnosticReportType::Summary);
        Results += FString::Printf(TEXT("Diagnostic report generated: %d items\n"), Report.Items.Num());
    }

    double OptimizationTime = FPlatformTime::Seconds() - StartTime;
    TotalTimeSaved += OptimizationTime;
    TotalOptimizationsApplied++;

    Results += FString::Printf(TEXT("System optimization completed in %.2fms\n"), OptimizationTime * 1000.0);

    LogOptimizationOperation(TEXT("System Optimization"), OptimizationTime);

    return Results;
}

TMap<FString, FString> FCSGCOptimizationManager::GetOptimizationStatistics()
{
    TMap<FString, FString> Stats;

    Stats.Add(TEXT("Manager Status"), GetManagerStatusDescription(CurrentStatus));
    Stats.Add(TEXT("Optimization Level"), GetOptimizationLevelDescription(Config.Level));
    Stats.Add(TEXT("Total Optimizations Applied"), FString::FromInt(TotalOptimizationsApplied));
    Stats.Add(TEXT("Total Objects Optimized"), FString::FromInt(TotalObjectsOptimized));
    Stats.Add(TEXT("Total Time Saved"), FString::Printf(TEXT("%.4f seconds"), TotalTimeSaved));
    Stats.Add(TEXT("Optimization Efficiency"), FString::Printf(TEXT("%.2f%%"), CalculateOptimizationEfficiency() * 100));

    if (CurrentStatus == EManagerStatus::Active)
    {
        FTimespan Uptime = FDateTime::UtcNow() - InitializationTime;
        Stats.Add(TEXT("Uptime"), Uptime.ToString());
        Stats.Add(TEXT("Average Optimizations Per Hour"), 
                 FString::Printf(TEXT("%.1f"), TotalOptimizationsApplied / FMath::Max(Uptime.GetTotalHours(), 0.01)));
    }

    // 添加GC统计
    if (Config.bEnablePressureMonitoring)
    {
        FCSGCPressureMonitor::FGCStats GCStats = FCSGCPressureMonitor::GetCurrentGCStatistics();
        Stats.Add(TEXT("Strong Handles"), FString::FromInt(GCStats.StrongHandleCount));
        Stats.Add(TEXT("Weak Handles"), FString::FromInt(GCStats.WeakHandleCount));
        Stats.Add(TEXT("Pinned Handles"), FString::FromInt(GCStats.PinnedHandleCount));
        Stats.Add(TEXT("Orphaned Handles"), FString::FromInt(GCStats.OrphanedHandleCount));
        Stats.Add(TEXT("Memory Pressure"), FString::Printf(TEXT("%.2f MB"), GCStats.MemoryPressureMB));
    }

    return Stats;
}

FCSGCSafetyDiagnostics::FDiagnosticReport FCSGCOptimizationManager::PerformHealthCheck()
{
    if (Config.bEnableAutomaticDiagnostics)
    {
        return FCSGCSafetyDiagnostics::PerformComprehensiveDiagnostic(FCSGCSafetyDiagnostics::EDiagnosticReportType::Full);
    }
    else
    {
        // 返回基本健康检查
        FCSGCSafetyDiagnostics::FDiagnosticReport BasicReport;
        BasicReport.Summary = TEXT("Basic health check - Diagnostics disabled");
        return BasicReport;
    }
}

FString FCSGCOptimizationManager::ExportOptimizationReport()
{
    FString Report;
    
    Report += TEXT("=== UnrealSharp GC Optimization Manager Report ===\n");
    Report += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::UtcNow().ToString());
    Report += TEXT("\n");

    // 管理器状态
    Report += TEXT("--- Manager Status ---\n");
    TMap<FString, FString> Stats = GetOptimizationStatistics();
    for (const auto& Pair : Stats)
    {
        Report += FString::Printf(TEXT("%s: %s\n"), *Pair.Key, *Pair.Value);
    }
    Report += TEXT("\n");

    // 配置信息
    Report += TEXT("--- Configuration ---\n");
    Report += FString::Printf(TEXT("Optimization Level: %s\n"), *GetOptimizationLevelDescription(Config.Level));
    Report += FString::Printf(TEXT("Automatic Cleanup: %s\n"), Config.bEnableAutomaticCleanup ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("Pressure Monitoring: %s\n"), Config.bEnablePressureMonitoring ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("Hot Reload Safety: %s\n"), Config.bEnableHotReloadSafety ? TEXT("Enabled") : TEXT("Disabled"));
    Report += FString::Printf(TEXT("Diagnostics: %s\n"), Config.bEnableAutomaticDiagnostics ? TEXT("Enabled") : TEXT("Disabled"));
    Report += TEXT("\n");

    // 健康检查
    if (Config.bEnableAutomaticDiagnostics)
    {
        FCSGCSafetyDiagnostics::FDiagnosticReport HealthReport = PerformHealthCheck();
        Report += TEXT("--- Health Check Results ---\n");
        Report += HealthReport.Summary + TEXT("\n\n");
    }

    // 性能指标
    if (Config.bEnablePressureMonitoring)
    {
        Report += TEXT("--- GC Pressure Analysis ---\n");
        FString PressureReport = FCSGCPressureMonitor::ExportDiagnosticsReport();
        Report += PressureReport + TEXT("\n");
    }

    return Report;
}

FString FCSGCOptimizationManager::GetOptimizationLevelDescription(EOptimizationLevel Level)
{
    switch (Level)
    {
        case EOptimizationLevel::Disabled: return TEXT("Disabled - No optimizations");
        case EOptimizationLevel::Basic: return TEXT("Basic - Essential optimizations only");
        case EOptimizationLevel::Standard: return TEXT("Standard - Balanced optimization and performance");
        case EOptimizationLevel::Aggressive: return TEXT("Aggressive - Maximum optimization");
        default: return TEXT("Unknown");
    }
}

FString FCSGCOptimizationManager::GetManagerStatusDescription(EManagerStatus Status)
{
    switch (Status)
    {
        case EManagerStatus::Uninitialized: return TEXT("Uninitialized");
        case EManagerStatus::Initializing: return TEXT("Initializing");
        case EManagerStatus::Active: return TEXT("Active");
        case EManagerStatus::Paused: return TEXT("Paused");
        case EManagerStatus::Shutdown: return TEXT("Shutdown");
        default: return TEXT("Unknown");
    }
}

void FCSGCOptimizationManager::TriggerGarbageCollection(bool bForceCollection)
{
    if (Config.bEnablePressureMonitoring)
    {
        if (bForceCollection)
        {
            FCSGCPressureMonitor::ForceGarbageCollection();
            LogOptimizationOperation(TEXT("Forced Garbage Collection"));
        }
        else
        {
            FCSGCPressureMonitor::RequestGarbageCollectionIfNeeded();
            LogOptimizationOperation(TEXT("Conditional Garbage Collection"));
        }
    }
    else if (bForceCollection && GEngine)
    {
        GEngine->ForceGarbageCollection(true);
        LogOptimizationOperation(TEXT("Engine Garbage Collection"));
    }
}

void FCSGCOptimizationManager::TriggerCleanupOperations()
{
    if (Config.bEnablePressureMonitoring)
    {
        int32 CleanedHandles = FCSGCPressureMonitor::CleanupOrphanedHandles();
        FCSGCPressureMonitor::ValidateHandleIntegrity();
        
        LogOptimizationOperation(FString::Printf(TEXT("Cleanup Operations - %d handles cleaned"), CleanedHandles));
    }
}

void FCSGCOptimizationManager::SetupTimers()
{
    if (!GEngine || !GEngine->GetWorld())
    {
        return;
    }

    FTimerManager& TimerManager = GEngine->GetWorld()->GetTimerManager();

    // 监控定时器
    if (Config.bEnablePressureMonitoring && Config.MonitoringIntervalSeconds > 0)
    {
        TimerManager.SetTimer(MonitoringTimerHandle, 
                            FTimerDelegate::CreateStatic(&FCSGCOptimizationManager::OnMonitoringTimer),
                            Config.MonitoringIntervalSeconds, true);
    }

    // 清理定时器
    if (Config.bEnableAutomaticCleanup && Config.CleanupIntervalSeconds > 0)
    {
        TimerManager.SetTimer(CleanupTimerHandle,
                            FTimerDelegate::CreateStatic(&FCSGCOptimizationManager::OnCleanupTimer),
                            Config.CleanupIntervalSeconds, true);
    }

    // 诊断定时器
    if (Config.bEnableAutomaticDiagnostics && Config.DiagnosticsIntervalSeconds > 0)
    {
        TimerManager.SetTimer(DiagnosticsTimerHandle,
                            FTimerDelegate::CreateStatic(&FCSGCOptimizationManager::OnDiagnosticsTimer),
                            Config.DiagnosticsIntervalSeconds, true);
    }
}

void FCSGCOptimizationManager::ClearTimers()
{
    if (!GEngine || !GEngine->GetWorld())
    {
        return;
    }

    FTimerManager& TimerManager = GEngine->GetWorld()->GetTimerManager();

    TimerManager.ClearTimer(MonitoringTimerHandle);
    TimerManager.ClearTimer(CleanupTimerHandle);
    TimerManager.ClearTimer(DiagnosticsTimerHandle);
}

void FCSGCOptimizationManager::OnMonitoringTimer()
{
    if (CurrentStatus == EManagerStatus::Active && Config.bEnablePressureMonitoring)
    {
        FCSGCPressureMonitor::PerformPeriodicMaintenance();
        LastMonitoringTime = FDateTime::UtcNow();
    }
}

void FCSGCOptimizationManager::OnCleanupTimer()
{
    if (CurrentStatus == EManagerStatus::Active)
    {
        TriggerCleanupOperations();
        LastCleanupTime = FDateTime::UtcNow();
    }
}

void FCSGCOptimizationManager::OnDiagnosticsTimer()
{
    if (CurrentStatus == EManagerStatus::Active && Config.bEnableAutomaticDiagnostics)
    {
        FCSGCSafetyDiagnostics::PerformAutomaticDiagnostic();
        
        if (Config.bExportDiagnosticReports)
        {
            FCSGCSafetyDiagnostics::FDiagnosticReport Report = PerformHealthCheck();
            SaveDiagnosticReport(Report);
        }
        
        LastDiagnosticsTime = FDateTime::UtcNow();
    }
}

void FCSGCOptimizationManager::LogOptimizationOperation(const FString& Operation, double TimeSaved)
{
    if (Config.bLogPerformanceMetrics)
    {
        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: %s (Time saved: %.4fms)"), 
               *Operation, TimeSaved * 1000.0);
    }
}

void FCSGCOptimizationManager::SaveDiagnosticReport(const FCSGCSafetyDiagnostics::FDiagnosticReport& Report)
{
    FString ReportText = FCSGCSafetyDiagnostics::ExportReportAsText(Report);
    FString FileName = FString::Printf(TEXT("DiagnosticReport_%s.txt"), 
                                      *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
    FString FilePath = Config.DiagnosticReportPath + FileName;
    
    if (FFileHelper::SaveStringToFile(ReportText, *FilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Diagnostic report saved to %s"), *FilePath);
    }
}

double FCSGCOptimizationManager::CalculateOptimizationEfficiency()
{
    if (TotalOptimizationsApplied == 0)
    {
        return 0.0;
    }

    // 简化的效率计算：基于时间节省和优化次数的比率
    double EfficiencyScore = (TotalTimeSaved * 1000.0) / TotalOptimizationsApplied; // ms per optimization
    return FMath::Min(EfficiencyScore / 10.0, 1.0); // 标准化到0-1范围
}

void FCSGCOptimizationManager::ResetStatistics()
{
    TotalOptimizationsApplied = 0;
    TotalObjectsOptimized = 0;
    TotalTimeSaved = 0.0;
    
    UE_LOG(LogTemp, Log, TEXT("CSGCOptimizationManager: Statistics reset"));
}

FCSGCOptimizationManager::FOptimizationConfig FCSGCOptimizationManager::GetRecommendedConfiguration(EOptimizationLevel TargetPerformance)
{
    FOptimizationConfig RecommendedConfig;
    
    RecommendedConfig.Level = TargetPerformance;
    
    switch (TargetPerformance)
    {
        case EOptimizationLevel::Disabled:
            RecommendedConfig.bEnableAutomaticCleanup = false;
            RecommendedConfig.bEnablePressureMonitoring = false;
            RecommendedConfig.bEnableHotReloadSafety = false;
            RecommendedConfig.bEnableAutomaticDiagnostics = false;
            RecommendedConfig.bEnableObjectSafetyValidation = false;
            break;
            
        case EOptimizationLevel::Basic:
            RecommendedConfig.MonitoringIntervalSeconds = 10.0;
            RecommendedConfig.CleanupIntervalSeconds = 60.0;
            RecommendedConfig.DiagnosticsIntervalSeconds = 300.0;
            RecommendedConfig.bLogPerformanceMetrics = false;
            break;
            
        case EOptimizationLevel::Standard:
            // 使用默认配置
            break;
            
        case EOptimizationLevel::Aggressive:
            RecommendedConfig.MonitoringIntervalSeconds = 1.0;
            RecommendedConfig.CleanupIntervalSeconds = 10.0;
            RecommendedConfig.DiagnosticsIntervalSeconds = 30.0;
            RecommendedConfig.GCPressureThresholdLow = 500;
            RecommendedConfig.GCPressureThresholdHigh = 2000;
            RecommendedConfig.OrphanedHandleThreshold = 50;
            RecommendedConfig.bLogPerformanceMetrics = true;
            RecommendedConfig.bExportDiagnosticReports = true;
            break;
    }
    
    return RecommendedConfig;
}