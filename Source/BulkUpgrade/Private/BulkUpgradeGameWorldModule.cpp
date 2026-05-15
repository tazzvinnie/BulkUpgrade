#include "BulkUpgradeGameWorldModule.h"

#include "BulkUpgrade.h"
#include "BulkUpgradeContent.h"
#include "BulkUpgradeEquipment.h"
#include "Command/CommandSender.h"
#include "Engine/World.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGInventoryComponentEquipment.h"
#include "FGPlayerController.h"
#include "FGSchematicManager.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "BulkUpgradeGameWorldModule"

namespace
{
	bool TryAddToolStackToInventory(UFGInventoryComponent* Inventory, TSubclassOf<UFGItemDescriptor> ToolDescriptor, const TCHAR* TargetName)
	{
		if (!Inventory)
		{
			UE_LOG(LogBulkUpgrade, Warning, TEXT("TryAddToolStackToInventory: Inventory is null for target=%s"), TargetName);
			return false;
		}

		if (!ToolDescriptor)
		{
			UE_LOG(LogBulkUpgrade, Error, TEXT("TryAddToolStackToInventory: ToolDescriptor is null for target=%s"), TargetName);
			return false;
		}

		const int32 BeforeCount = Inventory->GetNumItems(ToolDescriptor);
		const FInventoryStack ToolStack(1, ToolDescriptor);
		const bool bItemAllowed = Inventory->IsItemAllowed(ToolDescriptor);
		const bool bHasSpace = Inventory->HasEnoughSpaceForStack(ToolStack);
		const int32 Added = Inventory->AddStack(ToolStack, true);
		const int32 AfterCount = Inventory->GetNumItems(ToolDescriptor);

		UE_LOG(
			LogBulkUpgrade,
			Display,
			TEXT("GiveTool target=%s itemAllowed=%s hasSpace=%s before=%d addStackReturn=%d after=%d inventory=%s"),
			TargetName,
			bItemAllowed ? TEXT("true") : TEXT("false"),
			bHasSpace ? TEXT("true") : TEXT("false"),
			BeforeCount,
			Added,
			AfterCount,
			*GetNameSafe(Inventory));

		return Added > 0 || AfterCount > BeforeCount || AfterCount > 0;
	}
}

UBulkUpgradeGameWorldModule::UBulkUpgradeGameWorldModule()
{
	bRootModule = true;
	mSchematics.Add(UBulkUpgradeToolSchematic::StaticClass());
	mChatCommands.Add(ABulkUpgradeGiveToolCommand::StaticClass());
}

void UBulkUpgradeGameWorldModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	// Content is registered by UGameWorldModule during INITIALIZATION via mSchematics.
	// The player-facing path is research/milestone unlock -> Equipment Workshop craft -> normal inventory -> hand slot.
}

bool UBulkUpgradeGameWorldModule::GiveToolToPlayerController(AFGPlayerController* Controller, FText& OutMessage)
{
	if (!IsValid(Controller))
	{
		OutMessage = LOCTEXT("MissingController", "BulkUpgrade: no player controller is available.");
		return false;
	}

	AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(Controller->GetControlledCharacter());
	if (!IsValid(Character))
	{
		Character = Cast<AFGCharacterPlayer>(Controller->GetPawn());
	}
	if (!IsValid(Character) || !Character->GetInventory())
	{
		OutMessage = LOCTEXT("MissingCharacter", "BulkUpgrade: no player character inventory is available yet.");
		return false;
	}

	TSubclassOf<UFGItemDescriptor> ToolDescriptor = UBulkUpgradeEquipmentDescriptor::StaticClass();
	const bool bHasInInventory = Character->GetInventory()->GetNumItems(ToolDescriptor) > 0;
	UFGInventoryComponentEquipment* ArmSlot = Character->GetEquipmentSlot(EEquipmentSlot::ES_ARMS);
	const bool bHasInArmSlot = ArmSlot && ArmSlot->GetNumItems(ToolDescriptor) > 0;

	if (bHasInInventory || bHasInArmSlot)
	{
		OutMessage = LOCTEXT("AlreadyHasTool", "BulkUpgrade: tool is already in your inventory or arm equipment slot.");
		return true;
	}

	UE_LOG(
		LogBulkUpgrade,
		Display,
		TEXT("GiveTool requested controller=%s character=%s inventory=%s armSlot=%s"),
		*GetNameSafe(Controller),
		*GetNameSafe(Character),
		*GetNameSafe(Character->GetInventory()),
		*GetNameSafe(ArmSlot));

	if (TryAddToolStackToInventory(Character->GetInventory(), ToolDescriptor, TEXT("inventory")))
	{
		OutMessage = LOCTEXT("ToolGiven", "BulkUpgrade: tool added to normal inventory. Move it to a hand slot to equip it.");
		return true;
	}

	if (TryAddToolStackToInventory(ArmSlot, ToolDescriptor, TEXT("arms")))
	{
		const int32 SlotIndex = ArmSlot->FindFirstIndexWithItemType(ToolDescriptor);
		if (SlotIndex >= 0)
		{
			ArmSlot->SetActiveEquipmentIndex(SlotIndex);
		}
		OutMessage = LOCTEXT("ToolAddedToArms", "BulkUpgrade: tool added to your hand equipment slot.");
		return true;
	}

	OutMessage = LOCTEXT("ToolGiveFailed", "BulkUpgrade: normal inventory and hand equipment slot had no room for the tool.");
	return false;
}

