#include "BulkUpgradeExecutor.h"

#include "BulkUpgrade.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/PrimitiveComponent.h"
#include "FGBuildableSubsystem.h"
#include "FGConstructDisqualifier.h"
#include "FGCrate.h"
#include "FGDismantleInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGItemPickup_Spawnable.h"
#include "FGPipeConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "Hologram/FGConveyorBeltHologram.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGPipelineHologram.h"
#include "Kismet/GameplayStatics.h"
#include "Resources/FGBuildingDescriptor.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeExecutor"

namespace
{
void MarkFailed(FBulkUpgradePreviewRow& Row, const FText& Reason)
{
	Row.Status = EBulkUpgradeRowStatus::Failed;
	Row.StatusText = Reason;
}

void MarkCommitted(FBulkUpgradePreviewRow& Row)
{
	Row.Status = EBulkUpgradeRowStatus::Committed;
	Row.StatusText = LOCTEXT("Committed", "Upgrade committed through the MassUpgrade-derived upgrade path.");
}

FString DescribeConstructDisqualifiers(const AFGHologram* Hologram)
{
	if (!IsValid(Hologram))
	{
		return TEXT("<no hologram>");
	}

	TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
	Hologram->GetConstructDisqualifiers(Disqualifiers);
	if (Disqualifiers.Num() == 0)
	{
		return TEXT("<none>");
	}

	TArray<FString> Names;
	Names.Reserve(Disqualifiers.Num());
	for (TSubclassOf<UFGConstructDisqualifier> Disqualifier : Disqualifiers)
	{
		Names.Add(GetNameSafe(Disqualifier.Get()));
	}
	return FString::Join(Names, TEXT(","));
}

bool HasOnlyProgrammaticUpgradeDisqualifiers(const AFGHologram* Hologram)
{
	if (!IsValid(Hologram))
	{
		return false;
	}

	TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
	Hologram->GetConstructDisqualifiers(Disqualifiers);
	if (Disqualifiers.Num() == 0)
	{
		return true;
	}

	for (TSubclassOf<UFGConstructDisqualifier> Disqualifier : Disqualifiers)
	{
		const FString Name = GetNameSafe(Disqualifier.Get());
		if (Name != TEXT("FGCDInitializing") && Name != TEXT("FGCDInvalidAimLocation"))
		{
			return false;
		}
	}

	return true;
}

void AddExecutorCost(TArray<FItemAmount>& Accumulator, const TArray<FItemAmount>& Cost)
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

void RecountCommitPlan(FBulkUpgradePlan& Plan)
{
	Plan.EligibleCount = 0;
	Plan.SkippedCount = 0;
	Plan.FailedCount = 0;
	Plan.CommittedCount = 0;
	Plan.TotalEstimatedGrossCost.Reset();
	Plan.bCostIsFinal = Plan.Request.CostPolicy == EBulkUpgradeCostPolicy::FreeLocalDev;

	for (const FBulkUpgradePreviewRow& Row : Plan.Rows)
	{
		switch (Row.Status)
		{
		case EBulkUpgradeRowStatus::Eligible:
			Plan.EligibleCount += 1;
			AddExecutorCost(Plan.TotalEstimatedGrossCost, Row.EstimatedGrossCost);
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

bool CanAttemptMutation(const FBulkUpgradeCommitOptions& Options, FBulkUpgradeCommitReport& Report)
{
	if (Options.bDryRun)
	{
		Report.Messages.Add(LOCTEXT("DryRun", "Dry run only; no world changes were attempted."));
		return false;
	}

	if (Options.ExecutionProfile == EBulkUpgradeExecutionProfile::LocalOnlyDev && !Options.bAcknowledgeLocalSaveBackup)
	{
		Report.Messages.Add(LOCTEXT("BackupRequired", "World mutation is blocked until the local save backup acknowledgement is enabled."));
		return false;
	}

	if (!Options.bAllowUnverifiedWorldMutation)
	{
		Report.Messages.Add(LOCTEXT("MutationBlocked", "World mutation is blocked until the MassUpgrade-derived upgrade path is explicitly enabled."));
		return false;
	}

	return true;
}

bool ExecutorCanDismantleViaInterface(AActor* Actor)
{
	return IsValid(Actor) &&
		Actor->GetClass()->ImplementsInterface(UFGDismantleInterface::StaticClass()) &&
		IFGDismantleInterface::Execute_CanDismantle(Actor);
}

TSubclassOf<AFGBuildable> ExecutorGetBuildableClassFromRecipeSafe(TSubclassOf<UFGRecipe> Recipe)
{
	if (!Recipe)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ExecutorGetBuildableClassFromRecipeSafe: Recipe is null"));
		return nullptr;
	}

	const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);
	if (Products.Num() == 0)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ExecutorGetBuildableClassFromRecipeSafe: Recipe has no products"));
		return nullptr;
	}

	if (!Products[0].ItemClass)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ExecutorGetBuildableClassFromRecipeSafe: First product has null ItemClass"));
		return nullptr;
	}

	if (!Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ExecutorGetBuildableClassFromRecipeSafe: First product is not a building descriptor"));
		return nullptr;
	}

	const TSubclassOf<UFGBuildingDescriptor> BuildDescriptor(Products[0].ItemClass.Get());
	TSubclassOf<AFGBuildable> BuildableClass = UFGBuildingDescriptor::GetBuildableClass(BuildDescriptor);
	
	if (!BuildableClass)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ExecutorGetBuildableClassFromRecipeSafe: Failed to get buildable class from descriptor"));
	}
	
	return BuildableClass;
}

bool ValidateRowStillMatchesPlan(AFGBuildable* SourceBuildable, const FBulkUpgradePreviewRow& Row, FText& OutFailure)
{
	if (!IsValid(SourceBuildable))
	{
		OutFailure = LOCTEXT("InvalidBuildable", "Source buildable is no longer valid.");
		return false;
	}

	if (Row.SourceBuildableClass && SourceBuildable->GetClass() != Row.SourceBuildableClass.Get())
	{
		OutFailure = LOCTEXT("SourceClassChanged", "Source buildable class changed after preview.");
		return false;
	}

	if (Row.SourceRecipe && SourceBuildable->GetBuiltWithRecipe() != Row.SourceRecipe)
	{
		OutFailure = LOCTEXT("SourceRecipeChanged", "Source buildable recipe changed after preview.");
		return false;
	}

	TSubclassOf<AFGBuildable> TargetBuildableClass = ExecutorGetBuildableClassFromRecipeSafe(Row.TargetRecipe);
	if (!TargetBuildableClass)
	{
		OutFailure = LOCTEXT("TargetClassMissing", "Target recipe no longer resolves to a buildable class.");
		return false;
	}

	if (Row.TargetBuildableClass && TargetBuildableClass.Get() != Row.TargetBuildableClass.Get())
	{
		OutFailure = LOCTEXT("TargetClassChanged", "Target buildable class changed after preview.");
		return false;
	}

	if (SourceBuildable->GetClass() == TargetBuildableClass.Get())
	{
		OutFailure = LOCTEXT("AlreadyTargetAtCommit", "Source buildable is already the target class at commit time.");
		return false;
	}

	const int32 CurrentHash = static_cast<int32>(HashCombine(GetTypeHash(SourceBuildable->GetClass()), GetTypeHash(TargetBuildableClass.Get())));
	if (Row.PreconditionHash != 0 && CurrentHash != Row.PreconditionHash)
	{
		OutFailure = LOCTEXT("PreconditionHashChanged", "Preview precondition hash no longer matches the source and target classes.");
		return false;
	}

	if (!ExecutorCanDismantleViaInterface(SourceBuildable))
	{
		OutFailure = LOCTEXT("CannotDismantleAtCommit", "Source buildable cannot currently be dismantled/upgraded.");
		return false;
	}

	return true;
}

