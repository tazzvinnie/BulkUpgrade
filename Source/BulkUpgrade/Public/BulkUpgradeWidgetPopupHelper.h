#pragma once

#include "CoreMinimal.h"
#include "BulkUpgradeTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BulkUpgradeWidgetPopupHelper.generated.h"

class AFGBuildable;
class ABulkUpgradeEquipment;
class UFGRecipe;
class UFGBuildDescriptor;

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeComboBoxItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	TSubclassOf<UFGRecipe> Recipe = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	TSubclassOf<UFGBuildDescriptor> BuildDescriptor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	TSubclassOf<AFGBuildable> BuildableClass = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	float Amount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Widget")
	FName OptionName = NAME_None;
};

UCLASS()
class BULKUPGRADE_API UBulkUpgradeWidgetPopupHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget", meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static TArray<FBulkUpgradeComboBoxItem> GetRecipeOptionsByFamily(UObject* WorldContextObject, EBulkUpgradeFamily Family);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget", meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void GetConveyorsBySpeed(UObject* WorldContextObject, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& BeltBySpeed, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& LiftBySpeed, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& StorageByCapacity);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget", meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void GetPipesBySpeed(UObject* WorldContextObject, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PipeByFlowLimit, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PumpByHeadLift);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget", meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void GetPowerPolesByConnections(UObject* WorldContextObject, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& WireTypes, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PowerPoleByConnection, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PowerPoleWallByConnection, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PowerPoleWallDoubleByConnection, UPARAM(ref) TArray<FBulkUpgradeComboBoxItem>& PowerTowerByConnection);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget")
	static FBulkUpgradeProductionInfoSet CollectConveyorProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludeBelts, bool bIncludeLifts, bool bIncludeStorages, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent = EBulkUpgradeProductionInfoIntent::UpdateWidget);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget")
	static FBulkUpgradeProductionInfoSet CollectPipelineProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludePipelines, bool bIncludePumps, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent = EBulkUpgradeProductionInfoIntent::UpdateWidget);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget")
	static FBulkUpgradeProductionInfoSet CollectPowerPoleProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludeWires, bool bIncludePowerPoles, bool bIncludePowerPoleWalls, bool bIncludePowerPoleWallDoubles, bool bIncludePowerTowers, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent = EBulkUpgradeProductionInfoIntent::UpdateWidget);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static FBulkUpgradeComboBoxItem GetSelectedComboBoxItem(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static TSubclassOf<UFGRecipe> GetSelectedRecipe(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static TSubclassOf<UFGBuildDescriptor> GetSelectedBuildDescriptor(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static FName GetDefaultSelectedOptionName(const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems, AFGBuildable* TargetBuildable, bool bPreferCurrentType = true);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static EBulkUpgradeFamily ClassifyBuildable(AFGBuildable* Buildable);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static EBulkUpgradeFamily ClassifyBuildableClass(TSubclassOf<AFGBuildable> BuildableClass);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static float GetBuildableSortValue(TSubclassOf<AFGBuildable> BuildableClass);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static FText MakeFamilyLabel(EBulkUpgradeFamily Family);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static FText MakeAmountLabel(float Amount, EBulkUpgradeFamily Family);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static FName GetCheckboxName(TSubclassOf<UFGBuildDescriptor> BuildDescriptor);

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget")
	static void ResetCheckedBuildDescriptors();

	UFUNCTION(BlueprintCallable, Category = "BulkUpgrade|Widget")
	static void SetBuildDescriptorChecked(TSubclassOf<UFGBuildDescriptor> BuildDescriptor, bool bChecked);

	UFUNCTION(BlueprintPure, Category = "BulkUpgrade|Widget")
	static bool IsBuildDescriptorChecked(TSubclassOf<UFGBuildDescriptor> BuildDescriptor, bool bDefaultChecked = true);
};
