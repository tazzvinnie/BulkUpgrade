#include "BulkUpgradeEquipment.h"

#include "BulkUpgrade.h"
#include "BulkUpgradeExecutor.h"
#include "BulkUpgradeMassUpgradePopup.h"
#include "BulkUpgradePlanner.h"
#include "BulkUpgradeWidgetPopupHelper.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "FGCategory.h"
#include "FGCharacterPlayer.h"
#include "FGItemCategory.h"
#include "FGOutlineComponent.h"
#include "FGPlayerController.h"
#include "FGRecipeManager.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Materials/MaterialInterface.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeEquipment"

namespace
{
FText MakeTargetAmountLabel(const AFGBuildable* Buildable)
{
	if (!IsValid(Buildable))
	{
		return LOCTEXT("InvalidBuildableAmount", "Invalid");
	}

	float LengthCentimeters = 0.0f;
	if (const AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Buildable))
	{
		if (IsValid(Conveyor))
		{
			LengthCentimeters = Conveyor->GetLength();
		}
	}
	else if (const AFGBuildablePipeline* Pipeline = Cast<AFGBuildablePipeline>(Buildable))
	{
		if (IsValid(Pipeline))
		{
			LengthCentimeters = Pipeline->GetLength();
		}
	}
	else if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Buildable))
	{
		if (IsValid(Wire))
		{
			LengthCentimeters = Wire->GetLength();
		}
	}

	if (LengthCentimeters <= 0.0f)
	{
		return LOCTEXT("SingleBuildableAmount", "1");
	}

	const int32 Meters = FMath::Max(1, FMath::RoundToInt(LengthCentimeters / 100.0f));
	return FText::Format(LOCTEXT("AmountMeters", "{0} m"), FText::AsNumber(Meters));
}

FText MakeRefundLabel(const AFGBuildable* Buildable)
{
	if (!IsValid(Buildable))
	{
		return LOCTEXT("NoRefund", "Refund\n-");
	}

	TSubclassOf<UFGRecipe> BuiltWithRecipe = Buildable->GetBuiltWithRecipe();
	if (!BuiltWithRecipe)
	{
		return LOCTEXT("NoRefund", "Refund\n-");
	}

	TArray<FItemAmount> Ingredients = UFGRecipe::GetIngredients(BuiltWithRecipe);
	if (Ingredients.Num() == 0)
	{
		return LOCTEXT("NoRefundIngredients", "Refund\n-");
	}

	if (!Ingredients[0].ItemClass)
	{
		return LOCTEXT("NoRefundIngredients", "Refund\n-");
	}

	const FText ItemName = UFGItemDescriptor::GetItemName(Ingredients[0].ItemClass);
	return FText::Format(LOCTEXT("RefundOneItem", "Refund\n{0} {1}"), ItemName, FText::AsNumber(Ingredients[0].Amount));
}

}

UBulkUpgradeEquipmentDescriptor::UBulkUpgradeEquipmentDescriptor()
{
	mUseDisplayNameAndDescription = true;
	mDisplayName = LOCTEXT("ToolName", "Bulk Upgrade Tool");
	mAbbreviatedDisplayName = LOCTEXT("ToolShortName", "Bulk Upgrade");
	mDescription = LOCTEXT("ToolDescription", "Local development tool for previewing and testing logistics bulk upgrades.");
	mForm = EResourceForm::RF_SOLID;
	mStackSize = EStackSize::SS_ONE;
	// Native descriptor CDOs do not get the Blueprint asset PostLoad cache refresh before inventory checks.
	mCachedStackSize = 1;
	mCanBeDiscarded = true;
	mRememberPickUp = false;
	mEquipmentClass = ABulkUpgradeEquipment::StaticClass();

	static ConstructorHelpers::FClassFinder<UFGItemCategory> HandsCategory(
		TEXT("/Game/FactoryGame/Resource/ItemCategories/Cat_Hands"));
	if (HandsCategory.Succeeded())
	{
		mCategory = HandsCategory.Class;
	}

	static ConstructorHelpers::FObjectFinder<UTexture2D> SmallIcon(
		TEXT("/BulkUpgrade/Icons/MassUpgrade-icon-64.MassUpgrade-icon-64"));
	if (SmallIcon.Succeeded())
	{
		mSmallIcon = SmallIcon.Object;
	}

	static ConstructorHelpers::FObjectFinder<UTexture2D> BigIcon(
		TEXT("/BulkUpgrade/Icons/MassUpgrade-icon-256.MassUpgrade-icon-256"));
	if (BigIcon.Succeeded())
	{
		mPersistentBigIcon = BigIcon.Object;
	}
}

