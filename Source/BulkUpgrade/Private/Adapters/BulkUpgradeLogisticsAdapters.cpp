#include "Adapters/BulkUpgradeLogisticsAdapters.h"

#include "BulkUpgrade.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableWire.h"
#include "FGDismantleInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Resources/FGBuildDescriptor.h"
#include "Resources/FGBuildingDescriptor.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeLogisticsAdapters"

namespace
{
FName AdapterClassNameOf(const UClass* Class)
{
	return Class ? Class->GetFName() : NAME_None;
}

void AdapterScaleCost(TArray<FItemAmount>& Cost, int32 Multiplier)
{
	const int32 SafeMultiplier = FMath::Max(1, Multiplier);
	for (FItemAmount& ItemAmount : Cost)
	{
		ItemAmount.Amount *= SafeMultiplier;
	}
}

bool AdapterCanDismantleViaInterface(AActor* Actor)
{
	return IsValid(Actor) &&
		Actor->GetClass()->ImplementsInterface(UFGDismantleInterface::StaticClass()) &&
		IFGDismantleInterface::Execute_CanDismantle(Actor);
}

EBulkUpgradeFamily AdapterClassifyPowerPole(const AFGBuildablePowerPole* PowerPole)
{
	if (!IsValid(PowerPole))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AdapterClassifyPowerPole: PowerPole is null"));
		return EBulkUpgradeFamily::Unknown;
	}

	EPowerPoleType PoleType = PowerPole->GetPowerPoleType();
	switch (PoleType)
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
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AdapterClassifyPowerPole: Unknown power pole type: %d"), static_cast<int32>(PoleType));
		return EBulkUpgradeFamily::Unknown;
	}
}

EBulkUpgradeFamily AdapterClassifyBuildableClass(TSubclassOf<AFGBuildable> BuildableClass)
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
		return EBulkUpgradeFamily::PipelinePump;
	}
	if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		return EBulkUpgradeFamily::Wire;
	}
	if (BuildableClass->IsChildOf(AFGBuildablePowerPole::StaticClass()))
	{
		return AdapterClassifyPowerPole(BuildableClass->GetDefaultObject<AFGBuildablePowerPole>());
	}
	return EBulkUpgradeFamily::Unknown;
}

TSubclassOf<AFGBuildable> AdapterGetBuildableClassFromRecipeSafe(TSubclassOf<UFGRecipe> Recipe)
{
	const TArray<FItemAmount> Products = Recipe ? UFGRecipe::GetProducts(Recipe) : TArray<FItemAmount>();
	if (Products.Num() == 0 || !Products[0].ItemClass || !Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
	{
		return nullptr;
	}

	const TSubclassOf<UFGBuildingDescriptor> BuildDescriptor(Products[0].ItemClass.Get());
	return UFGBuildingDescriptor::GetBuildableClass(BuildDescriptor);
}

bool IsFamilyAllowedByRequest(EBulkUpgradeFamily Family, const FBulkUpgradeRequest& Request)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::Pipeline:
		return Request.bAllowPipelines;
	case EBulkUpgradeFamily::Storage:
		return Request.bAllowStorage;
	case EBulkUpgradeFamily::PipelinePump:
		return Request.bAllowPipelinePumps;
	case EBulkUpgradeFamily::Wire:
		return Request.bAllowPower && Request.bAllowWire;
	case EBulkUpgradeFamily::PowerPole:
		return Request.bAllowPower && Request.bAllowPowerPole;
	case EBulkUpgradeFamily::PowerPoleWall:
		return Request.bAllowPower && Request.bAllowPowerPoleWall;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return Request.bAllowPower && Request.bAllowPowerPoleWallDouble;
	case EBulkUpgradeFamily::PowerTower:
		return Request.bAllowPower && Request.bAllowPowerTower;
	case EBulkUpgradeFamily::ConveyorBelt:
		return Request.bAllowConveyorBelts;
	case EBulkUpgradeFamily::ConveyorLift:
		return Request.bAllowConveyorLifts;
	case EBulkUpgradeFamily::Unknown:
	default:
		return false;
	}
}

