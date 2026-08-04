#include "BulkUpgradePlanner.h"

#include "Adapters/BulkUpgradeLogisticsAdapters.h"
#include "Misc/DateTime.h"

#define LOCTEXT_NAMESPACE "BulkUpgradePlanner"

namespace
{
int32 NormalizeMaxOperations(const FBulkUpgradeRequest& Request)
{
	return FMath::Clamp(Request.MaxOperations, 1, 10000);
}

void AddPlannerCost(TArray<FItemAmount>& Accumulator, const TArray<FItemAmount>& Cost)
{
	for (const FItemAmount& ItemAmount : Cost)
	{
		if (!ItemAmount.ItemClass || ItemAmount.Amount <= 0)
		{
			continue;
		}

		if (FItemAmount* Existing = Accumulator.FindByPredicate([&ItemAmount](const FItemAmount& Candidate)
		{
			return Candidate.ItemClass == ItemAmount.ItemClass;
		}))
		{
			Existing->Amount += ItemAmount.Amount;
		}
		else
		{
			Accumulator.Add(ItemAmount);
		}
	}
}

void RecountPlannerPlan(FBulkUpgradePlan& Plan)
{
	Plan.EligibleCount = 0;
	Plan.SkippedCount = 0;
	Plan.FailedCount = 0;
	Plan.CommittedCount = 0;
	Plan.bCostIsFinal = Plan.Request.CostPolicy == EBulkUpgradeCostPolicy::FreeLocalDev;
	Plan.TotalEstimatedGrossCost.Reset();

	for (const FBulkUpgradePreviewRow& Row : Plan.Rows)
	{
		switch (Row.Status)
		{
		case EBulkUpgradeRowStatus::Eligible:
			Plan.EligibleCount += 1;
			AddPlannerCost(Plan.TotalEstimatedGrossCost, Row.EstimatedGrossCost);
			break;
		case EBulkUpgradeRowStatus::Skipped:
			Plan.SkippedCount += 1;
			break;
		case EBulkUpgradeRowStatus::Failed:
			Plan.FailedCount += 1;
			break;
		case EBulkUpgradeRowStatus::Committed:
			Plan.CommittedCount += 1;
			break;
		}
	}
}

FBulkUpgradePreviewRow MakePlannerSkippedRow(AActor* Actor, EBulkUpgradeFamily Family, const FText& Reason)
{
	FBulkUpgradePreviewRow Row;
	Row.Actor = Actor;
	Row.ActorName = IsValid(Actor) ? FName(*Actor->GetName()) : NAME_None;
	Row.Family = Family;
	Row.Status = EBulkUpgradeRowStatus::Skipped;
	Row.StatusText = Reason;
	return Row;
}

FBulkUpgradePlan MakeEmptyPlan(UObject* WorldContextObject, const FBulkUpgradeRequest& Request)
{
	FBulkUpgradePlan Plan;
	Plan.Request = Request;
	Plan.GeneratedAtIsoUtc = FDateTime::UtcNow().ToIso8601();
	Plan.bCostIsFinal = false;

	if (!WorldContextObject)
	{
		Plan.Warnings.Add(LOCTEXT("NoWorldContext", "No world context was provided."));
	}

	return Plan;
}

const IBulkUpgradeAdapter* SelectAdapter(AActor* SeedActor, EBulkUpgradeFamily RequestedFamily)
{
	for (const IBulkUpgradeAdapter* Adapter : GetBulkUpgradeAdapters())
	{
		if (!Adapter)
		{
			continue;
		}

		const bool bFamilyMatches = RequestedFamily == EBulkUpgradeFamily::Unknown || Adapter->GetFamily() == RequestedFamily;
		if (bFamilyMatches && Adapter->CanClassify(SeedActor))
		{
			return Adapter;
		}
	}

	return nullptr;
}

bool RequestAllowsFamily(EBulkUpgradeFamily Family, const FBulkUpgradeRequest& Request)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		return Request.bAllowConveyorBelts;
	case EBulkUpgradeFamily::ConveyorLift:
		return Request.bAllowConveyorLifts;
	case EBulkUpgradeFamily::Storage:
		return Request.bAllowStorage;
	case EBulkUpgradeFamily::Pipeline:
		return Request.bAllowPipelines;
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
	case EBulkUpgradeFamily::Unknown:
	default:
		return false;
	}
}

