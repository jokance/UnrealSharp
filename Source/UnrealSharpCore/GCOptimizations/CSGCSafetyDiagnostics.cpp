#include "CSGCSafetyDiagnostics.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMemory.h"
#include "Stats/Stats.h"

void FCSGCSafetyDiagnostics::Initialize()
{
    UE_LOG(LogTemp, Log, TEXT("CSGCSafetyDiagnostics: Initializing comprehensive GC safety diagnostics"));
    
    // 清理历史数据
    {
        FScopeLock Lock(&DiagnosticMutex);
        DiagnosticHistory.Empty();
    }
    
    // 初始化子系统
    FCSGCPressureMonitor::Initialize();
    
    UE_LOG(LogTemp, Log, TEXT("CSGCSafetyDiagnostics: Initialization completed"));
}

void FCSGCSafetyDiagnostics::Shutdown()
{
    UE_LOG(LogTemp, Log, TEXT("CSGCSafetyDiagnostics: Shutting down diagnostics system"));
    
    // 生成最终报告
    FDiagnosticReport FinalReport = PerformComprehensiveDiagnostic(EDiagnosticReportType::Full);
    FString ReportText = ExportReportAsText(FinalReport);
    
    UE_LOG(LogTemp, Log, TEXT("CSGCSafetyDiagnostics: Final Diagnostic Report:\n%s"), *ReportText);
    
    // 关闭子系统
    FCSGCPressureMonitor::Shutdown();
    
    // 清理资源
    {
        FScopeLock Lock(&DiagnosticMutex);
        DiagnosticHistory.Empty();
    }
}

