#include "CSThreadSafeiOSAssemblyCache.h"

#if WITH_MONO_RUNTIME && PLATFORM_IOS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"
#include "Misc/SecureHash.h"
#include "Engine/Engine.h"

namespace UnrealSharp::iOS::ThreadSafeAssemblyCache
{
    // 全局缓存实例
    static FThreadSafeiOSAssemblyCache GlobalCacheInstance;

    FThreadSafeiOSAssemblyCache& GetGlobalCache()
    {
        return GlobalCacheInstance;
    }

    bool FThreadSafeiOSAssemblyCache::Initialize(const FThreadSafeCacheConfig& InConfig)
    {
        std::lock_guard<std::mutex> Lock(OperationMutex);
        
        if (bIsInitialized.load(std::memory_order_relaxed))
        {
            UE_LOG(LogTemp, Warning, TEXT("ThreadSafeiOSAssemblyCache: Already initialized"));
            return true;
        }

        Config = InConfig;

        // 设置缓存路径
        PersistentCachePath = FPaths::ProjectSavedDir() / TEXT("UnrealSharp") / TEXT("iOS") / TEXT("AssemblyCache");
        TempCachePath = FPaths::ProjectIntermediateDir() / TEXT("UnrealSharp") / TEXT("iOS") / TEXT("TempCache");

        // 确保目录存在
        IFileManager& FileManager = IFileManager::Get();
        if (!FileManager.DirectoryExists(*PersistentCachePath))
        {
            FileManager.MakeDirectory(*PersistentCachePath, true);
        }
        if (!FileManager.DirectoryExists(*TempCachePath))
        {
            FileManager.MakeDirectory(*TempCachePath, true);
        }

        // 初始化缓存映射
        {
            FScopeLock CacheLock(&CacheMutex);
            MemoryCache.Empty();
            PersistentCacheIndex.Empty();
            CompiledAssemblies.Empty();
        }

        // 重置统计信息
        Stats = FThreadSafeCacheStats{};

        bIsInitialized.store(true, std::memory_order_release);

        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Successfully initialized with %d MB memory cache limit"), 
               Config.MaxMemoryCacheSize);

