#include "CoreMinimal.h"

#if WITH_MONO_RUNTIME

#include "mono/jit/jit.h"
#include "mono/metadata/assembly.h"
#include "mono/metadata/exception.h"
#include "mono/metadata/debug-helpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Engine/Engine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

/**
 * Enhanced Diagnostics and Error Handling System for UnrealSharp
 * 
 * Provides comprehensive error tracking, diagnostic information, and user-friendly error reporting
 * across all platforms with intelligent error categorization and resolution suggestions.
 */

namespace UnrealSharp::Diagnostics
{
    // Error categories for better classification
    enum class EErrorCategory
    {
        BuildError,
        RuntimeError,
        HotReloadError,
        PlatformError,
        DependencyError,
        ConfigurationError,
        MemoryError,
        NetworkError,
        Unknown
    };

    // Diagnostic entry structure
    struct FDiagnosticEntry
    {
        FString ErrorCode;
        EErrorCategory Category;
        FString Message;
        FString DetailedDescription;
        FString SuggestedResolution;
        FString StackTrace;
        FDateTime Timestamp;
        FString Platform;
        FString Context;
        int32 Severity; // 1-5, where 5 is critical
        bool bIsResolved;
        TArray<FString> RelatedFiles;
        TMap<FString, FString> AdditionalData;

        FDiagnosticEntry()
            : Category(EErrorCategory::Unknown)
            , Timestamp(FDateTime::Now())
            , Severity(3)
            , bIsResolved(false)
        {}
    };

    // Diagnostics system state
    struct FDiagnosticsSystemState
    {
        TArray<FDiagnosticEntry> DiagnosticHistory;
        TMap<FString, int32> ErrorFrequency;
        TMap<EErrorCategory, int32> CategoryCounts;
        FString LogFilePath;
        bool bIsInitialized;
        bool bEnableDetailedLogging;
        bool bShowUserNotifications;
        int32 MaxHistoryEntries;

        FDiagnosticsSystemState()
            : bIsInitialized(false)
            , bEnableDetailedLogging(true)
            , bShowUserNotifications(true)
            , MaxHistoryEntries(1000)
        {}
    };

    static FDiagnosticsSystemState DiagnosticsState;

    // Error code to category mapping
    static TMap<FString, EErrorCategory> ErrorCodeCategories = {
        {TEXT("US_BUILD_001"), EErrorCategory::BuildError},
        {TEXT("US_BUILD_002"), EErrorCategory::BuildError},
        {TEXT("US_RUNTIME_001"), EErrorCategory::RuntimeError},
        {TEXT("US_RUNTIME_002"), EErrorCategory::RuntimeError},
        {TEXT("US_HOTRELOAD_001"), EErrorCategory::HotReloadError},
        {TEXT("US_HOTRELOAD_002"), EErrorCategory::HotReloadError},
        {TEXT("US_PLATFORM_001"), EErrorCategory::PlatformError},
        {TEXT("US_PLATFORM_002"), EErrorCategory::PlatformError},
        {TEXT("US_DEPENDENCY_001"), EErrorCategory::DependencyError},
        {TEXT("US_CONFIG_001"), EErrorCategory::ConfigurationError},
        {TEXT("US_MEMORY_001"), EErrorCategory::MemoryError},
        {TEXT("US_NETWORK_001"), EErrorCategory::NetworkError}
    };

