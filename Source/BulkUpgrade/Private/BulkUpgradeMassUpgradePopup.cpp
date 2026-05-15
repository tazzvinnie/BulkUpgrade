#include "BulkUpgradeMassUpgradePopup.h"

#include "BulkUpgrade.h"
#include "BulkUpgradeEquipment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/CheckBox.h"
#include "Engine/Texture2D.h"
#include "FGRecipe.h"
#include "InputCoreTypes.h"
#include "Resources/FGBuildDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeMassUpgradePopup"

namespace
{
const FName ToggleBelts(TEXT("Belts"));
const FName ToggleLifts(TEXT("Lifts"));
const FName ToggleStorage(TEXT("Storage"));
const FName TogglePipelines(TEXT("Pipelines"));
const FName TogglePumps(TEXT("Pumps"));
const FName ToggleWires(TEXT("Wires"));
const FName TogglePowerPoles(TEXT("PowerPoles"));
const FName TogglePowerPoleWalls(TEXT("PowerPoleWalls"));
const FName TogglePowerPoleWallDoubles(TEXT("PowerPoleWallDoubles"));
const FName TogglePowerTowers(TEXT("PowerTowers"));
const FName ToggleCrossAttachments(TEXT("CrossAttachments"));
constexpr int32 MaxRenderedProductionRows = 80;

FLinearColor WindowBackground()
{
	return FLinearColor(0.145f, 0.170f, 0.178f, 0.90f);
}

FLinearColor TitleBackground()
{
	return FLinearColor(0.290f, 0.325f, 0.340f, 0.86f);
}

FLinearColor TitleIconBackground()
{
	return FLinearColor(0.070f, 0.080f, 0.085f, 0.95f);
}

FLinearColor CloseBackground()
{
	return FLinearColor(0.360f, 0.395f, 0.410f, 0.88f);
}

FLinearColor FieldBackground()
{
	return FLinearColor(0.760f, 0.790f, 0.820f, 0.92f);
}

FLinearColor DisabledFieldBackground()
{
	return FLinearColor(0.590f, 0.620f, 0.650f, 0.58f);
}

FLinearColor DropdownBackground()
{
	return FLinearColor(0.045f, 0.050f, 0.055f, 0.98f);
}

const FSlateBrush* FlatBrush()
{
	return FCoreStyle::Get().GetBrush("WhiteBrush");
}

FSlateFontInfo MassUpgradeFont(const TCHAR* Typeface, const int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle(FName(Typeface), Size);
}

UTexture2D* ResolveDescriptorIcon(TSubclassOf<UFGItemDescriptor> Descriptor)
{
	if (UTexture2D* Icon = UFGItemDescriptor::GetBigIcon(Descriptor))
	{
		return Icon;
	}
	return UFGItemDescriptor::GetSmallIcon(Descriptor);
}

FText MakeDescriptorName(TSubclassOf<UFGItemDescriptor> Descriptor)
{
	return Descriptor
		? UFGItemDescriptor::GetItemName(Descriptor)
		: LOCTEXT("UnknownDescriptor", "Unknown");
}

void AddItemAmount(TArray<FItemAmount>& Accumulator, const FItemAmount& Source, const int32 Multiplier)
{
	if (!Source.ItemClass || Source.Amount <= 0 || Multiplier <= 0)
	{
		return;
	}

	const int32 AmountToAdd = Source.Amount * Multiplier;
	if (FItemAmount* Existing = Accumulator.FindByPredicate([&Source](const FItemAmount& Candidate)
	{
		return Candidate.ItemClass == Source.ItemClass;
	}))
	{
		Existing->Amount += AmountToAdd;
		return;
	}

	FItemAmount NewAmount = Source;
	NewAmount.Amount = AmountToAdd;
	Accumulator.Add(NewAmount);
}

FText FormatMeterAmount(const float Meters)
{
	FNumberFormattingOptions Format;
	Format.MinimumFractionalDigits = 0;
	Format.MaximumFractionalDigits = FMath::IsNearlyEqual(Meters, FMath::RoundToFloat(Meters), 0.05f) ? 0 : 1;
	return FText::Format(LOCTEXT("MetersAmount", "{0} m"), FText::AsNumber(Meters, &Format));
}

float BuildableLengthMeters(const AFGBuildable* Buildable)
{
	if (const AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Buildable))
	{
		return FMath::Max(0.0f, Conveyor->GetLength() / 100.0f);
	}
	if (const AFGBuildablePipeline* Pipeline = Cast<AFGBuildablePipeline>(Buildable))
	{
		return FMath::Max(0.0f, Pipeline->GetLength() / 100.0f);
	}
	if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Buildable))
	{
		return FMath::Max(0.0f, Wire->GetLength() / 100.0f);
	}
	return 0.0f;
}
}