TSubclassOf<UFGRecipe> ResolveTargetRecipeForFamily(const FBulkUpgradeRequest& Request, EBulkUpgradeFamily Family)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return Request.TargetConveyorBeltRecipe ? Request.TargetConveyorBeltRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::ConveyorLift:
		return Request.TargetConveyorLiftRecipe ? Request.TargetConveyorLiftRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::Storage:
		return Request.TargetStorageRecipe ? Request.TargetStorageRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::Pipeline:
		return Request.TargetPipelineRecipe ? Request.TargetPipelineRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::PipelinePump:
		return Request.TargetPipelinePumpRecipe ? Request.TargetPipelinePumpRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::Wire:
		return Request.TargetWireRecipe ? Request.TargetWireRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::PowerPole:
		return Request.TargetPowerPoleRecipe ? Request.TargetPowerPoleRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::PowerPoleWall:
		return Request.TargetPowerPoleWallRecipe ? Request.TargetPowerPoleWallRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return Request.TargetPowerPoleWallDoubleRecipe ? Request.TargetPowerPoleWallDoubleRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::PowerTower:
		return Request.TargetPowerTowerRecipe ? Request.TargetPowerTowerRecipe : Request.TargetRecipe;
	case EBulkUpgradeFamily::Unknown:
	default:
		return Request.TargetRecipe;
	}
}

FBulkUpgradePreviewRow MakeAdapterSkippedRow(AActor* Actor, EBulkUpgradeFamily Family, const FText& Reason)
{
	FBulkUpgradePreviewRow Row;
	Row.Actor = Actor;
	Row.ActorName = IsValid(Actor) ? FName(*Actor->GetName()) : NAME_None;
	Row.Family = Family;
	Row.Status = EBulkUpgradeRowStatus::Skipped;
	Row.StatusText = Reason;
	return Row;
}

FBulkUpgradePreviewRow BuildCommonRow(AActor* Actor, const FBulkUpgradeRequest& Request, EBulkUpgradeFamily Family, UClass* ExpectedBuildableClass)
{
	AFGBuildable* Buildable = Cast<AFGBuildable>(Actor);
	if (!IsValid(Buildable))
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("NotBuildable", "Actor is not a buildable."));
	}

	if (!IsFamilyAllowedByRequest(Family, Request))
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("FamilyDisabled", "This MassUpgrade family is disabled for this request."));
	}

	TSubclassOf<UFGBuildDescriptor> SourceDescriptor = Buildable->GetBuiltWithDescriptor<UFGBuildDescriptor>();
	if (SourceDescriptor && Request.ExcludedBuildDescriptors.Contains(SourceDescriptor))
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("ExcludedByRow", "This build type was unchecked in the upgrade list."));
	}

	TSubclassOf<UFGRecipe> TargetRecipe = ResolveTargetRecipeForFamily(Request, Family);
	if (!TargetRecipe)
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("MissingTargetRecipe", "No target recipe was provided."));
	}

	if (!Buildable->GetBuiltWithRecipe())
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("MissingSourceRecipe", "Actor has no built-with recipe; skipping uncertain upgrade state."));
	}

	TSubclassOf<AFGBuildable> TargetBuildableClass = AdapterGetBuildableClassFromRecipeSafe(TargetRecipe);
	if (!TargetBuildableClass)
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("NoTargetBuildable", "Target recipe does not resolve to a buildable class."));
	}

	if (!TargetBuildableClass.Get()->IsChildOf(ExpectedBuildableClass) || AdapterClassifyBuildableClass(TargetBuildableClass) != Family)
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("WrongFamily", "Target recipe belongs to a different buildable family."));
	}

	if (Buildable->GetClass() == TargetBuildableClass.Get())
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("AlreadyTarget", "Actor already has the target buildable class."));
	}

	if (!AdapterCanDismantleViaInterface(Buildable))
	{
		return MakeAdapterSkippedRow(Actor, Family, LOCTEXT("CannotDismantle", "Actor cannot currently be dismantled/upgraded."));
	}

	FBulkUpgradePreviewRow Row;
	Row.Actor = Actor;
	Row.ActorName = FName(*Actor->GetName());
	Row.Family = Family;
	Row.Status = EBulkUpgradeRowStatus::Eligible;
	Row.StatusText = LOCTEXT("Eligible", "Eligible for previewed upgrade.");
	Row.SourceRecipe = Buildable->GetBuiltWithRecipe();
	Row.TargetRecipe = TargetRecipe;
	Row.SourceBuildableClass = Buildable->GetClass();
	Row.TargetBuildableClass = TargetBuildableClass;
	Row.SourceClassName = AdapterClassNameOf(Buildable->GetClass());
	Row.TargetClassName = AdapterClassNameOf(TargetBuildableClass.Get());
	Row.CostMultiplier = 1;
	Row.PreconditionHash = static_cast<int32>(HashCombine(GetTypeHash(Buildable->GetClass()), GetTypeHash(TargetBuildableClass.Get())));

	if (Request.CostPolicy == EBulkUpgradeCostPolicy::FreeLocalDev)
	{
		Row.CostNote = LOCTEXT("FreeLocalDevCostNote", "Free local-dev upgrade; material accounting is intentionally deferred.");
	}
	else if (Request.bEstimateCosts)
	{
		Row.EstimatedGrossCost = UFGRecipe::GetIngredients(Buildable, TargetRecipe);
		AdapterScaleCost(Row.EstimatedGrossCost, Row.CostMultiplier);
		Row.CostNote = Request.CostPolicy == EBulkUpgradeCostPolicy::VerifiedVanillaCost
			? LOCTEXT("VerifiedCostRequestedNote", "Vanilla cost policy requested, but this row still needs real inventory/payment verification.")
			: LOCTEXT("GrossCostNote", "Estimated gross target cost only; final vanilla net cost is not verified yet.");
	}
	else
	{
		Row.CostNote = LOCTEXT("CostSkippedNote", "Cost estimation skipped for this local-dev plan.");
	}

	return Row;
}

