# iOS 无重启热更新技术指南

UnrealSharp 现在支持 iOS 平台上**真正的无重启热更新**！这是通过 Mono 的高级特性实现的革命性功能，让你可以在运行中实时修改代码，无需重启应用程序。

## 🚀 核心技术

### 技术原理

我们的无重启热更新系统基于三个核心技术：

1. **方法体替换 (Method Body Replacement)**
   ```cpp
   // 运行时替换方法实现
   mono_method_set_unmanaged_thunk(OriginalMethod, NewCompiledCode);
   ```

2. **程序集上下文切换 (Assembly Load Context)**
   ```cpp
   // 创建隔离的热更新上下文
   MonoDomain* HotReloadDomain = mono_domain_create_appdomain("HotReload", NULL);
   mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
   ```

3. **混合 AOT+解释器模式**
   ```cpp
   // 核心程序集使用AOT，新代码使用解释器
   mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
   ```

## ⚡ 热更新方式对比

| 更新方式 | 重启需求 | 更新速度 | 状态保持 | 适用场景 |
|----------|----------|----------|----------|----------|
| 🟢 **方法热替换** | ❌ 无需重启 | ⚡ 毫秒级 | ✅ 完全保持 | 逻辑调试、参数调整 |
| 🟡 **程序集热加载** | ❌ 无需重启 | ⚡ 秒级 | ✅ 基本保持 | 功能增强、类修改 |
| 🔴 **传统热更新** | ✅ 需要重启 | ⏱️ 分钟级 | ❌ 状态丢失 | 结构性变更 |

## 🔧 API 使用指南

### 1. 初始化系统

```cpp
#include "iOS/UnrealSharp_iOS_InterpreterHotReload.h"

// 在游戏启动时初始化
bool bSuccess = UnrealSharp::iOS::InterpreterHotReload::InitializeInterpreterHotReload();
if (bSuccess)
{
    UE_LOG(LogTemp, Log, TEXT("iOS无重启热更新系统已就绪！"));
}
```

### 2. 方法级热更新（最快）

```cpp
// 实时替换单个方法 - 毫秒级生效
FString NewCode = TEXT(R"(
    public void UpdatedMethod()
    {
        Console.WriteLine("这是热更新后的方法！");
        // 新的逻辑代码
    }
)");

bool bUpdated = UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
    TEXT("MyGameClass"),    // 类名
    TEXT("UpdatedMethod"),  // 方法名
    NewCode                 // 新的C#代码
);
```

### 3. 程序集级热更新

```cpp
// 热更新整个程序集 - 秒级生效
TArray<uint8> NewAssemblyData = LoadNewAssemblyFromNetwork();

bool bReloaded = UnrealSharp::iOS::InterpreterHotReload::HotReloadAssembly(
    TEXT("GameLogic"),      // 程序集名称
    NewAssemblyData         // 新的程序集数据
);
```

### 4. 文件热更新（开发模式）

```cpp
// 从文件热更新 - 适合开发调试
bool bSuccess = UnrealSharp::iOS::InterpreterHotReload::HotReloadFromFile(
    TEXT("/path/to/updated/GameLogic.dll")
);
```

### 5. 会话管理和回滚

```cpp
// 开始热更新会话
FString SessionId = UnrealSharp::iOS::InterpreterHotReload::StartHotReloadSession();

// ... 进行多次热更新操作 ...

// 如果有问题，可以回滚整个会话的所有更改
bool bRolledBack = UnrealSharp::iOS::InterpreterHotReload::RollbackHotReloadSession(SessionId);
```

## 🎮 游戏中的应用场景

### 场景1：实时游戏平衡调整

```cpp
// 无重启调整游戏参数
FString BalanceUpdate = TEXT(R"(
    public class WeaponBalance 
    {
        public static int GetDamage() 
        { 
            return 150;  // 从100调整为150
        }
        
        public static float GetFireRate() 
        { 
            return 0.8f; // 降低射速
        }
    }
)");

UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
    TEXT("WeaponBalance"), TEXT("GetDamage"), BalanceUpdate
);

// 立即生效，无需重启！玩家可以马上体验新的武器平衡
```

### 场景2：实时UI逻辑修复

```cpp
// 修复UI交互逻辑
FString UIFix = TEXT(R"(
    public void OnButtonClick() 
    {
        // 修复了按钮重复点击的bug
        if (IsProcessing) return;
        
        IsProcessing = true;
        ProcessButtonAction();
        IsProcessing = false;
    }
)");

UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
    TEXT("MainMenuUI"), TEXT("OnButtonClick"), UIFix
);
```

### 场景3：实时AI行为调整

```cpp
// 调整AI行为模式
FString AIUpdate = TEXT(R"(
    public class EnemyAI 
    {
        public void UpdateBehavior() 
        {
            // 更智能的AI行为
            if (PlayerDistance < 10f) 
            {
                AttackPlayer();
            }
            else if (PlayerDistance < 20f) 
            {
                MoveTowardPlayer();
            }
            else 
            {
                Patrol(); // 新增巡逻行为
            }
        }
    }
)");

UnrealSharp::iOS::InterpreterHotReload::HotReloadAssembly(TEXT("AILogic"), CompileToAssembly(AIUpdate));
```

## 📊 性能监控

### 热更新统计信息