void UBulkUpgradeMassUpgradePopup::Configure(
	ABulkUpgradeEquipment* InEquipment,
	AFGBuildable* InTargetBuildable,
	EBulkUpgradeFamily InFamily,
	const TArray<FBulkUpgradeComboBoxItem>& InOptions,
	TSubclassOf<UFGRecipe> InSelectedRecipe,
	const FText& InSourceAmountLabel,
	const FText& InTargetAmountLabel,
	const FText& InRefundLabel,
	const FText& InInitialStatus)
{
	Equipment = InEquipment;
	TargetBuildable = InTargetBuildable;
	Family = InFamily;
	SourceAmountLabel = InSourceAmountLabel;
	TargetAmountLabel = InTargetAmountLabel;
	RefundLabel = InRefundLabel;
	CurrentStatusText = InInitialStatus;
	SetIsFocusable(true);

	DescriptorBrushes.Reset();
	ReferencedIcons.Reset();
	EmptyIconBrush = FSlateBrush();
	EmptyIconBrush.ImageSize = FVector2D(48.0f, 48.0f);

	BuildOptionBuckets(InOptions, InSelectedRecipe);

	FunctionToggles.Reset();
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
	case EBulkUpgradeFamily::ConveyorLift:
	case EBulkUpgradeFamily::Storage:
		FunctionToggles.Add(ToggleBelts, true);
		FunctionToggles.Add(ToggleLifts, true);
		FunctionToggles.Add(ToggleStorage, Family == EBulkUpgradeFamily::Storage);
		FunctionToggles.Add(ToggleCrossAttachments, true);
		break;
	case EBulkUpgradeFamily::Pipeline:
	case EBulkUpgradeFamily::PipelinePump:
		FunctionToggles.Add(TogglePipelines, true);
		FunctionToggles.Add(TogglePumps, true);
		FunctionToggles.Add(ToggleCrossAttachments, true);
		break;
	case EBulkUpgradeFamily::Wire:
	case EBulkUpgradeFamily::PowerPole:
	case EBulkUpgradeFamily::PowerPoleWall:
	case EBulkUpgradeFamily::PowerPoleWallDouble:
	case EBulkUpgradeFamily::PowerTower:
		FunctionToggles.Add(ToggleWires, Family == EBulkUpgradeFamily::Wire);
		FunctionToggles.Add(TogglePowerPoles, Family == EBulkUpgradeFamily::PowerPole);
		FunctionToggles.Add(TogglePowerPoleWalls, Family == EBulkUpgradeFamily::PowerPoleWall);
		FunctionToggles.Add(TogglePowerPoleWallDoubles, Family == EBulkUpgradeFamily::PowerPoleWallDouble);
		FunctionToggles.Add(TogglePowerTowers, Family == EBulkUpgradeFamily::PowerTower);
		FunctionToggles.Add(ToggleCrossAttachments, true);
		break;
	case EBulkUpgradeFamily::Unknown:
	default:
		break;
	}

	RefreshProductionInfo();
}

void UBulkUpgradeMassUpgradePopup::SetStatusText(const FText& Status)
{
	CurrentStatusText = Status;
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(Status);
	}
}

FReply UBulkUpgradeMassUpgradePopup::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade popup consumed Escape and closed before the pause menu could open."));
		if (Equipment.IsValid())
		{
			Equipment->CloseUpgradePopup();
		}
		return FReply::Handled();
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::RebuildWidget()
{
	TSharedRef<SWidget> Widget = SNew(SOverlay)
	+ SOverlay::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SBorder)
		.BorderImage(FlatBrush())
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f))
		.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
		{
			UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade popup closed by outside click."));
			return HandleCloseClicked();
		})
	]
	+ SOverlay::Slot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(116.0f, 96.0f, 0.0f, 0.0f))
	[
		SNew(SScaleBox)
		.Stretch(EStretch::UserSpecified)
		.UserSpecifiedScale(0.38f)
		[
			SNew(SBorder)
			.BorderImage(FlatBrush())
			.BorderBackgroundColor(WindowBackground())
			.Padding(0.0f)
			[
				SNew(SBox)
				.WidthOverride(1380.0f)
				.MinDesiredHeight(790.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						MakeTitleBar()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(38.0f, 42.0f, 38.0f, 0.0f))
					[
						MakeUpgradeTargetRow()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(44.0f, 28.0f, 44.0f, 0.0f))
					[
						MakeCrossAttachmentsToggle()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(44.0f, 28.0f, 44.0f, 0.0f))
					[
						MakeCostPanel()
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(FMargin(44.0f, 24.0f, 44.0f, 0.0f))
					[
						MakeProductionRowsPanel()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(44.0f, 16.0f, 44.0f, 30.0f))
					[
						MakeUpgradeFooter()
					]
				]
			]
		]
	];

	RebuildCostItems();
	RebuildProductionRows();
	return Widget;
}

void UBulkUpgradeMassUpgradePopup::NativeDestruct()
{
	StatusTextBlock.Reset();
	ProductionSummaryTextBlock.Reset();
	CostItemsBox.Reset();
	ProductionRowsBox.Reset();
	TypeOptions.Reset();
	ConveyorBeltOptions.Reset();
	ConveyorLiftOptions.Reset();
	StorageOptions.Reset();
	PipelineOptions.Reset();
	PipelinePumpOptions.Reset();
	WireOptions.Reset();
	PowerPoleOptions.Reset();
	PowerPoleWallOptions.Reset();
	PowerPoleWallDoubleOptions.Reset();
	PowerTowerOptions.Reset();
	SelectedTypeOption.Reset();
	SelectedFamilyOptions.Reset();
	DescriptorBrushes.Reset();
	ReferencedIcons.Reset();
	ProductionInfo = FBulkUpgradeProductionInfoSet();
	Super::NativeDestruct();
}

