#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

/**
 * 增强的对象安全性验证器
 * 提供比标准IsValid()更严格的安全检查
 */
class UNREALSHARPCORE_API FCSObjectSafetyValidator
{
public:
    /**
     * 扩展的安全检查 - 比IsValid()更严格
     * @param Object 要检查的UObject
     * @return 对象是否安全可用
     */
    static bool IsObjectSafeForManagedAccess(const UObject* Object)
    {
        if (!Object)
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectSafetyValidator: Object is null"));
            return false;
        }

        // 基础低级有效性检查
        if (!Object->IsValidLowLevel())
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Object failed IsValidLowLevel check: %p"), Object);
            return false;
        }

        // 检查对象是否已标记为不可达
        if (Object->IsUnreachable())
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Object is unreachable: %s"), 
                   *Object->GetClass()->GetName());
            return false;
        }

        // 检查销毁相关标志
        if (Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Object is being/has been destroyed: %s"), 
                   *Object->GetClass()->GetName());
            return false;
        }

        // 检查是否在垃圾回收过程中
        if (Object->HasAnyFlags(RF_WillBeLoaded))
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectSafetyValidator: Object is being loaded, may be unsafe: %s"), 
                   *Object->GetClass()->GetName());
            // 不直接返回false，但记录潜在风险
        }

        // 检查类的有效性
        UClass* ObjectClass = Object->GetClass();
        if (!ObjectClass || !ObjectClass->IsValidLowLevel())
        {
            UE_LOG(LogTemp, Error, TEXT("CSObjectSafetyValidator: Object has invalid class: %p"), Object);
            return false;
        }

        // 对于Actor对象的额外检查
        if (const AActor* Actor = Cast<AActor>(Object))
        {
            if (!IsActorSafeForAccess(Actor))
            {
                return false;
            }
        }

        // 对于World对象的特殊检查
        if (const UWorld* World = Cast<UWorld>(Object))
        {
            if (!IsWorldSafeForAccess(World))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * 检查Actor的特定安全条件
     */
    static bool IsActorSafeForAccess(const AActor* Actor)
    {
        if (!Actor)
        {
            return false;
        }

        // 检查Actor是否正在被销毁
        if (Actor->IsActorBeingDestroyed())
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Actor is being destroyed: %s"), 
                   *Actor->GetName());
            return false;
        }

        // 检查Actor是否有效
        if (!IsValid(Actor))
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Actor is not valid: %s"), 
                   *Actor->GetName());
            return false;
        }

        // 检查World是否有效
        UWorld* World = Actor->GetWorld();
        if (!World || !IsValid(World))
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Actor's world is invalid: %s"), 
                   *Actor->GetName());
            return false;
        }

        return true;
    }

    /**
     * 检查World对象的安全性
     */
    static bool IsWorldSafeForAccess(const UWorld* World)
    {
        if (!World)
        {
            return false;
        }

        // 检查World是否正在清理
        if (World->bIsTearingDown)
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: World is tearing down"));
            return false;
        }

        // 检查World的状态
        if (World->WorldType == EWorldType::None)
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("CSObjectSafetyValidator: World type is None"));
            return false;
        }

        return true;
    }

    /**
     * 安全的对象访问包装器
     * @param Object 要访问的对象
     * @param AccessFunction 访问函数
     * @return 访问是否成功
     */
    template<typename ObjectType, typename FunctionType>
    static bool SafeObjectAccess(ObjectType* Object, FunctionType AccessFunction)
    {
        if (!IsObjectSafeForManagedAccess(Object))
        {
            return false;
        }

        // 在访问前再次检查，防止竞态条件
        if (!Object || !Object->IsValidLowLevel())
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Object became invalid during access"));
            return false;
        }

        try
        {
            AccessFunction(Object);
            return true;
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("CSObjectSafetyValidator: Exception occurred during object access"));
            return false;
        }
    }

    /**
     * 批量对象安全性检查
     * @param Objects 要检查的对象数组
     * @return 所有安全对象的数组
     */
    static TArray<UObject*> FilterSafeObjects(const TArray<UObject*>& Objects)
    {
        TArray<UObject*> SafeObjects;
        SafeObjects.Reserve(Objects.Num());

        for (UObject* Object : Objects)
        {
            if (IsObjectSafeForManagedAccess(Object))
            {
                SafeObjects.Add(Object);
            }
        }

        if (SafeObjects.Num() != Objects.Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("CSObjectSafetyValidator: Filtered out %d unsafe objects from %d total"), 
                   Objects.Num() - SafeObjects.Num(), Objects.Num());
        }

        return SafeObjects;
    }

    /**
     * 获取对象的安全状态描述
     */
    static FString GetObjectSafetyDescription(const UObject* Object)
    {
        if (!Object)
        {
            return TEXT("Object is null");
        }

        TArray<FString> Issues;

        if (!Object->IsValidLowLevel())
        {
            Issues.Add(TEXT("Failed IsValidLowLevel"));
        }

        if (Object->IsUnreachable())
        {
            Issues.Add(TEXT("Is unreachable"));
        }

        if (Object->HasAnyFlags(RF_BeginDestroyed))
        {
            Issues.Add(TEXT("Has RF_BeginDestroyed flag"));
        }

        if (Object->HasAnyFlags(RF_FinishDestroyed))
        {
            Issues.Add(TEXT("Has RF_FinishDestroyed flag"));
        }

        if (Issues.Num() == 0)
        {
            return FString::Printf(TEXT("Object %s appears safe"), *Object->GetClass()->GetName());
        }
        else
        {
            return FString::Printf(TEXT("Object %s has issues: %s"), 
                                 *Object->GetClass()->GetName(),
                                 *FString::Join(Issues, TEXT(", ")));
        }
    }
};