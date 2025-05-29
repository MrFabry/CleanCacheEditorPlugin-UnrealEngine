// Fabrizio Pasini.

#include "CleanCacheEditorCommands.h"

#define LOCTEXT_NAMESPACE "FCleanCacheEditorModule"

void FCleanCacheEditorCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "Clean Cache & Restart", "Clean project cache files and restart the editor", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE