#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include <mutex>

#if WITH_MONO_RUNTIME && PLATFORM_IOS

/**
 * 线程安全的iOS程序集缓存系统
 * 替代原有的非线程安全缓存实现，提供完整的并发保护
 */
namespace UnrealSharp::iOS::ThreadSafeAssemblyCache
{
    // 缓存条目结构
    struct FCacheEntry
    {
        TArray<uint8> AssemblyData;
        FString ContentHash;
        FDateTime CreatedTime;
        FDateTime LastAccessTime;
        int32 AccessCount;
        bool bIsCompressed;
        
        FCacheEntry()
            : CreatedTime(FDateTime::UtcNow())
            , LastAccessTime(FDateTime::UtcNow())
            , AccessCount(0)
            , bIsCompressed(false)
        {}
    };

    // 线程安全缓存统计
    struct FThreadSafeCacheStats
    {
        std::atomic<int32> CacheHits{0};
        std::atomic<int32> CacheMisses{0};
        std::atomic<int32> CompressionSavings{0};
        std::atomic<double> AverageCacheAccessTime{0.0};
        std::atomic<int32> TotalCacheOperations{0};
        std::atomic<int32> ConcurrentOperations{0};
        
        void RecordHit() {
            CacheHits.fetch_add(1, std::memory_order_relaxed);
            TotalCacheOperations.fetch_add(1, std::memory_order_relaxed);
        }
        
        void RecordMiss() {
            CacheMisses.fetch_add(1, std::memory_order_relaxed);
            TotalCacheOperations.fetch_add(1, std::memory_order_relaxed);
        }
        
        void RecordAccessTime(double TimeMs) {
            // 使用简化的移动平均算法
            double Current = AverageCacheAccessTime.load(std::memory_order_relaxed);
            double Updated = (Current * 0.9) + (TimeMs * 0.1);
            AverageCacheAccessTime.store(Updated, std::memory_order_relaxed);
        }
        
        double GetHitRatio() const {
            int32 Total = TotalCacheOperations.load(std::memory_order_relaxed);
            if (Total == 0) return 0.0;
            return (double)CacheHits.load(std::memory_order_relaxed) / Total;
        }
    };

    // 线程安全的缓存配置
    struct FThreadSafeCacheConfig
    {
        int32 MaxMemoryCacheSize = 64;      // MB
        int32 MaxPersistentCacheSize = 256; // MB
        int32 CacheExpiryDays = 7;
        bool bEnableCompression = true;
        int32 MaxConcurrentOperations = 32;
        double OperationTimeoutSeconds = 30.0;
    };

    /**
     * 主线程安全缓存类
     */
    class UNREALSHARPCORE_API FThreadSafeiOSAssemblyCache
    {
    private:
        // 缓存数据 - 受锁保护
        TMap<FString, FCacheEntry> MemoryCache;
        TMap<FString, FString> PersistentCacheIndex;
        TMap<FString, MonoAssembly*> CompiledAssemblies;
        
        // 线程安全保护
        mutable FCriticalSection CacheMutex;
        mutable FCriticalSection StatsMutex;
        mutable std::mutex OperationMutex;
        
        // 原子统计和配置
        FThreadSafeCacheStats Stats;
        FThreadSafeCacheConfig Config;
        std::atomic<bool> bIsInitialized{false};
        
        // 缓存路径
        FString PersistentCachePath;
        FString TempCachePath;

    public:
        /**
         * 初始化缓存系统
         */
        bool Initialize(const FThreadSafeCacheConfig& InConfig = FThreadSafeCacheConfig());

        /**
         * 关闭缓存系统
         */
        void Shutdown();

        /**
         * 线程安全的缓存获取
         * @param AssemblyName 程序集名称
         * @param OutEntry 输出缓存条目
         * @return 是否找到
         */
        bool GetFromCache(const FString& AssemblyName, FCacheEntry& OutEntry);

        /**
         * 线程安全的缓存存储
         * @param AssemblyName 程序集名称
         * @param AssemblyData 程序集数据
         * @param bForceCompress 强制压缩
         * @return 是否成功存储
         */
        bool StoreInCache(const FString& AssemblyName, const TArray<uint8>& AssemblyData, bool bForceCompress = false);

