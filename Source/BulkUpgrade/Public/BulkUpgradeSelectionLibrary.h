#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BulkUpgradeTypes.h"
#include "BulkUpgradeSelectionLibrary.generated.h"

UCLASS()
class BULKUPGRADE_API UBulkUpgradeSelectionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Collection")
	static FBulkUpgradeProductionInfoSet CollectProductionInfoFromSeed(AActor* SeedActor, const FBulkUpgradeCollectionOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Collection")
	static TArray<AActor*> FlattenProductionInfo(const FBulkUpgradeProductionInfoSet& ProductionInfo);
};