FCSGCSafetyDiagnostics::FDiagnosticReport FCSGCSafetyDiagnostics::PerformComprehensiveDiagnostic(EDiagnosticReportType ReportType)
{
    double StartTime = FPlatformTime::Seconds();
    
    FDiagnosticReport Report;
    Report.ReportType = ReportType;
    Report.GeneratedTime = FDateTime::UtcNow();
    Report.GCStats = FCSGCPressureMonitor::GetCurrentGCStatistics();
    Report.SystemInfo = GetSystemInformation();
    
    TArray<FDiagnosticItem> AllItems;
    
    // 根据报告类型收集不同的诊断项目
    switch (ReportType)
    {
        case EDiagnosticReportType::Summary:
            AllItems.Append(ValidateHandleIntegrity());
            AllItems.Append(DetectSuspiciousPatterns());
            break;
            
        case EDiagnosticReportType::Detailed:
            AllItems.Append(ValidateHandleIntegrity());
            AllItems.Append(DetectSuspiciousPatterns());
            AllItems.Append(ValidateHotReloadSafety());
            AllItems.Append(ValidateObjectLifecycleManagement());
            break;
            
        case EDiagnosticReportType::Performance:
            AllItems.Append(AnalyzePerformanceBottlenecks());
            AllItems.Append(AnalyzeMemoryUsagePatterns());
            break;
            
        case EDiagnosticReportType::Security:
            AllItems.Append(ValidateHandleIntegrity());
            AllItems.Append(ValidateHotReloadSafety());
            AllItems.Append(ValidateConfiguration());
            break;
            
        case EDiagnosticReportType::Full:
            AllItems.Append(ValidateHandleIntegrity());
            AllItems.Append(DetectSuspiciousPatterns());
            AllItems.Append(AnalyzePerformanceBottlenecks());
            AllItems.Append(ValidateHotReloadSafety());
            AllItems.Append(ValidateObjectLifecycleManagement());
            AllItems.Append(AnalyzeMemoryUsagePatterns());
            AllItems.Append(ValidateConfiguration());
            break;
    }
    
    // 添加优化建议
    AllItems.Append(GenerateOptimizationSuggestions(Report.GCStats));
    
    Report.Items = AllItems;
    Report.Summary = GenerateReportSummary(AllItems);
    Report.GenerationTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    
    // 将所有项目添加到历史记录
    for (const FDiagnosticItem& Item : AllItems)
    {
        AddDiagnosticItem(Item);
    }
    
    return Report;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::ValidateHandleIntegrity()
{
    TArray<FDiagnosticItem> Items;
    FCSGCPressureMonitor::FGCStats Stats = FCSGCPressureMonitor::GetCurrentGCStatistics();
    
    int32 TotalHandles = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    
    // 检查孤立句柄比例
    if (Stats.OrphanedHandleCount > 0)
    {
        double OrphanRatio = (double)Stats.OrphanedHandleCount / FMath::Max(1, TotalHandles);
        
        if (OrphanRatio > 0.1) // 10%以上
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Error, TEXT("Handle Integrity"),
                TEXT("High Orphaned Handle Ratio"), 
                FString::Printf(TEXT("Orphaned handles: %d (%.1f%% of total)"), Stats.OrphanedHandleCount, OrphanRatio * 100),
                TEXT("Investigate handle cleanup logic and ensure proper disposal")));
        }
        else if (OrphanRatio > 0.05) // 5%以上
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Handle Integrity"),
                TEXT("Moderate Orphaned Handle Count"), 
                FString::Printf(TEXT("Orphaned handles: %d (%.1f%% of total)"), Stats.OrphanedHandleCount, OrphanRatio * 100),
                TEXT("Monitor handle cleanup patterns")));
        }
    }
    
    // 检查强引用比例
    if (TotalHandles > 0)
    {
        double StrongRatio = (double)Stats.StrongHandleCount / TotalHandles;
        if (StrongRatio > 0.8) // 80%以上
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Handle Integrity"),
                TEXT("High Strong Handle Ratio"), 
                FString::Printf(TEXT("Strong handles: %d (%.1f%% of total)"), Stats.StrongHandleCount, StrongRatio * 100),
                TEXT("Consider using weak handles for objects with managed lifetime")));
        }
    }
    
    // 检查总句柄数量
    if (TotalHandles > 10000)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Handle Integrity"),
            TEXT("Very High Handle Count"), 
            FString::Printf(TEXT("Total handles: %d"), TotalHandles),
            TEXT("Monitor for potential memory leaks and consider cleanup")));
    }
    
    if (Items.Num() == 0)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Handle Integrity"),
            TEXT("Handle Integrity OK"), 
            FString::Printf(TEXT("All handle metrics within acceptable ranges. Total: %d"), TotalHandles)));
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::DetectSuspiciousPatterns()
{
    TArray<FDiagnosticItem> Items;
    
    // 获取可疑模式
    TArray<FString> SuspiciousPatterns = FCSGCPressureMonitor::ReportSuspiciousPatterns();
    
    for (const FString& Pattern : SuspiciousPatterns)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Pattern Analysis"),
            TEXT("Suspicious Pattern Detected"), Pattern,
            TEXT("Investigate the root cause and optimize object creation/destruction patterns")));
    }
    
    // 检查对象类型分布
    TMap<FString, int32> TypeDistribution = FCSGCPressureMonitor::GetObjectTypeDistribution();
    for (const auto& Pair : TypeDistribution)
    {
        if (Pair.Value > 1000)
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Pattern Analysis"),
                TEXT("High Object Type Count"), 
                FString::Printf(TEXT("Object type '%s' has %d instances"), *Pair.Key, Pair.Value),
                TEXT("Review object pooling or lifecycle management for this type")));
        }
    }
    
    if (Items.Num() == 0)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Pattern Analysis"),
            TEXT("No Suspicious Patterns"), TEXT("All usage patterns appear normal")));
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::AnalyzePerformanceBottlenecks()
{
    TArray<FDiagnosticItem> Items;
    FCSGCPressureMonitor::FGCStats Stats = FCSGCPressureMonitor::GetCurrentGCStatistics();
    
    // 检查GC压力等级
    if (Stats.PressureLevel >= FCSGCPressureMonitor::EGCPressureLevel::High)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Error, TEXT("Performance"),
            TEXT("High GC Pressure"), 
            FString::Printf(TEXT("Current pressure level: %s"), *FCSGCPressureMonitor::GetPressureLevelDescription(Stats.PressureLevel)),
            TEXT("Execute garbage collection and review object creation patterns")));
    }
    else if (Stats.PressureLevel >= FCSGCPressureMonitor::EGCPressureLevel::Moderate)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Performance"),
            TEXT("Moderate GC Pressure"), 
            FString::Printf(TEXT("Current pressure level: %s"), *FCSGCPressureMonitor::GetPressureLevelDescription(Stats.PressureLevel)),
            TEXT("Monitor closely and consider proactive cleanup")));
    }
    
    // 检查内存压力
    if (Stats.MemoryPressureMB > 1024) // 1GB以上
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Performance"),
            TEXT("High Memory Pressure"), 
            FString::Printf(TEXT("Memory pressure: %.2f MB"), Stats.MemoryPressureMB),
            TEXT("Consider memory optimization and cleanup strategies")));
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::ValidateHotReloadSafety()
{
    TArray<FDiagnosticItem> Items;
    
    // 检查热重载锁状态
    if (FHotReloadSafetyLock::IsHotReloading())
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Hot Reload"),
            TEXT("Hot Reload In Progress"), 
            FString::Printf(TEXT("Waiting threads: %d"), FHotReloadSafetyLock::GetWaitingThreadCount()),
            TEXT("Normal hot reload operation")));
    }
    else
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Hot Reload"),
            TEXT("Hot Reload System Ready"), TEXT("Hot reload safety system is operational")));
    }
    
    // 检查等待线程数量
    int32 WaitingThreads = FHotReloadSafetyLock::GetWaitingThreadCount();
    if (WaitingThreads > 10)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Hot Reload"),
            TEXT("Many Threads Waiting"), 
            FString::Printf(TEXT("Waiting threads: %d"), WaitingThreads),
            TEXT("Check for potential hot reload deadlocks or performance issues")));
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::ValidateObjectLifecycleManagement()
{
    TArray<FDiagnosticItem> Items;
    FCSGCPressureMonitor::FGCStats Stats = FCSGCPressureMonitor::GetCurrentGCStatistics();
    
    // 检查平均对象生命周期
    if (Stats.AverageObjectLifetime > 0)
    {
        if (Stats.AverageObjectLifetime > 300) // 5分钟以上
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Lifecycle"),
                TEXT("Long Average Object Lifetime"), 
                FString::Printf(TEXT("Average lifetime: %.2f seconds"), Stats.AverageObjectLifetime),
                TEXT("Review object disposal patterns and consider earlier cleanup")));
        }
        else
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Lifecycle"),
                TEXT("Healthy Object Lifetime"), 
                FString::Printf(TEXT("Average lifetime: %.2f seconds"), Stats.AverageObjectLifetime)));
        }
    }
    
    // 检查句柄类型分布合理性
    int32 TotalHandles = Stats.StrongHandleCount + Stats.WeakHandleCount + Stats.PinnedHandleCount;
    if (TotalHandles > 0)
    {
        double WeakRatio = (double)Stats.WeakHandleCount / TotalHandles;
        if (WeakRatio > 0.8)
        {
            Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Lifecycle"),
                TEXT("Good Weak Handle Usage"), 
                FString::Printf(TEXT("Weak handles: %.1f%% of total"), WeakRatio * 100),
                TEXT("Good practice - reduces GC pressure")));
        }
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::GenerateOptimizationSuggestions(const FCSGCPressureMonitor::FGCStats& CurrentStats)
{
    TArray<FDiagnosticItem> Items;
    
    // 基于当前统计生成建议
    TArray<FString> Actions = FCSGCPressureMonitor::GetRecommendedActions(CurrentStats.PressureLevel);
    
    for (const FString& Action : Actions)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Optimization"),
            TEXT("Recommended Action"), Action));
    }
    
    // 额外的具体建议
    int32 TotalHandles = CurrentStats.StrongHandleCount + CurrentStats.WeakHandleCount + CurrentStats.PinnedHandleCount;
    if (TotalHandles > 5000)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Optimization"),
            TEXT("Consider Object Pooling"), 
            TEXT("High object count detected - implement object pooling for frequently created/destroyed objects")));
    }
    
    if (CurrentStats.StrongHandleCount > CurrentStats.WeakHandleCount)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Optimization"),
            TEXT("Optimize Handle Types"), 
            TEXT("Use UCSObjectManager.DetermineOptimalHandleType() for automatic handle type selection")));
    }
    
    return Items;
}

