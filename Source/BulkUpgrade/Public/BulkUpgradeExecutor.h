#pragma once

#include "CoreMinimal.h"
#include "FGCharacterPlayer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BulkUpgradeTypes.h"
#include "BulkUpgradeExecutor.generated.h"

class AFGBuildableConveyorBase;
class AFGConveyorChainActor;

UCLASS()
class BULKUPGRADE_API UBulkUpgradeExecutor : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade", meta = (WorldContext = "WorldContextObject"))
	static FBulkUpgradeCommitReport CommitPlan(UObject* WorldContextObject, AFGCharacterPlayer* Player, UPARAM(ref) FBulkUpgradePlan& Plan, const FBulkUpgradeCommitOptions& Options);

	static int32 SanitizeConveyorChainActorForPostLoad(AFGConveyorChainActor* ChainActor, const TCHAR* Context, bool& bOutShouldSkipVanillaPostLoad);

private:
	static bool CommitWithHologramPath(AFGCharacterPlayer* Player, FBulkUpgradePreviewRow& Row, FText& OutFailure);
	static bool CommitWithManualSpawnPath(AFGCharacterPlayer* Player, FBulkUpgradePreviewRow& Row, FText& OutFailure);
	static bool ConveyorChainContainsConveyor(const AFGConveyorChainActor* ChainActor, const AFGBuildableConveyorBase* Conveyor);
	static bool ConveyorSegmentBelongsToDifferentValidChain(const AFGConveyorChainActor* ExpectedChainActor, AFGBuildableConveyorBase* Conveyor);
	static bool ConveyorChainNeedsStructuralRescue(AFGConveyorChainActor* ChainActor);
	static int32 SanitizeConveyorChainActor(AFGConveyorChainActor* ChainActor, const TCHAR* Context);
};
