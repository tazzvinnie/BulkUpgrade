#include "BulkUpgradeWidgetPopupHelper.h"

#include "BulkUpgradeEquipment.h"
#include "BulkUpgradeSelectionLibrary.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableWire.h"
#include "FGPowerConnectionComponent.h"
#include "FGRecipeManager.h"
#include "Resources/FGBuildDescriptor.h"
#include "Resources/FGBuildingDescriptor.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeWidgetPopupHelper"

namespace
{
TMap<UClass*, bool> GCheckedBuildDescriptors;

FName MakeOptionName(int32 Index)
{
	return FName(*LexToString(Index));
}

float GetPipelineFlowLimitFallbackFromText(const FString& Text)
{
	if (Text.Contains(TEXT("Mk.2"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Mk 2"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("MK2"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Mk2"), ESearchCase::IgnoreCase))
	{
		return 600.0f;
	}

	if (Text.Contains(TEXT("Mk.1"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Mk 1"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("MK1"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Mk1"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Pipeline"), ESearchCase::IgnoreCase) ||
		Text.Contains(TEXT("Pipe"), ESearchCase::IgnoreCase))
	{
		return 300.0f;
	}

	return 0.0f;
}

FText MakeRecipeOptionLabel(TSubclassOf<UFGRecipe> Recipe, float Amount, EBulkUpgradeFamily Family)
{
	FText RecipeName = UFGRecipe::GetRecipeName(Recipe);
	if (RecipeName.IsEmpty())
	{
		RecipeName = FText::FromString(GetNameSafe(Recipe.Get()));
	}

	const FText AmountLabel = UBulkUpgradeWidgetPopupHelper::MakeAmountLabel(Amount, Family);
	return Amount > 0.0f
		? FText::Format(LOCTEXT("RecipeOptionWithAmount", "{0} ({1})"), RecipeName, AmountLabel)
		: RecipeName;
}

float GetPipelineFlowLimitFallbackPerMinute(TSubclassOf<AFGBuildable> BuildableClass)
{
	const FString ClassPath = GetPathNameSafe(BuildableClass.Get());
	return GetPipelineFlowLimitFallbackFromText(ClassPath);
}

bool TryMakeComboBoxItem(TSubclassOf<UFGRecipe> Recipe, int32 Index, FBulkUpgradeComboBoxItem& OutItem)
{
	if (!Recipe)
	{
		return false;
	}

	const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);
	if (Products.Num() == 0 || !Products[0].ItemClass || !Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
	{
		return false;
	}

	const TSubclassOf<UFGBuildingDescriptor> BuildingDescriptor(Products[0].ItemClass.Get());
	const TSubclassOf<UFGBuildDescriptor> BuildDescriptor(Products[0].ItemClass.Get());
	const TSubclassOf<AFGBuildable> BuildableClass = UFGBuildingDescriptor::GetBuildableClass(BuildingDescriptor);
	const EBulkUpgradeFamily Family = UBulkUpgradeWidgetPopupHelper::ClassifyBuildableClass(BuildableClass);
	if (Family == EBulkUpgradeFamily::Unknown)
	{
		return false;
	}

	float Amount = UBulkUpgradeWidgetPopupHelper::GetBuildableSortValue(BuildableClass);
	if (Family == EBulkUpgradeFamily::Pipeline)
	{
		const FString PipelineIdentity = FString::Printf(TEXT("%s %s %s"),
			*GetPathNameSafe(BuildableClass.Get()),
			*UFGRecipe::GetRecipeName(Recipe).ToString(),
			*UFGItemDescriptor::GetItemName(BuildingDescriptor).ToString());
		const float NamedFlowPerMinute = GetPipelineFlowLimitFallbackFromText(PipelineIdentity);
		if (NamedFlowPerMinute > 0.0f)
		{
			Amount = NamedFlowPerMinute;
		}
	}
	OutItem.Recipe = Recipe;
	OutItem.BuildDescriptor = BuildDescriptor;
	OutItem.BuildableClass = BuildableClass;
	OutItem.Family = Family;
	OutItem.Amount = Amount;
	OutItem.Label = MakeRecipeOptionLabel(Recipe, Amount, Family);
	OutItem.OptionName = MakeOptionName(Index);
	return true;
}

void SortAndRenumber(TArray<FBulkUpgradeComboBoxItem>& Items)
{
	Items.Sort([](const FBulkUpgradeComboBoxItem& Left, const FBulkUpgradeComboBoxItem& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Amount, Right.Amount))
		{
			return Left.Amount < Right.Amount;
		}
		return Left.Label.ToString() < Right.Label.ToString();
	});

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Items[Index].OptionName = MakeOptionName(Index);
	}
}

