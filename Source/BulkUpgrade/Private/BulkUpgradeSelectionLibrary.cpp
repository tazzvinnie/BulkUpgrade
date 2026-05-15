#include "BulkUpgradeSelectionLibrary.h"

#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCircuitConnectionComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPowerConnectionComponent.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeSelectionLibrary"

namespace
{
int32 NormalizeMaxVisited(const FBulkUpgradeCollectionOptions& Options)
{
	return FMath::Clamp(Options.MaxVisited, 1, 10000);
}

EBulkUpgradeFamily ClassifyBuildable(const AFGBuildable* Buildable)
{
	if (!IsValid(Buildable))
	{
		return EBulkUpgradeFamily::Unknown;
	}

	if (Buildable->IsA<AFGBuildableConveyorBelt>())
	{
		return EBulkUpgradeFamily::ConveyorBelt;
	}

	if (Buildable->IsA<AFGBuildableConveyorLift>())
	{
		return EBulkUpgradeFamily::ConveyorLift;
	}

	if (Buildable->IsA<AFGBuildableStorage>())
	{
		return EBulkUpgradeFamily::Storage;
	}

	if (Buildable->IsA<AFGBuildablePipeline>())
	{
		return EBulkUpgradeFamily::Pipeline;
	}

	if (const AFGBuildablePipelinePump* Pump = Cast<AFGBuildablePipelinePump>(Buildable))
	{
		return Pump->GetDesignHeadLift() > 0.0f ? EBulkUpgradeFamily::PipelinePump : EBulkUpgradeFamily::Unknown;
	}

	if (Buildable->IsA<AFGBuildableWire>())
	{
		return EBulkUpgradeFamily::Wire;
	}

	if (const AFGBuildablePowerPole* PowerPole = Cast<AFGBuildablePowerPole>(Buildable))
	{
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

bool ShouldCollectFamily(EBulkUpgradeFamily Family, const FBulkUpgradeCollectionOptions& Options)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return Options.bIncludeBelts;
	case EBulkUpgradeFamily::ConveyorLift:
		return Options.bIncludeLifts;
	case EBulkUpgradeFamily::Storage:
		return Options.bIncludeStorages;
	case EBulkUpgradeFamily::Pipeline:
		return Options.bIncludePipelines;
	case EBulkUpgradeFamily::PipelinePump:
		return Options.bIncludePipelinePumps;
	case EBulkUpgradeFamily::Wire:
		return Options.bIncludeWires;
	case EBulkUpgradeFamily::PowerPole:
		return Options.bIncludePowerPoles;
	case EBulkUpgradeFamily::PowerPoleWall:
		return Options.bIncludePowerPoleWalls;
	case EBulkUpgradeFamily::PowerPoleWallDouble:
		return Options.bIncludePowerPoleWallDoubles;
	case EBulkUpgradeFamily::PowerTower:
		return Options.bIncludePowerTowers;
	case EBulkUpgradeFamily::Unknown:
	default:
		return false;
	}
}

bool IsDescriptorExcluded(TSubclassOf<UFGBuildDescriptor> Descriptor, const FBulkUpgradeCollectionOptions& Options)
{
	return Descriptor && Options.ExcludedBuildDescriptors.Contains(Descriptor);
}

FName SelectionClassNameOf(const UClass* Class)
{
	return Class ? Class->GetFName() : NAME_None;
}

void AddBuildableToGroup(FBulkUpgradeProductionInfoSet& Result, AFGBuildable* Buildable, EBulkUpgradeFamily Family)
{
	if (!IsValid(Buildable))
	{
		return;
	}

	TSubclassOf<UFGBuildDescriptor> Descriptor = Buildable->GetBuiltWithDescriptor<UFGBuildDescriptor>();
	TSubclassOf<AFGBuildable> BuildableClass = Buildable->GetClass();

	FBulkUpgradeProductionGroup* ExistingGroup = Result.Groups.FindByPredicate(
		[Family, Descriptor, BuildableClass](const FBulkUpgradeProductionGroup& Group)
		{
			return Group.Family == Family &&
				Group.BuildDescriptor == Descriptor &&
				Group.BuildableClass == BuildableClass;
		});

	if (!ExistingGroup)
	{
		FBulkUpgradeProductionGroup& NewGroup = Result.Groups.AddDefaulted_GetRef();
		NewGroup.Family = Family;
		NewGroup.BuildDescriptor = Descriptor;
		NewGroup.BuildableClass = BuildableClass;
		NewGroup.BuildableClassName = SelectionClassNameOf(BuildableClass.Get());
		ExistingGroup = &NewGroup;
	}

	ExistingGroup->Buildables.AddUnique(Buildable);
	Result.TotalBuildables += 1;
}

void AddFactoryNeighborFromConnection(UFGFactoryConnectionComponent* Connection, TArray<AFGBuildable*>& OutBuildables)
{
	if (!Connection || !Connection->IsConnected())
	{
		return;
	}

	UFGFactoryConnectionComponent* OtherConnection = Connection->GetConnection();
	if (!OtherConnection)
	{
		return;
	}

	if (AFGBuildable* OtherBuildable = OtherConnection->GetOuterBuildable())
	{
		OutBuildables.AddUnique(OtherBuildable);
	}
}

void AddPipeNeighborFromConnection(UFGPipeConnectionComponentBase* Connection, TArray<AFGBuildable*>& OutBuildables)
{
	if (!Connection || !Connection->IsConnected())
	{
		return;
	}

	UFGPipeConnectionComponentBase* OtherConnection = Connection->GetConnection();
	if (!OtherConnection)
	{
		return;
	}

	if (AFGBuildable* OtherBuildable = Cast<AFGBuildable>(OtherConnection->GetOwner()))
	{
		OutBuildables.AddUnique(OtherBuildable);
	}
}

void AppendDirectLogisticsNeighbors(AFGBuildable* Buildable, TArray<AFGBuildable*>& OutBuildables)
{
	if (AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Buildable))
	{
		if (Conveyor->GetConnection0())
		{
			AddFactoryNeighborFromConnection(Conveyor->GetConnection0(), OutBuildables);
		}
		if (Conveyor->GetConnection1())
		{
			AddFactoryNeighborFromConnection(Conveyor->GetConnection1(), OutBuildables);
		}
	}
	else if (Buildable->IsA<AFGBuildableStorage>())
	{
		TArray<UFGFactoryConnectionComponent*> FactoryConnections;
		Buildable->GetComponents<UFGFactoryConnectionComponent>(FactoryConnections);
		for (UFGFactoryConnectionComponent* Connection : FactoryConnections)
		{
			AddFactoryNeighborFromConnection(Connection, OutBuildables);
		}
	}