void UBulkUpgradeMassUpgradePopup::BuildOptionBuckets(const TArray<FBulkUpgradeComboBoxItem>& InOptions, TSubclassOf<UFGRecipe> InSelectedRecipe)
{
	TypeOptions.Reset();
	ConveyorBeltOptions.Reset();
	ConveyorLiftOptions.Reset();
	StorageOptions.Reset();
	PipelineOptions.Reset();
	PipelinePumpOptions.Reset();
	WireOptions.Reset();
	PowerPoleOptions.Reset();
	PowerPoleWallOptions.Reset();
	PowerPoleWallDoubleOptions.Reset();
	PowerTowerOptions.Reset();
	SelectedTypeOption.Reset();
	SelectedFamilyOptions.Reset();

	if (Equipment.IsValid())
	{
		switch (Family)
		{
		case EBulkUpgradeFamily::ConveyorBelt:
		case EBulkUpgradeFamily::ConveyorLift:
		case EBulkUpgradeFamily::Storage:
		{
			TArray<FBulkUpgradeComboBoxItem> Belts;
			TArray<FBulkUpgradeComboBoxItem> Lifts;
			TArray<FBulkUpgradeComboBoxItem> Storages;
			UBulkUpgradeWidgetPopupHelper::GetConveyorsBySpeed(Equipment.Get(), Belts, Lifts, Storages);
			AddComboItems(Belts);
			AddComboItems(Lifts);
			AddComboItems(Storages);
			break;
		}
		case EBulkUpgradeFamily::Pipeline:
		case EBulkUpgradeFamily::PipelinePump:
		{
			TArray<FBulkUpgradeComboBoxItem> Pipelines;
			TArray<FBulkUpgradeComboBoxItem> Pumps;
			UBulkUpgradeWidgetPopupHelper::GetPipesBySpeed(Equipment.Get(), Pipelines, Pumps);
			AddComboItems(Pipelines);
			AddComboItems(Pumps);
			break;
		}
		case EBulkUpgradeFamily::Wire:
		case EBulkUpgradeFamily::PowerPole:
		case EBulkUpgradeFamily::PowerPoleWall:
		case EBulkUpgradeFamily::PowerPoleWallDouble:
		case EBulkUpgradeFamily::PowerTower:
		{
			TArray<FBulkUpgradeComboBoxItem> Wires;
			TArray<FBulkUpgradeComboBoxItem> Poles;
			TArray<FBulkUpgradeComboBoxItem> WallPoles;
			TArray<FBulkUpgradeComboBoxItem> DoubleWallPoles;
			TArray<FBulkUpgradeComboBoxItem> Towers;
			UBulkUpgradeWidgetPopupHelper::GetPowerPolesByConnections(Equipment.Get(), Wires, Poles, WallPoles, DoubleWallPoles, Towers);
			AddComboItems(Wires);
			AddComboItems(Poles);
			AddComboItems(WallPoles);
			AddComboItems(DoubleWallPoles);
			AddComboItems(Towers);
			break;
		}
		case EBulkUpgradeFamily::Unknown:
		default:
			break;
		}
	}

	if (TypeOptions.Num() == 0)
	{
		AddComboItems(InOptions);
	}

	SelectDefaultOption(EBulkUpgradeFamily::ConveyorBelt, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::ConveyorLift, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::Storage, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::Pipeline, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::PipelinePump, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::Wire, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::PowerPole, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::PowerPoleWall, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::PowerPoleWallDouble, InSelectedRecipe);
	SelectDefaultOption(EBulkUpgradeFamily::PowerTower, InSelectedRecipe);

	if (!SelectedTypeOption.IsValid())
	{
		SelectedTypeOption = GetSelectedOption(Family);
	}
}

void UBulkUpgradeMassUpgradePopup::AddComboItems(const TArray<FBulkUpgradeComboBoxItem>& Items)
{
	for (const FBulkUpgradeComboBoxItem& Item : Items)
	{
		AddPopupOption(Item, nullptr);
	}
}

void UBulkUpgradeMassUpgradePopup::AddPopupOption(const FBulkUpgradeComboBoxItem& Item, TSubclassOf<UFGRecipe> InSelectedRecipe)
{
	TArray<TSharedPtr<FBulkUpgradePopupOption>>* Bucket = GetMutableOptionsForFamily(Item.Family);
	if (!Bucket)
	{
		return;
	}

	TSharedPtr<FBulkUpgradePopupOption> PopupOption = MakeShared<FBulkUpgradePopupOption>();
	PopupOption->Item = Item;
	PopupOption->IconBrush.ImageSize = FVector2D(58.0f, 58.0f);
	if (UTexture2D* Icon = ResolveDescriptorIcon(Item.BuildDescriptor))
	{
		TrackIcon(Icon);
		PopupOption->IconBrush.SetResourceObject(Icon);
	}

	Bucket->Add(PopupOption);
	TypeOptions.Add(PopupOption);

	if (Item.Recipe == InSelectedRecipe)
	{
		SelectedFamilyOptions.Add(Item.Family, PopupOption);
	}
}

void UBulkUpgradeMassUpgradePopup::SelectDefaultOption(EBulkUpgradeFamily OptionFamily, TSubclassOf<UFGRecipe> InSelectedRecipe)
{
	TArray<TSharedPtr<FBulkUpgradePopupOption>>* Options = GetMutableOptionsForFamily(OptionFamily);
	if (!Options || Options->Num() == 0)
	{
		return;
	}

	TSharedPtr<FBulkUpgradePopupOption> Selection;
	if (InSelectedRecipe)
	{
		for (const TSharedPtr<FBulkUpgradePopupOption>& Option : *Options)
		{
			if (Option.IsValid() && Option->Item.Recipe == InSelectedRecipe)
			{
				Selection = Option;
				break;
			}
		}
	}

	if (!Selection.IsValid())
	{
		// Ensure Options array is not empty before accessing last element
		if (Options->Num() > 0)
		{
			Selection = (*Options)[Options->Num() - 1];
		}
	}

	SelectedFamilyOptions.Add(OptionFamily, Selection);
	if (OptionFamily == Family && Selection.IsValid())
	{
		SelectedTypeOption = Selection;
	}
}

TArray<TSharedPtr<FBulkUpgradePopupOption>>* UBulkUpgradeMassUpgradePopup::GetMutableOptionsForFamily(EBulkUpgradeFamily OptionFamily)
{
	switch (OptionFamily)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return &ConveyorBeltOptions;
	case EBulkUpgradeFamily::ConveyorLift:
		return &ConveyorLiftOptions;
	case EBulkUpgradeFamily::Storage:
		return &StorageOptions;
	case EBulkUpgradeFamily::Pipeline:
		return &PipelineOptions;
	case EBulkUpgradeFamily::PipelinePump:
		return &PipelinePumpOptions;
	case EBulkUpgradeFamily::Wire:
		return &WireOptions;
	case EBulkUpgradeFamily::PowerPole:
		return &PowerPoleOptions;
	case EBulkUpgradeFamily::PowerPoleWall:
		return &PowerPoleWallOptions;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return &PowerPoleWallDoubleOptions;
	case EBulkUpgradeFamily::PowerTower:
		return &PowerTowerOptions;
	case EBulkUpgradeFamily::Unknown:
	default:
		return nullptr;
	}
}

const TArray<TSharedPtr<FBulkUpgradePopupOption>>* UBulkUpgradeMassUpgradePopup::GetOptionsForFamily(EBulkUpgradeFamily OptionFamily) const
{
	return const_cast<UBulkUpgradeMassUpgradePopup*>(this)->GetMutableOptionsForFamily(OptionFamily);
}

TSharedPtr<FBulkUpgradePopupOption> UBulkUpgradeMassUpgradePopup::GetSelectedOption(EBulkUpgradeFamily OptionFamily) const
{
	if (const TSharedPtr<FBulkUpgradePopupOption>* Selection = SelectedFamilyOptions.Find(OptionFamily))
	{
		return *Selection;
	}
	return nullptr;
}

