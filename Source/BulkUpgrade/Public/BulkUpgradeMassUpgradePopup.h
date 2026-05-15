#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BulkUpgradeTypes.h"
#include "BulkUpgradeWidgetPopupHelper.h"
#include "Styling/SlateBrush.h"
#include "Types/SlateEnums.h"
#include "BulkUpgradeMassUpgradePopup.generated.h"

class ABulkUpgradeEquipment;
class AFGBuildable;
class SHorizontalBox;
class STextBlock;
class SVerticalBox;
class UFGItemDescriptor;
class UFGRecipe;
class UFGBuildDescriptor;
class UTexture2D;

struct FBulkUpgradePopupOption
{
	FBulkUpgradeComboBoxItem Item;
	FSlateBrush IconBrush;
};

UCLASS()
class BULKUPGRADE_API UBulkUpgradeMassUpgradePopup : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(
		ABulkUpgradeEquipment* InEquipment,
		AFGBuildable* InTargetBuildable,
		EBulkUpgradeFamily InFamily,
		const TArray<FBulkUpgradeComboBoxItem>& InOptions,
		TSubclassOf<UFGRecipe> InSelectedRecipe,
		const FText& InSourceAmountLabel,
		const FText& InTargetAmountLabel,
		const FText& InRefundLabel,
		const FText& InInitialStatus);

	void SetStatusText(const FText& Status);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildOptionBuckets(const TArray<FBulkUpgradeComboBoxItem>& InOptions, TSubclassOf<UFGRecipe> InSelectedRecipe);
	void AddComboItems(const TArray<FBulkUpgradeComboBoxItem>& Items);
	void AddPopupOption(const FBulkUpgradeComboBoxItem& Item, TSubclassOf<UFGRecipe> InSelectedRecipe);
	void SelectDefaultOption(EBulkUpgradeFamily OptionFamily, TSubclassOf<UFGRecipe> InSelectedRecipe);
	TArray<TSharedPtr<FBulkUpgradePopupOption>>* GetMutableOptionsForFamily(EBulkUpgradeFamily OptionFamily);
	const TArray<TSharedPtr<FBulkUpgradePopupOption>>* GetOptionsForFamily(EBulkUpgradeFamily OptionFamily) const;
	TSharedPtr<FBulkUpgradePopupOption> GetSelectedOption(EBulkUpgradeFamily OptionFamily) const;
	FName GetToggleNameForFamily(EBulkUpgradeFamily OptionFamily) const;
	bool IsLengthFamily(EBulkUpgradeFamily OptionFamily) const;

	TSharedRef<SWidget> MakeTitleBar();
	TSharedRef<SWidget> MakeUpgradeTargetRow();
	TSharedRef<SWidget> MakeFamilySelector(FName ToggleName, EBulkUpgradeFamily OptionFamily, float CardWidth);
	TSharedRef<SWidget> GenerateTypeOptionWidget(TSharedPtr<FBulkUpgradePopupOption> Option) const;
	TSharedRef<SWidget> MakeSelectedFamilyWidget(EBulkUpgradeFamily OptionFamily) const;
	TSharedRef<SWidget> MakeCrossAttachmentsToggle();
	TSharedRef<SWidget> MakeCostPanel();
	TSharedRef<SWidget> MakeProductionRowsPanel();
	TSharedRef<SWidget> MakeUpgradeFooter();
	TSharedRef<SWidget> MakeItemAmountWidget(const FItemAmount& ItemAmount, float IconSize) const;
	TSharedRef<SWidget> MakeRefundWidget(const FBulkUpgradeProductionGroup& Group);

	void HandleTypeSelectionChanged(TSharedPtr<FBulkUpgradePopupOption> Option, ESelectInfo::Type SelectionType);
	void HandleFunctionToggleChanged(ECheckBoxState NewState, FName ToggleName);
	FReply HandleUpgradeClicked();
	FReply HandleAddToListClicked();
	FReply HandleCloseClicked();

	ECheckBoxState GetFunctionToggleState(FName ToggleName) const;
	bool IsUpgradeEnabled() const;
	FText GetSelectedFamilyText(EBulkUpgradeFamily OptionFamily) const;
	FText GetOptionAmountText(TSharedPtr<FBulkUpgradePopupOption> Option) const;
	FText GetProductionSummaryText() const;
	FText GetGroupAmountText(const FBulkUpgradeProductionGroup& Group) const;

	void RefreshProductionInfo();
	FBulkUpgradeRequest BuildRequestFromSelections(EBulkUpgradeScope Scope) const;
	void RebuildProductionRows();
	void RebuildCostItems();
	void AddProductionRow(const FBulkUpgradeProductionGroup& Group);
	TArray<FItemAmount> BuildCostItems() const;
	TArray<FItemAmount> BuildRefundItems(const FBulkUpgradeProductionGroup& Group) const;
	float GetBuildableCostMultiplier(const AFGBuildable* Buildable) const;
	void TrackIcon(UTexture2D* Icon);
	const FSlateBrush* GetDescriptorBrush(TSubclassOf<UFGItemDescriptor> Descriptor);
	FSlateColor GetPanelTextColor() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<ABulkUpgradeEquipment> Equipment;

	UPROPERTY(Transient)
	TWeakObjectPtr<AFGBuildable> TargetBuildable;

	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;

	TArray<TSharedPtr<FBulkUpgradePopupOption>> TypeOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> ConveyorBeltOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> ConveyorLiftOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> StorageOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PipelineOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PipelinePumpOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> WireOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PowerPoleOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PowerPoleWallOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PowerPoleWallDoubleOptions;
	TArray<TSharedPtr<FBulkUpgradePopupOption>> PowerTowerOptions;
	TSharedPtr<FBulkUpgradePopupOption> SelectedTypeOption;
	TMap<EBulkUpgradeFamily, TSharedPtr<FBulkUpgradePopupOption>> SelectedFamilyOptions;

	UPROPERTY(Transient)
	FBulkUpgradeProductionInfoSet ProductionInfo;

	FText SourceAmountLabel;
	FText TargetAmountLabel;
	FText RefundLabel;
	FText CurrentStatusText;

	TMap<FName, bool> FunctionToggles;
	TMap<UClass*, TSharedPtr<FSlateBrush>> DescriptorBrushes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> ReferencedIcons;

	FSlateBrush EmptyIconBrush;

	TSharedPtr<STextBlock> StatusTextBlock;
	TSharedPtr<STextBlock> ProductionSummaryTextBlock;
	TSharedPtr<SHorizontalBox> CostItemsBox;
	TSharedPtr<SVerticalBox> ProductionRowsBox;
};
