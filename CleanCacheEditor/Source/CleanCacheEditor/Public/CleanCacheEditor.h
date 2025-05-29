// Copyright 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Notifications/SNotificationList.h"

class FToolBarBuilder;
class FMenuBuilder;

// Forward declaration for an async task
class FCleanupAsyncTask;

class FCleanCacheEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
    
	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

	/** Cleanup project cache files */
	void CleanupProjectFiles();
    
	/** Perform the actual cleanup work (called from async task) */
	void PerformCleanupWork(const TArray<FString>& FoldersToDelete);
    
	/** Called when cleanup is completed */
	void OnCleanupCompleted(bool bSuccess, int32 DeletedFolders, const FString& ErrorMessage);

private:

	void RegisterMenus();

	/** Show a notification to the user */
	void ShowNotification(const FText& Message, SNotificationItem::ECompletionState CompletionState);
    
	/** Get the current world for timer management */
	UWorld* GetWorld() const;
    
	/** Delete directory contents recursively */
	bool DeleteDirectoryContents(const FString& DirectoryPath);
    
	/** Force delete directory using platform-specific commands */
	bool ForceDeleteDirectory(const FString& DirectoryPath);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
    
	// Friend class for async task access
	friend class FCleanupAsyncTask;
};