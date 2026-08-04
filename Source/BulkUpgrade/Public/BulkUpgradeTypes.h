#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildable.h"
#include "FGRecipe.h"
#include "GameFramework/Actor.h"
#include "ItemAmount.h"
#include "Resources/FGBuildDescriptor.h"
#include "BulkUpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class EBulkUpgradeFamily : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	ConveyorBelt UMETA(DisplayName = "Conveyor Belt"),
	ConveyorLift UMETA(DisplayName = "Conveyor Lift"),
	Storage UMETA(DisplayName = "Storage Container"),
	Pipeline UMETA(DisplayName = "Pipeline"),
	PipelinePump UMETA(DisplayName = "Pipeline Pump"),
	Wire UMETA(DisplayName = "Wire"),
	PowerPole UMETA(DisplayName = "Power Pole"),
	PowerPoleWall UMETA(DisplayName = "Power Pole Wall"),
	PowerPoleWallDouble UMETA(DisplayName = "Power Pole Wall Double"),
	PowerTower UMETA(DisplayName = "Power Tower")
};

UENUM(BlueprintType)
enum class EBulkUpgradeScope : uint8
{
	Single UMETA(DisplayName = "Single"),
	ConnectedRun UMETA(DisplayName = "Connected Run"),
	ConnectedNetwork UMETA(DisplayName = "Connected Network"),
	Radius UMETA(DisplayName = "Radius")
};

UENUM(BlueprintType)
enum class EBulkUpgradeRowStatus : uint8
{
	Eligible UMETA(DisplayName = "Eligible"),
	Skipped UMETA(DisplayName = "Skipped"),
	Failed UMETA(DisplayName = "Failed"),
	Committed UMETA(DisplayName = "Committed")
};

UENUM(BlueprintType)
enum class EBulkUpgradeCostPolicy : uint8
{
	FreeLocalDev UMETA(DisplayName = "Free Local Dev"),
	EstimateOnly UMETA(DisplayName = "Estimate Only"),
	VerifiedVanillaCost UMETA(DisplayName = "Verified Vanilla Cost")
};

UENUM(BlueprintType)
enum class EBulkUpgradeExecutionProfile : uint8
{
	LocalOnlyDev UMETA(DisplayName = "Local Only Dev"),
	ReleaseCandidate UMETA(DisplayName = "Release Candidate")
};

UENUM(BlueprintType)
enum class EBulkUpgradeProductionInfoIntent : uint8
{
	UpdateWidget UMETA(DisplayName = "Update Widget"),
	Upgrade UMETA(DisplayName = "Upgrade")
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	EBulkUpgradeScope Scope = EBulkUpgradeScope::Single;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	TSubclassOf<UFGRecipe> TargetRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetConveyorBeltRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetConveyorLiftRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetStorageRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPipelineRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPipelinePumpRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetWireRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPowerPoleRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPowerPoleWallRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPowerPoleWallDoubleRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|MassUpgrade")
	TSubclassOf<UFGRecipe> TargetPowerTowerRecipe = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxOperations = 10;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	EBulkUpgradeExecutionProfile ExecutionProfile = EBulkUpgradeExecutionProfile::ReleaseCandidate;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	EBulkUpgradeCostPolicy CostPolicy = EBulkUpgradeCostPolicy::VerifiedVanillaCost;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowConnectedScope = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowConveyorBelts = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowConveyorLifts = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPipelines = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowStorage = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPipelinePumps = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPower = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowWire = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPowerPole = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPowerPoleWall = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPowerPoleWallDouble = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowPowerTower = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bEstimateCosts = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	TArray<TSubclassOf<UFGBuildDescriptor>> ExcludedBuildDescriptors;
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradePreviewRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FName ActorName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	EBulkUpgradeRowStatus Status = EBulkUpgradeRowStatus::Skipped;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FText StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TSubclassOf<UFGRecipe> SourceRecipe = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TSubclassOf<UFGRecipe> TargetRecipe = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TSubclassOf<AFGBuildable> SourceBuildableClass = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TSubclassOf<AFGBuildable> TargetBuildableClass = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FName SourceClassName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FName TargetClassName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 CostMultiplier = 1;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TArray<FItemAmount> EstimatedGrossCost;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FText CostNote;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 PreconditionHash = 0;

	bool IsEligible() const
	{
		return Status == EBulkUpgradeRowStatus::Eligible;
	}
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeCollectionOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludeBelts = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludeLifts = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludeStorages = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePipelines = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePipelinePumps = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludeWires = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePowerPoles = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePowerPoleWalls = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePowerPoleWallDoubles = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bIncludePowerTowers = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	bool bCrossAttachments = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 MaxVisited = 500;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade|Collection")
	TArray<TSubclassOf<UFGBuildDescriptor>> ExcludedBuildDescriptors;
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeProductionGroup
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	TSubclassOf<UFGBuildDescriptor> BuildDescriptor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	TSubclassOf<AFGBuildable> BuildableClass = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	FName BuildableClassName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	TArray<AFGBuildable*> Buildables;
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeProductionInfoSet
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	TArray<FBulkUpgradeProductionGroup> Groups;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	TArray<FText> Warnings;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	int32 TotalBuildables = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade|Collection")
	int32 VisitedBuildables = 0;
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradePlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FBulkUpgradeRequest Request;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	FString GeneratedAtIsoUtc;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TArray<FBulkUpgradePreviewRow> Rows;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TArray<FText> Warnings;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TArray<FItemAmount> TotalEstimatedGrossCost;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 EligibleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 SkippedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 FailedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 CommittedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	bool bCostIsFinal = false;

	bool HasEligibleRows() const
	{
		return EligibleCount > 0;
	}
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeCommitOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bDryRun = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	EBulkUpgradeExecutionProfile ExecutionProfile = EBulkUpgradeExecutionProfile::ReleaseCandidate;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAcknowledgeLocalSaveBackup = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade")
	bool bAllowUnverifiedWorldMutation = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BulkUpgrade", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxOperationsPerCall = 25;
};

USTRUCT(BlueprintType)
struct BULKUPGRADE_API FBulkUpgradeCommitReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 PlannedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 AttemptedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 CommittedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 SkippedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	int32 FailedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	bool bCommittedWorldChanges = false;

	UPROPERTY(BlueprintReadOnly, Category = "BulkUpgrade")
	TArray<FText> Messages;
};