void DestroyChildDismantleActors(AFGBuildable* Buildable)
{
	if (!IsValid(Buildable))
	{
		return;
	}

	TArray<AActor*> ChildDismantleActors;
	Buildable->GetChildDismantleActors_Implementation(ChildDismantleActors);

	for (AActor* Actor : ChildDismantleActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
}

void DestroyConstructedActors(const TArray<AActor*>& Children, AActor* PrimaryActor)
{
	for (AActor* Child : Children)
	{
		if (IsValid(Child) && Child != PrimaryActor)
		{
			Child->Destroy();
		}
	}

	if (IsValid(PrimaryActor))
	{
		PrimaryActor->Destroy();
	}
}

bool IsValidConveyorItemIndex(const int32 Index, const int32 ItemCapacity)
{
	return ItemCapacity > 0 && Index >= 0 && Index < ItemCapacity;
}

bool IsEmptyConveyorIndexRange(const int32 FirstItemIndex, const int32 LastItemIndex)
{
	return FirstItemIndex == INDEX_NONE && LastItemIndex == INDEX_NONE;
}

bool IsValidConveyorIndexRange(const int32 FirstItemIndex, const int32 LastItemIndex, const int32 ItemCapacity)
{
	return IsEmptyConveyorIndexRange(FirstItemIndex, LastItemIndex) ||
		(IsValidConveyorItemIndex(FirstItemIndex, ItemCapacity) && IsValidConveyorItemIndex(LastItemIndex, ItemCapacity));
}

struct FFactoryConnectionTransfer
{
	UFGFactoryConnectionComponent* OldConnection = nullptr;
	UFGFactoryConnectionComponent* NewConnection = nullptr;
	UFGFactoryConnectionComponent* ExternalConnection = nullptr;
};

bool ReconnectConveyor(AFGBuildableConveyorBase* SourceConveyor, AFGBuildableConveyorBase* TargetConveyor)
{
	if (!IsValid(SourceConveyor) || !IsValid(TargetConveyor) ||
		!SourceConveyor->GetConnection0() || !SourceConveyor->GetConnection1() ||
		!TargetConveyor->GetConnection0() || !TargetConveyor->GetConnection1())
	{
		return false;
	}

	if (UFGFactoryConnectionComponent* Connection = SourceConveyor->GetConnection0()->GetConnection())
	{
		SourceConveyor->GetConnection0()->ClearConnection();
		TargetConveyor->GetConnection0()->SetConnection(Connection);
	}

	if (UFGFactoryConnectionComponent* Connection = SourceConveyor->GetConnection1()->GetConnection())
	{
		SourceConveyor->GetConnection1()->ClearConnection();
		TargetConveyor->GetConnection1()->SetConnection(Connection);
	}

	return true;
}

void DetachConveyorFromSubsystem(AFGBuildableSubsystem* BuildableSubsystem, AFGBuildableConveyorBase* Conveyor, const TCHAR* Role)
{
	if (!IsValid(BuildableSubsystem) || !IsValid(Conveyor))
	{
		return;
	}

	const int32 OldBucket = Conveyor->GetConveyorBucketID();
	AFGConveyorChainActor* OldChain = Conveyor->GetConveyorChainActor();
	if (OldBucket != INDEX_NONE || IsValid(OldChain))
	{
		const bool bRemovedBucket = BuildableSubsystem->RemoveConveyorFromBucket(Conveyor);
		UE_LOG(LogBulkUpgrade, Display,
			TEXT("BulkUpgrade conveyor subsystem detach role=%s conveyor=%s oldBucket=%d oldChain=%s removedBucket=%s"),
			Role,
			*GetNameSafe(Conveyor),
			OldBucket,
			*GetNameSafe(OldChain),
			bRemovedBucket ? TEXT("true") : TEXT("false"));
	}

	Conveyor->SetNextTickConveyor(nullptr);
	Conveyor->SetConveyorChainFlags(0);
	Conveyor->SetConveyorBucketID(INDEX_NONE);
	Conveyor->SetConveyorChainActor(nullptr);
	Conveyor->MarkItemTransformsDirty();
}

bool RebuildConveyorThroughSubsystem(AFGBuildableSubsystem* BuildableSubsystem, AFGBuildableConveyorBase* SourceConveyor, AFGBuildableConveyorBase* TargetConveyor)
{
	if (!IsValid(BuildableSubsystem) || !IsValid(SourceConveyor) || !IsValid(TargetConveyor))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("RebuildConveyorThroughSubsystem: invalid parameters subsystem=%s source=%s target=%s"),
			*GetNameSafe(BuildableSubsystem),
			*GetNameSafe(SourceConveyor),
			*GetNameSafe(TargetConveyor));
		return false;
	}

	DetachConveyorFromSubsystem(BuildableSubsystem, SourceConveyor, TEXT("source"));
	DetachConveyorFromSubsystem(BuildableSubsystem, TargetConveyor, TEXT("target"));

	if (!ReconnectConveyor(SourceConveyor, TargetConveyor))
	{
		return false;
	}

	BuildableSubsystem->AddConveyor(TargetConveyor);
	const int32 NewBucket = TargetConveyor->GetConveyorBucketID();
	if (NewBucket != INDEX_NONE)
	{
		BuildableSubsystem->AssignConveyorTickOrder(NewBucket);
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor subsystem rebuild left target without an immediate bucket; keeping constructed actor for vanilla registration. target=%s"),
			*GetNameSafe(TargetConveyor));
	}

	UE_LOG(LogBulkUpgrade, Display,
		TEXT("BulkUpgrade conveyor subsystem rebuild target=%s bucket=%d chain=%s conn0=%s conn1=%s"),
		*GetNameSafe(TargetConveyor),
		NewBucket,
		*GetNameSafe(TargetConveyor->GetConveyorChainActor()),
		*GetNameSafe(TargetConveyor->GetConnection0() ? TargetConveyor->GetConnection0()->GetConnection() : nullptr),
		*GetNameSafe(TargetConveyor->GetConnection1() ? TargetConveyor->GetConnection1()->GetConnection() : nullptr));

	return true;
}

bool ReconnectPipeline(AFGBuildablePipeline* SourcePipeline, AFGBuildablePipeline* TargetPipeline)
{
	// Comprehensive validation
	if (!IsValid(SourcePipeline) || !IsValid(TargetPipeline))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPipeline: Invalid pipeline parameters"));
		return false;
	}

	// Get all connections first to validate they exist
	UFGPipeConnectionComponentBase* SourceConn0 = SourcePipeline->GetPipeConnection0();
	UFGPipeConnectionComponentBase* SourceConn1 = SourcePipeline->GetPipeConnection1();
	UFGPipeConnectionComponentBase* TargetConn0 = TargetPipeline->GetPipeConnection0();
	UFGPipeConnectionComponentBase* TargetConn1 = TargetPipeline->GetPipeConnection1();

	if (!SourceConn0 || !SourceConn1 || !TargetConn0 || !TargetConn1)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPipeline: Missing pipe connection components"));
		return false;
	}

	// Handle connection 0 safely
	if (IsValid(SourceConn0) && IsValid(TargetConn0))
	{
		UFGPipeConnectionComponentBase* Connection = SourceConn0->GetConnection();
		if (IsValid(Connection))
		{
			// Clear source connection first
			SourceConn0->ClearConnection();
			
			// Set target connection with validation
			TargetConn0->SetConnection(Connection);
			
			UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPipeline: Successfully reconnected pipe connection 0"));
		}
	}

	// Handle connection 1 safely
	if (IsValid(SourceConn1) && IsValid(TargetConn1))
	{
		UFGPipeConnectionComponentBase* Connection = SourceConn1->GetConnection();
		if (IsValid(Connection))
		{
			// Clear source connection first
			SourceConn1->ClearConnection();
			
			// Set target connection with validation
			TargetConn1->SetConnection(Connection);
			
			UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPipeline: Successfully reconnected pipe connection 1"));
		}
	}

	return true;
}