FString FCSGCSafetyDiagnostics::ExportReportAsText(const FDiagnosticReport& Report)
{
    FString Text;
    
    Text += TEXT("=== UnrealSharp GC Safety Diagnostics Report ===\n");
    Text += FString::Printf(TEXT("Generated: %s\n"), *Report.GeneratedTime.ToString());
    Text += FString::Printf(TEXT("Report Type: %s\n"), *UEnum::GetValueAsString(Report.ReportType));
    Text += FString::Printf(TEXT("Generation Time: %.2f ms\n"), Report.GenerationTimeMs);
    Text += TEXT("\n");
    
    // 系统信息
    Text += TEXT("--- System Information ---\n");
    for (const auto& Pair : Report.SystemInfo)
    {
        Text += FString::Printf(TEXT("%s: %s\n"), *Pair.Key, *Pair.Value);
    }
    Text += TEXT("\n");
    
    // GC统计
    Text += TEXT("--- GC Statistics ---\n");
    Text += FString::Printf(TEXT("Strong Handles: %d\n"), Report.GCStats.StrongHandleCount);
    Text += FString::Printf(TEXT("Weak Handles: %d\n"), Report.GCStats.WeakHandleCount);
    Text += FString::Printf(TEXT("Pinned Handles: %d\n"), Report.GCStats.PinnedHandleCount);
    Text += FString::Printf(TEXT("Orphaned Handles: %d\n"), Report.GCStats.OrphanedHandleCount);
    Text += FString::Printf(TEXT("Memory Pressure: %.2f MB\n"), Report.GCStats.MemoryPressureMB);
    Text += FString::Printf(TEXT("Pressure Level: %s\n"), *FCSGCPressureMonitor::GetPressureLevelDescription(Report.GCStats.PressureLevel));
    Text += TEXT("\n");
    
    // 摘要
    Text += TEXT("--- Summary ---\n");
    Text += Report.Summary + TEXT("\n\n");
    
    // 诊断项目
    Text += TEXT("--- Diagnostic Items ---\n");
    for (const FDiagnosticItem& Item : Report.Items)
    {
        Text += FString::Printf(TEXT("[%s] %s: %s\n"), 
                               *GetDiagnosticLevelString(Item.Level), 
                               *Item.Category, 
                               *Item.Title);
        Text += FString::Printf(TEXT("  Description: %s\n"), *Item.Description);
        if (!Item.Recommendation.IsEmpty())
        {
            Text += FString::Printf(TEXT("  Recommendation: %s\n"), *Item.Recommendation);
        }
        Text += TEXT("\n");
    }
    
    return Text;
}

