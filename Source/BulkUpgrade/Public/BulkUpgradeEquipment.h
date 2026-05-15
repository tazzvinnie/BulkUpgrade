#pragma once

#include "CoreMinimal.h"
#include "BulkUpgradeTypes.h"
#include "Equipment/FGEquipment.h"
#include "Input/Reply.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "BulkUpgradeEquipment.generated.h"

class AFGBuildable;
class AFGCharacterPlayer;
class UBulkUpgradeMassUpgradePopup;
class UFGItemDescriptor;
class UPointLightComponent;
class USkeletalMeshComponent;
struct FBulkUpgradeComboBoxItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FBulkUpgradeSetProductionInfos,
	const FBulkUpgradeProductionInfoSet&,
	ProductionInfo,
	EBulkUpgradeProductionInfoIntent,
	Intent);

UCLASS()
class BULKUPGRADE_API UBulkUpgradeEquipmentDescriptor : public UFGEquipmentDescriptor
{
	GENERATED_BODY()

public:
	UBulkUpgradeEquipmentDescriptor();
};

UCLASS()
class BULKUPGRADE_API ABulkUpgradeEquipment : public AFGEquipment
{
	GENERATED_BODY()

	friend class UBulkUpgradeMassUpgradePopup;

public:
	ABulkUpgradeEquipment();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Equip(AFGCharacterPlayer* Character) override;
	virtual void UnEquip() override;
	virtual void SetEquipmentVisibility_Implementation(bool bNewEquipmentVisible) override;

	UPROPERTY(BlueprintAssignable, Category = "MassUpgrade")
	FBulkUpgradeSetProductionInfos OnSetProductionInfos;

	UPROPERTY(BlueprintReadWrite, Category = "MassUpgrade")
	AFGBuildable* targetBuildable = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassUpgrade")
	bool bUseBlueprintMassUpgradePopupEvents = false;

	UFUNCTION(BlueprintCallable, Category = "MassUpgrade")
	void SetProductionInfos(const FBulkUpgradeProductionInfoSet& ProductionInfo, EBulkUpgradeProductionInfoIntent Intent);

	UFUNCTION(BlueprintImplementableEvent, Category = "MassUpgrade")
	void ShowConveyorPopupWidget();

	UFUNCTION(BlueprintImplementableEvent, Category = "MassUpgrade")
	void ShowPipelinePopupWidget();

	UFUNCTION(BlueprintImplementableEvent, Category = "MassUpgrade")
	void ShowPowerPolePopupWidget();

	UFUNCTION(BlueprintImplementableEvent, Category = "MassUpgrade")
	void setBuildDescriptor(TSubclassOf<UFGItemDescriptor> buildDescriptor);

protected:
	virtual void HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action, EDefaultEquipmentActionEvent ActionEvent) override;

private:
	void UpdateTarget();
	void SetTarget(AFGBuildable* NewTarget);
	void SetTargetLightEnabled(bool bEnabled);
	void ShowTargetOutline(AFGCharacterPlayer* Character, AActor* Actor);
	void HideTargetOutline(AFGCharacterPlayer* Character);
	bool IsUpgradePopupOpen() const;
	void PrimaryFirePressed();
	void SecondaryFirePressed();
	void ShowUpgradePopup();
	bool ShowMassUpgradePopupWidget(EBulkUpgradeFamily Family, const TArray<FBulkUpgradeComboBoxItem>& Options, const FText& SourceAmountLabel, const FText& TargetAmountLabel, const FText& RefundLabel, const FText& InitialStatus);
	void CloseUpgradePopup();
	FReply SelectTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe);
	FReply PreviewSelectedRecipe(EBulkUpgradeScope Scope);
	FReply UpgradeSelectedRecipe();
	FReply UpgradeSelectedRequest(const FBulkUpgradeRequest& Request);
	FReply PreviewTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe, EBulkUpgradeScope Scope);
	FReply UpgradeSingleTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe);
	FBulkUpgradeRequest MakeRequest(TSubclassOf<UFGRecipe> TargetRecipe, EBulkUpgradeScope Scope) const;
	FText MakePlanSummary(const FBulkUpgradePlan& Plan, const FText& Prefix) const;
	bool CanTargetRecipeChangeTarget(TSubclassOf<UFGRecipe> TargetRecipe) const;
	bool CanCommitSelectedFamily() const;
	void SetPopupStatus(const FText& Status) const;
	void SendLocalStatusMessage(const FString& Message) const;

	static EBulkUpgradeFamily ClassifyBuildable(const AFGBuildable* Buildable);
	static bool IsSupportedTarget(const AFGBuildable* Buildable);

	UPROPERTY()
	AActor* OutlinedActor = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = "BulkUpgrade")
	USkeletalMeshComponent* EquipmentMesh = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = "BulkUpgrade")
	UPointLightComponent* TargetPointLight = nullptr;

	UPROPERTY()
	TObjectPtr<UBulkUpgradeMassUpgradePopup> ActivePopupUserWidget = nullptr;

	TSubclassOf<UFGRecipe> SelectedPopupRecipe = nullptr;
};