ABulkUpgradeEquipment::ABulkUpgradeEquipment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	mEquipmentSlot = EEquipmentSlot::ES_ARMS;
	mArmAnimation = EArmEquipment::AE_Generic1Hand;
	mAttachSocket = TEXT("hand_rSocket");
	mNeedsDefaultEquipmentMappingContext = true;
	mDefaultEquipmentActions =
		static_cast<uint8>(EDefaultEquipmentAction::PrimaryFire) |
		static_cast<uint8>(EDefaultEquipmentAction::SecondaryFire);

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	EquipmentMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CrystalScanner_skl"));
	EquipmentMesh->SetMobility(EComponentMobility::Movable);
	EquipmentMesh->SetupAttachment(SceneRoot);
	EquipmentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EquipmentMesh->SetGenerateOverlapEvents(false);
	EquipmentMesh->SetRelativeLocation(FVector::ZeroVector);
	EquipmentMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	EquipmentMesh->SetRelativeScale3D(FVector(1.0f));
	EquipmentMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ScannerMesh(
		TEXT("/BulkUpgrade/Equipment/CrystalScanner_skl_V1.CrystalScanner_skl_V1"));
	if (ScannerMesh.Succeeded())
	{
		EquipmentMesh->SetSkeletalMesh(ScannerMesh.Object);
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> LegacyScannerMesh(
			TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Mesh/SK_ObjectScanner_01.SK_ObjectScanner_01"));
		if (LegacyScannerMesh.Succeeded())
		{
			EquipmentMesh->SetSkeletalMesh(LegacyScannerMesh.Object);
		}
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterial(
		TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Material/MI_ObjectScanner_01.MI_ObjectScanner_01"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScreenMaterial(
		TEXT("/Game/FactoryGame/Interface/UI/Material/Material_UI_ObjectScanner.Material_UI_ObjectScanner"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassMaterial(
		TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Material/MI_GlassBuilding_Inst.MI_GlassBuilding_Inst"));
	if (BodyMaterial.Succeeded())
	{
		EquipmentMesh->SetMaterial(0, BodyMaterial.Object);
	}
	if (ScreenMaterial.Succeeded())
	{
		EquipmentMesh->SetMaterial(1, ScreenMaterial.Object);
	}
	if (GlassMaterial.Succeeded())
	{
		EquipmentMesh->SetMaterial(2, GlassMaterial.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FirstPersonBodyMaterial(
		TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Material/MI_ObjectScanner_01_1p.MI_ObjectScanner_01_1p"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FirstPersonScreenMaterial(
		TEXT("/Game/FactoryGame/Interface/UI/Material/Material_UI_ObjectScanner_Inst_1p.Material_UI_ObjectScanner_Inst_1p"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FirstPersonGlassMaterial(
		TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Material/MI_GlassBuilding_Inst_1p.MI_GlassBuilding_Inst_1p"));

	FFirstPersonMaterialArray FirstPersonMaterials;
	if (FirstPersonBodyMaterial.Succeeded())
	{
		FirstPersonMaterials.FirstPersonMaterials.Add(FirstPersonBodyMaterial.Object);
	}
	if (FirstPersonScreenMaterial.Succeeded())
	{
		FirstPersonMaterials.FirstPersonMaterials.Add(FirstPersonScreenMaterial.Object);
	}
	if (FirstPersonGlassMaterial.Succeeded())
	{
		FirstPersonMaterials.FirstPersonMaterials.Add(FirstPersonGlassMaterial.Object);
	}
	if (FirstPersonMaterials.FirstPersonMaterials.Num() == 3)
	{
		mComponentNameToFirstPersonMaterials.Add(EquipmentMesh->GetFName(), FirstPersonMaterials);
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade equipment first-person material remap is incomplete for %s."), *EquipmentMesh->GetName());
	}

	static ConstructorHelpers::FObjectFinder<UAnimMontage> EquipMontage1P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/FirstPerson/ObjectScannerEquip_01_Montage.ObjectScannerEquip_01_Montage"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> EquipMontage3P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/ObjectScannerEquip_01_Montage.ObjectScannerEquip_01_Montage"));
	FFGWeightedEquipmentMontage EquipMontage;
	EquipMontage.Weight = 1.0f;
	if (EquipMontage1P.Succeeded())
	{
		EquipMontage.Montage_1P = EquipMontage1P.Object;
	}
	if (EquipMontage3P.Succeeded())
	{
		EquipMontage.Montage_3P = EquipMontage3P.Object;
	}
	if (EquipMontage.Montage_1P || EquipMontage.Montage_3P)
	{
		mEquipMontage.Montages.Add(EquipMontage);
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade equipment equip montages were not found."));
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdlePose1P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/FirstPerson/ObjectScannerIdle_01.ObjectScannerIdle_01"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdlePose3P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/ObjectScannerIdle_01.ObjectScannerIdle_01"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> CrouchPose3P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/CrouchMedkitPose_01.CrouchMedkitPose_01"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> SlidePose3P(
		TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/SlideMedkitPose_01.SlideMedkitPose_01"));
	static ConstructorHelpers::FObjectFinder<UAimOffsetBlendSpace> AttachmentIdleAO(
		TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/Idle_AO.Idle_AO"));
	if (IdlePose1P.Succeeded())
	{
		mIdlePoseAnimation = IdlePose1P.Object;
	}
	if (IdlePose3P.Succeeded())
	{
		mIdlePoseAnimation3p = IdlePose3P.Object;
	}
	if (CrouchPose3P.Succeeded())
	{
		mCrouchPoseAnimation3p = CrouchPose3P.Object;
	}
	if (SlidePose3P.Succeeded())
	{
		mSlidePoseAnimation3p = SlidePose3P.Object;
	}
	if (AttachmentIdleAO.Succeeded())
	{
		mAttachmentIdleAO = AttachmentIdleAO.Object;
	}

	TargetPointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TargetPointLight"));
	TargetPointLight->SetupAttachment(EquipmentMesh);
	TargetPointLight->SetIntensity(0.0f);
	TargetPointLight->SetLightColor(FLinearColor::Green);
	TargetPointLight->SetAttenuationRadius(250.0f);
	TargetPointLight->SetVisibility(false, true);
}

void ABulkUpgradeEquipment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsEquipped() || !IsLocalInstigator())
	{
		return;
	}

	if (IsUpgradePopupOpen())
	{
		return;
	}

	UpdateTarget();
}

void ABulkUpgradeEquipment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseUpgradePopup();
	Super::EndPlay(EndPlayReason);
}

void ABulkUpgradeEquipment::Equip(AFGCharacterPlayer* Character)
{
	Super::Equip(Character);
	if (EquipmentMesh)
	{
		UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade tool equipped: mesh=%s component=%s socket=%s armMode=%d visible=%s relative=%s/%s/%s world=%s extent=%s firstPersonMaterials=%d equipMontages=%d"),
			*GetNameSafe(EquipmentMesh->GetSkeletalMeshAsset()),
			*EquipmentMesh->GetName(),
			*mAttachSocket.ToString(),
			static_cast<int32>(mArmAnimation),
			EquipmentMesh->IsVisible() ? TEXT("true") : TEXT("false"),
			*EquipmentMesh->GetRelativeLocation().ToString(),
			*EquipmentMesh->GetRelativeRotation().ToString(),
			*EquipmentMesh->GetRelativeScale3D().ToString(),
			*EquipmentMesh->GetComponentLocation().ToString(),
			*EquipmentMesh->Bounds.BoxExtent.ToString(),
			mComponentNameToFirstPersonMaterials.Contains(EquipmentMesh->GetFName()) ? mComponentNameToFirstPersonMaterials[EquipmentMesh->GetFName()].FirstPersonMaterials.Num() : 0,
			mEquipMontage.Montages.Num());
	}
	UpdateTarget();
}

void ABulkUpgradeEquipment::UnEquip()
{
	CloseUpgradePopup();
	HideTargetOutline(GetInstigatorCharacter());
	SetTargetLightEnabled(false);
	targetBuildable = nullptr;
	Super::UnEquip();
}

void ABulkUpgradeEquipment::SetEquipmentVisibility_Implementation(bool bNewEquipmentVisible)
{
	Super::SetEquipmentVisibility_Implementation(bNewEquipmentVisible);

	if (TargetPointLight)
	{
		const bool bTargetLightVisible = bNewEquipmentVisible && targetBuildable != nullptr;
		TargetPointLight->SetIntensity(bTargetLightVisible ? 50.0f : 0.0f);
		TargetPointLight->SetVisibility(bTargetLightVisible, true);
	}
}

void ABulkUpgradeEquipment::SetProductionInfos(const FBulkUpgradeProductionInfoSet& ProductionInfo, EBulkUpgradeProductionInfoIntent Intent)
{
	OnSetProductionInfos.Broadcast(ProductionInfo, Intent);
}

void ABulkUpgradeEquipment::HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action, EDefaultEquipmentActionEvent ActionEvent)
{
	Super::HandleDefaultEquipmentActionEvent(Action, ActionEvent);

	if (Action == EDefaultEquipmentAction::PrimaryFire && ActionEvent == EDefaultEquipmentActionEvent::Pressed)
	{
		PrimaryFirePressed();
	}
	else if (Action == EDefaultEquipmentAction::SecondaryFire && ActionEvent == EDefaultEquipmentActionEvent::Pressed)
	{
		SecondaryFirePressed();
	}
}

void ABulkUpgradeEquipment::UpdateTarget()
{
	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	AFGPlayerController* PlayerController = Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		SetTarget(nullptr);
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BulkUpgradeToolTrace), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Character);

	FHitResult Hit;
	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * 5000.0f;
	GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, QueryParams);

	AFGBuildable* HitBuildable = Cast<AFGBuildable>(Hit.GetActor());
	SetTarget(IsSupportedTarget(HitBuildable) ? HitBuildable : nullptr);
}

void ABulkUpgradeEquipment::SetTarget(AFGBuildable* NewTarget)
{
	if (targetBuildable == NewTarget)
	{
		return;
	}

	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	HideTargetOutline(Character);

	targetBuildable = NewTarget;
	if (targetBuildable)
	{
		ShowTargetOutline(Character, targetBuildable);
		setBuildDescriptor(targetBuildable->GetBuiltWithDescriptor());
	}
	SetTargetLightEnabled(targetBuildable != nullptr);
}

void ABulkUpgradeEquipment::SetTargetLightEnabled(bool bEnabled)
{
	if (TargetPointLight)
	{
		TargetPointLight->SetIntensity(bEnabled ? 50.0f : 0.0f);
		TargetPointLight->SetVisibility(bEnabled, true);
	}
}

void ABulkUpgradeEquipment::ShowTargetOutline(AFGCharacterPlayer* Character, AActor* Actor)
{
	if (Character && Actor && Character->GetOutline())
	{
		Character->GetOutline()->ShowOutline(Actor, EOutlineColor::OC_USABLE);
		OutlinedActor = Actor;
	}
}

void ABulkUpgradeEquipment::HideTargetOutline(AFGCharacterPlayer* Character)
{
	if (Character && OutlinedActor && Character->GetOutline())
	{
		Character->GetOutline()->HideOutline(OutlinedActor);
	}
	OutlinedActor = nullptr;
}

void ABulkUpgradeEquipment::PrimaryFirePressed()
{
	if (IsUpgradePopupOpen())
	{
		return;
	}

	if (!targetBuildable)
	{
		SendLocalStatusMessage(TEXT("BulkUpgrade: no supported MassUpgrade target is aimed at."));
		return;
	}

	ShowUpgradePopup();
}

void ABulkUpgradeEquipment::SecondaryFirePressed()
{
}

bool ABulkUpgradeEquipment::IsUpgradePopupOpen() const
{
	return ActivePopupUserWidget != nullptr;
}

void ABulkUpgradeEquipment::ShowUpgradePopup()
{
	if (!targetBuildable)
	{
		SendLocalStatusMessage(TEXT("BulkUpgrade: no supported MassUpgrade target is aimed at."));
		return;
	}

	if (!GEngine || !GEngine->GameViewport)
	{
		SendLocalStatusMessage(TEXT("BulkUpgrade: game viewport is not available for the popup."));
		return;
	}

	CloseUpgradePopup();

	const EBulkUpgradeFamily Family = ClassifyBuildable(targetBuildable);
	if (bUseBlueprintMassUpgradePopupEvents)
	{
		switch (Family)
		{
		case EBulkUpgradeFamily::ConveyorBelt:
		case EBulkUpgradeFamily::ConveyorLift:
		case EBulkUpgradeFamily::Storage:
			ShowConveyorPopupWidget();
			return;
		case EBulkUpgradeFamily::Pipeline:
		case EBulkUpgradeFamily::PipelinePump:
			ShowPipelinePopupWidget();
			return;
		case EBulkUpgradeFamily::Wire:
		case EBulkUpgradeFamily::PowerPole:
		case EBulkUpgradeFamily::PowerPoleWall:
		case EBulkUpgradeFamily::PowerPoleWallDouble:
		case EBulkUpgradeFamily::PowerTower:
			ShowPowerPolePopupWidget();
			return;
		case EBulkUpgradeFamily::Unknown:
		default:
			break;
		}
	}

	const float SourceSortValue = UBulkUpgradeWidgetPopupHelper::GetBuildableSortValue(targetBuildable->GetClass());
	const TArray<FBulkUpgradeComboBoxItem> Options = UBulkUpgradeWidgetPopupHelper::GetRecipeOptionsByFamily(this, Family);
	const FText SourceRateLabel = UBulkUpgradeWidgetPopupHelper::MakeAmountLabel(SourceSortValue, Family);
	const FText AmountLabel = MakeTargetAmountLabel(targetBuildable);
	const FText RefundLabel = MakeRefundLabel(targetBuildable);

	SelectedPopupRecipe = nullptr;
	bool bHasDifferentOption = false;
	for (const FBulkUpgradeComboBoxItem& Option : Options)
	{
		if (CanTargetRecipeChangeTarget(Option.Recipe))
		{
			bHasDifferentOption = true;
		}
	}
	if (Options.Num() > 0)
	{
		const float CurrentAmount = SourceSortValue;
		const UClass* CurrentClass = targetBuildable ? targetBuildable->GetClass() : nullptr;
		const FBulkUpgradeComboBoxItem* HighestHigherOption = nullptr;
		const FBulkUpgradeComboBoxItem* FirstDifferentOption = nullptr;
		const FBulkUpgradeComboBoxItem* CurrentOption = nullptr;
		for (const FBulkUpgradeComboBoxItem& Option : Options)
		{
			const bool bIsCurrentOption = CurrentClass && Option.BuildableClass.Get() == CurrentClass;
			if (bIsCurrentOption)
			{
				CurrentOption = &Option;
			}
			else if (!FirstDifferentOption)
			{
				FirstDifferentOption = &Option;
			}

			if (!bIsCurrentOption && Option.Amount > CurrentAmount)
			{
				if (!HighestHigherOption || Option.Amount > HighestHigherOption->Amount)
				{
					HighestHigherOption = &Option;
				}
			}
		}

		SelectedPopupRecipe = HighestHigherOption
			? HighestHigherOption->Recipe
			: FirstDifferentOption
				? FirstDifferentOption->Recipe
				: CurrentOption
					? CurrentOption->Recipe
					: Options[0].Recipe;
	}

	const FText InitialStatus = Options.Num() == 0
		? LOCTEXT("NoTypesStatus", "No buildable logistics recipes are unlocked for this target type.")
		: bHasDifferentOption
			? LOCTEXT("PopupInitialStatus", "Choose a MassUpgrade target type, then upgrade the selected buildables.")
			: LOCTEXT("PopupMaxStatus", "This target has no different unlocked type in this family. The list is shown for reference.");

	if (ShowMassUpgradePopupWidget(Family, Options, SourceRateLabel, AmountLabel, RefundLabel, InitialStatus))
	{
		SendLocalStatusMessage(FString::Printf(TEXT("BulkUpgrade: opened MassUpgrade popup for %s."), *GetNameSafe(targetBuildable)));
		return;
	}

	SendLocalStatusMessage(TEXT("BulkUpgrade: failed to create MassUpgrade popup widget."));
}

bool ABulkUpgradeEquipment::ShowMassUpgradePopupWidget(
	EBulkUpgradeFamily Family,
	const TArray<FBulkUpgradeComboBoxItem>& Options,
	const FText& SourceAmountLabel,
	const FText& TargetAmountLabel,
	const FText& RefundLabel,
	const FText& InitialStatus)
{
	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	APlayerController* PlayerController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PlayerController)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade MassUpgrade popup fallback: player controller unavailable."));
		return false;
	}

	UBulkUpgradeMassUpgradePopup* Popup = CreateWidget<UBulkUpgradeMassUpgradePopup>(PlayerController, UBulkUpgradeMassUpgradePopup::StaticClass());
	if (!Popup)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade MassUpgrade popup fallback: native popup widget could not be created."));
		return false;
	}

	Popup->Configure(this, targetBuildable, Family, Options, SelectedPopupRecipe, SourceAmountLabel, TargetAmountLabel, RefundLabel, InitialStatus);
	Popup->AddToViewport(80);
	ActivePopupUserWidget = Popup;
	UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade MassUpgrade popup widget opened: family=%d options=%d target=%s"),
		static_cast<int32>(Family),
		Options.Num(),
		*GetNameSafe(targetBuildable));

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Popup->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);

	return true;
}