void AppendConveyorNeighbors(AActor* Actor, TArray<AActor*>& OutActors)
{
	if (!Actor)
	{
		return;
	}

	if (const AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Actor))
	{
		UFGFactoryConnectionComponent* Connections[] = {
			Conveyor->GetConnection0(),
			Conveyor->GetConnection1()
		};

		for (UFGFactoryConnectionComponent* Connection : Connections)
		{
			if (!Connection || !Connection->IsConnected())
			{
				continue;
			}

			UFGFactoryConnectionComponent* OtherConnection = Connection->GetConnection();
			if (!OtherConnection)
			{
				continue;
			}

			if (AFGBuildable* OtherBuildable = OtherConnection->GetOuterBuildable())
			{
				OutActors.AddUnique(OtherBuildable);
			}
		}
	}

	TArray<UFGFactoryConnectionComponent*> GenericConnections;
	Actor->GetComponents<UFGFactoryConnectionComponent>(GenericConnections);
	for (UFGFactoryConnectionComponent* Connection : GenericConnections)
	{
		if (!Connection || !Connection->IsConnected())
		{
			continue;
		}

		UFGFactoryConnectionComponent* OtherConnection = Connection->GetConnection();
		if (!OtherConnection)
		{
			continue;
		}

		if (AFGBuildable* OtherBuildable = OtherConnection->GetOuterBuildable())
		{
			OutActors.AddUnique(OtherBuildable);
		}
	}
}

