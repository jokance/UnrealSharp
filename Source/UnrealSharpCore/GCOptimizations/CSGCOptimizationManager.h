#pragma once

#include "CoreMinimal.h"
#include "CSObjectManager.h"
#include "CSGCPressureMonitor.h"
#include "CSObjectSafetyValidator.h"
#include "CSHotReloadSafetyLock.h"
#include "CSGCSafetyDiagnostics.h"
#include "Engine/EngineTypes.h"
#include "Misc/DateTime.h"

/**
 * 统一的GC优化管理器
 * 整合所有GC优化组件，提供统一的接口和管理功能
 */
class UNREALSHARPCORE_API FCSGCOptimizationManager
{
public:
    // 优化等级
    enum class EOptimizationLevel : uint8
    {
        Disabled,       // 禁用优化
        Basic,          // 基础优化
        Standard,       // 标准优化
        Aggressive      // 激进优化
    };

    // 管理器状态
    enum class EManagerStatus : uint8
    {
        Uninitialized,  // 未初始化
        Initializing,   // 初始化中
        Active,         // 活动状态
        Paused,         // 暂停状态
        Shutdown        // 已关闭
    };

    // 优化配置
    struct FOptimizationConfig
    {
        EOptimizationLevel Level = EOptimizationLevel::Standard;
        bool bEnableAutomaticCleanup = true;
        bool bEnablePressureMonitoring = true;
        bool bEnableHotReloadSafety = true;
        bool bEnableAutomaticDiagnostics = true;
        bool bEnableObjectSafetyValidation = true;
        
        // 监控间隔设置
        double MonitoringIntervalSeconds = 5.0;
        double CleanupIntervalSeconds = 30.0;
        double DiagnosticsIntervalSeconds = 60.0;
        
        // 压力阈值设置
        int32 GCPressureThresholdLow = 1000;
        int32 GCPressureThresholdHigh = 5000;
        int32 OrphanedHandleThreshold = 100;
        
        // 性能设置
        bool bLogPerformanceMetrics = false;
        bool bExportDiagnosticReports = false;
        FString DiagnosticReportPath = TEXT("Logs/UnrealSharp/");
    };

private:
    static EManagerStatus CurrentStatus;
    static FOptimizationConfig Config;
    static FDateTime LastMonitoringTime;
    static FDateTime LastCleanupTime;
    static FDateTime LastDiagnosticsTime;
    static FDateTime InitializationTime;
    
    // 定时器句柄
    static FTimerHandle MonitoringTimerHandle;
    static FTimerHandle CleanupTimerHandle;
    static FTimerHandle DiagnosticsTimerHandle;
    
    // 统计数据
    static int32 TotalOptimizationsApplied;
    static int32 TotalObjectsOptimized;
    static double TotalTimeSaved;

public:
    /**
     * 初始化GC优化管理器
     * @param InConfig 优化配置
     * @return 是否成功初始化
     */
    static bool Initialize(const FOptimizationConfig& InConfig = FOptimizationConfig());

    /**
     * 关闭GC优化管理器
     */
    static void Shutdown();

    /**
     * 暂停优化管理器
     */
    static void Pause();

    /**
     * 恢复优化管理器
     */
    static void Resume();

    /**
     * 获取管理器状态
     */
    static EManagerStatus GetStatus() { return CurrentStatus; }

    /**
     * 更新配置
     */
    static void UpdateConfiguration(const FOptimizationConfig& NewConfig);

    /**
     * 获取当前配置
     */
    static const FOptimizationConfig& GetConfiguration() { return Config; }

    /**
     * 创建优化的GC句柄（统一入口）
     * @param Object 源UObject
     * @param TypeHandle 托管类型句柄
     * @param Error 错误输出
     * @return 优化的GC句柄
     */
    static FGCHandle CreateOptimizedGCHandle(const UObject* Object, void* TypeHandle, FString* Error = nullptr);

    /**
     * 安全访问托管对象（统一入口）
     * @param Object 要访问的对象
     * @param AccessFunction 访问函数
     * @return 访问是否成功
     */
    template<typename ObjectType, typename FunctionType>
    static bool SafeAccessManagedObject(ObjectType* Object, FunctionType AccessFunction);