void AppendRecipeOptions(UObject* WorldContextObject, EBulkUpgradeFamily Family, TArray<FBulkUpgradeComboBoxItem>& OutItems)
{
	OutItems.Reset();

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("GetAvailableRecipesForFamily: Failed to get world from context object"));
		return;
	}

	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
	if (!RecipeManager)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("GetAvailableRecipesForFamily: Failed to get recipe manager from world"));
		return;
	}

	if (Family == EBulkUpgradeFamily::Unknown)
	{
		return;
	}

	TArray<TSubclassOf<UFGRecipe>> AvailableRecipes;
	RecipeManager->GetAllAvailableRecipes(AvailableRecipes);

	TSet<UClass*> SeenRecipeClasses;
	int32 SourceIndex = 0;
	for (const TSubclassOf<UFGRecipe>& Recipe : AvailableRecipes)
	{
		if (!Recipe || SeenRecipeClasses.Contains(Recipe.Get()))
		{
			continue;
		}
		SeenRecipeClasses.Add(Recipe.Get());

		FBulkUpgradeComboBoxItem Item;
		if (TryMakeComboBoxItem(Recipe, SourceIndex++, Item) && Item.Family == Family)
		{
			OutItems.Add(Item);
		}
	}

	SortAndRenumber(OutItems);
}
}

TArray<FBulkUpgradeComboBoxItem> UBulkUpgradeWidgetPopupHelper::GetRecipeOptionsByFamily(UObject* WorldContextObject, EBulkUpgradeFamily Family)
{
	TArray<FBulkUpgradeComboBoxItem> Items;
	AppendRecipeOptions(WorldContextObject, Family, Items);
	return Items;
}

void UBulkUpgradeWidgetPopupHelper::GetConveyorsBySpeed(UObject* WorldContextObject, TArray<FBulkUpgradeComboBoxItem>& BeltBySpeed, TArray<FBulkUpgradeComboBoxItem>& LiftBySpeed, TArray<FBulkUpgradeComboBoxItem>& StorageByCapacity)
{
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::ConveyorBelt, BeltBySpeed);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::ConveyorLift, LiftBySpeed);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::Storage, StorageByCapacity);
}

void UBulkUpgradeWidgetPopupHelper::GetPipesBySpeed(UObject* WorldContextObject, TArray<FBulkUpgradeComboBoxItem>& PipeByFlowLimit, TArray<FBulkUpgradeComboBoxItem>& PumpByHeadLift)
{
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::Pipeline, PipeByFlowLimit);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::PipelinePump, PumpByHeadLift);
}

void UBulkUpgradeWidgetPopupHelper::GetPowerPolesByConnections(UObject* WorldContextObject, TArray<FBulkUpgradeComboBoxItem>& WireTypes, TArray<FBulkUpgradeComboBoxItem>& PowerPoleByConnection, TArray<FBulkUpgradeComboBoxItem>& PowerPoleWallByConnection, TArray<FBulkUpgradeComboBoxItem>& PowerPoleWallDoubleByConnection, TArray<FBulkUpgradeComboBoxItem>& PowerTowerByConnection)
{
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::Wire, WireTypes);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::PowerPole, PowerPoleByConnection);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::PowerPoleWall, PowerPoleWallByConnection);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::PowerPoleWallDouble, PowerPoleWallDoubleByConnection);
	AppendRecipeOptions(WorldContextObject, EBulkUpgradeFamily::PowerTower, PowerTowerByConnection);
}

FBulkUpgradeProductionInfoSet UBulkUpgradeWidgetPopupHelper::CollectConveyorProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludeBelts, bool bIncludeLifts, bool bIncludeStorages, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent)
{
	FBulkUpgradeCollectionOptions Options;
	Options.bIncludeBelts = bIncludeBelts;
	Options.bIncludeLifts = bIncludeLifts;
	Options.bIncludeStorages = bIncludeStorages;
	Options.bCrossAttachments = bCrossAttachmentsAndStorages;

	FBulkUpgradeProductionInfoSet ProductionInfo = UBulkUpgradeSelectionLibrary::CollectProductionInfoFromSeed(TargetBuildable, Options);
	if (Equipment)
	{
		Equipment->SetProductionInfos(ProductionInfo, Intent);
	}
	return ProductionInfo;
}