void ABulkUpgradeEquipment::CloseUpgradePopup()
{
	if (ActivePopupUserWidget)
	{
		ActivePopupUserWidget->RemoveFromParent();
		ActivePopupUserWidget = nullptr;
	}

	SelectedPopupRecipe = nullptr;

	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	APlayerController* PlayerController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (PlayerController)
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

FReply ABulkUpgradeEquipment::SelectTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe)
{
	SelectedPopupRecipe = TargetRecipe;
	if (!SelectedPopupRecipe)
	{
		SetPopupStatus(LOCTEXT("RecipeSelectionMissing", "No target type is selected."));
		return FReply::Handled();
	}

	const FText RecipeName = UFGRecipe::GetRecipeName(SelectedPopupRecipe);
	if (CanTargetRecipeChangeTarget(SelectedPopupRecipe))
	{
		if (CanCommitSelectedFamily())
		{
			SetPopupStatus(FText::Format(LOCTEXT("RecipeSelectedCanChange", "Selected {0}. Preview first, then upgrade the connected line."), RecipeName));
		}
		else
		{
			SetPopupStatus(FText::Format(LOCTEXT("RecipeSelectedPreviewOnly", "Selected {0}. Preview is available; this target family is not commit-enabled yet."), RecipeName));
		}
	}
	else
	{
		SetPopupStatus(FText::Format(LOCTEXT("RecipeSelectedSame", "Selected {0}. This is already the targeted type, so Upgrade is blocked."), RecipeName));
	}
	return FReply::Handled();
}