    // Predefined error resolutions
    static TMap<FString, FString> ErrorResolutions = {
        {TEXT("US_BUILD_001"), TEXT("Ensure .NET 9 SDK is installed and in PATH. Run 'dotnet --version' to verify.")},
        {TEXT("US_BUILD_002"), TEXT("Check that all required dependencies are available. Run platform detection to identify missing components.")},
        {TEXT("US_RUNTIME_001"), TEXT("Verify Mono runtime is properly initialized. Check UnrealSharp module load order.")},
        {TEXT("US_RUNTIME_002"), TEXT("Assembly loading failed. Verify assembly file exists and is not corrupted.")},
        {TEXT("US_HOTRELOAD_001"), TEXT("Hot reload failed. Check that the assembly is registered and method signatures match.")},
        {TEXT("US_HOTRELOAD_002"), TEXT("JIT compilation failed. Verify code syntax and try rebuilding the assembly.")},
        {TEXT("US_PLATFORM_001"), TEXT("Platform not supported. Check supported platform list and update if necessary.")},
        {TEXT("US_PLATFORM_002"), TEXT("Platform-specific libraries missing. Install required SDKs and tools.")},
        {TEXT("US_DEPENDENCY_001"), TEXT("Required dependency missing. Check installation and PATH environment variables.")},
        {TEXT("US_CONFIG_001"), TEXT("Configuration error. Review settings and verify all paths are correct.")},
        {TEXT("US_MEMORY_001"), TEXT("Memory allocation failed. Check available system memory and try reducing memory usage.")},
        {TEXT("US_NETWORK_001"), TEXT("Network operation failed. Check connectivity and firewall settings.")}
    };

    /**
     * Initialize diagnostics system
     */
    bool InitializeDiagnosticsSystem()
    {
        if (DiagnosticsState.bIsInitialized)
        {
            return true;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Diagnostics: Initializing diagnostics system"));

        // Setup log file path
        FString LogDir = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("UnrealSharp"));
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(*LogDir);
        
        DiagnosticsState.LogFilePath = FPaths::Combine(LogDir, TEXT("UnrealSharp_Diagnostics.log"));

        // Load existing diagnostics history
        LoadDiagnosticsHistory();

        DiagnosticsState.bIsInitialized = true;

        // Log initialization success
        LogDiagnosticEvent(TEXT("US_INIT_001"), TEXT("Diagnostics system initialized successfully"), 1);

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Diagnostics: System initialized, log file: %s"), *DiagnosticsState.LogFilePath);
        return true;
    }

    /**
     * Get error category from error code
     */
    EErrorCategory GetErrorCategory(const FString& ErrorCode)
    {
        if (EErrorCategory* Category = ErrorCodeCategories.Find(ErrorCode))
        {
            return *Category;
        }
        return EErrorCategory::Unknown;
    }

    /**
     * Get category name as string
     */
    FString GetCategoryName(EErrorCategory Category)
    {
        switch (Category)
        {
            case EErrorCategory::BuildError: return TEXT("Build");
            case EErrorCategory::RuntimeError: return TEXT("Runtime");
            case EErrorCategory::HotReloadError: return TEXT("Hot Reload");
            case EErrorCategory::PlatformError: return TEXT("Platform");
            case EErrorCategory::DependencyError: return TEXT("Dependency");
            case EErrorCategory::ConfigurationError: return TEXT("Configuration");
            case EErrorCategory::MemoryError: return TEXT("Memory");
            case EErrorCategory::NetworkError: return TEXT("Network");
            default: return TEXT("Unknown");
        }
    }

    /**
     * Log diagnostic event with enhanced information
     */
    void LogDiagnosticEvent(const FString& ErrorCode, const FString& Message, int32 Severity, 
                           const FString& Context, const TArray<FString>& RelatedFiles,
                           const TMap<FString, FString>& AdditionalData)
    {
        if (!DiagnosticsState.bIsInitialized)
        {
            InitializeDiagnosticsSystem();
        }

        // Create diagnostic entry
        FDiagnosticEntry Entry;
        Entry.ErrorCode = ErrorCode;
        Entry.Category = GetErrorCategory(ErrorCode);
        Entry.Message = Message;
        Entry.Severity = FMath::Clamp(Severity, 1, 5);
        Entry.Context = Context;
        Entry.Platform = FString(FPlatformProperties::PlatformName());
        Entry.RelatedFiles = RelatedFiles;
        Entry.AdditionalData = AdditionalData;
        
        // Get suggested resolution
        if (FString* Resolution = ErrorResolutions.Find(ErrorCode))
        {
            Entry.SuggestedResolution = *Resolution;
        }
        else
        {
            Entry.SuggestedResolution = TEXT("No specific resolution available. Check logs and documentation.");
        }

        // Generate detailed description based on category
        Entry.DetailedDescription = GenerateDetailedDescription(Entry);

        // Capture stack trace for runtime errors
        if (Entry.Category == EErrorCategory::RuntimeError && Entry.Severity >= 4)
        {
            Entry.StackTrace = CaptureStackTrace();
        }

        // Add to history
        DiagnosticsState.DiagnosticHistory.Add(Entry);

        // Update statistics
        DiagnosticsState.ErrorFrequency.FindOrAdd(ErrorCode, 0)++;
        DiagnosticsState.CategoryCounts.FindOrAdd(Entry.Category, 0)++;

        // Limit history size
        if (DiagnosticsState.DiagnosticHistory.Num() > DiagnosticsState.MaxHistoryEntries)
        {
            DiagnosticsState.DiagnosticHistory.RemoveAt(0);
        }

        // Log to UE4/UE5 log system
        ELogVerbosity::Type LogLevel = GetLogLevel(Severity);
        UE_LOG(LogTemp, LogLevel, TEXT("UnrealSharp [%s|%s]: %s"), 
               *ErrorCode, *GetCategoryName(Entry.Category), *Message);

        // Write to diagnostics log file
        if (DiagnosticsState.bEnableDetailedLogging)
        {
            WriteDiagnosticToFile(Entry);
        }

        // Show user notification for high severity errors
        if (DiagnosticsState.bShowUserNotifications && Severity >= 4)
        {
            ShowUserNotification(Entry);
        }
    }