FBulkUpgradeProductionInfoSet UBulkUpgradeWidgetPopupHelper::CollectPipelineProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludePipelines, bool bIncludePumps, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent)
{
	FBulkUpgradeCollectionOptions Options;
	Options.bIncludePipelines = bIncludePipelines;
	Options.bIncludePipelinePumps = bIncludePumps;
	Options.bCrossAttachments = bCrossAttachmentsAndStorages;

	FBulkUpgradeProductionInfoSet ProductionInfo = UBulkUpgradeSelectionLibrary::CollectProductionInfoFromSeed(TargetBuildable, Options);
	if (Equipment)
	{
		Equipment->SetProductionInfos(ProductionInfo, Intent);
	}
	return ProductionInfo;
}

FBulkUpgradeProductionInfoSet UBulkUpgradeWidgetPopupHelper::CollectPowerPoleProductionInfo(ABulkUpgradeEquipment* Equipment, AFGBuildable* TargetBuildable, bool bIncludeWires, bool bIncludePowerPoles, bool bIncludePowerPoleWalls, bool bIncludePowerPoleWallDoubles, bool bIncludePowerTowers, bool bCrossAttachmentsAndStorages, EBulkUpgradeProductionInfoIntent Intent)
{
	FBulkUpgradeCollectionOptions Options;
	Options.bIncludeWires = bIncludeWires;
	Options.bIncludePowerPoles = bIncludePowerPoles;
	Options.bIncludePowerPoleWalls = bIncludePowerPoleWalls;
	Options.bIncludePowerPoleWallDoubles = bIncludePowerPoleWallDoubles;
	Options.bIncludePowerTowers = bIncludePowerTowers;
	Options.bCrossAttachments = bCrossAttachmentsAndStorages;

	FBulkUpgradeProductionInfoSet ProductionInfo = UBulkUpgradeSelectionLibrary::CollectProductionInfoFromSeed(TargetBuildable, Options);
	if (Equipment)
	{
		Equipment->SetProductionInfos(ProductionInfo, Intent);
	}
	return ProductionInfo;
}

FBulkUpgradeComboBoxItem UBulkUpgradeWidgetPopupHelper::GetSelectedComboBoxItem(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems)
{
	int32 Index = -1;
	if (OptionName.ToString().IsNumeric())
	{
		LexFromString(Index, *OptionName.ToString());
	}

	// Ensure Index is non-negative and within array bounds
	if (Index >= 0 && ComboBoxItems.IsValidIndex(Index))
	{
		return ComboBoxItems[Index];
	}
	
	return FBulkUpgradeComboBoxItem();
}

TSubclassOf<UFGRecipe> UBulkUpgradeWidgetPopupHelper::GetSelectedRecipe(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems)
{
	return GetSelectedComboBoxItem(OptionName, ComboBoxItems).Recipe;
}

TSubclassOf<UFGBuildDescriptor> UBulkUpgradeWidgetPopupHelper::GetSelectedBuildDescriptor(FName OptionName, const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems)
{
	return GetSelectedComboBoxItem(OptionName, ComboBoxItems).BuildDescriptor;
}

FName UBulkUpgradeWidgetPopupHelper::GetDefaultSelectedOptionName(const TArray<FBulkUpgradeComboBoxItem>& ComboBoxItems, AFGBuildable* TargetBuildable, bool bPreferCurrentType)
{
	if (ComboBoxItems.Num() == 0)
	{
		return NAME_None;
	}

	if (bPreferCurrentType && IsValid(TargetBuildable))
	{
		const UClass* CurrentClass = TargetBuildable->GetClass();
		const FBulkUpgradeComboBoxItem* CurrentItem = ComboBoxItems.FindByPredicate([CurrentClass](const FBulkUpgradeComboBoxItem& Item)
		{
			return CurrentClass && Item.BuildableClass.Get() == CurrentClass;
		});
		if (CurrentItem)
		{
			return CurrentItem->OptionName;
		}
	}

	return ComboBoxItems[0].OptionName;
}

EBulkUpgradeFamily UBulkUpgradeWidgetPopupHelper::ClassifyBuildable(AFGBuildable* Buildable)
{
	return IsValid(Buildable) ? ClassifyBuildableClass(Buildable->GetClass()) : EBulkUpgradeFamily::Unknown;
}