FName UBulkUpgradeMassUpgradePopup::GetToggleNameForFamily(EBulkUpgradeFamily OptionFamily) const
{
	switch (OptionFamily)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return ToggleBelts;
	case EBulkUpgradeFamily::ConveyorLift:
		return ToggleLifts;
	case EBulkUpgradeFamily::Storage:
		return ToggleStorage;
	case EBulkUpgradeFamily::Pipeline:
		return TogglePipelines;
	case EBulkUpgradeFamily::PipelinePump:
		return TogglePumps;
	case EBulkUpgradeFamily::Wire:
		return ToggleWires;
	case EBulkUpgradeFamily::PowerPole:
		return TogglePowerPoles;
	case EBulkUpgradeFamily::PowerPoleWall:
		return TogglePowerPoleWalls;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return TogglePowerPoleWallDoubles;
	case EBulkUpgradeFamily::PowerTower:
		return TogglePowerTowers;
	case EBulkUpgradeFamily::Unknown:
	default:
		return NAME_None;
	}
}

bool UBulkUpgradeMassUpgradePopup::IsLengthFamily(EBulkUpgradeFamily OptionFamily) const
{
	return OptionFamily == EBulkUpgradeFamily::ConveyorBelt ||
		OptionFamily == EBulkUpgradeFamily::ConveyorLift ||
		OptionFamily == EBulkUpgradeFamily::Pipeline ||
		OptionFamily == EBulkUpgradeFamily::Wire;
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeTitleBar()
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SBorder)
		.BorderImage(FlatBrush())
		.BorderBackgroundColor(TitleBackground())
		.Padding(FMargin(18.0f, 9.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 13.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FlatBrush())
				.BorderBackgroundColor(FieldBackground())
				.Padding(3.0f)
				[
					SNew(SBox)
					.WidthOverride(26.0f)
					.HeightOverride(26.0f)
					[
						SNew(SBorder)
						.BorderImage(FlatBrush())
						.BorderBackgroundColor(TitleIconBackground())
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PopupTitle", "Mass Upgrade"))
				.Font(MassUpgradeFont(TEXT("Regular"), 28))
				.ColorAndOpacity(GetPanelTextColor())
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
		]
	]
	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		SNew(SButton)
		.ButtonColorAndOpacity(CloseBackground())
		.ContentPadding(FMargin(30.0f, 5.0f))
		.OnClicked_Lambda([this]()
		{
			return HandleCloseClicked();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CloseX", "X"))
			.Font(MassUpgradeFont(TEXT("Bold"), 34))
			.ColorAndOpacity(GetPanelTextColor())
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeUpgradeTargetRow()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 22.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("UpgradeToLabel", "Upgrade to:"))
		.Font(MassUpgradeFont(TEXT("Bold"), 32))
		.ColorAndOpacity(GetPanelTextColor())
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor::Black)
	];

	auto AddSelector = [&Row, this](EBulkUpgradeFamily OptionFamily, const float Width)
	{
		const FName ToggleName = GetToggleNameForFamily(OptionFamily);
		if (!FunctionToggles.Contains(ToggleName))
		{
			return;
		}

		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 24.0f, 0.0f)
		[
			MakeFamilySelector(ToggleName, OptionFamily, Width)
		];
	};

	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
	case EBulkUpgradeFamily::ConveyorLift:
	case EBulkUpgradeFamily::Storage:
		AddSelector(EBulkUpgradeFamily::ConveyorBelt, 168.0f);
		AddSelector(EBulkUpgradeFamily::ConveyorLift, 168.0f);
		AddSelector(EBulkUpgradeFamily::Storage, 168.0f);
		break;
	case EBulkUpgradeFamily::Pipeline:
	case EBulkUpgradeFamily::PipelinePump:
		AddSelector(EBulkUpgradeFamily::Pipeline, 190.0f);
		AddSelector(EBulkUpgradeFamily::PipelinePump, 190.0f);
		break;
	case EBulkUpgradeFamily::Wire:
	case EBulkUpgradeFamily::PowerPole:
	case EBulkUpgradeFamily::PowerPoleWall:
	case EBulkUpgradeFamily::PowerPoleWallDouble:
	case EBulkUpgradeFamily::PowerTower:
		AddSelector(EBulkUpgradeFamily::Wire, 128.0f);
		AddSelector(EBulkUpgradeFamily::PowerPole, 128.0f);
		AddSelector(EBulkUpgradeFamily::PowerPoleWall, 128.0f);
		AddSelector(EBulkUpgradeFamily::PowerPoleWallDouble, 128.0f);
		AddSelector(EBulkUpgradeFamily::PowerTower, 128.0f);
		break;
	case EBulkUpgradeFamily::Unknown:
	default:
		break;
	}

	Row->AddSlot()
	.FillWidth(1.0f)
	[
		SNew(SSpacer)
	];

	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SButton)
		.ContentPadding(FMargin(42.0f, 28.0f))
		.ButtonColorAndOpacity(FieldBackground())
		.OnClicked_Lambda([this]()
		{
			return HandleAddToListClicked();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddToListButton", "Add To List"))
			.Font(MassUpgradeFont(TEXT("Bold"), 28))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.04f, 0.045f, 0.050f, 1.0f)))
		]
	];

	return Row;
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeFamilySelector(FName ToggleName, EBulkUpgradeFamily OptionFamily, float CardWidth)
{
	const TArray<TSharedPtr<FBulkUpgradePopupOption>>* Options = GetOptionsForFamily(OptionFamily);

	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 7.0f, 0.0f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this, ToggleName]()
		{
			return GetFunctionToggleState(ToggleName);
		})
		.OnCheckStateChanged_Lambda([this, ToggleName](ECheckBoxState NewState)
		{
			HandleFunctionToggleChanged(NewState, ToggleName);
		})
	]
	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		SNew(SBox)
		.WidthOverride(CardWidth)
		.HeightOverride(96.0f)
		[
			SNew(SBorder)
			.BorderImage(FlatBrush())
			.BorderBackgroundColor_Lambda([this, ToggleName]()
			{
				return GetFunctionToggleState(ToggleName) == ECheckBoxState::Checked ? FieldBackground() : DisabledFieldBackground();
			})
			.Padding(0.0f)
			[
				SNew(SComboBox<TSharedPtr<FBulkUpgradePopupOption>>)
				.OptionsSource(Options)
				.InitiallySelectedItem(GetSelectedOption(OptionFamily))
				.IsEnabled_Lambda([this, OptionFamily, ToggleName]()
				{
					const TArray<TSharedPtr<FBulkUpgradePopupOption>>* FamilyOptions = GetOptionsForFamily(OptionFamily);
					return FamilyOptions && FamilyOptions->Num() > 0 && GetFunctionToggleState(ToggleName) == ECheckBoxState::Checked;
				})
				.OnGenerateWidget_Lambda([this](TSharedPtr<FBulkUpgradePopupOption> Option)
				{
					return GenerateTypeOptionWidget(Option);
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FBulkUpgradePopupOption> Option, ESelectInfo::Type SelectionType)
				{
					HandleTypeSelectionChanged(Option, SelectionType);
				})
				.Content()
				[
					MakeSelectedFamilyWidget(OptionFamily)
				]
			]
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::GenerateTypeOptionWidget(TSharedPtr<FBulkUpgradePopupOption> Option) const
{
	const FSlateBrush* IconBrush = Option.IsValid() ? &Option->IconBrush : &EmptyIconBrush;
	const FText AmountText = GetOptionAmountText(Option);
	return SNew(SBorder)
	.BorderImage(FlatBrush())
	.BorderBackgroundColor(DropdownBackground())
	.Padding(FMargin(10.0f, 8.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(50.0f)
			.HeightOverride(50.0f)
			[
				SNew(SImage)
				.Image(IconBrush)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(Option.IsValid() ? UFGRecipe::GetRecipeName(Option->Item.Recipe) : LOCTEXT("NoTypeOption", "No type"))
				.Font(MassUpgradeFont(TEXT("Regular"), 18))
				.ColorAndOpacity(GetPanelTextColor())
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(AmountText)
				.Font(MassUpgradeFont(TEXT("Bold"), 20))
				.ColorAndOpacity(GetPanelTextColor())
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeSelectedFamilyWidget(EBulkUpgradeFamily OptionFamily) const
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.FillHeight(0.58f)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	.Padding(0.0f, 5.0f, 0.0f, 0.0f)
	[
		SNew(SImage)
		.Image_Lambda([this, OptionFamily]()
		{
			TSharedPtr<FBulkUpgradePopupOption> Option = GetSelectedOption(OptionFamily);
			return Option.IsValid() ? &Option->IconBrush : &EmptyIconBrush;
		})
	]
	+ SVerticalBox::Slot()
	.FillHeight(0.42f)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	.Padding(FMargin(6.0f, 0.0f, 6.0f, 5.0f))
	[
		SNew(STextBlock)
		.Text_Lambda([this, OptionFamily]()
		{
			return GetSelectedFamilyText(OptionFamily);
		})
		.Font(MassUpgradeFont(TEXT("Bold"), 21))
		.ColorAndOpacity(GetPanelTextColor())
		.AutoWrapText(true)
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor::Black)
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeCrossAttachmentsToggle()
{
	return SNew(SCheckBox)
	.IsChecked_Lambda([this]()
	{
		return GetFunctionToggleState(ToggleCrossAttachments);
	})
	.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
	{
		HandleFunctionToggleChanged(NewState, ToggleCrossAttachments);
	})
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CrossAttachmentsToggle", "Go accross junctions"))
		.Font(MassUpgradeFont(TEXT("Bold"), 30))
		.ColorAndOpacity(GetPanelTextColor())
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor::Black)
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeCostPanel()
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 38.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CostLabel", "Cost:"))
		.Font(MassUpgradeFont(TEXT("Bold"), 32))
		.ColorAndOpacity(GetPanelTextColor())
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor::Black)
	]
	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()
		[
			SAssignNew(CostItemsBox, SHorizontalBox)
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeProductionRowsPanel()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 12.0f)
	[
		SNew(SGridPanel)
		+ SGridPanel::Slot(0, 0)
		.Padding(78.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BuildTypeHeader", "Build Type"))
			.Font(MassUpgradeFont(TEXT("Bold"), 32))
			.ColorAndOpacity(GetPanelTextColor())
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
		+ SGridPanel::Slot(1, 0)
		.Padding(40.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AmountHeader", "Amount"))
			.Font(MassUpgradeFont(TEXT("Bold"), 32))
			.ColorAndOpacity(GetPanelTextColor())
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
		+ SGridPanel::Slot(2, 0)
		.Padding(80.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RefundHeader", "Refund"))
			.Font(MassUpgradeFont(TEXT("Bold"), 32))
			.ColorAndOpacity(GetPanelTextColor())
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
	]
	+ SVerticalBox::Slot()
	.FillHeight(1.0f)
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(ProductionRowsBox, SVerticalBox)
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeUpgradeFooter()
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SSpacer)
	]
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.HAlign(HAlign_Center)
	[
		SNew(SButton)
		.ContentPadding(FMargin(58.0f, 13.0f))
		.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.94f, 0.97f, 1.0f)))
		.OnClicked_Lambda([this]()
		{
			return HandleUpgradeClicked();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("UpgradeButton", "Upgrade"))
			.Font(MassUpgradeFont(TEXT("Bold"), 30))
			.ColorAndOpacity(FSlateColor(FLinearColor::Black))
			.ShadowOffset(FVector2D::ZeroVector)
		]
	]
	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SSpacer)
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeItemAmountWidget(const FItemAmount& ItemAmount, float IconSize) const
{
	const FSlateBrush* IconBrush = const_cast<UBulkUpgradeMassUpgradePopup*>(this)->GetDescriptorBrush(ItemAmount.ItemClass);
	const FText ItemText = ItemAmount.ItemClass
		? FText::Format(LOCTEXT("ItemAmount", "{0} {1}"), FText::AsNumber(ItemAmount.Amount), UFGItemDescriptor::GetItemName(ItemAmount.ItemClass))
		: LOCTEXT("NoItemAmount", "--");

	return SNew(SBox)
	.WidthOverride(160.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(IconSize)
			.HeightOverride(IconSize)
			[
				SNew(SImage)
				.Image(IconBrush)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 3.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(ItemText)
			.Font(MassUpgradeFont(TEXT("Bold"), 20))
			.ColorAndOpacity(GetPanelTextColor())
			.AutoWrapText(true)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
	];
}

TSharedRef<SWidget> UBulkUpgradeMassUpgradePopup::MakeRefundWidget(const FBulkUpgradeProductionGroup& Group)
{
	const TArray<FItemAmount> RefundItems = BuildRefundItems(Group);
	if (RefundItems.Num() == 0)
	{
		return SNew(STextBlock)
		.Text(LOCTEXT("NoRefund", "--"))
		.Font(MassUpgradeFont(TEXT("Bold"), 24))
		.ColorAndOpacity(GetPanelTextColor())
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor::Black);
	}

	return MakeItemAmountWidget(RefundItems[0], 42.0f);
}

