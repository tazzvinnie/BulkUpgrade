#include "BulkUpgradeContent.h"

#include "BulkUpgradeEquipment.h"
#include "FGCategory.h"
#include "FGItemCategory.h"
#include "FGSchematicCategory.h"
#include "Engine/Texture2D.h"
#include "Resources/FGItemDescriptor.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeContent"

namespace
{
	template <typename T>
	TSubclassOf<T> FindClassCheckedForBulkUpgrade(const TCHAR* Path)
	{
		ConstructorHelpers::FClassFinder<T> ClassFinder(Path);
		return ClassFinder.Succeeded() ? ClassFinder.Class : nullptr;
	}
}

UBulkUpgradeToolRecipe::UBulkUpgradeToolRecipe()
{
	mDisplayNameOverride = true;
	mDisplayName = LOCTEXT("RecipeName", "Bulk Upgrade Tool");
	mManufacturingMenuPriority = 0.0f;
	mManufactoringDuration = 8.0f;
	mManualManufacturingMultiplier = 1.0f;
	mOverriddenCategory = FindClassCheckedForBulkUpgrade<UFGItemCategory>(
		TEXT("/Game/FactoryGame/Resource/ItemCategories/Cat_Hands"));

	mProducedIn.Add(TSoftClassPtr<UObject>(
		FSoftClassPath(TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"))));

	TSubclassOf<UFGItemDescriptor> ObjectScanner = FindClassCheckedForBulkUpgrade<UFGItemDescriptor>(
		TEXT("/Game/FactoryGame/Resource/Equipment/GemstoneScanner/BP_EquipmentDescriptorObjectScanner"));
	TSubclassOf<UFGItemDescriptor> ReinforcedPlate = FindClassCheckedForBulkUpgrade<UFGItemDescriptor>(
		TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced"));
	TSubclassOf<UFGItemDescriptor> Wire = FindClassCheckedForBulkUpgrade<UFGItemDescriptor>(
		TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire"));

	if (ObjectScanner)
	{
		mIngredients.Add(FItemAmount(ObjectScanner, 1));
	}
	if (ReinforcedPlate)
	{
		mIngredients.Add(FItemAmount(ReinforcedPlate, 5));
	}
	if (Wire)
	{
		mIngredients.Add(FItemAmount(Wire, 20));
	}

	mProduct.Add(FItemAmount(UBulkUpgradeEquipmentDescriptor::StaticClass(), 1));
}

UBulkUpgradeToolUnlock::UBulkUpgradeToolUnlock()
{
	mRecipes.Add(UBulkUpgradeToolRecipe::StaticClass());
}

UBulkUpgradeToolSchematic::UBulkUpgradeToolSchematic()
{
	mType = ESchematicType::EST_Milestone;
	mDisplayName = LOCTEXT("SchematicName", "Bulk Upgrade Tool");
	mDescription = LOCTEXT("SchematicDescription", "Unlocks the Bulk Upgrade Tool recipe for the Equipment Workshop.");
	mTechTier = 1;
	mMenuPriority = 0.0f;
	mTimeToComplete = 0.0f;
	mIsPlayerSpecific = false;
	mDependenciesBlocksSchematicAccess = false;
	mHiddenUntilDependenciesMet = false;

	mSchematicCategory = FindClassCheckedForBulkUpgrade<UFGSchematicCategory>(
		TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Production"));

	TSubclassOf<UFGItemDescriptor> ReinforcedPlate = FindClassCheckedForBulkUpgrade<UFGItemDescriptor>(
		TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced"));
	TSubclassOf<UFGItemDescriptor> Wire = FindClassCheckedForBulkUpgrade<UFGItemDescriptor>(
		TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire"));

	if (ReinforcedPlate)
	{
		mCost.Add(FItemAmount(ReinforcedPlate, 10));
	}
	if (Wire)
	{
		mCost.Add(FItemAmount(Wire, 50));
	}

	UBulkUpgradeToolUnlock* RecipeUnlock = CreateDefaultSubobject<UBulkUpgradeToolUnlock>(TEXT("Unlock_BulkUpgradeToolRecipe"));
	mUnlocks.Add(RecipeUnlock);

	static ConstructorHelpers::FObjectFinder<UTexture2D> SmallIcon(
		TEXT("/BulkUpgrade/Icons/MassUpgrade-icon-64.MassUpgrade-icon-64"));
	if (SmallIcon.Succeeded())
	{
		mSmallSchematicIcon = SmallIcon.Object;
	}

	static ConstructorHelpers::FObjectFinder<UTexture2D> BigIcon(
		TEXT("/BulkUpgrade/Icons/MassUpgrade-icon-256.MassUpgrade-icon-256"));
	if (BigIcon.Succeeded())
	{
		mSchematicIcon.SetResourceObject(BigIcon.Object);
		mSchematicIcon.ImageSize = FVector2D(256.0f, 256.0f);
	}
}

#undef LOCTEXT_NAMESPACE