const IBulkUpgradeAdapter* SelectAllowedAdapter(AActor* Actor, const FBulkUpgradeRequest& Request)
{
	const IBulkUpgradeAdapter* Adapter = SelectAdapter(Actor, EBulkUpgradeFamily::Unknown);
	return Adapter && RequestAllowsFamily(Adapter->GetFamily(), Request) ? Adapter : nullptr;
}

FBulkUpgradePreviewRow BuildPlannerRowForActor(AActor* Actor, const FBulkUpgradeRequest& Request)
{
	const IBulkUpgradeAdapter* Adapter = SelectAdapter(Actor, EBulkUpgradeFamily::Unknown);
	if (!Adapter)
	{
		return MakePlannerSkippedRow(Actor, EBulkUpgradeFamily::Unknown, LOCTEXT("UnsupportedActorInList", "Actor does not match a supported MassUpgrade-style family."));
	}

	if (!RequestAllowsFamily(Adapter->GetFamily(), Request))
	{
		return MakePlannerSkippedRow(Actor, Adapter->GetFamily(), LOCTEXT("FamilyNotRequested", "This MassUpgrade family is not enabled for this request."));
	}

	return Adapter->BuildPreviewRow(Actor, Request);
}

TArray<AActor*> CollectActors(AActor* SeedActor, const IBulkUpgradeAdapter& Adapter, const FBulkUpgradeRequest& Request, TArray<FText>& Warnings)
{
	TArray<AActor*> Result;
	if (!IsValid(SeedActor))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("CollectActors: SeedActor is invalid"));
		return Result;
	}

	Result.Add(SeedActor);

	if (Request.Scope == EBulkUpgradeScope::Single)
	{
		return Result;
	}

	if (Request.Scope == EBulkUpgradeScope::Radius)
	{
		Warnings.Add(LOCTEXT("RadiusScopeNotImplemented", "Radius scope is not implemented yet; planning only the seed actor."));
		return Result;
	}

	if (!Request.bAllowConnectedScope)
	{
		Warnings.Add(LOCTEXT("ConnectedScopeDisabled", "Connected scope was requested but disabled; planning only the seed actor."));
		return Result;
	}

	const int32 MaxOperations = NormalizeMaxOperations(Request);
	const int32 MaxVisitedActors = FMath::Max(MaxOperations * 8, MaxOperations + 64);
	TSet<AActor*> Visited;
	TArray<AActor*> Queue;
	Visited.Add(SeedActor);
	Queue.Add(SeedActor);

	for (int32 QueueIndex = 0; QueueIndex < Queue.Num() && Result.Num() < MaxOperations && Visited.Num() < MaxVisitedActors; ++QueueIndex)
	{
		AActor* Current = Queue[QueueIndex];
		if (!IsValid(Current))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("CollectActors: Current actor in queue is invalid"));
			continue;
		}

		TArray<AActor*> Neighbors;
		Adapter.AppendConnectedActors(Current, Neighbors);

		for (AActor* Neighbor : Neighbors)
		{
			if (!IsValid(Neighbor) || Visited.Contains(Neighbor))
			{
				continue;
			}

			Visited.Add(Neighbor);
			Queue.Add(Neighbor);
			if (SelectAllowedAdapter(Neighbor, Request))
			{
				Result.Add(Neighbor);

				if (Result.Num() >= MaxOperations)
				{
					Warnings.Add(LOCTEXT("ScopeCapped", "Connected scope reached MaxOperations and was capped."));
					break;
				}
			}
		}
	}

	if (Visited.Num() >= MaxVisitedActors && Result.Num() < MaxOperations)
	{
		Warnings.Add(LOCTEXT("TraversalCapped", "Connected scope reached its traversal guard before all connected actors were inspected."));
	}

	return Result;
}
}