bool IsManualSpawnFamily(EBulkUpgradeFamily Family)
{
	return Family == EBulkUpgradeFamily::Storage || Family == EBulkUpgradeFamily::PipelinePump;
}

void AttachToBlueprintDesigner(AFGBuildable* SourceBuildable, AFGBuildable* TargetBuildable)
{
	// Comprehensive validation
	if (!IsValid(SourceBuildable) || !IsValid(TargetBuildable))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AttachToBlueprintDesigner: Invalid buildable parameters"));
		return;
	}

	AFGBuildableBlueprintDesigner* BlueprintDesigner = SourceBuildable->GetBlueprintDesigner();
	if (!IsValid(BlueprintDesigner))
	{
		UE_LOG(LogBulkUpgrade, Log, TEXT("AttachToBlueprintDesigner: Source buildable has no blueprint designer"));
		return;
	}

	// Safely set blueprint designer relationship
	TargetBuildable->SetInsideBlueprintDesigner(BlueprintDesigner);
	
	// Validate the target is still valid before calling the blueprint designer
	if (IsValid(TargetBuildable))
	{
		BlueprintDesigner->OnBuildableConstructedInsideDesigner(TargetBuildable);
		UE_LOG(LogBulkUpgrade, Log, TEXT("AttachToBlueprintDesigner: Successfully attached target to blueprint designer"));
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Error, TEXT("AttachToBlueprintDesigner: Target buildable became invalid during attachment"));
	}
}

FVector GetPlayerDropLocation(AFGCharacterPlayer* Player)
{
	if (!IsValid(Player))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("GetPlayerDropLocation: Invalid player"));
		return FVector::ZeroVector;
	}

	FVector PlayerLocation = Player->GetActorLocation();
	if (PlayerLocation.IsZero())
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("GetPlayerDropLocation: Player has zero location"));
		return FVector::ZeroVector;
	}

	FHitResult HitResult;
	float WaterDepth = 0.0f;
	FVector TraceEnd = PlayerLocation + FVector(0.0f, 0.0f, -60000.0f);
	
	// Safe trace with validation
	if (!Player->TraceForGround(PlayerLocation, TraceEnd, HitResult, WaterDepth))
	{
		UE_LOG(LogBulkUpgrade, Log, TEXT("GetPlayerDropLocation: Ground trace failed, using player location"));
		return PlayerLocation;
	}

	// Validate hit result location
	if (!HitResult.Location.IsZero())
	{
		FVector ResultLocation = HitResult.Location + FVector(0.0f, 0.0f, WaterDepth);
		UE_LOG(LogBulkUpgrade, Log, TEXT("GetPlayerDropLocation: Successfully found drop location"));
		return ResultLocation;
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Log, TEXT("GetPlayerDropLocation: Hit result location is zero, using player location"));
		return PlayerLocation;
	}
}

void AddStacksToPlayerOrCrate(AFGCharacterPlayer* Player, const TArray<FInventoryStack>& Stacks)
{
	// Early validation with comprehensive checks
	if (!IsValid(Player))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Invalid player"));
		return;
	}

	if (Stacks.Num() == 0)
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: No stacks to process"));
		return;
	}

	UFGInventoryComponent* PlayerInventory = Player->GetInventory();
	if (!IsValid(PlayerInventory))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Invalid player inventory"));
		return;
	}

	// Pre-allocate arrays for performance
	TArray<FInventoryStack> ValidStacks;
	ValidStacks.Reserve(Stacks.Num());
	TArray<FInventoryStack> ExcessStacks;
	ExcessStacks.Reserve(Stacks.Num());

	// Process stacks with bulletproof validation
	for (const FInventoryStack& SourceStack : Stacks)
	{
		// Comprehensive validation before any operations
		if (!SourceStack.HasItems())
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Skipping empty stack"));
			continue;
		}

		if (!IsValid(SourceStack.Item.GetItemClass()))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Skipping stack with null ItemClass"));
			continue;
		}

		// Add valid stack to our processing list
		ValidStacks.Add(SourceStack);
	}

	// Process all valid stacks
	for (const FInventoryStack& Stack : ValidStacks)
	{
		// Double-check ItemClass before inventory operations
		if (!IsValid(Stack.Item.GetItemClass()))
		{
			UE_LOG(LogBulkUpgrade, Error, TEXT("AddStacksToPlayerOrCrate: CRITICAL - ItemClass became null during processing"));
			continue;
		}

		// Additional validation for stack amount
		if (Stack.NumItems <= 0)
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Skipping stack with invalid amount: %d"), Stack.NumItems);
			continue;
		}

		const int32 ItemsAdded = PlayerInventory->AddStack(Stack, true);
		if (ItemsAdded < 0)
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: Failed to add stack to inventory"));
			continue;
		}

		// Handle excess items with proper validation
		if (ItemsAdded < Stack.NumItems)
		{
			FInventoryStack ExcessStack = Stack;
			ExcessStack.NumItems = Stack.NumItems - ItemsAdded;

			// Final validation before adding to excess
			if (IsValid(ExcessStack.Item.GetItemClass()))
			{
				ExcessStacks.Add(ExcessStack);
			}
			else
			{
				UE_LOG(LogBulkUpgrade, Error, TEXT("AddStacksToPlayerOrCrate: Excess stack has null ItemClass, discarding"));
			}
		}
	}

	// Spawn crates only with completely validated stacks
	if (ExcessStacks.Num() > 0)
	{
		// Final validation of all excess stacks
		TArray<FInventoryStack> ValidExcessStacks;
		ValidExcessStacks.Reserve(ExcessStacks.Num());
		
		for (const FInventoryStack& Stack : ExcessStacks)
		{
			if (IsValid(Stack.Item.GetItemClass()))
			{
				ValidExcessStacks.Add(Stack);
			}
			else
			{
				UE_LOG(LogBulkUpgrade, Error, TEXT("AddStacksToPlayerOrCrate: Excess stack has null ItemClass, not spawning crate"));
			}
		}

		if (ValidExcessStacks.Num() > 0)
		{
			AFGCrate* Crate = nullptr;
			AFGItemPickup_Spawnable::SpawnInventoryCrate(
				Player->GetWorld(),
				ValidExcessStacks,
				GetPlayerDropLocation(Player),
				TArray<AActor*>(),
				Crate,
				EFGCrateType::CT_DismantleCrate);
			if (IsValid(Crate))
			{
				UE_LOG(LogBulkUpgrade, Display, TEXT("AddStacksToPlayerOrCrate: Spawned crate with %d stacks"), ValidExcessStacks.Num());
			}
			else
			{
				UE_LOG(LogBulkUpgrade, Error, TEXT("AddStacksToPlayerOrCrate: Failed to spawn crate"));
			}
		}
		else
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("AddStacksToPlayerOrCrate: No valid excess stacks to spawn crate"));
		}
	}
	else
	{
		UE_LOG(LogBulkUpgrade, Display, TEXT("AddStacksToPlayerOrCrate: Successfully processed %d stacks"), ValidStacks.Num());
	}
}

