#pragma once

#include "Adapters/BulkUpgradeAdapter.h"

class FConveyorBeltUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FConveyorLiftUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FPipelineUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FStorageUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FPipelinePumpUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FWireUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;
};

class FPowerPoleFamilyUpgradeAdapter final : public IBulkUpgradeAdapter
{
public:
	explicit FPowerPoleFamilyUpgradeAdapter(EBulkUpgradeFamily InFamily);

	virtual EBulkUpgradeFamily GetFamily() const override;
	virtual bool CanClassify(const AActor* Actor) const override;
	virtual FBulkUpgradePreviewRow BuildPreviewRow(AActor* Actor, const FBulkUpgradeRequest& Request) const override;
	virtual void AppendConnectedActors(AActor* Actor, TArray<AActor*>& OutActors) const override;

private:
	EBulkUpgradeFamily Family = EBulkUpgradeFamily::Unknown;
};

const TArray<const IBulkUpgradeAdapter*>& GetBulkUpgradeAdapters();