FReply ABulkUpgradeEquipment::PreviewSelectedRecipe(EBulkUpgradeScope Scope)
{
	if (!SelectedPopupRecipe)
	{
		SetPopupStatus(LOCTEXT("PreviewNoSelection", "Select a target type before previewing."));
		return FReply::Handled();
	}
	return PreviewTargetRecipe(SelectedPopupRecipe, Scope);
}

FReply ABulkUpgradeEquipment::UpgradeSelectedRecipe()
{
	UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade upgrade button pressed: target=%s selectedRecipe=%s canChange=%s canCommit=%s popupOpen=%s"),
		*GetNameSafe(targetBuildable),
		*GetNameSafe(SelectedPopupRecipe.Get()),
		CanTargetRecipeChangeTarget(SelectedPopupRecipe) ? TEXT("true") : TEXT("false"),
		CanCommitSelectedFamily() ? TEXT("true") : TEXT("false"),
		IsUpgradePopupOpen() ? TEXT("true") : TEXT("false"));

	if (!SelectedPopupRecipe)
	{
		SetPopupStatus(LOCTEXT("UpgradeNoSelection", "Select a target type before upgrading."));
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade upgrade blocked: no selected recipe."));
		return FReply::Handled();
	}

	if (!CanTargetRecipeChangeTarget(SelectedPopupRecipe))
	{
		SetPopupStatus(LOCTEXT("UpgradeSameType", "Upgrade blocked: select a type that differs from the targeted segment."));
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade upgrade blocked: selected recipe does not change the target class."));
		return FReply::Handled();
	}

	if (!CanCommitSelectedFamily())
	{
		SetPopupStatus(LOCTEXT("UpgradeFamilyPreviewOnly", "Upgrade blocked for this family. Preview remains safe."));
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade upgrade blocked: family is not commit-enabled for target=%s."), *GetNameSafe(targetBuildable));
		return FReply::Handled();
	}

	FBulkUpgradeRequest Request = MakeRequest(SelectedPopupRecipe, EBulkUpgradeScope::ConnectedNetwork);
	Request.MaxOperations = 1000;
	return UpgradeSelectedRequest(Request);
}