bool PlanStorageFactoryConnectionTransfers(AFGBuildableStorage* SourceStorage, AFGBuildableStorage* TargetStorage, TArray<FFactoryConnectionTransfer>& OutTransfers, FText& OutFailure)
{
	if (!IsValid(SourceStorage) || !IsValid(TargetStorage))
	{
		OutFailure = LOCTEXT("StorageConnectionInvalidActor", "Source or replacement storage is no longer valid.");
		return false;
	}

	TArray<UFGFactoryConnectionComponent*> OldComponents;
	SourceStorage->GetComponents(OldComponents);

	TArray<UFGFactoryConnectionComponent*> NewComponents;
	TargetStorage->GetComponents(NewComponents);

	if (OldComponents.Num() == 0 || NewComponents.Num() == 0)
	{
		return true;
	}

	TSet<UFGFactoryConnectionComponent*> UsedNewConnections;
	for (UFGFactoryConnectionComponent* OldConnection : OldComponents)
	{
		if (!IsValid(OldConnection) || !OldConnection->IsConnected())
		{
			continue;
		}

		UFGFactoryConnectionComponent* ConnectedComponent = OldConnection->GetConnection();
		if (!IsValid(ConnectedComponent))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("Storage reconnect skipped stale connection %s on %s."),
				*GetNameSafe(OldConnection),
				*GetNameSafe(SourceStorage));
			continue;
		}

		const FVector OldLocation = OldConnection->GetComponentLocation();
		const EFactoryConnectionDirection OldDirection = OldConnection->GetDirection();
		UFGFactoryConnectionComponent* BestConnection = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (UFGFactoryConnectionComponent* NewConnection : NewComponents)
		{
			if (!IsValid(NewConnection) || UsedNewConnections.Contains(NewConnection) || NewConnection->IsConnected())
			{
				continue;
			}

			if (NewConnection->GetDirection() != OldDirection)
			{
				continue;
			}

			const float Distance = FVector::Distance(NewConnection->GetComponentLocation(), OldLocation);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestConnection = NewConnection;
			}
		}

		if (!IsValid(BestConnection) || BestDistance > 25.0f)
		{
			OutFailure = FText::Format(
				LOCTEXT("StorageConnectionNoMatch", "Could not match storage conveyor connection {0}; skipped replacement to avoid breaking the conveyor chain."),
				FText::FromString(GetNameSafe(OldConnection)));
			return false;
		}

		UsedNewConnections.Add(BestConnection);
		FFactoryConnectionTransfer& Transfer = OutTransfers.AddDefaulted_GetRef();
		Transfer.OldConnection = OldConnection;
		Transfer.NewConnection = BestConnection;
		Transfer.ExternalConnection = ConnectedComponent;
	}

	return true;
}

void ApplyStorageFactoryConnectionTransfers(const TArray<FFactoryConnectionTransfer>& Transfers)
{
	for (const FFactoryConnectionTransfer& Transfer : Transfers)
	{
		if (!IsValid(Transfer.OldConnection) || !IsValid(Transfer.NewConnection) || !IsValid(Transfer.ExternalConnection))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("Storage reconnect skipped invalid transfer old=%s new=%s external=%s."),
				*GetNameSafe(Transfer.OldConnection),
				*GetNameSafe(Transfer.NewConnection),
				*GetNameSafe(Transfer.ExternalConnection));
			continue;
		}

		Transfer.OldConnection->ClearConnection();
		Transfer.NewConnection->SetConnection(Transfer.ExternalConnection);
		UE_LOG(LogBulkUpgrade, Display, TEXT("Storage reconnect moved %s -> %s external=%s."),
			*GetNameSafe(Transfer.OldConnection),
			*GetNameSafe(Transfer.NewConnection),
			*GetNameSafe(Transfer.ExternalConnection));
	}
}

void ReconnectPumpPipeConnections(AFGBuildablePipelinePump* SourcePump, AFGBuildablePipelinePump* TargetPump)
{
	// Comprehensive validation
	if (!IsValid(SourcePump) || !IsValid(TargetPump))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPipeConnections: Invalid pump parameters"));
		return;
	}

	const TArray<UFGPipeConnectionComponent*> OldComponents = SourcePump->GetPipeConnections();
	const TArray<UFGPipeConnectionComponent*> NewComponents = TargetPump->GetPipeConnections();

	if (OldComponents.Num() == 0 || NewComponents.Num() == 0)
	{
		UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPipeConnections: No pipe connections to reconnect"));
		return;
	}

	const int32 NumPairs = FMath::Min(OldComponents.Num(), NewComponents.Num());
	int32 SuccessfulConnections = 0;

	for (int32 Index = 0; Index < NumPairs; ++Index)
	{
		UFGPipeConnectionComponent* OldConnection = OldComponents[Index];
		UFGPipeConnectionComponent* NewConnection = NewComponents[Index];
		
		if (!IsValid(OldConnection))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPipeConnections: Invalid old pipe connection at index %d"), Index);
			continue;
		}
		
		if (!IsValid(NewConnection))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPipeConnections: Invalid new pipe connection at index %d"), Index);
			continue;
		}

		UFGPipeConnectionComponentBase* ConnectedComponent = OldConnection->GetConnection();
		if (!IsValid(ConnectedComponent))
		{
			UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPipeConnections: Old pipe connection %d has no connected component"), Index);
			continue;
		}

		// Clear old connection safely
		OldConnection->ClearConnection();
		
		// Set new connection safely
		NewConnection->SetConnection(ConnectedComponent);
		SuccessfulConnections++;
		
		UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPipeConnections: Successfully reconnected pipe connection %d"), Index);
	}

	UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPipeConnections: Successfully reconnected %d pipe connections"), SuccessfulConnections);
}

void ReconnectPumpPowerConnections(AFGBuildablePipelinePump* SourcePump, AFGBuildablePipelinePump* TargetPump)
{
	// Comprehensive validation
	if (!IsValid(SourcePump) || !IsValid(TargetPump))
	{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPowerConnections: Invalid pump parameters"));
		return;
	}

	TArray<UFGPowerConnectionComponent*> OldPowerConnections;
	SourcePump->GetComponents(OldPowerConnections);

	TArray<UFGPowerConnectionComponent*> NewPowerConnections;
	TargetPump->GetComponents(NewPowerConnections);

	if (OldPowerConnections.Num() == 0 || NewPowerConnections.Num() == 0)
	{
		UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPowerConnections: No power connections to reconnect"));
		return;
	}

	const int32 NumPairs = FMath::Min(OldPowerConnections.Num(), NewPowerConnections.Num());
	int32 SuccessfulConnections = 0;

	for (int32 Index = 0; Index < NumPairs; ++Index)
	{
		UFGPowerConnectionComponent* OldPowerConnection = OldPowerConnections[Index];
		UFGPowerConnectionComponent* NewPowerConnection = NewPowerConnections[Index];
		
		if (!IsValid(OldPowerConnection))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPowerConnections: Invalid old power connection at index %d"), Index);
			continue;
		}
		
		if (!IsValid(NewPowerConnection))
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPowerConnections: Invalid new power connection at index %d"), Index);
			continue;
		}
		if (!OldPowerConnection || !NewPowerConnection)
		{
			continue;
		}

		TArray<AFGBuildableWire*> Wires;
		OldPowerConnection->GetWires(Wires);

		if (Wires.Num() == 0)
		{
			UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPowerConnections: No wires to reconnect for connection %d"), Index);
			continue;
		}

		for (AFGBuildableWire* Wire : Wires)
		{
			if (!IsValid(Wire))
			{
				UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPowerConnections: Invalid wire in connection %d"), Index);
				continue;
			}

			UFGCircuitConnectionComponent* OppositeConnection = Wire->GetOppositeConnection(OldPowerConnection);
			if (!IsValid(OppositeConnection))
			{
				UE_LOG(LogBulkUpgrade, Warning, TEXT("ReconnectPumpPowerConnections: Wire has invalid opposite connection"));
				continue;
			}

			// Safe wire reconnection
			Wire->Disconnect();
			
			// Validate wire is still valid after disconnect
			if (IsValid(Wire) && IsValid(NewPowerConnection) && IsValid(OppositeConnection))
			{
				Wire->Connect(NewPowerConnection, OppositeConnection);
				SuccessfulConnections++;
				UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPowerConnections: Successfully reconnected wire for connection %d"), Index);
			}
			else
			{
				UE_LOG(LogBulkUpgrade, Error, TEXT("ReconnectPumpPowerConnections: Failed to reconnect wire - components became invalid"));
			}
		}
	}

	UE_LOG(LogBulkUpgrade, Log, TEXT("ReconnectPumpPowerConnections: Successfully reconnected %d power connections"), SuccessfulConnections);
}

