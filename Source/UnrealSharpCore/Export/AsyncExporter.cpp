#include "AsyncExporter.h"
#include "CSManagedDelegate.h"
#include "../GCOptimizations/CSHotReloadSafetyLock.h"

void UAsyncExporter::RunOnThread(TWeakObjectPtr<UObject> WorldContextObject, ENamedThreads::Type Thread, FGCHandleIntPtr DelegateHandle)
{
	AsyncTask(Thread, [WorldContextObject, DelegateHandle]()
	{
		// 使用热重载安全访问确保线程安全
		bool bAccessSuccess = FHotReloadSafetyLock::SafeManagedObjectAccess([&]()
		{
			FCSManagedDelegate ManagedDelegate = FGCHandle(DelegateHandle, GCHandleType::StrongHandle);
			
			if (!WorldContextObject.IsValid())
			{
				ManagedDelegate.Dispose();
				return;
			}
			
			ManagedDelegate.Invoke(WorldContextObject.Get());
		});

		if (!bAccessSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsyncExporter: Failed to safely access managed delegate during hot reload"));
			// 创建临时委托只为清理目的
			FCSManagedDelegate CleanupDelegate = FGCHandle(DelegateHandle, GCHandleType::StrongHandle);
			CleanupDelegate.Dispose();
		}
	});
}

int UAsyncExporter::GetCurrentNamedThread()
{
	return FTaskGraphInterface::Get().GetCurrentThreadIfKnown();
}