FReply ABulkUpgradeEquipment::PreviewTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe, EBulkUpgradeScope Scope)
{
	if (!targetBuildable || !TargetRecipe)
	{
		SetPopupStatus(LOCTEXT("PreviewMissingTarget", "Preview failed: target or recipe is no longer valid."));
		return FReply::Handled();
	}

	const FBulkUpgradeRequest Request = MakeRequest(TargetRecipe, Scope);
	const FBulkUpgradePlan Plan = UBulkUpgradePlanner::BuildPlan(this, targetBuildable, Request);
	const FText Prefix = Scope == EBulkUpgradeScope::Single
		? LOCTEXT("PreviewSinglePrefix", "Single preview")
		: LOCTEXT("PreviewRunPrefix", "Connected-run preview");
	const FText Summary = MakePlanSummary(Plan, Prefix);
	SetPopupStatus(Summary);
	SendLocalStatusMessage(Summary.ToString());
	return FReply::Handled();
}

FReply ABulkUpgradeEquipment::UpgradeSingleTargetRecipe(TSubclassOf<UFGRecipe> TargetRecipe)
{
	if (!targetBuildable || !TargetRecipe)
	{
		SetPopupStatus(LOCTEXT("UpgradeMissingTarget", "Upgrade failed: target or recipe is no longer valid."));
		return FReply::Handled();
	}

	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	if (!Character)
	{
		SetPopupStatus(LOCTEXT("UpgradeMissingCharacter", "Upgrade failed: player character is unavailable."));
		return FReply::Handled();
	}

	FBulkUpgradeRequest Request = MakeRequest(TargetRecipe, EBulkUpgradeScope::ConnectedNetwork);
	Request.MaxOperations = 1000;
	return UpgradeSelectedRequest(Request);
}

FReply ABulkUpgradeEquipment::UpgradeSelectedRequest(const FBulkUpgradeRequest& Request)
{
	if (!targetBuildable)
	{
		SetPopupStatus(LOCTEXT("UpgradeMissingTargetRequest", "Upgrade failed: target is no longer valid."));
		return FReply::Handled();
	}

	AFGCharacterPlayer* Character = GetInstigatorCharacter();
	if (!Character)
	{
		SetPopupStatus(LOCTEXT("UpgradeMissingCharacterRequest", "Upgrade failed: player character is unavailable."));
		return FReply::Handled();
	}

	FBulkUpgradePlan Plan = UBulkUpgradePlanner::BuildPlan(this, targetBuildable, Request);
	UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade connected upgrade plan: rows=%d eligible=%d skipped=%d failed=%d target=%s family=%d recipe=%s pipeRecipe=%s pumpRecipe=%s allowPipes=%s allowPumps=%s"),
		Plan.Rows.Num(),
		Plan.EligibleCount,
		Plan.SkippedCount,
		Plan.FailedCount,
		*GetNameSafe(targetBuildable),
		static_cast<int32>(Request.Family),
		*GetNameSafe(Request.TargetRecipe.Get()),
		*GetNameSafe(Request.TargetPipelineRecipe.Get()),
		*GetNameSafe(Request.TargetPipelinePumpRecipe.Get()),
		Request.bAllowPipelines ? TEXT("true") : TEXT("false"),
		Request.bAllowPipelinePumps ? TEXT("true") : TEXT("false"));
	if (!Plan.HasEligibleRows())
	{
		const FText Summary = MakePlanSummary(Plan, LOCTEXT("UpgradeNotEligiblePrefix", "Upgrade not eligible"));
		SetPopupStatus(Summary);
		SendLocalStatusMessage(Summary.ToString());
		return FReply::Handled();
	}

	FBulkUpgradeCommitOptions Options;
	Options.bDryRun = false;
	Options.ExecutionProfile = EBulkUpgradeExecutionProfile::LocalOnlyDev;
	Options.bAcknowledgeLocalSaveBackup = true;
	Options.bAllowUnverifiedWorldMutation = true;
	Options.MaxOperationsPerCall = Request.MaxOperations;

	const FBulkUpgradeCommitReport Report = UBulkUpgradeExecutor::CommitPlan(this, Character, Plan, Options);
	for (const FText& Message : Report.Messages)
	{
		UE_LOG(LogBulkUpgrade, Display, TEXT("Commit message: %s"), *Message.ToString());
	}

	FText FirstFailure = FText::GetEmpty();
	for (const FBulkUpgradePreviewRow& Row : Plan.Rows)
	{
		if (Row.Status == EBulkUpgradeRowStatus::Failed && !Row.StatusText.IsEmpty())
		{
			FirstFailure = Row.StatusText;
			break;
		}
	}

	const FText Summary = Report.CommittedCount == 0 && Report.FailedCount > 0 && !FirstFailure.IsEmpty()
		? FText::Format(
			LOCTEXT("CommitReportNoChangesWithFailure", "Upgrade line: no world changes; attempted {0}, failed {1}, skipped {2}. First failure: {3}"),
			FText::AsNumber(Report.AttemptedCount),
			FText::AsNumber(Report.FailedCount),
			FText::AsNumber(Report.SkippedCount),
			FirstFailure)
		: FText::Format(
			LOCTEXT("CommitReportSummary", "Upgrade line: attempted {0}, committed {1}, failed {2}, skipped {3}."),
			FText::AsNumber(Report.AttemptedCount),
			FText::AsNumber(Report.CommittedCount),
			FText::AsNumber(Report.FailedCount),
			FText::AsNumber(Report.SkippedCount));
	SetPopupStatus(Summary);
	SendLocalStatusMessage(Summary.ToString());

	if (Report.bCommittedWorldChanges)
	{
		HideTargetOutline(Character);
		targetBuildable = nullptr;
		UpdateTarget();
	}

	return FReply::Handled();
}

