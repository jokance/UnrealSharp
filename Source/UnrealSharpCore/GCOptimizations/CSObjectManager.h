#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/GameInstance.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "../CSManagedGCHandle.h"

/**
 * 智能GC对象管理器
 * 根据UObject类型自动选择最优的GCHandle类型，防止内存泄漏
 */
class UNREALSHARPCORE_API UCSObjectManager
{
public:
    /**
     * 根据UObject类型智能确定最优的GCHandle类型
     * @param Object 要分析的UObject
     * @return 推荐的GCHandleType
     */
    static GCHandleType DetermineOptimalHandleType(const UObject* Object)
    {
        if (!Object || !Object->IsValidLowLevel())
        {
            return GCHandleType::Null;
        }

        // 系统关键对象 -> StrongHandle (需要保证生命周期)
        if (Object->IsA<UWorld>() || 
            Object->IsA<UGameInstance>() ||
            Object->IsA<UEngine>())
        {
            UE_LOG(LogTemp, Verbose, TEXT("CSObjectManager: Using StrongHandle for system object: %s"), 
                   *Object->GetClass()->GetName());
            return GCHandleType::StrongHandle;
        }

        // 静态/持久化对象 -> StrongHandle
        if (Object->HasAnyFlags(RF_MarkAsRootSet) ||
            Object->IsRooted())
        {
            UE_LOG(LogTemp, Verbose, TEXT("CSObjectManager: Using StrongHandle for rooted object: %s"), 
                   *Object->GetClass()->GetName());
            return GCHandleType::StrongHandle;
        }

        // 游戏逻辑对象 -> WeakHandle (避免阻止GC)
        if (Object->IsA<APawn>() || 
            Object->IsA<AController>() ||
            Object->IsA<AActor>())
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectManager: Using WeakHandle for game object: %s"), 
                   *Object->GetClass()->GetName());
            return GCHandleType::WeakHandle;
        }

        // 组件对象 -> WeakHandle (生命周期由Owner管理)
        if (Object->IsA<UActorComponent>())
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectManager: Using WeakHandle for component: %s"), 
                   *Object->GetClass()->GetName());
            return GCHandleType::WeakHandle;
        }

        // 资产对象 -> 根据是否在内存中决定
        if (Object->IsA<UObject>() && Object->IsAsset())
        {
            // 已加载的资产使用弱引用，让资产管理器控制生命周期
            return GCHandleType::WeakHandle;
        }

        // 默认弱引用 - 更安全的选择
        UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectManager: Using default WeakHandle for object: %s"), 
               *Object->GetClass()->GetName());
        return GCHandleType::WeakHandle;
    }

    /**
     * 创建智能GC句柄
     * @param Object 源UObject
     * @param TypeHandle 托管类型句柄
     * @param Error 错误输出
     * @return 优化的GC句柄
     */
    static FGCHandle CreateOptimizedHandle(const UObject* Object, void* TypeHandle, FString* Error = nullptr)
    {
        if (!Object)
        {
            if (Error) *Error = TEXT("Object is null");
            return FGCHandle::Null();
        }

        GCHandleType OptimalType = DetermineOptimalHandleType(Object);
        
        // 使用现有的托管回调创建句柄
        FGCHandle NewHandle = FCSManagedCallbacks::ManagedCallbacks.CreateNewManagedObject(
            const_cast<UObject*>(Object), TypeHandle, Error);
            
        if (!NewHandle.IsNull())
        {
            // 设置优化的句柄类型
            NewHandle.Type = OptimalType;
            
            UE_LOG(LogTemp, Verbose, TEXT("CSObjectManager: Created %s handle for %s"), 
                   OptimalType == GCHandleType::StrongHandle ? TEXT("Strong") : TEXT("Weak"),
                   *Object->GetClass()->GetName());
        }
        
        return NewHandle;
    }

    /**
     * 验证句柄类型是否最优
     * @param Object UObject实例
     * @param CurrentType 当前使用的句柄类型
     * @return 是否需要优化
     */
    static bool ShouldOptimizeHandleType(const UObject* Object, GCHandleType CurrentType)
    {
        if (!Object) return false;
        
        GCHandleType OptimalType = DetermineOptimalHandleType(Object);
        
        if (CurrentType != OptimalType)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectManager: Suboptimal handle type for %s - Current: %d, Optimal: %d"),
                   *Object->GetClass()->GetName(), (int32)CurrentType, (int32)OptimalType);
            return true;
        }
        
        return false;
    }

    /**
     * 获取句柄类型的描述性名称
     */
    static FString GetHandleTypeName(GCHandleType Type)
    {
        switch (Type)
        {
            case GCHandleType::StrongHandle: return TEXT("Strong");
            case GCHandleType::WeakHandle: return TEXT("Weak");
            case GCHandleType::PinnedHandle: return TEXT("Pinned");
            case GCHandleType::Null: return TEXT("Null");
            default: return TEXT("Unknown");
        }
    }
};