void UBulkUpgradeMassUpgradePopup::HandleTypeSelectionChanged(TSharedPtr<FBulkUpgradePopupOption> Option, ESelectInfo::Type SelectionType)
{
	if (!Option.IsValid())
	{
		return;
	}

	SelectedFamilyOptions.Add(Option->Item.Family, Option);
	if (Option->Item.Family == Family)
	{
		SelectedTypeOption = Option;
		if (Equipment.IsValid())
		{
			Equipment->SelectTargetRecipe(Option->Item.Recipe);
		}
	}

	RefreshProductionInfo();
	SetStatusText(LOCTEXT("SelectionUpdated", "Selection updated."));
}

void UBulkUpgradeMassUpgradePopup::HandleFunctionToggleChanged(ECheckBoxState NewState, FName ToggleName)
{
	if (bool* Toggle = FunctionToggles.Find(ToggleName))
	{
		*Toggle = NewState == ECheckBoxState::Checked;
	}
	RefreshProductionInfo();
	SetStatusText(LOCTEXT("SelectionUpdated", "Selection updated."));
}

FReply UBulkUpgradeMassUpgradePopup::HandleUpgradeClicked()
{
	UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade popup Upgrade button clicked: equipment=%s selectedType=%s family=%d targetFamily=%d uiEnabled=%s"),
		Equipment.IsValid() ? TEXT("valid") : TEXT("invalid"),
		SelectedTypeOption.IsValid() ? *GetNameSafe(SelectedTypeOption->Item.Recipe.Get()) : TEXT("none"),
		SelectedTypeOption.IsValid() ? static_cast<int32>(SelectedTypeOption->Item.Family) : -1,
		static_cast<int32>(Family),
		IsUpgradeEnabled() ? TEXT("true") : TEXT("false"));

	if (!Equipment.IsValid())
	{
		SetStatusText(LOCTEXT("UpgradeNoEquipment", "Upgrade failed: equipment actor is no longer valid."));
		return FReply::Handled();
	}

	if (!SelectedTypeOption.IsValid())
	{
		SetStatusText(LOCTEXT("UpgradeNoSelectedType", "Select an upgrade target first."));
		return FReply::Handled();
	}

	SetStatusText(LOCTEXT("UpgradeClicked", "Upgrade clicked; applying selected targets..."));
	return Equipment->UpgradeSelectedRequest(BuildRequestFromSelections(EBulkUpgradeScope::ConnectedNetwork));
}