bool ValidateConstructedFamily(EBulkUpgradeFamily Family, AFGBuildable* SourceBuildable, AFGBuildable* TargetBuildable, FText& OutFailure)
{
	switch (Family)
	{
	case EBulkUpgradeFamily::ConveyorBelt:
		if (!TargetBuildable->IsA<AFGBuildableConveyorBelt>())
		{
			OutFailure = LOCTEXT("TargetNotConveyorBelt", "Constructed target is not a conveyor belt.");
			return false;
		}
		return true;
	case EBulkUpgradeFamily::ConveyorLift:
		if (!TargetBuildable->IsA<AFGBuildableConveyorLift>())
		{
			OutFailure = LOCTEXT("TargetNotConveyorLift", "Constructed target is not a conveyor lift.");
			return false;
		}
		return true;
	case EBulkUpgradeFamily::Pipeline:
		if (!Cast<AFGBuildablePipeline>(SourceBuildable) || !Cast<AFGBuildablePipeline>(TargetBuildable))
		{
			OutFailure = LOCTEXT("ConstructedPipelineMismatch", "Constructed actor is not a compatible pipeline buildable.");
			return false;
		}
		if (!Cast<AFGBuildablePipeline>(SourceBuildable)->GetPipeConnection0() ||
			!Cast<AFGBuildablePipeline>(SourceBuildable)->GetPipeConnection1() ||
			!Cast<AFGBuildablePipeline>(TargetBuildable)->GetPipeConnection0() ||
			!Cast<AFGBuildablePipeline>(TargetBuildable)->GetPipeConnection1())
		{
			OutFailure = LOCTEXT("ConstructedPipelineMissingConnections", "Constructed pipeline is missing endpoint connections.");
			return false;
		}
		return true;
	case EBulkUpgradeFamily::Wire:
		if (!Cast<AFGBuildableWire>(SourceBuildable) || !Cast<AFGBuildableWire>(TargetBuildable))
		{
			OutFailure = LOCTEXT("ConstructedWireMismatch", "Constructed actor is not a compatible wire buildable.");
			return false;
		}
		return true;
	case EBulkUpgradeFamily::PowerPole:
	case EBulkUpgradeFamily::PowerPoleWall:
	case EBulkUpgradeFamily::PowerPoleWallDouble:
	case EBulkUpgradeFamily::PowerTower:
		if (!Cast<AFGBuildablePowerPole>(SourceBuildable) || !Cast<AFGBuildablePowerPole>(TargetBuildable))
		{
			OutFailure = LOCTEXT("ConstructedPowerPoleMismatch", "Constructed actor is not a compatible power pole buildable.");
			return false;
		}
		return true;
	case EBulkUpgradeFamily::Unknown:
	default:
		OutFailure = LOCTEXT("UnsupportedFamilyForCommit", "Row family is not supported by the commit path.");
		return false;
	}
}

}

bool UBulkUpgradeExecutor::ConveyorSegmentBelongsToDifferentValidChain(const AFGConveyorChainActor* ExpectedChainActor, AFGBuildableConveyorBase* Conveyor)
{
	return IsValid(ExpectedChainActor) &&
		IsValid(Conveyor) &&
		IsValid(Conveyor->GetConveyorChainActor()) &&
		Conveyor->GetConveyorChainActor() != ExpectedChainActor;
}

bool UBulkUpgradeExecutor::ConveyorChainContainsConveyor(const AFGConveyorChainActor* ChainActor, const AFGBuildableConveyorBase* Conveyor)
{
	if (!IsValid(ChainActor) || !IsValid(Conveyor))
	{
		return false;
	}

	for (const FConveyorChainSplineSegment& Segment : ChainActor->mChainSplineSegments)
	{
		if (Segment.ConveyorBase == Conveyor)
		{
			return true;
		}
	}

	return false;
}

bool UBulkUpgradeExecutor::ConveyorChainNeedsStructuralRescue(AFGConveyorChainActor* ChainActor)
{
	if (!IsValid(ChainActor))
	{
		return false;
	}

	const TArray<FConveyorChainSplineSegment>& ChainSegments = ChainActor->mChainSplineSegments;
	if (ChainSegments.Num() == 0)
	{
		return true;
	}

	for (const FConveyorChainSplineSegment& Segment : ChainSegments)
	{
		if (!IsValid(Segment.ConveyorBase) ||
			ConveyorSegmentBelongsToDifferentValidChain(ChainActor, Segment.ConveyorBase))
		{
			return true;
		}
	}

	return false;
}

int32 UBulkUpgradeExecutor::SanitizeConveyorChainActor(AFGConveyorChainActor* ChainActor, const TCHAR* Context)
{
	if (!IsValid(ChainActor))
	{
		return 0;
	}

	// Skip if chain is already in good state to prevent over-repairing
	if (ChainActor->bHasValidLUT && ChainActor->mChainSplineSegments.Num() > 0)
	{
		return 0;
	}

	int32 ChangeCount = 0;
	const int32 ItemCapacity = ChainActor->mConveyorChainItems.Num();
	const bool bGlobalRangeWasEmpty = IsEmptyConveyorIndexRange(ChainActor->mLeadItemIndex, ChainActor->mTailItemIndex);
	const bool bGlobalRangeWasValid = IsValidConveyorIndexRange(ChainActor->mLeadItemIndex, ChainActor->mTailItemIndex, ItemCapacity);

	// Only fix critical issues, don't over-repair
	if (ChainActor->mItemRemovals.ChainActor != ChainActor)
	{
		ChainActor->mItemRemovals.ChainActor = ChainActor;
		ChangeCount += 1;
	}
	if (ChainActor->mItemAdditions.ChainActor != ChainActor)
	{
		ChainActor->mItemAdditions.ChainActor = ChainActor;
		ChangeCount += 1;
	}

	// Only clear invalid ranges if they're actually broken
	if (!bGlobalRangeWasValid && !bGlobalRangeWasEmpty)
	{
		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor sanitizer [%s]: clearing invalid chain item range chain=%s items=%d lead=%d tail=%d"),
			Context,
			*GetNameSafe(ChainActor),
			ItemCapacity,
			ChainActor->mLeadItemIndex,
			ChainActor->mTailItemIndex);
		ChainActor->mLeadItemIndex = INDEX_NONE;
		ChainActor->mTailItemIndex = INDEX_NONE;
		ChangeCount += 1;
	}

	if (ChangeCount > 0)
	{
		ChainActor->bHasValidLUT = false;
		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor sanitizer [%s]: repaired chain=%s changes=%d segments=%d items=%d lead=%d tail=%d"),
			Context,
			*GetNameSafe(ChainActor),
			ChangeCount,
			ChainActor->mChainSplineSegments.Num(),
			ItemCapacity,
			ChainActor->mLeadItemIndex,
			ChainActor->mTailItemIndex);
	}

	return ChangeCount;
}

