# UnrealSharp iOS Hot Reload Guide

UnrealSharp现在支持iOS平台上使用Mono的有限热更新功能。由于iOS系统和App Store的限制，iOS热更新有特殊的实现策略和限制。

## 🍎 iOS热更新策略

### 技术实现

UnrealSharp在iOS上使用**混合AOT+解释器模式**(`MONO_AOT_MODE_INTERP`)：

- **核心系统程序集**：预编译为AOT，包含在应用包中，无法热更新
- **游戏逻辑程序集**：可通过解释器模式实现有限的热更新
- **字节码替换**：通过Assembly.Load从内存加载新的程序集字节码

### 限制说明

| 组件类型 | 热更新支持 | 说明 |
|---------|-----------|------|
| 🟢 游戏逻辑代码 | ✅ 支持 | 可以热更新，但需要应用重启 |
| 🟡 UnrealSharp绑定 | ❌ 不支持 | 核心引擎绑定，AOT编译 |
| 🔴 系统程序集 | ❌ 不支持 | .NET核心库，AOT编译 |

## 📱 使用方法

### 1. 开发环境配置

```cpp
// 在UnrealSharp初始化时，iOS热更新会自动启用
#if PLATFORM_IOS && WITH_MONO_RUNTIME
    UnrealSharp::iOS::HotReload::InitializeHotReload();
#endif
```

### 2. 检查热更新支持

```cpp
#include "iOS/UnrealSharp_iOS_HotReload.h"

// 检查程序集是否可以热更新
bool bCanReload = UnrealSharp::iOS::HotReload::CanAssemblyBeHotReloaded(TEXT("MyGameLogic"));
```

### 3. 加载程序集字节码

```cpp
// 从内存加载程序集
TArray<uint8> AssemblyData; // 你的编译后字节码
bool bSuccess = UnrealSharp::iOS::HotReload::LoadAssemblyFromBytecode(
    TEXT("MyGameLogic"), 
    AssemblyData
);
```

### 4. 热更新程序集

```cpp
// 热更新现有程序集（会缓存到下次启动）
TArray<uint8> NewAssemblyData; // 新编译的字节码
bool bHotReloaded = UnrealSharp::iOS::HotReload::HotReloadAssembly(
    TEXT("MyGameLogic"), 
    NewAssemblyData
);
```

## 🔄 热更新工作流

### 开发阶段

1. **编写C#游戏逻辑代码**
2. **编译为字节码** (`.dll`文件)
3. **通过网络或文件加载新字节码**
4. **调用热更新API缓存新代码**
5. **重启应用以应用更改**

### 生产环境

```
⚠️ 注意：iOS热更新仅供开发使用，不能用于App Store发布版本
```

## 📂 文件结构

热更新系统会在以下位置缓存程序集：

```
📱 iOS设备 Documents/
└── UnrealSharp/
    └── HotReloadCache/
        ├── MyGameLogic.dll
        ├── GameplayScripts.dll
        └── CustomBehaviors.dll
```

## 🛠️ API参考

### 初始化和清理

```cpp
// 初始化热更新系统
UnrealSharp::iOS::HotReload::InitializeHotReload();

// 清理热更新系统
UnrealSharp::iOS::HotReload::ShutdownHotReload();
```

### 程序集管理

```cpp
// 从文件加载程序集
bool LoadAssemblyFromFile(const FString& AssemblyPath);

// 从字节码加载程序集  
bool LoadAssemblyFromBytecode(const FString& AssemblyName, const TArray<uint8>& BytecodeData);

// 热更新程序集
bool HotReloadAssembly(const FString& AssemblyName, const TArray<uint8>& NewBytecodeData);
```

### 查询功能

```cpp
// 检查程序集是否支持热更新
bool CanAssemblyBeHotReloaded(const FString& AssemblyName);

// 获取已加载的程序集列表
TArray<FString> GetLoadedAssemblies();
```

## ⚠️ 重要限制

### 1. App Store政策
- **禁止动态代码执行**：热更新不能包含原生代码
- **禁止绕过应用审核**：不能下载并执行未审核的代码
- **开发版本限定**：仅用于开发和内部测试

### 2. 技术限制
- **核心程序集不可更新**：系统程序集和UnrealSharp绑定无法热更新
- **需要应用重启**：真正的运行时热更新受限，通常需要重启
- **性能影响**：解释器模式性能低于AOT编译代码

### 3. 调试限制
- **调试器支持有限**：解释器模式的调试能力受限
- **断点功能**：可能无法在热更新的代码中设置断点
- **性能分析**：热更新代码的性能分析工具支持有限

## 🎯 最佳实践

### 1. 代码组织
```csharp
// ✅ 推荐：将游戏逻辑分离到独立程序集
namespace MyGame.Logic
{
    public class GameplayManager : MonoBehaviour
    {
        // 可热更新的游戏逻辑
    }
}

// ❌ 避免：在核心绑定中混入游戏逻辑
```

### 2. 错误处理
```cpp
// 始终检查热更新结果
if (!UnrealSharp::iOS::HotReload::CanAssemblyBeHotReloaded(AssemblyName))
{
    UE_LOG(LogTemp, Warning, TEXT("Assembly %s cannot be hot reloaded on iOS"), *AssemblyName);
    return false;
}
```

### 3. 缓存管理
- 定期清理热更新缓存避免占用过多存储空间
- 实现版本控制避免加载不兼容的程序集
- 提供回退机制以防热更新失败

## 🔍 故障排除

### 常见问题

1. **"Assembly cannot be hot reloaded"**
   - 检查程序集是否为核心系统程序集
   - 使用`CanAssemblyBeHotReloaded()`验证

2. **"Failed to load assembly from bytecode"**
   - 验证字节码数据完整性
   - 检查程序集是否与当前Mono版本兼容

3. **"Hot reload requires app restart"**
   - iOS限制：这是正常行为
   - 重启应用以应用更改

### 调试技巧

```cpp
// 启用详细日志
UE_LOG(LogTemp, Log, TEXT("Hot reload assemblies: %s"), 
       *FString::Join(UnrealSharp::iOS::HotReload::GetLoadedAssemblies(), TEXT(", ")));

// 检查平台支持
if (UnrealSharp::Platform::IsHotReloadSupported())
{
    FString Limitations = UnrealSharp::Platform::GetHotReloadLimitations();
    UE_LOG(LogTemp, Log, TEXT("Hot reload limitations: %s"), *Limitations);
}
```

## 📚 进阶主题

### 自定义热更新服务器

```cpp
// 示例：从HTTP服务器下载热更新
class FiOSHotReloadService 
{
public:
    void DownloadAndApplyUpdate(const FString& UpdateURL)
    {
        // 1. 下载新的程序集字节码
        // 2. 验证字节码签名和版本
        // 3. 应用热更新
        // 4. 通知用户重启应用
    }
};
```

### 增量更新策略

考虑实现增量更新以减少下载大小：
- 使用程序集版本控制
- 仅更新变更的程序集
- 实现程序集依赖检查

---

**总结：** iOS热更新为开发阶段提供了有限但有用的代码更新能力。虽然受到平台限制，但对于快速迭代游戏逻辑仍然很有价值。记住始终遵守App Store政策，不要在发布版本中使用热更新功能。