	if (AFGBuildablePipeBase* Pipe = Cast<AFGBuildablePipeBase>(Buildable))
	{
		if (Pipe->GetConnection0())
		{
			AddPipeNeighborFromConnection(Pipe->GetConnection0(), OutBuildables);
		}
		if (Pipe->GetConnection1())
		{
			AddPipeNeighborFromConnection(Pipe->GetConnection1(), OutBuildables);
		}
	}
	else if (AFGBuildablePipelineAttachment* Attachment = Cast<AFGBuildablePipelineAttachment>(Buildable))
	{
		for (UFGPipeConnectionComponent* Connection : Attachment->GetPipeConnections())
		{
			AddPipeNeighborFromConnection(Connection, OutBuildables);
		}
	}

	if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Buildable))
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UFGCircuitConnectionComponent* Connection = Wire->GetConnection(Index);
			if (Connection)
			{
				if (AFGBuildable* OtherBuildable = Cast<AFGBuildable>(Connection->GetOwner()))
				{
					OutBuildables.AddUnique(OtherBuildable);
				}
			}
		}
	}
	else
	{
		TArray<UFGPowerConnectionComponent*> PowerConnections;
		Buildable->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
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
				if (Connection)
				{
					if (AFGBuildable* OtherBuildable = Cast<AFGBuildable>(Connection->GetOwner()))
					{
						OutBuildables.AddUnique(OtherBuildable);
					}
				}
			}

			TArray<AFGBuildableWire*> Wires;
			PowerConnection->GetWires(Wires);
			for (AFGBuildableWire* ConnectedWire : Wires)
			{
				if (IsValid(ConnectedWire))
				{
					OutBuildables.AddUnique(ConnectedWire);
				}
			}
		}
	}
}