int32 UBulkUpgradeExecutor::SanitizeConveyorChainActorForPostLoad(AFGConveyorChainActor* ChainActor, const TCHAR* Context, bool& bOutShouldSkipVanillaPostLoad)
{
	bOutShouldSkipVanillaPostLoad = false;
	if (!IsValid(ChainActor))
	{
		return 0;
	}

	// Prevent infinite repair loops by checking if chain was recently repaired
	if (ChainActor->bHasValidLUT)
	{
		return 0;
	}

	if (!ConveyorChainNeedsStructuralRescue(ChainActor))
	{
		return 0;
	}

	int32 ChangeCount = 0;
	int32 RemovedSegments = 0;
	const int32 OriginalSegmentCount = ChainActor->mChainSplineSegments.Num();

	TArray<FConveyorChainSplineSegment> ValidSegments;
	ValidSegments.Reserve(OriginalSegmentCount);

	const int32 SegmentCount = ChainActor->mChainSplineSegments.Num();
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		if (!ChainActor->mChainSplineSegments.IsValidIndex(SegmentIndex))
		{
			continue;
		}
		
		FConveyorChainSplineSegment Segment = ChainActor->mChainSplineSegments[SegmentIndex];
		if (IsValid(Segment.ConveyorBase) && !ConveyorSegmentBelongsToDifferentValidChain(ChainActor, Segment.ConveyorBase))
		{
			ValidSegments.Add(MoveTemp(Segment));
			continue;
		}

		RemovedSegments += 1;
		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor sanitizer [%s]: removing stale conveyor segment chain=%s segment=%d oldConveyor=%s conveyorOwner=%s first=%d last=%d"),
			Context,
			*GetNameSafe(ChainActor),
			SegmentIndex,
			*GetNameSafe(Segment.ConveyorBase),
			*GetNameSafe(IsValid(Segment.ConveyorBase) ? Segment.ConveyorBase->GetConveyorChainActor() : nullptr),
			Segment.FirstItemIndex,
			Segment.LastItemIndex);
	}

	if (RemovedSegments > 0)
	{
		ChainActor->mChainSplineSegments = MoveTemp(ValidSegments);
		ChainActor->mConveyorToSplineSegment.Empty();
		ChainActor->mClientAvailableConveyors.Empty();
		ChainActor->mClientAvailableConveyorsChanged = true;
		ChainActor->mLeadItemIndex = INDEX_NONE;
		ChainActor->mTailItemIndex = INDEX_NONE;
		for (int32 i = 0; i < ChainActor->mChainSplineSegments.Num(); ++i)
		{
			ChainActor->mChainSplineSegments[i].SetItemIndices(INDEX_NONE, INDEX_NONE);
		}
		ChainActor->bHasValidLUT = false;
		ChangeCount += RemovedSegments + 1;

		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor sanitizer [%s]: removed missing conveyors chain=%s removed=%d originalSegments=%d remainingSegments=%d itemRangesCleared=1"),
			Context,
			*GetNameSafe(ChainActor),
			RemovedSegments,
			OriginalSegmentCount,
			ChainActor->mChainSplineSegments.Num());
	}

	ChangeCount += SanitizeConveyorChainActor(ChainActor, Context);

	if (ChainActor->mChainSplineSegments.Num() == 0)
	{
		ChainActor->SetActorTickEnabled(false);
		ChainActor->SetActorHiddenInGame(true);
		ChainActor->SetActorEnableCollision(false);
		bOutShouldSkipVanillaPostLoad = true;

		UE_LOG(LogBulkUpgrade, Warning,
			TEXT("BulkUpgrade conveyor sanitizer [%s]: quarantined empty conveyor chain actor=%s"),
			Context,
			*GetNameSafe(ChainActor));
	}

	return ChangeCount;
}

bool UBulkUpgradeExecutor::CommitWithHologramPath(AFGCharacterPlayer* Player, FBulkUpgradePreviewRow& Row, FText& OutFailure)
{
	if (!IsValid(Player))
	{
		OutFailure = LOCTEXT("NoPlayer", "No valid player was provided for commit.");
		return false;
	}

	if (!Player->GetBuildGun())
	{
		OutFailure = LOCTEXT("NoBuildGun", "Player build gun is not available.");
		return false;
	}

	AFGBuildable* SourceBuildable = Cast<AFGBuildable>(Row.Actor.Get());
	if (!IsValid(SourceBuildable))
	{
		OutFailure = LOCTEXT("InvalidBuildable", "Source buildable is no longer valid.");
		return false;
	}

	if (!SourceBuildable->HasAuthority())
	{
		OutFailure = LOCTEXT("NoAuthority", "Source buildable is not on the authoritative game context.");
		return false;
	}

	if (!Row.TargetRecipe)
	{
		OutFailure = LOCTEXT("NoTargetRecipe", "Row has no target recipe.");
		return false;
	}

	if (!ValidateRowStillMatchesPlan(SourceBuildable, Row, OutFailure))
	{
		return false;
	}

	const bool bIsConveyorFamily = Row.Family == EBulkUpgradeFamily::ConveyorBelt || Row.Family == EBulkUpgradeFamily::ConveyorLift;
	AFGBuildableConveyorBase* SourceConveyor = bIsConveyorFamily ? Cast<AFGBuildableConveyorBase>(SourceBuildable) : nullptr;
	if (bIsConveyorFamily && !IsValid(SourceConveyor))
	{
		OutFailure = LOCTEXT("SourceConveyorMissing", "Source buildable is not a valid conveyor.");
		return false;
	}

	AFGBuildableSubsystem* BuildableSubsystem = AFGBuildableSubsystem::Get(SourceBuildable->GetWorld());
	if (!BuildableSubsystem)
	{
		OutFailure = LOCTEXT("NoBuildableSubsystem", "Buildable subsystem is not available.");
		return false;
	}

	AFGHologram* Hologram = AFGHologram::SpawnHologramFromRecipe(Row.TargetRecipe, Player->GetBuildGun(), SourceBuildable->GetActorLocation(), Player);
	if (!IsValid(Hologram))
	{
		OutFailure = LOCTEXT("HologramSpawnFailed", "Failed to spawn target hologram from recipe.");
		return false;
	}

	auto FailWithHologram = [&OutFailure, Hologram](const FText& Reason)
	{
		OutFailure = Reason;
		if (IsValid(Hologram))
		{
			Hologram->Destroy();
		}
		return false;
	};

	Hologram->SetInsideBlueprintDesigner(SourceBuildable->GetBlueprintDesigner());

	UPrimitiveComponent* PrimitiveComponent = Hologram->GetComponentByClass<UPrimitiveComponent>();
	FHitResult HitResult(SourceBuildable, PrimitiveComponent, SourceBuildable->GetActorLocation(), SourceBuildable->GetActorRotation().Vector());

	if (!Hologram->TryUpgrade(HitResult))
	{
		return FailWithHologram(LOCTEXT("TryUpgradeFailed", "Hologram TryUpgrade rejected the source buildable."));
	}

	Hologram->ValidatePlacementAndCost(Player->GetInventory());

	if (!Hologram->IsUpgrade())
	{
		return FailWithHologram(LOCTEXT("NotUpgrade", "Hologram did not resolve to an upgrade operation."));
	}

	int32 StepGuard = 32;
	while (Hologram->CanTakeNextBuildStep() && StepGuard > 0)
	{
		if (Hologram->DoMultiStepPlacement(true))
		{
			break;
		}
		StepGuard -= 1;
	}

	if (StepGuard <= 0)
	{
		return FailWithHologram(LOCTEXT("BuildStepGuard", "Hologram build-step guard was reached before construction."));
	}

	if (AFGPipelineHologram* PipelineHologram = Cast<AFGPipelineHologram>(Hologram))
	{
		PipelineHologram->GenerateAndUpdateSpline(HitResult);
	}
	else if (AFGConveyorBeltHologram* ConveyorBeltHologram = Cast<AFGConveyorBeltHologram>(Hologram))
	{
		ConveyorBeltHologram->GenerateAndUpdateSpline(HitResult);
	}

	Hologram->ValidatePlacementAndCost(Player->GetInventory());
	if (!Hologram->CanConstruct())
	{
		const FString DisqualifierText = DescribeConstructDisqualifiers(Hologram);
		if (bIsConveyorFamily && HasOnlyProgrammaticUpgradeDisqualifiers(Hologram))
		{
			UE_LOG(LogBulkUpgrade, Display, TEXT("BulkUpgrade conveyor/lift hologram has programmatic-only disqualifiers for %s -> %s; continuing with subsystem bucket rebuild. disqualifiers=[%s]"),
				*GetNameSafe(SourceBuildable),
				*GetNameSafe(Row.TargetRecipe.Get()),
				*DisqualifierText);
		}
		else
		{
		UE_LOG(LogBulkUpgrade, Warning, TEXT("BulkUpgrade hologram CanConstruct rejected %s -> %s; disqualifiers=[%s]; skipping unsafe forced construction."),
			*GetNameSafe(SourceBuildable),
			*GetNameSafe(Row.TargetRecipe.Get()),
			*DisqualifierText);
		return FailWithHologram(LOCTEXT("HologramCanConstructRejected", "Target hologram rejected construction; skipped to avoid unsafe forced replacement."));
		}
	}

	TArray<AActor*> Children;
	AFGBuildable* TargetBuildable = Cast<AFGBuildable>(Hologram->Construct(Children, BuildableSubsystem->GetNewNetConstructionID()));
	Hologram->Destroy();

	if (!IsValid(TargetBuildable))
	{
		DestroyConstructedActors(Children, TargetBuildable);
		OutFailure = LOCTEXT("ConstructFailed", "Hologram construction did not return a valid buildable.");
		return false;
	}

	FText FamilyFailure;
	if (!ValidateConstructedFamily(Row.Family, SourceBuildable, TargetBuildable, FamilyFailure))
	{
		DestroyConstructedActors(Children, TargetBuildable);
		OutFailure = FamilyFailure;
		return false;
	}

	SourceBuildable->PreUpgrade_Implementation();
	SourceBuildable->Upgrade_Implementation(TargetBuildable);

	bool bReconnected = true;
	if (bIsConveyorFamily)
	{
		AFGBuildableConveyorBase* TargetConveyor = Cast<AFGBuildableConveyorBase>(TargetBuildable);
		bReconnected = RebuildConveyorThroughSubsystem(BuildableSubsystem, SourceConveyor, TargetConveyor);
	}
	else if (Row.Family == EBulkUpgradeFamily::Pipeline)
	{
		bReconnected = ReconnectPipeline(Cast<AFGBuildablePipeline>(SourceBuildable), Cast<AFGBuildablePipeline>(TargetBuildable));
	}

	if (!bReconnected)
	{
		DestroyConstructedActors(Children, TargetBuildable);
		OutFailure = LOCTEXT("ReconnectFailed", "Upgrade constructed, but endpoint reconnection did not validate.");
		return false;
	}

	DestroyChildDismantleActors(SourceBuildable);
	SourceBuildable->Destroy();
	return true;
}