    /**
     * Simplified logging function
     */
    void LogDiagnosticEvent(const FString& ErrorCode, const FString& Message, int32 Severity)
    {
        LogDiagnosticEvent(ErrorCode, Message, Severity, TEXT(""), TArray<FString>(), TMap<FString, FString>());
    }

    /**
     * Generate detailed description based on error information
     */
    FString GenerateDetailedDescription(const FDiagnosticEntry& Entry)
    {
        FString Description = Entry.Message;
        
        if (!Entry.Context.IsEmpty())
        {
            Description += FString::Printf(TEXT("\nContext: %s"), *Entry.Context);
        }

        if (!Entry.Platform.IsEmpty())
        {
            Description += FString::Printf(TEXT("\nPlatform: %s"), *Entry.Platform);
        }

        if (Entry.RelatedFiles.Num() > 0)
        {
            Description += TEXT("\nRelated Files:");
            for (const FString& File : Entry.RelatedFiles)
            {
                Description += FString::Printf(TEXT("\n  - %s"), *File);
            }
        }

        if (Entry.AdditionalData.Num() > 0)
        {
            Description += TEXT("\nAdditional Information:");
            for (const auto& Data : Entry.AdditionalData)
            {
                Description += FString::Printf(TEXT("\n  %s: %s"), *Data.Key, *Data.Value);
            }
        }

        return Description;
    }

    /**
     * Capture stack trace for debugging
     */
    FString CaptureStackTrace()
    {
        // Simplified stack trace capture
        // In a real implementation, this would use platform-specific APIs
        return TEXT("Stack trace capture not implemented in this demo");
    }

    /**
     * Get appropriate log level for severity
     */
    ELogVerbosity::Type GetLogLevel(int32 Severity)
    {
        switch (Severity)
        {
            case 1: return ELogVerbosity::VeryVerbose;
            case 2: return ELogVerbosity::Verbose;
            case 3: return ELogVerbosity::Log;
            case 4: return ELogVerbosity::Warning;
            case 5: return ELogVerbosity::Error;
            default: return ELogVerbosity::Log;
        }
    }

    /**
     * Write diagnostic entry to file
     */
    void WriteDiagnosticToFile(const FDiagnosticEntry& Entry)
    {
        FString LogLine = FString::Printf(
            TEXT("[%s] [%s|%s|%d] %s\n"),
            *Entry.Timestamp.ToString(),
            *Entry.ErrorCode,
            *GetCategoryName(Entry.Category),
            Entry.Severity,
            *Entry.Message
        );

        if (!Entry.DetailedDescription.IsEmpty())
        {
            LogLine += FString::Printf(TEXT("Details: %s\n"), *Entry.DetailedDescription);
        }

        if (!Entry.SuggestedResolution.IsEmpty())
        {
            LogLine += FString::Printf(TEXT("Resolution: %s\n"), *Entry.SuggestedResolution);
        }

        LogLine += TEXT("---\n");

        FFileHelper::SaveStringToFile(LogLine, *DiagnosticsState.LogFilePath, 
                                     FFileHelper::EEncodingOptions::AutoDetect, 
                                     &IFileManager::Get(), FILEWRITE_Append);
    }