FBulkUpgradeRequest ABulkUpgradeEquipment::MakeRequest(TSubclassOf<UFGRecipe> TargetRecipe, EBulkUpgradeScope Scope) const
{
	FBulkUpgradeRequest Request;
	Request.Family = ClassifyBuildable(targetBuildable);
	Request.Scope = Scope;
	Request.TargetRecipe = TargetRecipe;
	switch (Request.Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		Request.TargetConveyorBeltRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::ConveyorLift:
		Request.TargetConveyorLiftRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::Storage:
		Request.TargetStorageRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::Pipeline:
		Request.TargetPipelineRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::PipelinePump:
		Request.TargetPipelinePumpRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::Wire:
		Request.TargetWireRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::PowerPole:
		Request.TargetPowerPoleRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::PowerPoleWall:
		Request.TargetPowerPoleWallRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		Request.TargetPowerPoleWallDoubleRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::PowerTower:
		Request.TargetPowerTowerRecipe = TargetRecipe;
		break;
	case EBulkUpgradeFamily::Unknown:
	default:
		// Handle unknown family gracefully
		break;
	}
	Request.MaxOperations = Scope == EBulkUpgradeScope::Single ? 1 : 50;
	Request.ExecutionProfile = EBulkUpgradeExecutionProfile::LocalOnlyDev;
	Request.CostPolicy = EBulkUpgradeCostPolicy::FreeLocalDev;
	Request.bAllowConnectedScope = Scope != EBulkUpgradeScope::Single;
	Request.bAllowPipelines =
		Request.Family == EBulkUpgradeFamily::Pipeline ||
		Request.Family == EBulkUpgradeFamily::PipelinePump;
	Request.bAllowStorage = Request.Family == EBulkUpgradeFamily::Storage;
	Request.bAllowPipelinePumps =
		Request.Family == EBulkUpgradeFamily::Pipeline ||
		Request.Family == EBulkUpgradeFamily::PipelinePump;
	Request.bAllowPower =
		Request.Family == EBulkUpgradeFamily::Wire ||
		Request.Family == EBulkUpgradeFamily::PowerPole ||
		Request.Family == EBulkUpgradeFamily::PowerPoleWall ||
		Request.Family == EBulkUpgradeFamily::PowerPoleWallDouble ||
		Request.Family == EBulkUpgradeFamily::PowerTower;
	Request.bEstimateCosts = false;
	return Request;
}

