#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <mutex>

/**
 * 热重载安全锁系统
 * 防止热重载期间的并发访问问题，确保托管对象访问的线程安全性
 */
class UNREALSHARPCORE_API FHotReloadSafetyLock
{
private:
    // 原子布尔值跟踪热重载状态
    static std::atomic<bool> bIsHotReloading;
    
    // 互斥锁保护关键操作
    static std::mutex HotReloadMutex;
    
    // 等待热重载完成的线程计数
    static std::atomic<int32> WaitingThreads;

public:
    /**
     * 检查当前是否可以安全访问托管对象
     * @return true如果可以安全访问，false如果正在热重载
     */
    static bool CanAccessManagedObjects()
    {
        return !bIsHotReloading.load(std::memory_order_acquire);
    }

    /**
     * 获取热重载状态
     */
    static bool IsHotReloading()
    {
        return bIsHotReloading.load(std::memory_order_acquire);
    }

    /**
     * 等待热重载完成（带超时）
     * @param TimeoutMs 超时时间（毫秒）
     * @return true如果热重载完成，false如果超时
     */
    static bool WaitForHotReloadCompletion(int32 TimeoutMs = 5000)
    {
        if (!IsHotReloading())
        {
            return true;
        }

        WaitingThreads.fetch_add(1, std::memory_order_relaxed);

        const auto StartTime = FDateTime::UtcNow();
        const auto TimeoutTime = StartTime + FTimespan::FromMilliseconds(TimeoutMs);

        while (IsHotReloading() && FDateTime::UtcNow() < TimeoutTime)
        {
            FPlatformProcess::Sleep(0.01f); // 10ms休眠
        }

        WaitingThreads.fetch_sub(1, std::memory_order_relaxed);

        if (IsHotReloading())
        {
            UE_LOG(LogTemp, Warning, TEXT("HotReloadSafetyLock: Timeout waiting for hot reload completion"));
            return false;
        }

        return true;
    }

    /**
     * 获取等待线程数量
     */
    static int32 GetWaitingThreadCount()
    {
        return WaitingThreads.load(std::memory_order_relaxed);
    }

    /**
     * RAII风格的热重载锁
     * 构造时启用热重载锁，析构时自动释放
     */
    class FScopedHotReloadLock
    {
    private:
        bool bWasAlreadyLocked;

    public:
        FScopedHotReloadLock()
        {
            std::lock_guard<std::mutex> Lock(HotReloadMutex);
            
            bWasAlreadyLocked = bIsHotReloading.load(std::memory_order_relaxed);
            
            if (!bWasAlreadyLocked)
            {
                bIsHotReloading.store(true, std::memory_order_release);
                UE_LOG(LogTemp, Log, TEXT("HotReloadSafetyLock: Hot reload lock acquired"));
                
                // 等待所有等待线程得到通知
                FPlatformProcess::Sleep(0.001f); // 给其他线程一个机会检查状态
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("HotReloadSafetyLock: Hot reload already in progress"));
            }
        }

        ~FScopedHotReloadLock()
        {
            if (!bWasAlreadyLocked)
            {
                std::lock_guard<std::mutex> Lock(HotReloadMutex);
                bIsHotReloading.store(false, std::memory_order_release);
                
                UE_LOG(LogTemp, Log, TEXT("HotReloadSafetyLock: Hot reload lock released"));
                
                // 通知等待的线程
                if (GetWaitingThreadCount() > 0)
                {
                    UE_LOG(LogTemp, Log, TEXT("HotReloadSafetyLock: Notifying %d waiting threads"), 
                           GetWaitingThreadCount());
                }
            }
        }

        // 禁用拷贝和移动
        FScopedHotReloadLock(const FScopedHotReloadLock&) = delete;
        FScopedHotReloadLock(FScopedHotReloadLock&&) = delete;
        FScopedHotReloadLock& operator=(const FScopedHotReloadLock&) = delete;
        FScopedHotReloadLock& operator=(FScopedHotReloadLock&&) = delete;
    };

    /**
     * 安全的托管对象访问包装器
     * 确保只在非热重载期间执行操作
     */
    template<typename FunctionType>
    static bool SafeManagedObjectAccess(FunctionType&& AccessFunction, int32 TimeoutMs = 1000)
    {
        if (!WaitForHotReloadCompletion(TimeoutMs))
        {
            UE_LOG(LogTemp, Error, TEXT("HotReloadSafetyLock: Failed to acquire safe access within timeout"));
            return false;
        }

        // 双重检查以防竞态条件
        if (IsHotReloading())
        {
            UE_LOG(LogTemp, Warning, TEXT("HotReloadSafetyLock: Hot reload started during access attempt"));
            return false;
        }

        try
        {
            AccessFunction();
            return true;
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("HotReloadSafetyLock: Exception during managed object access"));
            return false;
        }
    }

    /**
     * 强制释放热重载锁（紧急情况使用）
     */
    static void ForceReleaseLock()
    {
        std::lock_guard<std::mutex> Lock(HotReloadMutex);
        if (bIsHotReloading.load())
        {
            UE_LOG(LogTemp, Warning, TEXT("HotReloadSafetyLock: Force releasing hot reload lock"));
            bIsHotReloading.store(false, std::memory_order_release);
        }
    }

    /**
     * 获取锁状态的调试信息
     */
    static FString GetLockStatusDescription()
    {
        return FString::Printf(TEXT("HotReload: %s, Waiting Threads: %d"), 
                             IsHotReloading() ? TEXT("Active") : TEXT("Inactive"),
                             GetWaitingThreadCount());
    }
};

// 静态成员初始化
std::atomic<bool> FHotReloadSafetyLock::bIsHotReloading{false};
std::mutex FHotReloadSafetyLock::HotReloadMutex;
std::atomic<int32> FHotReloadSafetyLock::WaitingThreads{0};