    /**
     * Show user notification for important errors
     */
    void ShowUserNotification(const FDiagnosticEntry& Entry)
    {
        if (!GEngine)
        {
            return;
        }

        // Show on-screen message
        FColor MessageColor = Entry.Severity >= 5 ? FColor::Red : FColor::Yellow;
        GEngine->AddOnScreenDebugMessage(-1, 10.0f, MessageColor,
            FString::Printf(TEXT("UnrealSharp %s: %s"), *GetCategoryName(Entry.Category), *Entry.Message));

        // Show notification toast in editor
        #if WITH_EDITOR
        if (IsInGameThread())
        {
            FNotificationInfo Info(FText::FromString(Entry.Message));
            Info.SubText = FText::FromString(Entry.SuggestedResolution);
            Info.Image = nullptr; // Could add custom UnrealSharp icon
            Info.FadeInDuration = 0.1f;
            Info.FadeOutDuration = 0.5f;
            Info.ExpireDuration = Entry.Severity >= 5 ? 0.0f : 5.0f; // Critical errors stay visible
            Info.bUseThrobber = false;
            Info.bUseSuccessFailIcons = true;
            Info.bUseLargeFont = Entry.Severity >= 5;

            FSlateNotificationManager::Get().AddNotification(Info);
        }
        #endif
    }

    /**
     * Load diagnostics history from file
     */
    void LoadDiagnosticsHistory()
    {
        // Simplified implementation - in real code, this would parse JSON or binary format
        UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Diagnostics: Loading diagnostics history"));
        
        if (!FPaths::FileExists(DiagnosticsState.LogFilePath))
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Diagnostics: No existing log file found"));
            return;
        }