        return true;
    }

    void FThreadSafeiOSAssemblyCache::Shutdown()
    {
        std::lock_guard<std::mutex> Lock(OperationMutex);
        
        if (!bIsInitialized.load(std::memory_order_relaxed))
        {
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Shutting down cache system"));

        // 导出最终统计信息
        FString DiagnosticsReport = ExportDiagnosticsReport();
        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Final Statistics:\n%s"), *DiagnosticsReport);

        // 清空所有缓存
        {
            FScopeLock CacheLock(&CacheMutex);
            MemoryCache.Empty();
            PersistentCacheIndex.Empty();
            CompiledAssemblies.Empty();
        }

        bIsInitialized.store(false, std::memory_order_release);
    }

    bool FThreadSafeiOSAssemblyCache::GetFromCache(const FString& AssemblyName, FCacheEntry& OutEntry)
    {
        if (!bIsInitialized.load(std::memory_order_acquire))
        {
            return false;
        }

        FScopedOperationCounter OpCounter(*this);
        double StartTime = FPlatformTime::Seconds();

        {
            FScopeLock CacheLock(&CacheMutex);
            
            if (FCacheEntry* Found = MemoryCache.Find(AssemblyName))
            {
                // 更新访问统计
                Found->LastAccessTime = FDateTime::UtcNow();
                Found->AccessCount++;
                
                OutEntry = *Found;
                Stats.RecordHit();
                
                double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                Stats.RecordAccessTime(ElapsedTime);
                
                UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Cache hit for %s (%.2fms)"), 
                       *AssemblyName, ElapsedTime);
                       
                return true;
            }
        }

        // L1缓存未命中，尝试L2持久化缓存
        FString PersistentFilePath;
        {
            FScopeLock CacheLock(&CacheMutex);
            if (FString* Found = PersistentCacheIndex.Find(AssemblyName))
            {
                PersistentFilePath = *Found;
            }
        }

        if (!PersistentFilePath.IsEmpty())
        {
            TArray<uint8> FileData;
            if (FFileHelper::LoadFileToArray(FileData, *PersistentFilePath))
            {
                FCacheEntry NewEntry;
                NewEntry.AssemblyData = FileData;
                NewEntry.ContentHash = CalculateContentHash(FileData);
                NewEntry.LastAccessTime = FDateTime::UtcNow();
                NewEntry.AccessCount = 1;
                
                // 提升到L1缓存
                {
                    FScopeLock CacheLock(&CacheMutex);
                    MemoryCache.Add(AssemblyName, NewEntry);
                }
                
                OutEntry = NewEntry;
                Stats.RecordHit();
                
                UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: L2 cache hit for %s"), *AssemblyName);
                return true;
            }
        }

        Stats.RecordMiss();
        UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Cache miss for %s"), *AssemblyName);
        return false;
    }

    bool FThreadSafeiOSAssemblyCache::StoreInCache(const FString& AssemblyName, const TArray<uint8>& AssemblyData, bool bForceCompress)
    {
        if (!bIsInitialized.load(std::memory_order_acquire))
        {
            return false;
        }

        FScopedOperationCounter OpCounter(*this);

        FCacheEntry NewEntry;
        
        // 压缩数据（如果启用）
        if (Config.bEnableCompression || bForceCompress)
        {
            if (CompressAssemblyDataInternal(AssemblyData, NewEntry.AssemblyData))
            {
                NewEntry.bIsCompressed = true;
                int32 Savings = AssemblyData.Num() - NewEntry.AssemblyData.Num();
                Stats.CompressionSavings.fetch_add(Savings, std::memory_order_relaxed);
                
                UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Compressed %s: %d -> %d bytes (%.1f%% saved)"), 
                       *AssemblyName, AssemblyData.Num(), NewEntry.AssemblyData.Num(), 
                       (float)Savings / AssemblyData.Num() * 100.0f);
            }
            else
            {
                NewEntry.AssemblyData = AssemblyData;
                NewEntry.bIsCompressed = false;
            }
        }
        else
        {
            NewEntry.AssemblyData = AssemblyData;
            NewEntry.bIsCompressed = false;
        }

        NewEntry.ContentHash = CalculateContentHash(AssemblyData);
        NewEntry.CreatedTime = FDateTime::UtcNow();
        NewEntry.LastAccessTime = FDateTime::UtcNow();
        NewEntry.AccessCount = 0;

        // 存储到L1缓存
        {
            FScopeLock CacheLock(&CacheMutex);
            MemoryCache.Add(AssemblyName, NewEntry);
            
            // 检查是否需要LRU清理
            if (GetCacheSizeMB() > Config.MaxMemoryCacheSize)
            {
                PerformLRUCleanup();
            }
        }

        // 异步保存到持久化缓存
        FString PersistentFilePath = PersistentCachePath / (AssemblyName + TEXT(".cache"));
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PersistentFilePath, NewEntry, AssemblyName, this]()
        {
            if (FFileHelper::SaveArrayToFile(NewEntry.AssemblyData, *PersistentFilePath))
            {
                FScopeLock CacheLock(&CacheMutex);
                PersistentCacheIndex.Add(AssemblyName, PersistentFilePath);
                
                UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Saved %s to persistent cache"), *AssemblyName);
            }
        });

        UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Stored %s in cache (%s, %d bytes)"), 
               *AssemblyName, NewEntry.bIsCompressed ? TEXT("compressed") : TEXT("uncompressed"), 
               NewEntry.AssemblyData.Num());

        return true;
    }

    bool FThreadSafeiOSAssemblyCache::CacheCompiledAssembly(const FString& AssemblyName, MonoAssembly* CompiledAssembly)
    {
        if (!bIsInitialized.load(std::memory_order_acquire) || !CompiledAssembly)
        {
            return false;
        }

        FScopedOperationCounter OpCounter(*this);

        {
            FScopeLock CacheLock(&CacheMutex);
            CompiledAssemblies.Add(AssemblyName, CompiledAssembly);
        }

        UE_LOG(LogTemp, VeryVerbose, TEXT("ThreadSafeiOSAssemblyCache: Cached compiled assembly %s"), *AssemblyName);
        return true;
    }

    MonoAssembly* FThreadSafeiOSAssemblyCache::GetCompiledAssembly(const FString& AssemblyName)
    {
        if (!bIsInitialized.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        FScopeLock CacheLock(&CacheMutex);
        if (MonoAssembly** Found = CompiledAssemblies.Find(AssemblyName))
        {
            Stats.RecordHit();
            return *Found;
        }

        Stats.RecordMiss();
        return nullptr;
    }

    int32 FThreadSafeiOSAssemblyCache::CleanupExpiredEntries()
    {
        if (!bIsInitialized.load(std::memory_order_acquire))
        {
            return 0;
        }

        FScopedOperationCounter OpCounter(*this);
        int32 CleanedCount = 0;
        FDateTime ExpiryTime = FDateTime::UtcNow() - FTimespan::FromDays(Config.CacheExpiryDays);

        {
            FScopeLock CacheLock(&CacheMutex);
            
            // 清理内存缓存中过期的条目
            TArray<FString> ExpiredKeys;
            for (const auto& [Key, Entry] : MemoryCache)
            {
                if (Entry.CreatedTime < ExpiryTime && Entry.LastAccessTime < ExpiryTime)
                {
                    ExpiredKeys.Add(Key);
                }
            }

            for (const FString& Key : ExpiredKeys)
            {
                MemoryCache.Remove(Key);
                CleanedCount++;
            }
        }

        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Cleaned up %d expired entries"), CleanedCount);
        return CleanedCount;
    }

    FThreadSafeCacheStats FThreadSafeiOSAssemblyCache::GetCacheStatistics() const
    {
        return Stats; // 原子操作，线程安全
    }

    void FThreadSafeiOSAssemblyCache::ClearAllCaches()
    {
        if (!bIsInitialized.load(std::memory_order_acquire))
        {
            return;
        }

        std::lock_guard<std::mutex> Lock(OperationMutex);
        
        {
            FScopeLock CacheLock(&CacheMutex);
            MemoryCache.Empty();
            PersistentCacheIndex.Empty();
            CompiledAssemblies.Empty();
        }

        // 重置统计信息
        Stats = FThreadSafeCacheStats{};

        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Cleared all caches"));
    }

    double FThreadSafeiOSAssemblyCache::GetCacheSizeMB() const
    {
        FScopeLock CacheLock(&CacheMutex);
        
        double TotalSize = 0;
        for (const auto& [Key, Entry] : MemoryCache)
        {
            TotalSize += Entry.AssemblyData.Num();
        }
        
        return TotalSize / (1024.0 * 1024.0);
    }

    FString FThreadSafeiOSAssemblyCache::ExportDiagnosticsReport()
    {
        FString Report;
        FThreadSafeCacheStats CurrentStats = GetCacheStatistics();
        
        Report += TEXT("=== ThreadSafe iOS Assembly Cache Diagnostics ===\n");
        Report += FString::Printf(TEXT("Cache Hits: %d\n"), CurrentStats.CacheHits.load());
        Report += FString::Printf(TEXT("Cache Misses: %d\n"), CurrentStats.CacheMisses.load());
        Report += FString::Printf(TEXT("Hit Ratio: %.2f%%\n"), CurrentStats.GetHitRatio() * 100.0);
        Report += FString::Printf(TEXT("Average Access Time: %.2f ms\n"), CurrentStats.AverageCacheAccessTime.load());
        Report += FString::Printf(TEXT("Compression Savings: %d bytes\n"), CurrentStats.CompressionSavings.load());
        Report += FString::Printf(TEXT("Current Cache Size: %.2f MB\n"), GetCacheSizeMB());
        Report += FString::Printf(TEXT("Concurrent Operations: %d\n"), CurrentStats.ConcurrentOperations.load());
        
        {
            FScopeLock CacheLock(&CacheMutex);
            Report += FString::Printf(TEXT("Memory Cache Entries: %d\n"), MemoryCache.Num());
            Report += FString::Printf(TEXT("Persistent Cache Entries: %d\n"), PersistentCacheIndex.Num());
            Report += FString::Printf(TEXT("Compiled Assemblies: %d\n"), CompiledAssemblies.Num());
        }
        
        Report += TEXT("Cache Configuration:\n");
        Report += FString::Printf(TEXT("  Max Memory Cache: %d MB\n"), Config.MaxMemoryCacheSize);
        Report += FString::Printf(TEXT("  Max Persistent Cache: %d MB\n"), Config.MaxPersistentCacheSize);
        Report += FString::Printf(TEXT("  Cache Expiry: %d days\n"), Config.CacheExpiryDays);
        Report += FString::Printf(TEXT("  Compression Enabled: %s\n"), Config.bEnableCompression ? TEXT("Yes") : TEXT("No"));
        
        return Report;
    }

    FString FThreadSafeiOSAssemblyCache::CalculateContentHash(const TArray<uint8>& AssemblyData)
    {
        return FMD5::HashBytes(AssemblyData.GetData(), AssemblyData.Num());
    }

    bool FThreadSafeiOSAssemblyCache::CompressAssemblyDataInternal(const TArray<uint8>& OriginalData, TArray<uint8>& CompressedData)
    {
        int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, OriginalData.Num());
        CompressedData.SetNumUninitialized(CompressedSize);

        bool bSuccess = FCompression::CompressMemory(
            NAME_Zlib,
            CompressedData.GetData(),
            CompressedSize,
            OriginalData.GetData(),
            OriginalData.Num()
        );

        if (bSuccess)
        {
            CompressedData.SetNum(CompressedSize);
        }

        return bSuccess;
    }

    bool FThreadSafeiOSAssemblyCache::DecompressAssemblyDataInternal(const TArray<uint8>& CompressedData, TArray<uint8>& DecompressedData)
    {
        // 预估解压缩大小（压缩数据的3-5倍通常是安全的）
        int32 EstimatedSize = CompressedData.Num() * 4;
        DecompressedData.SetNumUninitialized(EstimatedSize);

        bool bSuccess = FCompression::UncompressMemory(
            NAME_Zlib,
            DecompressedData.GetData(),
            EstimatedSize,
            CompressedData.GetData(),
            CompressedData.Num()
        );

        if (bSuccess)
        {
            DecompressedData.SetNum(EstimatedSize);
        }

        return bSuccess;
    }

    void FThreadSafeiOSAssemblyCache::PerformLRUCleanup()
    {
        // LRU清理逻辑 - 删除最少访问和最旧的条目
        TArray<TPair<FString, FDateTime>> AccessTimes;
        
        for (const auto& [Key, Entry] : MemoryCache)
        {
            AccessTimes.Emplace(Key, Entry.LastAccessTime);
        }
        
        // 按访问时间排序
        AccessTimes.Sort([](const TPair<FString, FDateTime>& A, const TPair<FString, FDateTime>& B)
        {
            return A.Value < B.Value;
        });
        
        // 删除最旧的25%条目
        int32 ToRemove = FMath::Max(1, AccessTimes.Num() / 4);
        for (int32 i = 0; i < ToRemove; ++i)
        {
            MemoryCache.Remove(AccessTimes[i].Key);
        }
        
        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: LRU cleanup removed %d entries"), ToRemove);
    }

    int32 FThreadSafeiOSAssemblyCache::ValidateCacheIntegrity()
    {
        FScopedOperationCounter OpCounter(*this);
        int32 Issues = 0;
        
        FScopeLock CacheLock(&CacheMutex);
        
        // 验证内存缓存完整性
        for (const auto& [Key, Entry] : MemoryCache)
        {
            // 检查哈希值
            FString CalculatedHash = CalculateContentHash(Entry.AssemblyData);
            if (CalculatedHash != Entry.ContentHash)
            {
                UE_LOG(LogTemp, Error, TEXT("ThreadSafeiOSAssemblyCache: Hash mismatch for %s"), *Key);
                Issues++;
            }
            
            // 检查时间戳
            if (Entry.CreatedTime > Entry.LastAccessTime)
            {
                UE_LOG(LogTemp, Error, TEXT("ThreadSafeiOSAssemblyCache: Invalid timestamps for %s"), *Key);
                Issues++;
            }
        }
        
        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Cache integrity check found %d issues"), Issues);
        return Issues;
    }

    void FThreadSafeiOSAssemblyCache::UpdateConfiguration(const FThreadSafeCacheConfig& NewConfig)
    {
        std::lock_guard<std::mutex> Lock(OperationMutex);
        Config = NewConfig;
        
        UE_LOG(LogTemp, Log, TEXT("ThreadSafeiOSAssemblyCache: Configuration updated"));
    }
}

#endif // WITH_MONO_RUNTIME && PLATFORM_IOS