EBulkUpgradeFamily UBulkUpgradeWidgetPopupHelper::ClassifyBuildableClass(TSubclassOf<AFGBuildable> BuildableClass)
{
	if (!BuildableClass)
	{
		return EBulkUpgradeFamily::Unknown;
	}
	if (BuildableClass->IsChildOf(AFGBuildableConveyorBelt::StaticClass()))
	{
		return EBulkUpgradeFamily::ConveyorBelt;
	}
	if (BuildableClass->IsChildOf(AFGBuildableConveyorLift::StaticClass()))
	{
		return EBulkUpgradeFamily::ConveyorLift;
	}
	if (BuildableClass->IsChildOf(AFGBuildableStorage::StaticClass()))
	{
		return EBulkUpgradeFamily::Storage;
	}
	if (BuildableClass->IsChildOf(AFGBuildablePipeline::StaticClass()))
	{
		return EBulkUpgradeFamily::Pipeline;
	}
	if (BuildableClass->IsChildOf(AFGBuildablePipelinePump::StaticClass()))
	{
		const AFGBuildablePipelinePump* PumpDefault = BuildableClass->GetDefaultObject<AFGBuildablePipelinePump>();
		return PumpDefault && PumpDefault->GetDesignHeadLift() > 0.0f ? EBulkUpgradeFamily::PipelinePump : EBulkUpgradeFamily::Unknown;
	}
	if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		return EBulkUpgradeFamily::Wire;
	}
	if (BuildableClass->IsChildOf(AFGBuildablePowerPole::StaticClass()))
	{
		const AFGBuildablePowerPole* PowerPole = BuildableClass->GetDefaultObject<AFGBuildablePowerPole>();
		if (!PowerPole)
		{
			return EBulkUpgradeFamily::Unknown;
		}

		switch (PowerPole->GetPowerPoleType())
		{
		case EPowerPoleType::PPT_POLE:
			return EBulkUpgradeFamily::PowerPole;
		case EPowerPoleType::PPT_WALL:
			return EBulkUpgradeFamily::PowerPoleWall;
		case EPowerPoleType::PPT_WALL_DOUBLE:
			return EBulkUpgradeFamily::PowerPoleWallDouble;
		case EPowerPoleType::PPT_TOWER:
			return EBulkUpgradeFamily::PowerTower;
		default:
			return EBulkUpgradeFamily::Unknown;
		}
	}
	return EBulkUpgradeFamily::Unknown;
}

float UBulkUpgradeWidgetPopupHelper::GetBuildableSortValue(TSubclassOf<AFGBuildable> BuildableClass)
{
	if (!BuildableClass)
	{
		return 0.0f;
	}

	if (BuildableClass->IsChildOf(AFGBuildableConveyorBase::StaticClass()))
	{
		const AFGBuildableConveyorBase* ConveyorDefault = BuildableClass->GetDefaultObject<AFGBuildableConveyorBase>();
		return ConveyorDefault ? ConveyorDefault->GetSpeed() / 2.0f : 0.0f;
	}

	if (BuildableClass->IsChildOf(AFGBuildablePipeline::StaticClass()))
	{
		const AFGBuildablePipeline* PipelineDefault = BuildableClass->GetDefaultObject<AFGBuildablePipeline>();
		const float FlowPerMinute = PipelineDefault ? PipelineDefault->GetFlowLimit() * 60.0f : 0.0f;
		return FlowPerMinute > 0.0f ? FlowPerMinute : GetPipelineFlowLimitFallbackPerMinute(BuildableClass);
	}

	if (BuildableClass->IsChildOf(AFGBuildablePipelinePump::StaticClass()))
	{
		const AFGBuildablePipelinePump* PumpDefault = BuildableClass->GetDefaultObject<AFGBuildablePipelinePump>();
		return PumpDefault ? PumpDefault->GetDesignHeadLift() : 0.0f;
	}

	if (BuildableClass->IsChildOf(AFGBuildableStorage::StaticClass()))
	{
		const AFGBuildableStorage* StorageDefault = BuildableClass->GetDefaultObject<AFGBuildableStorage>();
		return StorageDefault ? static_cast<float>(StorageDefault->mInventorySizeX * StorageDefault->mInventorySizeY) : 0.0f;
	}

	if (BuildableClass->IsChildOf(AFGBuildablePowerPole::StaticClass()))
	{
		const AFGBuildablePowerPole* PowerPoleDefault = BuildableClass->GetDefaultObject<AFGBuildablePowerPole>();
		if (PowerPoleDefault && PowerPoleDefault->GetPowerConnections().Num() > 0 && PowerPoleDefault->GetPowerConnection(0))
		{
			return PowerPoleDefault->GetPowerConnection(0)->GetMaxNumConnections();
		}
	}

	if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const AFGBuildableWire* WireDefault = BuildableClass->GetDefaultObject<AFGBuildableWire>();
		return WireDefault ? WireDefault->mMaxLength / 100.0f : 0.0f;
	}

	return 0.0f;
}