        // For now, just log that we would load history
        UE_LOG(LogTemp, VeryVerbose, TEXT("UnrealSharp Diagnostics: Historical log file found at %s"), 
               *DiagnosticsState.LogFilePath);
    }

    /**
     * Get diagnostic statistics
     */
    FString GetDiagnosticsReport()
    {
        if (!DiagnosticsState.bIsInitialized)
        {
            return TEXT("Diagnostics system not initialized");
        }

        FString Report;
        Report += TEXT("UnrealSharp Diagnostics Report\n");
        Report += TEXT("==============================\n\n");

        Report += FString::Printf(TEXT("Total Events: %d\n"), DiagnosticsState.DiagnosticHistory.Num());
        Report += FString::Printf(TEXT("Log File: %s\n\n"), *DiagnosticsState.LogFilePath);

        // Category breakdown
        Report += TEXT("Events by Category:\n");
        for (const auto& CategoryCount : DiagnosticsState.CategoryCounts)
        {
            Report += FString::Printf(TEXT("  %s: %d\n"), 
                                     *GetCategoryName(CategoryCount.Key), CategoryCount.Value);
        }

        // Most frequent errors
        Report += TEXT("\nMost Frequent Errors:\n");
        TArray<TPair<FString, int32>> SortedErrors;
        for (const auto& ErrorFreq : DiagnosticsState.ErrorFrequency)
        {
            SortedErrors.Add(TPair<FString, int32>(ErrorFreq.Key, ErrorFreq.Value));
        }
        
        SortedErrors.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) {
            return A.Value > B.Value;
        });

        int32 MaxShow = FMath::Min(5, SortedErrors.Num());
        for (int32 i = 0; i < MaxShow; i++)
        {
            Report += FString::Printf(TEXT("  %s: %d occurrences\n"), 
                                     *SortedErrors[i].Key, SortedErrors[i].Value);
        }

        // Recent critical errors
        Report += TEXT("\nRecent Critical Errors:\n");
        int32 CriticalCount = 0;
        for (int32 i = DiagnosticsState.DiagnosticHistory.Num() - 1; i >= 0 && CriticalCount < 3; i--)
        {
            const FDiagnosticEntry& Entry = DiagnosticsState.DiagnosticHistory[i];
            if (Entry.Severity >= 4)
            {
                Report += FString::Printf(TEXT("  [%s] %s: %s\n"), 
                                         *Entry.Timestamp.ToString(TEXT("%m/%d %H:%M")),
                                         *Entry.ErrorCode, *Entry.Message);
                CriticalCount++;
            }
        }

        if (CriticalCount == 0)
        {
            Report += TEXT("  No recent critical errors\n");
        }

        return Report;
    }

    /**
     * Clear diagnostics history
     */
    void ClearDiagnosticsHistory()
    {
        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Diagnostics: Clearing diagnostics history"));

        DiagnosticsState.DiagnosticHistory.Empty();
        DiagnosticsState.ErrorFrequency.Empty();
        DiagnosticsState.CategoryCounts.Empty();

        // Clear log file
        FFileHelper::SaveStringToFile(TEXT(""), *DiagnosticsState.LogFilePath);

        LogDiagnosticEvent(TEXT("US_ADMIN_001"), TEXT("Diagnostics history cleared"), 1);
    }

    /**
     * Export diagnostics to file
     */
    bool ExportDiagnostics(const FString& ExportPath)
    {
        FString Report = GetDiagnosticsReport();
        
        // Add detailed event history
        Report += TEXT("\n\nDetailed Event History:\n");
        Report += TEXT("=======================\n");
        
        for (const FDiagnosticEntry& Entry : DiagnosticsState.DiagnosticHistory)
        {
            Report += FString::Printf(TEXT("\n[%s] %s (%s)\n"), 
                                     *Entry.Timestamp.ToString(), *Entry.ErrorCode, 
                                     *GetCategoryName(Entry.Category));
            Report += FString::Printf(TEXT("Severity: %d\n"), Entry.Severity);
            Report += FString::Printf(TEXT("Message: %s\n"), *Entry.Message);
            
            if (!Entry.DetailedDescription.IsEmpty())
            {
                Report += FString::Printf(TEXT("Details: %s\n"), *Entry.DetailedDescription);
            }
            
            if (!Entry.SuggestedResolution.IsEmpty())
            {
                Report += FString::Printf(TEXT("Resolution: %s\n"), *Entry.SuggestedResolution);
            }
            
            Report += TEXT("---\n");
        }

        bool bSuccess = FFileHelper::SaveStringToFile(Report, *ExportPath);
        
        if (bSuccess)
        {
            LogDiagnosticEvent(TEXT("US_ADMIN_002"), 
                              FString::Printf(TEXT("Diagnostics exported to %s"), *ExportPath), 1);
        }
        else
        {
            LogDiagnosticEvent(TEXT("US_ADMIN_003"), 
                              FString::Printf(TEXT("Failed to export diagnostics to %s"), *ExportPath), 4);
        }

        return bSuccess;
    }

    /**
     * Shutdown diagnostics system
     */
    void ShutdownDiagnosticsSystem()
    {
        if (!DiagnosticsState.bIsInitialized)
        {
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("UnrealSharp Diagnostics: Shutting down diagnostics system"));

        // Log final statistics
        FString FinalReport = GetDiagnosticsReport();
        UE_LOG(LogTemp, Log, TEXT("Final Diagnostics Report:\n%s"), *FinalReport);

        // Save final state to file
        WriteDiagnosticToFile(FDiagnosticEntry{}); // Empty entry as separator

        DiagnosticsState.bIsInitialized = false;
    }

    // Convenience macros for common error scenarios
    #define LOG_BUILD_ERROR(Code, Message) UnrealSharp::Diagnostics::LogDiagnosticEvent(Code, Message, 4)
    #define LOG_RUNTIME_ERROR(Code, Message) UnrealSharp::Diagnostics::LogDiagnosticEvent(Code, Message, 5)
    #define LOG_HOTRELOAD_ERROR(Code, Message) UnrealSharp::Diagnostics::LogDiagnosticEvent(Code, Message, 3)
    #define LOG_PLATFORM_ERROR(Code, Message) UnrealSharp::Diagnostics::LogDiagnosticEvent(Code, Message, 4)
}

#endif // WITH_MONO_RUNTIME