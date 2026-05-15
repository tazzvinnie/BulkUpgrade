#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBulkUpgrade, Log, All);

class FDelegateHandle;

class FBulkUpgradeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle ChainPostLoadProcessEventHookHandle;
};