bool UBulkUpgradeGameWorldModule::UnlockToolForPlayerController(AFGPlayerController* Controller, FText& OutMessage)
{
	if (!IsValid(Controller))
	{
		OutMessage = LOCTEXT("UnlockMissingController", "BulkUpgrade: no player controller is available.");
		return false;
	}

	UWorld* World = Controller->GetWorld();
	AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(World);
	if (!SchematicManager)
	{
		OutMessage = LOCTEXT("UnlockMissingSchematicManager", "BulkUpgrade: schematic manager is not available yet.");
		return false;
	}

	AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(Controller->GetControlledCharacter());
	if (!IsValid(Character))
	{
		Character = Cast<AFGCharacterPlayer>(Controller->GetPawn());
	}

	TSubclassOf<UFGSchematic> ToolSchematic = UBulkUpgradeToolSchematic::StaticClass();
	if (SchematicManager->IsSchematicPurchased(ToolSchematic, Controller))
	{
		OutMessage = LOCTEXT("UnlockAlreadyPurchased", "BulkUpgrade: tool schematic is already unlocked.");
		return true;
	}

	SchematicManager->GiveAccessToSchematic(ToolSchematic, Character, ESchematicUnlockFlags::Force);
	OutMessage = LOCTEXT("UnlockGranted", "BulkUpgrade: tool schematic unlocked. Craft it in the Equipment Workshop.");
	return true;
}

void UBulkUpgradeGameWorldModule::GiveToolToExistingPlayers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		FText Message;
		GiveToolToPlayerController(Cast<AFGPlayerController>(It->Get()), Message);
	}
}

void UBulkUpgradeGameWorldModule::ScheduleGiveToolRetry(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateUObject(this, &UBulkUpgradeGameWorldModule::GiveToolToExistingPlayers),
		DelaySeconds,
		false);
}

ABulkUpgradeGiveToolCommand::ABulkUpgradeGiveToolCommand()
{
	bOnlyUsableByPlayer = true;
	MinNumberOfArguments = 0;
	CommandName = TEXT("bulkupgrade");
	Aliases.Add(TEXT("bu"));
	Usage = LOCTEXT("GiveToolUsage", "/bulkupgrade unlock - Dev fallback: unlock the Bulk Upgrade Tool recipe. /bulkupgrade give - Dev fallback: add/equip the tool.");
}

EExecutionStatus ABulkUpgradeGiveToolCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	if (Arguments.Num() == 0)
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::COMPLETED;
	}

	const FString& Action = Arguments[0];
	FText Message;
	bool bSucceeded = false;
	if (Action.Equals(TEXT("unlock"), ESearchCase::IgnoreCase))
	{
		bSucceeded = UBulkUpgradeGameWorldModule::UnlockToolForPlayerController(Sender ? Sender->GetPlayer() : nullptr, Message);
	}
	else if (Action.Equals(TEXT("give"), ESearchCase::IgnoreCase))
	{
		bSucceeded = UBulkUpgradeGameWorldModule::GiveToolToPlayerController(Sender ? Sender->GetPlayer() : nullptr, Message);
	}
	else
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	if (Sender)
	{
		Sender->SendChatMessage(Message.ToString(), bSucceeded ? FLinearColor::Green : FLinearColor::Red);
	}
	return bSucceeded ? EExecutionStatus::COMPLETED : EExecutionStatus::UNCOMPLETED;
}

#undef LOCTEXT_NAMESPACE