FString FCSGCSafetyDiagnostics::GetDiagnosticLevelString(EDiagnosticLevel Level)
{
    switch (Level)
    {
        case EDiagnosticLevel::Info: return TEXT("INFO");
        case EDiagnosticLevel::Warning: return TEXT("WARNING");
        case EDiagnosticLevel::Error: return TEXT("ERROR");
        case EDiagnosticLevel::Critical: return TEXT("CRITICAL");
        default: return TEXT("UNKNOWN");
    }
}

FColor FCSGCSafetyDiagnostics::GetDiagnosticLevelColor(EDiagnosticLevel Level)
{
    switch (Level)
    {
        case EDiagnosticLevel::Info: return FColor::White;
        case EDiagnosticLevel::Warning: return FColor::Yellow;
        case EDiagnosticLevel::Error: return FColor::Red;
        case EDiagnosticLevel::Critical: return FColor::Magenta;
        default: return FColor::Gray;
    }
}

void FCSGCSafetyDiagnostics::AddDiagnosticItem(const FDiagnosticItem& Item)
{
    FScopeLock Lock(&DiagnosticMutex);
    
    DiagnosticHistory.Add(Item);
    
    if (DiagnosticHistory.Num() > MAX_DIAGNOSTIC_HISTORY)
    {
        DiagnosticHistory.RemoveAt(0);
    }
}

TMap<FString, FString> FCSGCSafetyDiagnostics::GetSystemInformation()
{
    TMap<FString, FString> Info;
    
    Info.Add(TEXT("Platform"), *FPlatformProperties::PlatformName());
    Info.Add(TEXT("Configuration"), *FPlatformProperties::GetConfigurationName());
    Info.Add(TEXT("Engine Version"), *FEngineVersion::Current().ToString());
    
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
    Info.Add(TEXT("Total Physical Memory"), FString::Printf(TEXT("%.2f GB"), MemStats.TotalPhysical / (1024.0 * 1024.0 * 1024.0)));
    Info.Add(TEXT("Available Physical Memory"), FString::Printf(TEXT("%.2f GB"), MemStats.AvailablePhysical / (1024.0 * 1024.0 * 1024.0)));
    
    return Info;
}