    /**
     * 执行全面的系统优化
     * @return 优化结果摘要
     */
    static FString PerformSystemOptimization();

    /**
     * 获取优化统计信息
     */
    static TMap<FString, FString> GetOptimizationStatistics();

    /**
     * 执行健康检查
     * @return 健康检查报告
     */
    static FCSGCSafetyDiagnostics::FDiagnosticReport PerformHealthCheck();

    /**
     * 导出完整的优化报告
     */
    static FString ExportOptimizationReport();

    /**
     * 重置统计数据
     */
    static void ResetStatistics();

    /**
     * 获取优化等级描述
     */
    static FString GetOptimizationLevelDescription(EOptimizationLevel Level);

    /**
     * 获取管理器状态描述
     */
    static FString GetManagerStatusDescription(EManagerStatus Status);

    /**
     * 手动触发垃圾回收
     */
    static void TriggerGarbageCollection(bool bForceCollection = false);

    /**
     * 手动触发清理操作
     */
    static void TriggerCleanupOperations();

    /**
     * 验证系统完整性
     */
    static bool ValidateSystemIntegrity();

    /**
     * 获取推荐的配置
     * @param TargetPerformance 目标性能等级
     * @return 推荐配置
     */
    static FOptimizationConfig GetRecommendedConfiguration(EOptimizationLevel TargetPerformance);

private:
    /**
     * 设置定时器
     */
    static void SetupTimers();

    /**
     * 清理定时器
     */
    static void ClearTimers();

    /**
     * 定期监控回调
     */
    static void OnMonitoringTimer();

    /**
     * 定期清理回调
     */
    static void OnCleanupTimer();

    /**
     * 定期诊断回调
     */
    static void OnDiagnosticsTimer();

    /**
     * 应用优化配置
     */
    static void ApplyOptimizationConfiguration();

    /**
     * 记录优化操作
     */
    static void LogOptimizationOperation(const FString& Operation, double TimeSaved = 0.0);

    /**
     * 保存诊断报告
     */
    static void SaveDiagnosticReport(const FCSGCSafetyDiagnostics::FDiagnosticReport& Report);

    /**
     * 计算优化效果
     */
    static double CalculateOptimizationEfficiency();
};

// 静态成员初始化声明
FCSGCOptimizationManager::EManagerStatus FCSGCOptimizationManager::CurrentStatus = EManagerStatus::Uninitialized;
FCSGCOptimizationManager::FOptimizationConfig FCSGCOptimizationManager::Config;
FDateTime FCSGCOptimizationManager::LastMonitoringTime;
FDateTime FCSGCOptimizationManager::LastCleanupTime;
FDateTime FCSGCOptimizationManager::LastDiagnosticsTime;
FDateTime FCSGCOptimizationManager::InitializationTime;
FTimerHandle FCSGCOptimizationManager::MonitoringTimerHandle;
FTimerHandle FCSGCOptimizationManager::CleanupTimerHandle;
FTimerHandle FCSGCOptimizationManager::DiagnosticsTimerHandle;
int32 FCSGCOptimizationManager::TotalOptimizationsApplied = 0;
int32 FCSGCOptimizationManager::TotalObjectsOptimized = 0;
double FCSGCOptimizationManager::TotalTimeSaved = 0.0;

// 模板函数实现
template<typename ObjectType, typename FunctionType>
bool FCSGCOptimizationManager::SafeAccessManagedObject(ObjectType* Object, FunctionType AccessFunction)
{
    if (CurrentStatus != EManagerStatus::Active)
    {
        UE_LOG(LogTemp, Warning, TEXT("CSGCOptimizationManager: Attempted access while not active"));
        return false;
    }

    // 使用安全验证器检查对象
    if (!FCSObjectSafetyValidator::IsObjectSafeForManagedAccess(Object))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSGCOptimizationManager: Object failed safety validation"));
        return false;
    }

    // 使用热重载安全锁进行访问
    return FHotReloadSafetyLock::SafeManagedObjectAccess([&]()
    {
        AccessFunction(Object);
        TotalObjectsOptimized++;
    });
}