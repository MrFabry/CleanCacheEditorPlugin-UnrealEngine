// Fabrizio Pasini.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "CleanCacheEditorStyle.h"

class FCleanCacheEditorCommands : public TCommands<FCleanCacheEditorCommands>
{
public:

	FCleanCacheEditorCommands()
		: TCommands<FCleanCacheEditorCommands>(TEXT("CleanCacheEditor"), NSLOCTEXT("Contexts", "CleanCacheEditor", "CleanCacheEditor Plugin"), NAME_None, FCleanCacheEditorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};