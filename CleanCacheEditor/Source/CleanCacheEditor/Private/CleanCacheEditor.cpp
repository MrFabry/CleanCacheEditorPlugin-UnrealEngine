// Fabrizio Pasini.

#include "CleanCacheEditor.h"
#include "CleanCacheEditorStyle.h"
#include "CleanCacheEditorCommands.h"
#include "ToolMenus.h"

#include "Interfaces/IMainFrameModule.h"
#include "LevelEditor.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Engine/Engine.h"
#include "EditorStyle.h"
#include "Async/AsyncWork.h"
#include "HAL/PlatformProcess.h"

static const FName CleanCacheEditorTabName("CleanCacheEditor");

#define LOCTEXT_NAMESPACE "FCleanCacheEditorModule"

// Async task for cleanup operations
class FCleanupAsyncTask : public FNonAbandonableTask
{
public:
    FCleanupAsyncTask(FCleanCacheEditorModule* InModule, const TArray<FString>& InFoldersToDelete)
        : Module(InModule), FoldersToDelete(InFoldersToDelete)
    {
    }

    void DoWork()
    {
        Module->PerformCleanupWork(FoldersToDelete);
    }

    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FCleanupAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
    }

private:
    FCleanCacheEditorModule* Module;
    TArray<FString> FoldersToDelete;
};

void FCleanCacheEditorModule::StartupModule()
{
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

    FCleanCacheEditorStyle::Initialize();
    FCleanCacheEditorStyle::ReloadTextures();

    FCleanCacheEditorCommands::Register();

    PluginCommands = MakeShareable(new FUICommandList);

    PluginCommands->MapAction(
        FCleanCacheEditorCommands::Get().PluginAction,
        FExecuteAction::CreateRaw(this, &FCleanCacheEditorModule::PluginButtonClicked),
        FCanExecuteAction());

    IMainFrameModule& mainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");
    mainFrame.GetMainFrameCommandBindings()->Append(PluginCommands.ToSharedRef());

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCleanCacheEditorModule::RegisterMenus));
}

void FCleanCacheEditorModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.

    UToolMenus::UnRegisterStartupCallback(this);

    UToolMenus::UnregisterOwner(this);

    FCleanCacheEditorStyle::Shutdown();

    FCleanCacheEditorCommands::Unregister();
}

void FCleanCacheEditorModule::PluginButtonClicked()
{
    // Show detailed confirmation dialog
    const FText DialogTitle = LOCTEXT("CleanupConfirmTitle", "Cleanup Cache and Restart");
    const FText DialogMessage = LOCTEXT("CleanupConfirmMessage", 
        "This will delete intermediate files, binaries, and saved data, then restart the editor.\n\n"
        "The following folders will be deleted:\n"
        "• Intermediate/ (Build cache and temp files)\n"
        "• Binaries/ (Compiled binaries)\n"
        "• Saved/ (Editor settings, logs, crashes)\n"
        "• .vs/ (Visual Studio cache)\n\n"
        "WARNING: This will close the editor and may take several minutes.\n"
        "Save your work before continuing!\n\n"
        "Are you sure you want to continue?");

    EAppReturnType::Type Result = FMessageDialog::Open(
        EAppMsgType::YesNo, 
        DialogMessage, 
        &DialogTitle
    );

    if (Result == EAppReturnType::Yes)
    {
        CleanupProjectFiles();
    }
}

void FCleanCacheEditorModule::CleanupProjectFiles()
{
    const FString ProjectDir = FPaths::ProjectDir();
    
    TArray FoldersToDelete = {
        FPaths::Combine(ProjectDir, TEXT("Intermediate")),
        FPaths::Combine(ProjectDir, TEXT("Binaries")),
        FPaths::Combine(ProjectDir, TEXT("Saved")),
        FPaths::Combine(ProjectDir, TEXT(".vs"))
    };

    ShowNotification(LOCTEXT("CleanupStarted", "Starting cleanup process... This may take a few seconds."), SNotificationItem::CS_Pending);

    if (GEngine)
    {
        GEngine->ForceGarbageCollection(true);
    }

    (new FAutoDeleteAsyncTask<FCleanupAsyncTask>(this, FoldersToDelete))->StartBackgroundTask();
}

void FCleanCacheEditorModule::PerformCleanupWork(const TArray<FString>& FoldersToDelete)
{
    IFileManager& FileManager = IFileManager::Get();
    bool bCleanupSuccess = true;
    FString ErrorMessage;
    int32 DeletedFolders = 0;

    for (const FString& FolderPath : FoldersToDelete)
    {
        if (FileManager.DirectoryExists(*FolderPath))
        {
            UE_LOG(LogTemp, Log, TEXT("Attempting to delete folder: %s"), *FolderPath);
            
            bool bDeleted = false;
            
            // Try standard deletion with force
            bDeleted = FileManager.DeleteDirectory(*FolderPath, false, true);
            
            if (!bDeleted)
            {
                // Try to delete contents first, then folder
                bDeleted = DeleteDirectoryContents(FolderPath) && FileManager.DeleteDirectory(*FolderPath, true, false);
            }
            
            if (!bDeleted)
            {
                // Try platform-specific deletion
                bDeleted = ForceDeleteDirectory(FolderPath);
            }
            
            if (bDeleted)
            {
                DeletedFolders++;
                UE_LOG(LogTemp, Log, TEXT("Successfully deleted: %s"), *FolderPath);
            }
            else
            {
                bCleanupSuccess = false;
                ErrorMessage += FString::Printf(TEXT("Failed to delete: %s\n"), *FolderPath);
                UE_LOG(LogTemp, Error, TEXT("Failed to delete folder: %s"), *FolderPath);
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Folder does not exist, skipping: %s"), *FolderPath);
        }
    }

    AsyncTask(ENamedThreads::GameThread, [this, bCleanupSuccess, DeletedFolders, ErrorMessage]()
    {
        OnCleanupCompleted(bCleanupSuccess, DeletedFolders, ErrorMessage);
    });
}

bool FCleanCacheEditorModule::DeleteDirectoryContents(const FString& DirectoryPath)
{
    IFileManager& FileManager = IFileManager::Get();
    
    TArray<FString> Files;
    FileManager.FindFiles(Files, *(DirectoryPath / TEXT("*")), true, false);
    
    for (const FString& File : Files)
    {
        FString FilePath = DirectoryPath / File;
        if (!FileManager.Delete(*FilePath, false, true, true))
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not delete file: %s"), *FilePath);
        }
    }
    
    // Recursively delete subdirectories
    TArray<FString> SubDirs;
    FileManager.FindFiles(SubDirs, *(DirectoryPath / TEXT("*")), false, true);
    
    for (const FString& SubDir : SubDirs)
    {
        FString SubDirPath = DirectoryPath / SubDir;
        if (!DeleteDirectoryContents(SubDirPath))
        {
            return false;
        }
        FileManager.DeleteDirectory(*SubDirPath, false, false);
    }
    
    return true;
}

bool FCleanCacheEditorModule::ForceDeleteDirectory(const FString& DirectoryPath)
{
#if PLATFORM_WINDOWS
    // Use Windows command line for stubborn folders
    FString Command = FString::Printf(TEXT("rmdir /s /q \"%s\""), *DirectoryPath);
    
    int32 ReturnCode = -1;
    FString StdOut, StdErr;
    
    bool bResult = FPlatformProcess::ExecProcess(
        TEXT("cmd.exe"),
        *FString::Printf(TEXT("/c %s"), *Command),
        &ReturnCode,
        &StdOut,
        &StdErr
    );
    
    return bResult && ReturnCode == 0;
#elif PLATFORM_MAC || PLATFORM_LINUX
    // Use Unix rm command
    FString Command = FString::Printf(TEXT("rm -rf \"%s\""), *DirectoryPath);
    
    int32 ReturnCode = -1;
    FString StdOut, StdErr;
    
    bool bResult = FPlatformProcess::ExecProcess(
        TEXT("/bin/sh"),
        *FString::Printf(TEXT("-c \"%s\""), *Command),
        &ReturnCode,
        &StdOut,
        &StdErr
    );
    
    return bResult && ReturnCode == 0;
#else
    return false;
#endif
}

void FCleanCacheEditorModule::OnCleanupCompleted(bool bSuccess, int32 DeletedFolders, const FString& ErrorMessage)
{
    // Show result and restart if successful
    if (bSuccess && DeletedFolders > 0)
    {
        const FText SuccessMessage = FText::Format(
            LOCTEXT("CleanupSuccess", "Successfully cleaned up {0} folders. Restarting editor in 3 seconds..."),
            FText::AsNumber(DeletedFolders)
        );
        
        ShowNotification(SuccessMessage, SNotificationItem::CS_Success);
        
        // Delay before restart to show the notification
        FTimerHandle RestartTimer;
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(RestartTimer, FTimerDelegate::CreateLambda([this]()
            {
                FUnrealEdMisc::Get().RestartEditor(false);
            }), 3.0f, false);
        }
    }
    else if (DeletedFolders == 0)
    {
        ShowNotification(LOCTEXT("NothingToClean", "No cache folders found to clean up."), SNotificationItem::CS_None);
    }
    else
    {
        const FText ErrorText = FText::Format(
            LOCTEXT("CleanupError", "Cleanup completed with errors:\n{0}"),
            FText::FromString(ErrorMessage)
        );
        
        ShowNotification(ErrorText, SNotificationItem::CS_Fail);
        
        // Ask if user still wants to restart despite errors
        EAppReturnType::Type Result = FMessageDialog::Open(
            EAppMsgType::YesNo,
            LOCTEXT("RestartDespiteErrors", "Some files could not be deleted. Do you still want to restart the editor?")
        );
        
        if (Result == EAppReturnType::Yes)
        {
            FUnrealEdMisc::Get().RestartEditor(false);
        }
    }
}

void FCleanCacheEditorModule::ShowNotification(const FText& Message, SNotificationItem::ECompletionState CompletionState)
{
    FNotificationInfo Info(Message);
    Info.FadeInDuration = 0.1f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = (CompletionState == SNotificationItem::CS_Pending) ? 0.0f : 5.0f; // Don't auto-hide pending notifications
    Info.bUseThrobber = (CompletionState == SNotificationItem::CS_Pending);
    Info.bUseSuccessFailIcons = true;
    Info.bUseLargeFont = true;
    Info.bFireAndForget = true;

    auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(CompletionState);
    }
}

UWorld* FCleanCacheEditorModule::GetWorld() const
{
    if (GEditor && GEditor->GetEditorWorldContext().World())
    {
        return GEditor->GetEditorWorldContext().World();
    }
    return nullptr;
}

void FCleanCacheEditorModule::RegisterMenus()
{
    // Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
    FToolMenuOwnerScoped OwnerScoped(this);
    {
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        {
            FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
            Section.AddMenuEntryWithCommandList(FCleanCacheEditorCommands::Get().PluginAction, PluginCommands);
        }
    }
    {
        UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
        {
            // Use the same section as RestartEditor plugin
            FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginEditorTools");
            {
                FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FCleanCacheEditorCommands::Get().PluginAction));
                Entry.SetCommandList(PluginCommands);
            }
        }
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCleanCacheEditorModule, CleanCacheEditor)