void AppendAttachmentCrossingNeighbors(AFGBuildable* Buildable, TArray<AFGBuildable*>& OutBuildables)
{
	TArray<UFGFactoryConnectionComponent*> FactoryConnections;
	Buildable->GetComponents<UFGFactoryConnectionComponent>(FactoryConnections);
	for (UFGFactoryConnectionComponent* Connection : FactoryConnections)
	{
		AddFactoryNeighborFromConnection(Connection, OutBuildables);
	}

	TArray<UFGPipeConnectionComponentBase*> PipeConnections;
	Buildable->GetComponents<UFGPipeConnectionComponentBase>(PipeConnections);
	for (UFGPipeConnectionComponentBase* Connection : PipeConnections)
	{
		AddPipeNeighborFromConnection(Connection, OutBuildables);
	}
}

void AppendNeighbors(AFGBuildable* Buildable, const FBulkUpgradeCollectionOptions& Options, TArray<AFGBuildable*>& OutBuildables)
{
	AppendDirectLogisticsNeighbors(Buildable, OutBuildables);

	if (Options.bCrossAttachments)
	{
		AppendAttachmentCrossingNeighbors(Buildable, OutBuildables);
	}
}

void SortGroups(FBulkUpgradeProductionInfoSet& Result)
{
	Result.Groups.Sort([](const FBulkUpgradeProductionGroup& Left, const FBulkUpgradeProductionGroup& Right)
	{
		const uint8 LeftFamily = static_cast<uint8>(Left.Family);
		const uint8 RightFamily = static_cast<uint8>(Right.Family);
		if (LeftFamily != RightFamily)
		{
			return LeftFamily < RightFamily;
		}

		return Left.BuildableClassName.ToString() < Right.BuildableClassName.ToString();
	});
}
}

FBulkUpgradeProductionInfoSet UBulkUpgradeSelectionLibrary::CollectProductionInfoFromSeed(AActor* SeedActor, const FBulkUpgradeCollectionOptions& Options)
{
	FBulkUpgradeProductionInfoSet Result;
	AFGBuildable* SeedBuildable = Cast<AFGBuildable>(SeedActor);
	if (!IsValid(SeedBuildable))
	{
		Result.Warnings.Add(LOCTEXT("NoSeedBuildable", "No valid buildable seed was provided."));
		return Result;
	}

	const int32 MaxVisited = NormalizeMaxVisited(Options);
	TSet<AFGBuildable*> Visited;
	TArray<AFGBuildable*> Queue;
	Visited.Add(SeedBuildable);
	Queue.Add(SeedBuildable);

	for (int32 QueueIndex = 0; QueueIndex < Queue.Num() && Result.VisitedBuildables < MaxVisited; ++QueueIndex)
	{
		AFGBuildable* Current = Queue[QueueIndex];
		if (!IsValid(Current))
		{
			continue;
		}

		Result.VisitedBuildables += 1;

		const EBulkUpgradeFamily Family = ClassifyBuildable(Current);
		const TSubclassOf<UFGBuildDescriptor> Descriptor = Current->GetBuiltWithDescriptor<UFGBuildDescriptor>();
		if (ShouldCollectFamily(Family, Options) && !IsDescriptorExcluded(Descriptor, Options))
		{
			AddBuildableToGroup(Result, Current, Family);
		}

		TArray<AFGBuildable*> Neighbors;
		AppendNeighbors(Current, Options, Neighbors);
		for (AFGBuildable* Neighbor : Neighbors)
		{
			if (!IsValid(Neighbor) || Visited.Contains(Neighbor))
			{
				continue;
			}

			Visited.Add(Neighbor);
			Queue.Add(Neighbor);
		}
	}

	if (Queue.Num() > Result.VisitedBuildables)
	{
		Result.Warnings.Add(LOCTEXT("CollectionCapped", "Connected collection reached MaxVisited and was capped."));
	}

	if (Result.Groups.Num() == 0)
	{
		Result.Warnings.Add(LOCTEXT("NoGroups", "No supported MassUpgrade-style buildables were collected from the seed."));
	}

	SortGroups(Result);
	return Result;
}

TArray<AActor*> UBulkUpgradeSelectionLibrary::FlattenProductionInfo(const FBulkUpgradeProductionInfoSet& ProductionInfo)
{
	TArray<AActor*> Actors;
	for (const FBulkUpgradeProductionGroup& Group : ProductionInfo.Groups)
	{
		for (AFGBuildable* Buildable : Group.Buildables)
		{
			if (IsValid(Buildable))
			{
				Actors.AddUnique(Buildable);
			}
		}
	}

	return Actors;
}

#undef LOCTEXT_NAMESPACE