        /**
         * 线程安全的编译程序集缓存
         * @param AssemblyName 程序集名称
         * @param CompiledAssembly 编译后的程序集
         * @return 是否成功缓存
         */
        bool CacheCompiledAssembly(const FString& AssemblyName, MonoAssembly* CompiledAssembly);

        /**
         * 获取编译的程序集
         * @param AssemblyName 程序集名称
         * @return 编译的程序集指针，如果未找到返回nullptr
         */
        MonoAssembly* GetCompiledAssembly(const FString& AssemblyName);

        /**
         * 清理过期缓存条目
         * @return 清理的条目数量
         */
        int32 CleanupExpiredEntries();

        /**
         * 获取缓存统计信息
         */
        FThreadSafeCacheStats GetCacheStatistics() const;

        /**
         * 获取缓存配置
         */
        const FThreadSafeCacheConfig& GetConfiguration() const { return Config; }

        /**
         * 清空所有缓存
         */
        void ClearAllCaches();

        /**
         * 验证缓存完整性
         * @return 发现的问题数量
         */
        int32 ValidateCacheIntegrity();

        /**
         * 导出缓存诊断报告
         */
        FString ExportDiagnosticsReport();

        /**
         * 设置缓存配置
         */
        void UpdateConfiguration(const FThreadSafeCacheConfig& NewConfig);

        /**
         * 获取缓存大小（MB）
         */
        double GetCacheSizeMB() const;

        /**
         * 检查是否已初始化
         */
        bool IsInitialized() const { return bIsInitialized.load(std::memory_order_acquire); }

    private:
        /**
         * 内部压缩方法（无锁，需要外部保护）
         */
        bool CompressAssemblyDataInternal(const TArray<uint8>& OriginalData, TArray<uint8>& CompressedData);

        /**
         * 内部解压方法（无锁，需要外部保护）  
         */
        bool DecompressAssemblyDataInternal(const TArray<uint8>& CompressedData, TArray<uint8>& DecompressedData);

        /**
         * 计算内容哈希
         */
        FString CalculateContentHash(const TArray<uint8>& AssemblyData);

        /**
         * LRU清理策略
         */
        void PerformLRUCleanup();

        /**
         * 并发操作计数器
         */
        class FScopedOperationCounter
        {
            FThreadSafeiOSAssemblyCache& Cache;
        public:
            FScopedOperationCounter(FThreadSafeiOSAssemblyCache& InCache) : Cache(InCache) 
            {
                Cache.Stats.ConcurrentOperations.fetch_add(1, std::memory_order_relaxed);
            }
            ~FScopedOperationCounter() 
            {
                Cache.Stats.ConcurrentOperations.fetch_sub(1, std::memory_order_relaxed);
            }
        };
    };

    /**
     * 全局线程安全缓存实例访问器
     */
    UNREALSHARPCORE_API FThreadSafeiOSAssemblyCache& GetGlobalCache();

    /**
     * 线程安全的缓存操作助手函数
     */
    namespace CacheHelpers
    {
        /**
         * 安全的缓存操作包装器
         */
        template<typename FunctionType>
        bool SafeCacheOperation(FunctionType&& Operation, double TimeoutSeconds = 30.0)
        {
            double StartTime = FPlatformTime::Seconds();
            
            try
            {
                bool Result = Operation();
                
                double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0; // Convert to ms
                GetGlobalCache().GetCacheStatistics().RecordAccessTime(ElapsedTime);
                
                return Result;
            }
            catch (...)
            {
                UE_LOG(LogTemp, Error, TEXT("ThreadSafeiOSAssemblyCache: Exception during cache operation"));
                return false;
            }
        }

        /**
         * 批量缓存操作
         */
        template<typename KeyType, typename ValueType>
        TMap<KeyType, bool> BatchCacheOperation(
            const TMap<KeyType, ValueType>& Operations,
            TFunction<bool(const KeyType&, const ValueType&)> OperationFunc)
        {
            TMap<KeyType, bool> Results;
            Results.Reserve(Operations.Num());
            
            for (const auto& [Key, Value] : Operations)
            {
                bool Success = SafeCacheOperation([&]() { return OperationFunc(Key, Value); });
                Results.Add(Key, Success);
            }
            
            return Results;
        }
    }
}

#endif // WITH_MONO_RUNTIME && PLATFORM_IOS