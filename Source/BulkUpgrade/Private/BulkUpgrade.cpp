#include "BulkUpgrade.h"

#include "BulkUpgradeExecutor.h"
#include "FGConveyorChainActor.h"
#include "GameFramework/Actor.h"
#include "Patching/NativeHookManager.h"

#define LOCTEXT_NAMESPACE "FBulkUpgradeModule"

DEFINE_LOG_CATEGORY(LogBulkUpgrade);

void FBulkUpgradeModule::StartupModule()
{
	UE_LOG(LogBulkUpgrade, Log, TEXT("BulkUpgrade module loaded."));

	ChainPostLoadProcessEventHookHandle = SUBSCRIBE_METHOD_VIRTUAL(AActor::ProcessEvent, GetDefault<AFGConveyorChainActor>(), [](auto& Scope, AActor* Actor, UFunction* Function, void* Parameters)
	{
		static const FName PostLoadGameName(TEXT("PostLoadGame"));
		if (!IsValid(Actor) || !Function || Function->GetFName() != PostLoadGameName)
		{
			return;
		}

		AFGConveyorChainActor* ChainActor = Cast<AFGConveyorChainActor>(Actor);
		if (!IsValid(ChainActor))
		{
			return;
		}

		bool bShouldSkipVanillaPostLoad = false;
		const int32 Changes = UBulkUpgradeExecutor::SanitizeConveyorChainActorForPostLoad(ChainActor, TEXT("pre-postload-hook"), bShouldSkipVanillaPostLoad);
		if (Changes > 0)
		{
			UE_LOG(LogBulkUpgrade, Warning,
				TEXT("BulkUpgrade pre-load conveyor chain rescue: chain=%s changes=%d skipVanillaPostLoad=%d"),
				*GetNameSafe(ChainActor),
				Changes,
				bShouldSkipVanillaPostLoad ? 1 : 0);
		}

		if (bShouldSkipVanillaPostLoad)
		{
			UE_LOG(LogBulkUpgrade, Warning,
				TEXT("BulkUpgrade skipped vanilla conveyor-chain PostLoadGame for empty quarantined chain=%s"),
				*GetNameSafe(ChainActor));
			Scope.Cancel();
		}
	});
}

void FBulkUpgradeModule::ShutdownModule()
{
	if (ChainPostLoadProcessEventHookHandle.IsValid())
	{
		UNSUBSCRIBE_METHOD(AActor::ProcessEvent, ChainPostLoadProcessEventHookHandle);
	}

	UE_LOG(LogBulkUpgrade, Log, TEXT("BulkUpgrade module unloaded."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBulkUpgradeModule, BulkUpgrade)