FReply UBulkUpgradeMassUpgradePopup::HandleAddToListClicked()
{
	SetStatusText(LOCTEXT("AddToListPending", "Add To List is visible for MassUpgrade parity; list execution is still pending in this port."));
	return FReply::Handled();
}

FReply UBulkUpgradeMassUpgradePopup::HandleCloseClicked()
{
	if (Equipment.IsValid())
	{
		Equipment->CloseUpgradePopup();
	}
	return FReply::Handled();
}

ECheckBoxState UBulkUpgradeMassUpgradePopup::GetFunctionToggleState(FName ToggleName) const
{
	const bool* Toggle = FunctionToggles.Find(ToggleName);
	return Toggle && *Toggle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool UBulkUpgradeMassUpgradePopup::IsUpgradeEnabled() const
{
	return Equipment.IsValid() && SelectedTypeOption.IsValid();
}

FText UBulkUpgradeMassUpgradePopup::GetSelectedFamilyText(EBulkUpgradeFamily OptionFamily) const
{
	return GetOptionAmountText(GetSelectedOption(OptionFamily));
}

FText UBulkUpgradeMassUpgradePopup::GetOptionAmountText(TSharedPtr<FBulkUpgradePopupOption> Option) const
{
	return Option.IsValid()
		? UBulkUpgradeWidgetPopupHelper::MakeAmountLabel(Option->Item.Amount, Option->Item.Family)
		: LOCTEXT("SelectType", "Select");
}

FText UBulkUpgradeMassUpgradePopup::GetProductionSummaryText() const
{
	if (ProductionInfo.TotalBuildables <= 0)
	{
		return LOCTEXT("NoProductionInfo", "No matching buildables selected.");
	}
	return FText::Format(
		LOCTEXT("ProductionSummary", "{0} buildables in {1} groups"),
		FText::AsNumber(ProductionInfo.TotalBuildables),
		FText::AsNumber(ProductionInfo.Groups.Num()));
}

FText UBulkUpgradeMassUpgradePopup::GetGroupAmountText(const FBulkUpgradeProductionGroup& Group) const
{
	if (IsLengthFamily(Group.Family))
	{
		float TotalMeters = 0.0f;
		for (const AFGBuildable* Buildable : Group.Buildables)
		{
			TotalMeters += BuildableLengthMeters(Buildable);
		}
		if (TotalMeters > 0.0f)
		{
			return FormatMeterAmount(TotalMeters);
		}
	}

	return Group.Buildables.Num() == 1
		? LOCTEXT("OneBuildable", "1")
		: FText::AsNumber(Group.Buildables.Num());
}

void UBulkUpgradeMassUpgradePopup::RefreshProductionInfo()
{
	if (!Equipment.IsValid() || !TargetBuildable.IsValid())
	{
		ProductionInfo = FBulkUpgradeProductionInfoSet();
		RebuildCostItems();
		RebuildProductionRows();
		return;
	}

	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
	case EBulkUpgradeFamily::ConveyorLift:
	case EBulkUpgradeFamily::Storage:
		ProductionInfo = UBulkUpgradeWidgetPopupHelper::CollectConveyorProductionInfo(
			Equipment.Get(),
			TargetBuildable.Get(),
			FunctionToggles.FindRef(ToggleBelts),
			FunctionToggles.FindRef(ToggleLifts),
			FunctionToggles.FindRef(ToggleStorage),
			FunctionToggles.FindRef(ToggleCrossAttachments));
		break;
	case EBulkUpgradeFamily::Pipeline:
	case EBulkUpgradeFamily::PipelinePump:
		ProductionInfo = UBulkUpgradeWidgetPopupHelper::CollectPipelineProductionInfo(
			Equipment.Get(),
			TargetBuildable.Get(),
			FunctionToggles.FindRef(TogglePipelines),
			FunctionToggles.FindRef(TogglePumps),
			FunctionToggles.FindRef(ToggleCrossAttachments));
		break;
	case EBulkUpgradeFamily::Wire:
	case EBulkUpgradeFamily::PowerPole:
	case EBulkUpgradeFamily::PowerPoleWall:
	case EBulkUpgradeFamily::PowerPoleWallDouble:
	case EBulkUpgradeFamily::PowerTower:
		ProductionInfo = UBulkUpgradeWidgetPopupHelper::CollectPowerPoleProductionInfo(
			Equipment.Get(),
			TargetBuildable.Get(),
			FunctionToggles.FindRef(ToggleWires),
			FunctionToggles.FindRef(TogglePowerPoles),
			FunctionToggles.FindRef(TogglePowerPoleWalls),
			FunctionToggles.FindRef(TogglePowerPoleWallDoubles),
			FunctionToggles.FindRef(TogglePowerTowers),
			FunctionToggles.FindRef(ToggleCrossAttachments));
		break;
	case EBulkUpgradeFamily::Unknown:
	default:
		ProductionInfo = FBulkUpgradeProductionInfoSet();
		break;
	}

	if (ProductionSummaryTextBlock.IsValid())
	{
		ProductionSummaryTextBlock->SetText(GetProductionSummaryText());
	}
	RebuildCostItems();
	RebuildProductionRows();
}