bool UBulkUpgradeExecutor::CommitWithManualSpawnPath(AFGCharacterPlayer* Player, FBulkUpgradePreviewRow& Row, FText& OutFailure)
{
	if (!IsValid(Player))
	{
		OutFailure = LOCTEXT("NoPlayer", "No valid player was provided for commit.");
		return false;
	}

	AFGBuildable* SourceBuildable = Cast<AFGBuildable>(Row.Actor.Get());
	if (!IsValid(SourceBuildable))
	{
		OutFailure = LOCTEXT("InvalidBuildable", "Source buildable is no longer valid.");
		return false;
	}

	if (!SourceBuildable->HasAuthority())
	{
		OutFailure = LOCTEXT("NoAuthority", "Source buildable is not on the authoritative game context.");
		return false;
	}

	if (!Row.TargetRecipe)
	{
		OutFailure = LOCTEXT("NoTargetRecipe", "Row has no target recipe.");
		return false;
	}

	if (!ValidateRowStillMatchesPlan(SourceBuildable, Row, OutFailure))
	{
		return false;
	}

	UWorld* World = SourceBuildable->GetWorld();
	if (!World)
	{
		OutFailure = LOCTEXT("NoWorld", "Source buildable world is unavailable.");
		return false;
	}

	TSubclassOf<AFGBuildable> TargetBuildableClass = ExecutorGetBuildableClassFromRecipeSafe(Row.TargetRecipe);
	if (!TargetBuildableClass)
	{
		OutFailure = LOCTEXT("TargetClassMissing", "Target recipe no longer resolves to a buildable class.");
		return false;
	}

	if (Row.Family == EBulkUpgradeFamily::Storage)
	{
		AFGBuildableStorage* SourceStorage = Cast<AFGBuildableStorage>(SourceBuildable);
		if (!SourceStorage || !TargetBuildableClass->IsChildOf(AFGBuildableStorage::StaticClass()))
		{
			OutFailure = LOCTEXT("StorageClassMismatch", "Source or target is not a compatible storage buildable.");
			return false;
		}

		UFGInventoryComponent* SourceInventory = SourceStorage->GetStorageInventory();
		if (!SourceInventory)
		{
			OutFailure = LOCTEXT("StorageSourceInventoryMissing", "Source storage inventory is unavailable.");
			return false;
		}

		const FTransform Transform = SourceStorage->GetActorTransform();
		AFGBuildableStorage* TargetStorage = World->SpawnActorDeferred<AFGBuildableStorage>(
			TargetBuildableClass.Get(),
			Transform,
			nullptr,
			Player,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			ESpawnActorScaleMethod::MultiplyWithRoot);
		if (!IsValid(TargetStorage))
		{
			OutFailure = LOCTEXT("StorageSpawnFailed", "Failed to spawn replacement storage.");
			return false;
		}

		AttachToBlueprintDesigner(SourceStorage, TargetStorage);
		UGameplayStatics::FinishSpawningActor(TargetStorage, Transform);
		TargetStorage->SetBuiltWithRecipe(Row.TargetRecipe);

		UFGInventoryComponent* TargetInventory = TargetStorage->GetStorageInventory();
		if (!TargetInventory)
		{
			TargetStorage->Destroy();
			OutFailure = LOCTEXT("StorageTargetInventoryMissing", "Replacement storage inventory is unavailable.");
			return false;
		}

		TArray<FFactoryConnectionTransfer> ConnectionTransfers;
		if (!PlanStorageFactoryConnectionTransfers(SourceStorage, TargetStorage, ConnectionTransfers, OutFailure))
		{
			TargetStorage->Destroy();
			return false;
		}

		TArray<FInventoryStack> ExcessStacks;
		TArray<FInventoryStack> SourceStacks;
		SourceStacks.Reserve(SourceInventory->GetSizeLinear());
		const int32 InventorySize = SourceInventory->GetSizeLinear();

		for (int32 Index = 0; Index < InventorySize; ++Index)
		{
			FInventoryStack Stack;
			if (!SourceInventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems() || !Stack.Item.GetItemClass())
			{
				continue;
			}

			SourceStacks.Add(Stack);
		}

		UE_LOG(LogBulkUpgrade, Log, TEXT("CommitWithManualSpawnPath: Successfully extracted %d valid stacks from %d inventory slots"), 
			SourceStacks.Num(), InventorySize);

		ApplyStorageFactoryConnectionTransfers(ConnectionTransfers);

		for (const FInventoryStack& Stack : SourceStacks)
		{
			if (!Stack.Item.GetItemClass())
			{
				continue;
			}

			const int32 ItemsAdded = TargetInventory->AddStack(Stack, true);
			const int32 ClampedItemsAdded = FMath::Clamp(ItemsAdded, 0, Stack.NumItems);
			if (ClampedItemsAdded < Stack.NumItems)
			{
				FInventoryStack ExcessStack = Stack;
				ExcessStack.NumItems = Stack.NumItems - ClampedItemsAdded;
				if (ExcessStack.Item.GetItemClass())
				{
					ExcessStacks.Add(ExcessStack);
				}
			}
		}

		AddStacksToPlayerOrCrate(Player, ExcessStacks);

		SourceStorage->SetActorHiddenInGame(true);
		TargetStorage->PlayBuildEffects(Player);
		TargetStorage->ExecutePlayBuildEffects();
		DestroyChildDismantleActors(SourceStorage);
		SourceStorage->Destroy();
		return true;
	}

	if (Row.Family == EBulkUpgradeFamily::PipelinePump)
	{
		AFGBuildablePipelinePump* SourcePump = Cast<AFGBuildablePipelinePump>(SourceBuildable);
		if (!SourcePump || !TargetBuildableClass->IsChildOf(AFGBuildablePipelinePump::StaticClass()))
		{
			OutFailure = LOCTEXT("PumpClassMismatch", "Source or target is not a compatible pipeline pump buildable.");
			return false;
		}

		const TArray<UFGPipeConnectionComponent*> SourcePipeConnections = SourcePump->GetPipeConnections();
		if (SourcePipeConnections.Num() < 2)
		{
			OutFailure = LOCTEXT("PumpSourcePipeConnectionsMissing", "Source pump is missing pipe connections.");
			return false;
		}

		const FTransform Transform = SourcePump->GetActorTransform();
		AFGBuildablePipelinePump* TargetPump = World->SpawnActorDeferred<AFGBuildablePipelinePump>(
			TargetBuildableClass.Get(),
			Transform,
			nullptr,
			Player,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			ESpawnActorScaleMethod::MultiplyWithRoot);
		if (!IsValid(TargetPump))
		{
			OutFailure = LOCTEXT("PumpSpawnFailed", "Failed to spawn replacement pipeline pump.");
			return false;
		}

		AttachToBlueprintDesigner(SourcePump, TargetPump);
		UGameplayStatics::FinishSpawningActor(TargetPump, Transform);
		TargetPump->SetBuiltWithRecipe(Row.TargetRecipe);

		if (TargetPump->GetPipeConnections().Num() < 2)
		{
			TargetPump->Destroy();
			OutFailure = LOCTEXT("PumpTargetPipeConnectionsMissing", "Replacement pump is missing pipe connections.");
			return false;
		}

		ReconnectPumpPipeConnections(SourcePump, TargetPump);
		TargetPump->SetActorHiddenInGame(true);
		TargetPump->PlayBuildEffects(Player);
		TargetPump->ExecutePlayBuildEffects();
		TargetPump->SetActorHiddenInGame(false);
		ReconnectPumpPowerConnections(SourcePump, TargetPump);
		DestroyChildDismantleActors(SourcePump);
		SourcePump->TurnOffAndDestroy();
		TargetPump->TryStartProductionLoopEffects(true);
		return true;
	}

	OutFailure = LOCTEXT("UnsupportedManualFamily", "Row family is not supported by the manual spawn path.");
	return false;
}