FBulkUpgradePlan UBulkUpgradePlanner::BuildPlan(UObject* WorldContextObject, AActor* SeedActor, const FBulkUpgradeRequest& Request)
{
	FBulkUpgradePlan Plan = MakeEmptyPlan(WorldContextObject, Request);

	if (!IsValid(SeedActor))
	{
		Plan.Warnings.Add(LOCTEXT("NoSeedActor", "No valid seed actor was provided."));
		RecountPlannerPlan(Plan);
		return Plan;
	}

	const IBulkUpgradeAdapter* Adapter = SelectAdapter(SeedActor, Request.Family);
	if (!Adapter)
	{
		Plan.Warnings.Add(LOCTEXT("NoAdapter", "Seed actor does not match a supported upgrade family."));
		RecountPlannerPlan(Plan);
		return Plan;
	}

	const TArray<AActor*> Actors = CollectActors(SeedActor, *Adapter, Request, Plan.Warnings);
	for (AActor* Actor : Actors)
	{
		Plan.Rows.Add(BuildPlannerRowForActor(Actor, Request));
	}

	if (Plan.Rows.Num() == 0)
	{
		Plan.Warnings.Add(LOCTEXT("NoRows", "No actors were collected for this plan."));
	}

	RecountPlannerPlan(Plan);
	return Plan;
}

FBulkUpgradePlan UBulkUpgradePlanner::BuildPlanFromActors(UObject* WorldContextObject, const TArray<AActor*>& Actors, const FBulkUpgradeRequest& Request)
{
	FBulkUpgradePlan Plan = MakeEmptyPlan(WorldContextObject, Request);
	const int32 MaxOperations = NormalizeMaxOperations(Request);
	TSet<AActor*> SeenActors;

	for (AActor* Actor : Actors)
	{
		if (Plan.Rows.Num() >= MaxOperations)
		{
			Plan.Warnings.Add(LOCTEXT("ActorPlanCapped", "Actor plan reached MaxOperations and was capped."));
			break;
		}

		if (!IsValid(Actor))
		{
			Plan.Rows.Add(MakePlannerSkippedRow(Actor, EBulkUpgradeFamily::Unknown, LOCTEXT("InvalidActorInList", "Actor list contains an invalid actor.")));
			continue;
		}

		if (SeenActors.Contains(Actor))
		{
			continue;
		}
		SeenActors.Add(Actor);

		FBulkUpgradePreviewRow Row = BuildPlannerRowForActor(Actor, Request);
		if (Row.Status == EBulkUpgradeRowStatus::Skipped)
		{
			Plan.Rows.Add(Row);
			continue;
		}

		Plan.Rows.Add(Row);
	}

	if (Plan.Rows.Num() == 0)
	{
		Plan.Warnings.Add(LOCTEXT("NoActorRows", "No actors were available for this plan."));
	}

	RecountPlannerPlan(Plan);
	return Plan;
}

FBulkUpgradePlan UBulkUpgradePlanner::BuildPlanFromProductionInfo(UObject* WorldContextObject, const FBulkUpgradeProductionInfoSet& ProductionInfo, const FBulkUpgradeRequest& Request)
{
	TArray<AActor*> Actors;
	for (const FBulkUpgradeProductionGroup& Group : ProductionInfo.Groups)
	{
		for (AFGBuildable* Buildable : Group.Buildables)
		{
			Actors.Add(Buildable);
		}
	}

	FBulkUpgradePlan Plan = BuildPlanFromActors(WorldContextObject, Actors, Request);
	for (const FText& Warning : ProductionInfo.Warnings)
	{
		Plan.Warnings.Add(Warning);
	}

	return Plan;
}

#undef LOCTEXT_NAMESPACE