```cpp
// 获取系统性能统计
auto Stats = UnrealSharp::iOS::InterpreterHotReload::GetHotReloadSystemStats();

UE_LOG(LogTemp, Log, TEXT("热更新统计："));
UE_LOG(LogTemp, Log, TEXT("- 活跃会话：%d"), Stats.ActiveSessions);
UE_LOG(LogTemp, Log, TEXT("- 方法替换数：%d"), Stats.TotalMethodReplacements);
UE_LOG(LogTemp, Log, TEXT("- 程序集重载数：%d"), Stats.TotalAssemblyReloads);
UE_LOG(LogTemp, Log, TEXT("- 系统就绪：%s"), Stats.bSystemReady ? TEXT("是") : TEXT("否"));
```

### 实时性能日志

```cpp
// 记录详细性能信息
UnrealSharp::iOS::InterpreterHotReload::LogHotReloadPerformance();
```

## 🎯 最佳实践

### 1. 热更新优先级

```cpp
// 推荐的热更新策略
enum class EHotReloadPriority 
{
    Critical,    // 游戏崩溃修复 - 立即应用
    High,        // 游戏性问题 - 几分钟内应用
    Medium,      // 平衡性调整 - 下次匹配应用
    Low          // 优化改进 - 下次更新应用
};
```

### 2. 状态保护模式

```cpp
// 在关键操作期间暂停热更新
class HotReloadSafetyGuard 
{
public:
    HotReloadSafetyGuard() 
    {
        // 暂停热更新以保护游戏状态
        UnrealSharp::iOS::InterpreterHotReload::PauseHotReload();
    }
    
    ~HotReloadSafetyGuard() 
    {
        // 恢复热更新
        UnrealSharp::iOS::InterpreterHotReload::ResumeHotReload();
    }
};

// 使用示例
void CriticalGameOperation() 
{
    HotReloadSafetyGuard Guard; // 自动保护
    // 执行关键操作...
} // 析构时自动恢复
```

### 3. 增量更新策略

```cpp
// 仅更新变化的方法，减少性能影响
TArray<FString> ChangedMethods = DetectChangedMethods(OldAssembly, NewAssembly);

for (const FString& MethodName : ChangedMethods)
{
    UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
        ClassName, MethodName, GetNewMethodCode(MethodName)
    );
}
```

## ⚠️ 限制和注意事项

### 技术限制

1. **核心系统程序集**
   - ❌ UnrealSharp.Core 不可热更新（AOT编译）
   - ❌ 系统绑定不可修改
   - ✅ 游戏逻辑代码可以热更新

2. **结构性变更限制**
   - ✅ 方法体修改 - 完全支持
   - 🟡 添加新方法 - 部分支持
   - 🟡 添加新类 - 需要程序集热加载
   - ❌ 修改继承关系 - 不支持

3. **性能考虑**
   - 解释器模式比AOT慢20-50%
   - 热更新操作本身耗时1-100毫秒
   - 内存使用增加（维护多个上下文）

### 安全限制

```cpp
// App Store发布限制
#if !DEVELOPMENT_BUILD
    #error "无重启热更新功能仅限开发版本，发布版本禁用"
#endif
```

## 🔬 高级用法

### 1. 自定义热更新服务器

```cpp
class HotReloadServer 
{
public:
    void StartListening(int32 Port = 9999) 
    {
        // 监听热更新请求
        HttpServer->BindUFunction(this, TEXT("HandleHotReloadRequest"), TEXT("/hotreload"));
    }
    
    UFUNCTION()
    void HandleHotReloadRequest(FString MethodName, FString NewCode) 
    {
        // 处理远程热更新请求
        UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
            TEXT("RemoteUpdate"), MethodName, NewCode
        );
    }
};
```

### 2. 热更新版本控制

```cpp
struct FHotReloadVersion 
{
    int32 Major;
    int32 Minor; 
    int32 Patch;
    FString Hash;
};

bool ApplyHotReloadIfCompatible(const FHotReloadVersion& Version, const TArray<uint8>& Data)
{
    if (IsVersionCompatible(GetCurrentVersion(), Version))
    {
        return UnrealSharp::iOS::InterpreterHotReload::HotReloadAssembly(TEXT("GameLogic"), Data);
    }
    return false;
}
```

### 3. A/B测试热更新

```cpp
// 随机选择热更新版本进行A/B测试
void ApplyABTestHotReload() 
{
    if (FMath::RandBool()) 
    {
        // 版本A：保守的游戏平衡
        UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
            TEXT("GameBalance"), TEXT("GetDifficulty"), ConservativeBalanceCode
        );
    }
    else 
    {
        // 版本B：激进的游戏平衡
        UnrealSharp::iOS::InterpreterHotReload::HotReloadMethod(
            TEXT("GameBalance"), TEXT("GetDifficulty"), AggressiveBalanceCode
        );
    }
}
```

## 🎉 总结

iOS无重启热更新技术为游戏开发带来了革命性的改变：

### ✅ 主要优势

- **🚀 即时生效**：方法级更新毫秒级生效
- **💾 状态保持**：玩家游戏状态完全保留
- **🔧 灵活调试**：实时调整参数和逻辑
- **⚡ 快速迭代**：无需重启即可测试修改
- **🛡️ 安全回滚**：可以随时撤销更改

### 🎯 适用场景

- **游戏平衡调整**：实时调整数值参数
- **Bug紧急修复**：立即修复游戏崩溃问题
- **功能快速验证**：快速验证新功能想法
- **A/B测试**：实时切换不同版本进行测试
- **用户体验优化**：根据反馈即时优化

### 🚀 未来展望

无重启热更新技术将继续发展，未来可能支持：
- 更复杂的结构性变更
- 更高的性能（接近AOT性能）
- 更好的调试工具集成
- 云端热更新服务

---

**现在就开始体验iOS无重启热更新的强大功能吧！让你的游戏开发效率提升到一个全新的水平！** 🎮✨