FBulkUpgradeRequest UBulkUpgradeMassUpgradePopup::BuildRequestFromSelections(EBulkUpgradeScope Scope) const
{
	FBulkUpgradeRequest Request;
	Request.Family = Family;
	Request.Scope = Scope;
	Request.TargetRecipe = SelectedTypeOption.IsValid() ? SelectedTypeOption->Item.Recipe : nullptr;

	const auto SelectedRecipeForFamily = [this](EBulkUpgradeFamily OptionFamily) -> TSubclassOf<UFGRecipe>
	{
		const TSharedPtr<FBulkUpgradePopupOption> Selection = GetSelectedOption(OptionFamily);
		return Selection.IsValid() ? Selection->Item.Recipe : nullptr;
	};

	Request.TargetConveyorBeltRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::ConveyorBelt);
	Request.TargetConveyorLiftRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::ConveyorLift);
	Request.TargetStorageRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::Storage);
	Request.TargetPipelineRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::Pipeline);
	Request.TargetPipelinePumpRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::PipelinePump);
	Request.TargetWireRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::Wire);
	Request.TargetPowerPoleRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::PowerPole);
	Request.TargetPowerPoleWallRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::PowerPoleWall);
	Request.TargetPowerPoleWallDoubleRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::PowerPoleWallDouble);
	Request.TargetPowerTowerRecipe = SelectedRecipeForFamily(EBulkUpgradeFamily::PowerTower);

	Request.MaxOperations = Scope == EBulkUpgradeScope::Single ? 1 : 1000;
	Request.ExecutionProfile = EBulkUpgradeExecutionProfile::LocalOnlyDev;
	Request.CostPolicy = EBulkUpgradeCostPolicy::FreeLocalDev;
	Request.bAllowConnectedScope = Scope != EBulkUpgradeScope::Single;
	Request.bAllowPipelines = FunctionToggles.FindRef(TogglePipelines);
	Request.bAllowStorage = FunctionToggles.FindRef(ToggleStorage);
	Request.bAllowPipelinePumps = FunctionToggles.FindRef(TogglePumps);
	Request.bAllowPower =
		FunctionToggles.FindRef(ToggleWires) ||
		FunctionToggles.FindRef(TogglePowerPoles) ||
		FunctionToggles.FindRef(TogglePowerPoleWalls) ||
		FunctionToggles.FindRef(TogglePowerPoleWallDoubles) ||
		FunctionToggles.FindRef(TogglePowerTowers);
	Request.bEstimateCosts = false;
	return Request;
}

void UBulkUpgradeMassUpgradePopup::RebuildProductionRows()
{
	if (!ProductionRowsBox.IsValid())
	{
		return;
	}

	ProductionRowsBox->ClearChildren();
	if (ProductionInfo.Groups.Num() == 0)
	{
		ProductionRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 18.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoRows", "No selected buildables."))
			.AutoWrapText(true)
			.Font(MassUpgradeFont(TEXT("Bold"), 24))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.78f, 0.35f)))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		];
		return;
	}

	int32 RenderedRows = 0;
	for (const FBulkUpgradeProductionGroup& Group : ProductionInfo.Groups)
	{
		if (RenderedRows >= MaxRenderedProductionRows)
		{
			const int32 HiddenRows = ProductionInfo.Groups.Num() - RenderedRows;
			ProductionRowsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 14.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("RowsCapped", "{0} more groups hidden in this safety build."), FText::AsNumber(HiddenRows)))
				.AutoWrapText(true)
				.Font(MassUpgradeFont(TEXT("Bold"), 22))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.78f, 0.35f)))
			];
			break;
		}
		AddProductionRow(Group);
		++RenderedRows;
	}
}

