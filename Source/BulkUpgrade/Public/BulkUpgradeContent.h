#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "BulkUpgradeContent.generated.h"

UCLASS()
class BULKUPGRADE_API UBulkUpgradeToolRecipe : public UFGRecipe
{
	GENERATED_BODY()

public:
	UBulkUpgradeToolRecipe();
};

UCLASS()
class BULKUPGRADE_API UBulkUpgradeToolUnlock : public UFGUnlockRecipe
{
	GENERATED_BODY()

public:
	UBulkUpgradeToolUnlock();
};

UCLASS()
class BULKUPGRADE_API UBulkUpgradeToolSchematic : public UFGSchematic
{
	GENERATED_BODY()

public:
	UBulkUpgradeToolSchematic();
};
