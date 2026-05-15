#pragma once

#include "CoreMinimal.h"
#include "BulkUpgradeTypes.h"

class IBulkUpgradeAdapter
{
public:
	virtual ~IBulkUpgradeAdapter() = default;

	virtual EBulkUpgradeFamily GetFamily() const = 0;
	virtual bool CanClassify(const AActor* Actor) const = 0;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const = 0;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const = 0;
};
