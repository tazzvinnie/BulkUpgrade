#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "Module/GameWorldModule.h"
#include "BulkUpgradeGameWorldModule.generated.h"

class AFGPlayerController;
class UCommandSender;

UCLASS()
class BULKUPGRADE_API UBulkUpgradeGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UBulkUpgradeGameWorldModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

	static bool GiveToolToPlayerController(AFGPlayerController* Controller, FText& OutMessage);
	static bool UnlockToolForPlayerController(AFGPlayerController* Controller, FText& OutMessage);

private:
	void GiveToolToExistingPlayers();
	void ScheduleGiveToolRetry(float DelaySeconds);
};

UCLASS()
class BULKUPGRADE_API ABulkUpgradeGiveToolCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ABulkUpgradeGiveToolCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};