void UBulkUpgradeMassUpgradePopup::RebuildCostItems()
{
	if (!CostItemsBox.IsValid())
	{
		return;
	}

	CostItemsBox->ClearChildren();
	const TArray<FItemAmount> CostItems = BuildCostItems();
	if (CostItems.Num() == 0)
	{
		CostItemsBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoCostItems", "--"))
			.Font(MassUpgradeFont(TEXT("Bold"), 24))
			.ColorAndOpacity(GetPanelTextColor())
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		];
		return;
	}

	for (const FItemAmount& ItemAmount : CostItems)
	{
		CostItemsBox->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 28.0f, 0.0f)
		[
			MakeItemAmountWidget(ItemAmount, 48.0f)
		];
	}
}

void UBulkUpgradeMassUpgradePopup::AddProductionRow(const FBulkUpgradeProductionGroup& Group)
{
	TSubclassOf<UFGBuildDescriptor> BuildDescriptor = Group.BuildDescriptor;
	const FSlateBrush* IconBrush = GetDescriptorBrush(BuildDescriptor);

	ProductionRowsBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 7.0f)
	[
		SNew(SGridPanel)
		+ SGridPanel::Slot(0, 0)
		.Padding(0.0f, 0.0f, 30.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(450.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([BuildDescriptor]()
					{
						return UBulkUpgradeWidgetPopupHelper::IsBuildDescriptorChecked(BuildDescriptor, true)
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, BuildDescriptor](ECheckBoxState State)
					{
						UBulkUpgradeWidgetPopupHelper::SetBuildDescriptorChecked(BuildDescriptor, State == ECheckBoxState::Checked);
						RebuildCostItems();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(48.0f)
					.HeightOverride(48.0f)
					[
						SNew(SImage)
						.Image(IconBrush)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(MakeDescriptorName(BuildDescriptor))
					.Font(MassUpgradeFont(TEXT("Bold"), 23))
					.AutoWrapText(true)
					.ColorAndOpacity(GetPanelTextColor())
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor::Black)
				]
			]
		]
		+ SGridPanel::Slot(1, 0)
		.Padding(0.0f, 0.0f, 30.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(270.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(GetGroupAmountText(Group))
				.Font(MassUpgradeFont(TEXT("Bold"), 27))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(GetPanelTextColor())
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
		]
		+ SGridPanel::Slot(2, 0)
		[
			SNew(SBox)
			.WidthOverride(360.0f)
			.VAlign(VAlign_Center)
			[
				MakeRefundWidget(Group)
			]
		]
	];
}

TArray<FItemAmount> UBulkUpgradeMassUpgradePopup::BuildCostItems() const
{
	TArray<FItemAmount> CostItems;
	for (const FBulkUpgradeProductionGroup& Group : ProductionInfo.Groups)
	{
		const FName ToggleName = GetToggleNameForFamily(Group.Family);
		if (FunctionToggles.Contains(ToggleName) && GetFunctionToggleState(ToggleName) != ECheckBoxState::Checked)
		{
			continue;
		}

		if (!UBulkUpgradeWidgetPopupHelper::IsBuildDescriptorChecked(Group.BuildDescriptor, true))
		{
			continue;
		}

		const TSharedPtr<FBulkUpgradePopupOption> SelectedOption = GetSelectedOption(Group.Family);
		if (!SelectedOption.IsValid() || !SelectedOption->Item.Recipe)
		{
			continue;
		}

		int32 Multiplier = 0;
		for (const AFGBuildable* Buildable : Group.Buildables)
		{
			Multiplier += FMath::Max(1, FMath::RoundToInt(GetBuildableCostMultiplier(Buildable)));
		}

		const TArray<FItemAmount> Ingredients = UFGRecipe::GetIngredients(SelectedOption->Item.Recipe);
		for (const FItemAmount& Ingredient : Ingredients)
		{
			AddItemAmount(CostItems, Ingredient, Multiplier);
		}
	}
	return CostItems;
}

TArray<FItemAmount> UBulkUpgradeMassUpgradePopup::BuildRefundItems(const FBulkUpgradeProductionGroup& Group) const
{
	TArray<FItemAmount> RefundItems;
	for (const AFGBuildable* Buildable : Group.Buildables)
	{
		if (!IsValid(Buildable) || !Buildable->GetBuiltWithRecipe())
		{
			continue;
		}

		const int32 Multiplier = FMath::Max(1, FMath::RoundToInt(GetBuildableCostMultiplier(Buildable)));
		const TArray<FItemAmount> Ingredients = UFGRecipe::GetIngredients(Buildable->GetBuiltWithRecipe());
		for (const FItemAmount& Ingredient : Ingredients)
		{
			AddItemAmount(RefundItems, Ingredient, Multiplier);
		}
	}
	return RefundItems;
}

float UBulkUpgradeMassUpgradePopup::GetBuildableCostMultiplier(const AFGBuildable* Buildable) const
{
	const float Meters = BuildableLengthMeters(Buildable);
	return Meters > 0.0f ? FMath::CeilToFloat(Meters) : 1.0f;
}

void UBulkUpgradeMassUpgradePopup::TrackIcon(UTexture2D* Icon)
{
	if (Icon)
	{
		ReferencedIcons.AddUnique(Icon);
	}
}

const FSlateBrush* UBulkUpgradeMassUpgradePopup::GetDescriptorBrush(TSubclassOf<UFGItemDescriptor> Descriptor)
{
	if (!Descriptor)
	{
		return &EmptyIconBrush;
	}

	TSharedPtr<FSlateBrush>& Brush = DescriptorBrushes.FindOrAdd(Descriptor.Get());
	if (!Brush.IsValid())
	{
		Brush = MakeShared<FSlateBrush>();
		Brush->ImageSize = FVector2D(48.0f, 48.0f);
		if (UTexture2D* Icon = ResolveDescriptorIcon(Descriptor))
		{
			TrackIcon(Icon);
			Brush->SetResourceObject(Icon);
		}
	}
	return Brush.Get();
}

FSlateColor UBulkUpgradeMassUpgradePopup::GetPanelTextColor() const
{
	return FSlateColor(FLinearColor(0.94f, 0.96f, 0.98f, 1.0f));
}

#undef LOCTEXT_NAMESPACE
