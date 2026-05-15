#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BulkUpgradeTypes.h"
#include "BulkUpgradePlanner.generated.h"

UCLASS()
class BULKUPGRADE_API UBulkUpgradePlanner : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade", meta = (WorldContext = "WorldContextObject"))
	static FBulkUpgradePlan BuildPlan(UObject* WorldContextObject, AActor* SeedActor, const FBulkUpgradeRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade", meta = (WorldContext = "WorldContextObject"))
	static FBulkUpgradePlan BuildPlanFromActors(UObject* WorldContextObject, const TArray<AActor*>& Actors, const FBulkUpgradeRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade", meta = (WorldContext = "WorldContextObject"))
	static FBulkUpgradePlan BuildPlanFromProductionInfo(UObject* WorldContextObject, const FBulkUpgradeProductionInfoSet& ProductionInfo, const FBulkUpgradeRequest& Request);
};
