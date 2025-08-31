#pragma once

#include "CoreMinimal.h"
#include "CSObjectManager.h"
#include "CSGCPressureMonitor.h"
#include "CSObjectSafetyValidator.h"
#include "CSHotReloadSafetyLock.h"
#include "Containers/Map.h"
#include "Misc/DateTime.h"

/**
 * 全面的GC安全性诊断系统
 * 整合所有GC优化组件，提供统一的诊断和报告接口
 */
class UNREALSHARPCORE_API FCSGCSafetyDiagnostics
{
public:
    // 诊断报告类型
    enum class EDiagnosticReportType : uint8
    {
        Summary,        // 简要报告
        Detailed,       // 详细报告  
        Performance,    // 性能报告
        Security,       // 安全报告
        Full           // 完整报告
    };

    // 诊断结果等级
    enum class EDiagnosticLevel : uint8
    {
        Info,           // 信息
        Warning,        // 警告
        Error,          // 错误
        Critical        // 严重
    };

    // 诊断项目
    struct FDiagnosticItem
    {
        EDiagnosticLevel Level = EDiagnosticLevel::Info;
        FString Category;
        FString Title;
        FString Description;
        FString Recommendation;
        FDateTime Timestamp = FDateTime::UtcNow();
        TMap<FString, FString> AdditionalData;
    };

    // 诊断报告
    struct FDiagnosticReport
    {
        EDiagnosticReportType ReportType = EDiagnosticReportType::Summary;
        FDateTime GeneratedTime = FDateTime::UtcNow();
        TArray<FDiagnosticItem> Items;
        FCSGCPressureMonitor::FGCStats GCStats;
        TMap<FString, FString> SystemInfo;
        FString Summary;
        double GenerationTimeMs = 0.0;
    };

private:
    static TArray<FDiagnosticItem> DiagnosticHistory;
    static FCriticalSection DiagnosticMutex;
    static constexpr int32 MAX_DIAGNOSTIC_HISTORY = 1000;

public:
    /**
     * 初始化诊断系统
     */
    static void Initialize();

    /**
     * 关闭诊断系统
     */
    static void Shutdown();

    /**
     * 执行完整的GC安全性诊断
     * @param ReportType 报告类型
     * @return 诊断报告
     */
    static FDiagnosticReport PerformComprehensiveDiagnostic(EDiagnosticReportType ReportType = EDiagnosticReportType::Detailed);

    /**
     * 验证句柄完整性
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> ValidateHandleIntegrity();

    /**
     * 检测可疑的GC模式
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> DetectSuspiciousPatterns();

    /**
     * 分析性能瓶颈
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> AnalyzePerformanceBottlenecks();

    /**
     * 检查热重载安全性
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> ValidateHotReloadSafety();

    /**
     * 检查对象生命周期管理
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> ValidateObjectLifecycleManagement();

    /**
     * 生成优化建议
     * @param CurrentStats 当前GC统计
     * @return 诊断项目数组
     */
    static TArray<FDiagnosticItem> GenerateOptimizationSuggestions(const FCSGCPressureMonitor::FGCStats& CurrentStats);

    /**
     * 添加诊断项目到历史记录
     */
    static void AddDiagnosticItem(const FDiagnosticItem& Item);

    /**
     * 获取诊断历史
     */
    static TArray<FDiagnosticItem> GetDiagnosticHistory(EDiagnosticLevel MinLevel = EDiagnosticLevel::Info);

    /**
     * 导出诊断报告为文本
     */
    static FString ExportReportAsText(const FDiagnosticReport& Report);

    /**
     * 导出诊断报告为JSON
     */
    static FString ExportReportAsJSON(const FDiagnosticReport& Report);

    /**
     * 保存诊断报告到文件
     */
    static bool SaveReportToFile(const FDiagnosticReport& Report, const FString& FilePath, bool bAsJSON = false);

    /**
     * 获取系统信息
     */
    static TMap<FString, FString> GetSystemInformation();

    /**
     * 获取诊断等级的字符串表示
     */
    static FString GetDiagnosticLevelString(EDiagnosticLevel Level);

    /**
     * 获取诊断等级的颜色代码（用于UI显示）
     */
    static FColor GetDiagnosticLevelColor(EDiagnosticLevel Level);

    /**
     * 执行自动诊断（定期调用）
     */
    static void PerformAutomaticDiagnostic();

    /**
     * 获取诊断摘要
     */
    static FString GetDiagnosticSummary();

    /**
     * 清理诊断历史
     */
    static void ClearDiagnosticHistory();

    /**
     * 设置诊断项目过滤器
     */
    static TArray<FDiagnosticItem> FilterDiagnosticItems(const TArray<FDiagnosticItem>& Items, 
                                                         EDiagnosticLevel MinLevel = EDiagnosticLevel::Info,
                                                         const FString& CategoryFilter = TEXT(""));

private:
    /**
     * 创建诊断项目
     */
    static FDiagnosticItem CreateDiagnosticItem(EDiagnosticLevel Level, const FString& Category, 
                                              const FString& Title, const FString& Description,
                                              const FString& Recommendation = TEXT(""));

    /**
     * 收集系统性能指标
     */
    static TMap<FString, FString> CollectPerformanceMetrics();

    /**
     * 分析内存使用模式
     */
    static TArray<FDiagnosticItem> AnalyzeMemoryUsagePatterns();

    /**
     * 检查配置合理性
     */
    static TArray<FDiagnosticItem> ValidateConfiguration();

    /**
     * 生成报告摘要
     */
    static FString GenerateReportSummary(const TArray<FDiagnosticItem>& Items);

    /**
     * 计算诊断分数
     */
    static int32 CalculateDiagnosticScore(const TArray<FDiagnosticItem>& Items);
};

// 静态成员初始化
TArray<FCSGCSafetyDiagnostics::FDiagnosticItem> FCSGCSafetyDiagnostics::DiagnosticHistory;
FCriticalSection FCSGCSafetyDiagnostics::DiagnosticMutex;