FText ABulkUpgradeEquipment::MakePlanSummary(const FBulkUpgradePlan& Plan, const FText& Prefix) const
{
	FString Detail;
	if (Plan.Warnings.Num() > 0)
	{
		Detail = Plan.Warnings[0].ToString();
	}
	else if (Plan.Rows.Num() > 0 && !Plan.HasEligibleRows())
	{
		Detail = Plan.Rows[0].StatusText.ToString();
	}
	else if (Plan.Rows.Num() > 0)
	{
		Detail = Plan.Rows[0].StatusText.ToString();
	}

	const FString Summary = FString::Printf(
		TEXT("%s: %d eligible, %d skipped, %d failed. %s"),
		*Prefix.ToString(),
		Plan.EligibleCount,
		Plan.SkippedCount,
		Plan.FailedCount,
		*Detail);
	return FText::FromString(Summary.TrimEnd());
}

bool ABulkUpgradeEquipment::CanTargetRecipeChangeTarget(TSubclassOf<UFGRecipe> TargetRecipe) const
{
	if (!targetBuildable || !TargetRecipe)
	{
		return false;
	}

	const TArray<FItemAmount> Products = UFGRecipe::GetProducts(TargetRecipe);
	if (Products.Num() == 0 || !Products[0].ItemClass || !Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
	{
		return false;
	}

	const TSubclassOf<UFGBuildingDescriptor> BuildDescriptor(Products[0].ItemClass.Get());
	const TSubclassOf<AFGBuildable> TargetBuildableClass = UFGBuildingDescriptor::GetBuildableClass(BuildDescriptor);
	return TargetBuildableClass &&
		UBulkUpgradeWidgetPopupHelper::ClassifyBuildableClass(TargetBuildableClass) == ClassifyBuildable(targetBuildable) &&
		TargetBuildableClass.Get() != targetBuildable->GetClass();
}

bool ABulkUpgradeEquipment::CanCommitSelectedFamily() const
{
	const EBulkUpgradeFamily Family = ClassifyBuildable(targetBuildable);
	return Family == EBulkUpgradeFamily::ConveyorBelt ||
		Family == EBulkUpgradeFamily::ConveyorLift ||
		Family == EBulkUpgradeFamily::Storage ||
		Family == EBulkUpgradeFamily::Pipeline ||
		Family == EBulkUpgradeFamily::PipelinePump ||
		Family == EBulkUpgradeFamily::Wire ||
		Family == EBulkUpgradeFamily::PowerPole ||
		Family == EBulkUpgradeFamily::PowerPoleWall ||
		Family == EBulkUpgradeFamily::PowerPoleWallDouble ||
		Family == EBulkUpgradeFamily::PowerTower;
}

void ABulkUpgradeEquipment::SetPopupStatus(const FText& Status) const
{
	if (ActivePopupUserWidget)
	{
		ActivePopupUserWidget->SetStatusText(Status);
	}
}

void ABulkUpgradeEquipment::SendLocalStatusMessage(const FString& Message) const
{
	if (GEngine && IsLocalInstigator())
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Green, Message);
	}
	UE_LOG(LogBulkUpgrade, Display, TEXT("%s"), *Message);
}

EBulkUpgradeFamily ABulkUpgradeEquipment::ClassifyBuildable(const AFGBuildable* Buildable)
{
	return IsValid(Buildable)
		? UBulkUpgradeWidgetPopupHelper::ClassifyBuildableClass(Buildable->GetClass())
		: EBulkUpgradeFamily::Unknown;
}

bool ABulkUpgradeEquipment::IsSupportedTarget(const AFGBuildable* Buildable)
{
	return ClassifyBuildable(Buildable) != EBulkUpgradeFamily::Unknown;
}

#undef LOCTEXT_NAMESPACE
