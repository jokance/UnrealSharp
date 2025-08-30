#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpragma-once-outside-header"
#endif
#pragma once
#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

#include "UnrealSharpCore.h"
#include "CoreMinimal.h"
#include "CSManager.h"
#include "Modules/ModuleManager.h"
#include "TypeGenerator/Properties/PropertyGeneratorManager.h"
#include "HotReload/UnrealSharp_UnifiedHotReload.h"

#define LOCTEXT_NAMESPACE "FUnrealSharpCoreModule"

DEFINE_LOG_CATEGORY(LogUnrealSharp);

void FUnrealSharpCoreModule::StartupModule()
{
	FPropertyGeneratorManager::Init();
	
	// Initialize the C# runtime
	UCSManager& CSManager = UCSManager::GetOrCreate();
	CSManager.Initialize();
	
	// Initialize the unified hot reload system
	UnrealSharp::Platform::InitializeHotReloadSystem();
}

void FUnrealSharpCoreModule::ShutdownModule()
{
	// Shutdown the unified hot reload system
	UnrealSharp::Platform::ShutdownHotReloadSystem();
	
	UCSManager::Shutdown();
	FPropertyGeneratorManager::Shutdown();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealSharpCoreModule, UnrealSharpCore)