void AppendPipelineNeighbors(AActor* Actor, TArray<AActor*>& OutActors)
{
	if (!Actor)
	{
		return;
	}

	if (const AFGBuildablePipeBase* Pipe = Cast<AFGBuildablePipeBase>(Actor))
	{
		UFGPipeConnectionComponentBase* Connections[] = {
			Pipe->GetConnection0(),
			Pipe->GetConnection1()
		};

		for (UFGPipeConnectionComponentBase* Connection : Connections)
		{
			if (!Connection || !Connection->IsConnected())
			{
				continue;
			}

			UFGPipeConnectionComponentBase* OtherConnection = Connection->GetConnection();
			if (!OtherConnection)
			{
				continue;
			}

			if (AActor* OtherActor = OtherConnection->GetOwner())
			{
				OutActors.AddUnique(OtherActor);
			}
		}
	}

	if (AFGBuildablePipelineAttachment* Attachment = Cast<AFGBuildablePipelineAttachment>(Actor))
	{
		for (UFGPipeConnectionComponent* Connection : Attachment->GetPipeConnections())
		{
			if (!Connection || !Connection->IsConnected())
			{
				continue;
			}

			UFGPipeConnectionComponentBase* OtherConnection = Connection->GetConnection();
			if (!OtherConnection)
			{
				continue;
			}

			if (AActor* OtherActor = OtherConnection->GetOwner())
			{
				OutActors.AddUnique(OtherActor);
			}
		}
	}

	TArray<UFGPipeConnectionComponent*> GenericConnections;
	Actor->GetComponents<UFGPipeConnectionComponent>(GenericConnections);
	for (UFGPipeConnectionComponent* Connection : GenericConnections)
	{
		if (!Connection || !Connection->IsConnected())
		{
			continue;
		}

		UFGPipeConnectionComponentBase* OtherConnection = Connection->GetConnection();
		if (!OtherConnection)
		{
			continue;
		}

		if (AActor* OtherActor = OtherConnection->GetOwner())
		{
			OutActors.AddUnique(OtherActor);
		}
	}
}

void AppendStorageNeighbors(AActor* Actor, TArray<AActor*>& OutActors)
{
	AFGBuildable* Buildable = Cast<AFGBuildable>(Actor);
	if (!Buildable)
	{
		return;
	}

	TArray<UFGFactoryConnectionComponent*> Connections;
	Buildable->GetComponents<UFGFactoryConnectionComponent>(Connections);
	for (UFGFactoryConnectionComponent* Connection : Connections)
	{
		if (!Connection || !Connection->IsConnected())
		{
			continue;
		}

		UFGFactoryConnectionComponent* OtherConnection = Connection->GetConnection();
		if (!OtherConnection)
		{
			continue;
		}

		if (AFGBuildable* OtherBuildable = OtherConnection->GetOuterBuildable())
		{
			OutActors.AddUnique(OtherBuildable);
		}
	}
}

void AppendPowerNeighbors(AActor* Actor, TArray<AActor*>& OutActors)
{
	if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Actor))
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UFGCircuitConnectionComponent* Connection = Wire->GetConnection(Index);
			if (!Connection)
			{
				continue;
			}

			if (AActor* Owner = Connection->GetOwner())
			{
				OutActors.AddUnique(Owner);
			}
		}
	}

	TArray<UFGPowerConnectionComponent*> PowerConnections;
	Actor->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
	for (UFGPowerConnectionComponent* PowerConnection : PowerConnections)
	{
		if (!PowerConnection)
		{
			continue;
		}

		TArray<UFGCircuitConnectionComponent*> Connections;
		PowerConnection->GetConnections(Connections);
		for (UFGCircuitConnectionComponent* Connection : Connections)
		{
			if (Connection && Connection->GetOwner())
			{
				OutActors.AddUnique(Connection->GetOwner());
			}
		}

		TArray<AFGBuildableWire*> Wires;
		PowerConnection->GetWires(Wires);
		for (AFGBuildableWire* Wire : Wires)
		{
			if (IsValid(Wire))
			{
				OutActors.AddUnique(Wire);
			}
		}
	}
}
}

EBulkUpgradeFamily FConveyorBeltUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::ConveyorBelt;
}

bool FConveyorBeltUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	return IsValid(Actor) && Actor->IsA<AFGBuildableConveyorBelt>();
}

FBulkUpgradePreviewRow FConveyorBeltUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildableConveyorBelt::StaticClass());
}

void FConveyorBeltUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendConveyorNeighbors(Actor, OutActors);
}

EBulkUpgradeFamily FConveyorLiftUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::ConveyorLift;
}

bool FConveyorLiftUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	return IsValid(Actor) && Actor->IsA<AFGBuildableConveyorLift>();
}

FBulkUpgradePreviewRow FConveyorLiftUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildableConveyorLift::StaticClass());
}

void FConveyorLiftUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendConveyorNeighbors(Actor, OutActors);
}

EBulkUpgradeFamily FPipelineUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::Pipeline;
}