FText UBulkUpgradeWidgetPopupHelper::MakeFamilyLabel(EBulkUpgradeFamily Family)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return LOCTEXT("FamilyBelt", "Conveyor Belt");
	case EBulkUpgradeFamily::ConveyorLift:
		return LOCTEXT("FamilyLift", "Conveyor Lift");
	case EBulkUpgradeFamily::Storage:
		return LOCTEXT("FamilyStorage", "Storage Container");
	case EBulkUpgradeFamily::Pipeline:
		return LOCTEXT("FamilyPipe", "Pipeline");
	case EBulkUpgradeFamily::PipelinePump:
		return LOCTEXT("FamilyPump", "Pipeline Pump");
	case EBulkUpgradeFamily::Wire:
		return LOCTEXT("FamilyWire", "Wire");
	case EBulkUpgradeFamily::PowerPole:
		return LOCTEXT("FamilyPowerPole", "Power Pole");
	case EBulkUpgradeFamily::PowerPoleWall:
		return LOCTEXT("FamilyPowerPoleWall", "Power Pole Wall");
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return LOCTEXT("FamilyPowerPoleWallDouble", "Power Pole Wall Double");
	case EBulkUpgradeFamily::PowerTower:
		return LOCTEXT("FamilyPowerTower", "Power Tower");
	case EBulkUpgradeFamily::Unknown:
	default:
		return LOCTEXT("FamilyUnknown", "Unsupported target");
	}
}

FText UBulkUpgradeWidgetPopupHelper::MakeAmountLabel(float Amount, EBulkUpgradeFamily Family)
{
	if (Amount <= 0.0f)
	{
		return LOCTEXT("UnknownAmountLabel", "Unknown");
	}

	FNumberFormattingOptions WholeNumber;
	WholeNumber.MinimumFractionalDigits = 0;
	WholeNumber.MaximumFractionalDigits = 0;

	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
	case EBulkUpgradeFamily::ConveyorLift:
		return FText::Format(LOCTEXT("RateLabel", "{0} /min"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::Pipeline:
		return FText::Format(LOCTEXT("PipelineFlowRateLabel", "{0} m3/min"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::Storage:
		return FText::Format(LOCTEXT("SlotLabel", "{0} slots"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::PipelinePump:
		return FText::Format(LOCTEXT("HeadLiftLabel", "{0} m"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::PowerPole:
	case EBulkUpgradeFamily::PowerPoleWall:
	case EBulkUpgradeFamily::PowerPoleWallDouble:
	case EBulkUpgradeFamily::PowerTower:
		return FText::Format(LOCTEXT("ConnectionLabel", "{0} connections"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::Wire:
		return FText::Format(LOCTEXT("WireMaxLengthLabel", "{0} m max"), FText::AsNumber(Amount, &WholeNumber));
	case EBulkUpgradeFamily::Unknown:
	default:
		return FText::AsNumber(Amount, &WholeNumber);
	}
}

FName UBulkUpgradeWidgetPopupHelper::GetCheckboxName(TSubclassOf<UFGBuildDescriptor> BuildDescriptor)
{
	return BuildDescriptor
		? FName(*(TEXT("check") + BuildDescriptor->GetName()))
		: NAME_None;
}

void UBulkUpgradeWidgetPopupHelper::ResetCheckedBuildDescriptors()
{
	GCheckedBuildDescriptors.Reset();
}

void UBulkUpgradeWidgetPopupHelper::SetBuildDescriptorChecked(TSubclassOf<UFGBuildDescriptor> BuildDescriptor, bool bChecked)
{
	if (BuildDescriptor)
	{
		GCheckedBuildDescriptors.FindOrAdd(BuildDescriptor.Get()) = bChecked;
	}
}

bool UBulkUpgradeWidgetPopupHelper::IsBuildDescriptorChecked(TSubclassOf<UFGBuildDescriptor> BuildDescriptor, bool bDefaultChecked)
{
	if (!BuildDescriptor)
	{
		return bDefaultChecked;
	}

	if (const bool* Checked = GCheckedBuildDescriptors.Find(BuildDescriptor.Get()))
	{
		return *Checked;
	}
	return bDefaultChecked;
}

#undef LOCTEXT_NAMESPACE