FBulkUpgradeCommitReport UBulkUpgradeExecutor::CommitPlan(UObject* WorldContextObject, AFGCharacterPlayer* Player, FBulkUpgradePlan& Plan, const FBulkUpgradeCommitOptions& Options)
{
	FBulkUpgradeCommitReport Report;
	Report.PlannedCount = Plan.Rows.Num();

	if (!WorldContextObject)
	{
		Report.Messages.Add(LOCTEXT("NoWorldContext", "No world context was provided."));
	}

	if (Options.ExecutionProfile == EBulkUpgradeExecutionProfile::LocalOnlyDev)
	{
		Report.Messages.Add(LOCTEXT("LocalValidationProfile", "Local validation profile is active; mutation is guarded while the full MassUpgrade port is being verified."));
		if (!Options.bAcknowledgeLocalSaveBackup)
		{
			Report.Messages.Add(LOCTEXT("BackupNotAcknowledged", "Local save backup has not been acknowledged."));
		}
	}
	else
	{
		Report.Messages.Add(LOCTEXT("ReleaseCandidateProfile", "Release-candidate profile is active; keep mutation disabled until multiplayer and packaging behavior are explicitly verified."));
	}

	if (Plan.Request.CostPolicy != EBulkUpgradeCostPolicy::FreeLocalDev)
	{
		Report.Messages.Add(LOCTEXT("FreeLocalDevCostRequired", "The current commit path only supports the free local-dev cost policy."));
		Report.SkippedCount = Plan.EligibleCount;
		return Report;
	}

	if (!CanAttemptMutation(Options, Report))
	{
		Report.SkippedCount = Plan.EligibleCount;
		return Report;
	}

	Report.Messages.Add(LOCTEXT("MassUpgradeUpgradePath", "Using MassUpgrade-derived local upgrade path for this commit."));

	const int32 MaxAttempts = FMath::Clamp(Options.MaxOperationsPerCall, 1, 1000);
	
	// Performance optimization: Pre-allocate arrays for large operations
	TArray<FBulkUpgradePreviewRow*> EligibleRows;
	EligibleRows.Reserve(Plan.Rows.Num());
	
	// Batch filter eligible rows to avoid repeated checks
	for (FBulkUpgradePreviewRow& Row : Plan.Rows)
	{
		if (Row.IsEligible())
		{
			EligibleRows.Add(&Row);
		}
		else
		{
			Report.SkippedCount += 1;
		}
	}
	
	// Process eligible rows with optimized validation
	for (FBulkUpgradePreviewRow* RowPtr : EligibleRows)
	{
		if (Report.AttemptedCount >= MaxAttempts)
		{
			Report.Messages.Add(LOCTEXT("AttemptCapReached", "Commit attempt cap reached; remaining eligible rows were left untouched."));
			break;
		}

		Report.AttemptedCount += 1;

		AActor* Actor = RowPtr->Actor.Get();
		if (!IsValid(Actor))
		{
			MarkFailed(*RowPtr, LOCTEXT("ActorInvalid", "Actor is no longer valid."));
			Report.FailedCount += 1;
			continue;
		}

		if (!Actor->HasAuthority())
		{
			MarkFailed(*RowPtr, LOCTEXT("NoAuthority", "Actor is not on the authoritative server context."));
			Report.FailedCount += 1;
			continue;
		}

		FText FailureReason;
		const bool bCommitted = IsManualSpawnFamily(RowPtr->Family)
			? UBulkUpgradeExecutor::CommitWithManualSpawnPath(Player, *RowPtr, FailureReason)
			: UBulkUpgradeExecutor::CommitWithHologramPath(Player, *RowPtr, FailureReason);

		if (bCommitted)
		{
			MarkCommitted(*RowPtr);
			Report.CommittedCount += 1;
			Report.bCommittedWorldChanges = true;
		}
		else
		{
			MarkFailed(*RowPtr, FailureReason);
			Report.FailedCount += 1;
		}
	}

	Report.SkippedCount = FMath::Max(0, Report.PlannedCount - Report.AttemptedCount);
	RecountCommitPlan(Plan);
	return Report;
}

#undef LOCTEXT_NAMESPACE