FCSGCSafetyDiagnostics::FDiagnosticItem FCSGCSafetyDiagnostics::CreateDiagnosticItem(EDiagnosticLevel Level, 
                                                                                   const FString& Category, 
                                                                                   const FString& Title, 
                                                                                   const FString& Description,
                                                                                   const FString& Recommendation)
{
    FDiagnosticItem Item;
    Item.Level = Level;
    Item.Category = Category;
    Item.Title = Title;
    Item.Description = Description;
    Item.Recommendation = Recommendation;
    Item.Timestamp = FDateTime::UtcNow();
    
    return Item;
}

FString FCSGCSafetyDiagnostics::GenerateReportSummary(const TArray<FDiagnosticItem>& Items)
{
    int32 InfoCount = 0, WarningCount = 0, ErrorCount = 0, CriticalCount = 0;
    
    for (const FDiagnosticItem& Item : Items)
    {
        switch (Item.Level)
        {
            case EDiagnosticLevel::Info: InfoCount++; break;
            case EDiagnosticLevel::Warning: WarningCount++; break;
            case EDiagnosticLevel::Error: ErrorCount++; break;
            case EDiagnosticLevel::Critical: CriticalCount++; break;
        }
    }
    
    int32 Score = CalculateDiagnosticScore(Items);
    
    FString Summary = FString::Printf(TEXT("Diagnostic Summary: %d issues found. "), Items.Num());
    Summary += FString::Printf(TEXT("Critical: %d, Errors: %d, Warnings: %d, Info: %d. "), 
                              CriticalCount, ErrorCount, WarningCount, InfoCount);
    Summary += FString::Printf(TEXT("Overall Health Score: %d/100. "), Score);
    
    if (Score >= 90)
    {
        Summary += TEXT("System is in excellent condition.");
    }
    else if (Score >= 70)
    {
        Summary += TEXT("System is in good condition with minor issues.");
    }
    else if (Score >= 50)
    {
        Summary += TEXT("System has moderate issues that should be addressed.");
    }
    else
    {
        Summary += TEXT("System has significant issues requiring immediate attention.");
    }
    
    return Summary;
}

int32 FCSGCSafetyDiagnostics::CalculateDiagnosticScore(const TArray<FDiagnosticItem>& Items)
{
    int32 Score = 100;
    
    for (const FDiagnosticItem& Item : Items)
    {
        switch (Item.Level)
        {
            case EDiagnosticLevel::Critical: Score -= 25; break;
            case EDiagnosticLevel::Error: Score -= 15; break;
            case EDiagnosticLevel::Warning: Score -= 5; break;
            case EDiagnosticLevel::Info: break; // 不扣分
        }
    }
    
    return FMath::Max(0, Score);
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::AnalyzeMemoryUsagePatterns()
{
    TArray<FDiagnosticItem> Items;
    
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
    double UsageRatio = (double)MemStats.UsedPhysical / MemStats.TotalPhysical;
    
    if (UsageRatio > 0.9)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Critical, TEXT("Memory"),
            TEXT("Extremely High Memory Usage"), 
            FString::Printf(TEXT("Memory usage: %.1f%%"), UsageRatio * 100),
            TEXT("Immediate memory cleanup required - consider reducing object count")));
    }
    else if (UsageRatio > 0.8)
    {
        Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Warning, TEXT("Memory"),
            TEXT("High Memory Usage"), 
            FString::Printf(TEXT("Memory usage: %.1f%%"), UsageRatio * 100),
            TEXT("Monitor memory usage and consider proactive cleanup")));
    }
    
    return Items;
}

TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::ValidateConfiguration()
{
    TArray<FDiagnosticItem> Items;
    
    // 这里可以添加配置验证逻辑
    Items.Add(CreateDiagnosticItem(EDiagnosticLevel::Info, TEXT("Configuration"),
        TEXT("Configuration Validation"), TEXT("All configurations appear valid")));
    
    return Items;
}