bool FPipelineUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	return IsValid(Actor) && Actor->IsA<AFGBuildablePipeline>();
}

FBulkUpgradePreviewRow FPipelineUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildablePipeline::StaticClass());
}

void FPipelineUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendPipelineNeighbors(Actor, OutActors);
}

EBulkUpgradeFamily FStorageUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::Storage;
}

bool FStorageUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	return IsValid(Actor) && Actor->IsA<AFGBuildableStorage>();
}

FBulkUpgradePreviewRow FStorageUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildableStorage::StaticClass());
}

void FStorageUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendStorageNeighbors(Actor, OutActors);
}

EBulkUpgradeFamily FPipelinePumpUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::PipelinePump;
}

bool FPipelinePumpUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	const AFGBuildablePipelinePump* Pump = Cast<AFGBuildablePipelinePump>(Actor);
	return IsValid(Pump) && Pump->GetDesignHeadLift() > 0.0f;
}

FBulkUpgradePreviewRow FPipelinePumpUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildablePipelinePump::StaticClass());
}

void FPipelinePumpUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendPipelineNeighbors(Actor, OutActors);
}

EBulkUpgradeFamily FWireUpgradeAdapter::GetFamily() const
{
	return EBulkUpgradeFamily::Wire;
}

bool FWireUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	return IsValid(Actor) && Actor->IsA<AFGBuildableWire>();
}

FBulkUpgradePreviewRow FWireUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildableWire::StaticClass());
}

void FWireUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendPowerNeighbors(Actor, OutActors);
}

FPowerPoleFamilyUpgradeAdapter::FPowerPoleFamilyUpgradeAdapter(EBulkUpgradeFamily InFamily)
	: Family(InFamily)
{
}

EBulkUpgradeFamily FPowerPoleFamilyUpgradeAdapter::GetFamily() const
{
	return Family;
}

bool FPowerPoleFamilyUpgradeAdapter::CanClassify(const AActor* Actor) const
{
	const AFGBuildablePowerPole* PowerPole = Cast<AFGBuildablePowerPole>(Actor);
	return IsValid(PowerPole) && AdapterClassifyPowerPole(PowerPole) == Family;
}

FBulkUpgradePreviewRow FPowerPoleFamilyUpgradeAdapter::BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const
{
	return BuildCommonRow(Actor, Request, GetFamily(), AFGBuildablePowerPole::StaticClass());
}

void FPowerPoleFamilyUpgradeAdapter::AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const
{
	AppendPowerNeighbors(Actor, OutActors);
}

const TArray<const IBulkUpgradeAdapter*>& GetBulkUpgradeAdapters()
{
	static FConveyorBeltUpgradeAdapter ConveyorBeltAdapter;
	static FConveyorLiftUpgradeAdapter ConveyorLiftAdapter;
	static FStorageUpgradeAdapter StorageAdapter;
	static FPipelineUpgradeAdapter PipelineAdapter;
	static FPipelinePumpUpgradeAdapter PipelinePumpAdapter;
	static FWireUpgradeAdapter WireAdapter;
	static FPowerPoleFamilyUpgradeAdapter PowerPoleAdapter(EBulkUpgradeFamily::PowerPole);
	static FPowerPoleFamilyUpgradeAdapter PowerPoleWallAdapter(EBulkUpgradeFamily::PowerPoleWall);
	static FPowerPoleFamilyUpgradeAdapter PowerPoleWallDoubleAdapter(EBulkUpgradeFamily::PowerPoleWallDouble);
	static FPowerPoleFamilyUpgradeAdapter PowerTowerAdapter(EBulkUpgradeFamily::PowerTower);
	static TArray<const IBulkUpgradeAdapter*> Adapters = {
		&ConveyorBeltAdapter,
		&ConveyorLiftAdapter,
		&StorageAdapter,
		&PipelineAdapter,
		&PipelinePumpAdapter,
		&WireAdapter,
		&PowerPoleAdapter,
		&PowerPoleWallAdapter,
		&PowerPoleWallDoubleAdapter,
		&PowerTowerAdapter
	};

	return Adapters;
}

#undef LOCTEXT_NAMESPACE
