# Bulk Upgrade 1.0.0

## Short Description

Mass-upgrade belts, lifts, pipes, and pumps with an equipable MassUpgrade-style tool.

## Description

Bulk Upgrade is a GPLv3 fork/port of the original MassUpgrade mod, rebuilt for the current Satisfactory/SML APIs.

Equip the Bulk Upgrade Tool, aim at a supported logistics buildable, and open a MassUpgrade-style upgrade window. From there you can select target tiers and upgrade connected runs instead of rebuilding long factory lines by hand.

Current focus is logistics infrastructure:

- Conveyor belts
- Conveyor lifts
- Pipelines
- Pipeline pumps
- Connected-line upgrades through splitters/mergers and pipe junctions
- MassUpgrade-style popup with target selection, cost preview, build type list, refund view, and upgrade action
- Craftable/unlockable hand tool with reused MassUpgrade visual lineage

This first public build is marked beta because multiplayer and dedicated-server behavior have not been fully claimed yet. Single-player testing on large local factories has been stable after the current conveyor-chain rewrite, but please keep save backups before bulk-editing important worlds.

## How To Use

1. Unlock/research the Bulk Upgrade Tool.
2. Craft it in the Equipment Workshop.
3. Equip it in a hand slot.
4. Aim at a belt, lift, pipe, or pump.
5. Click to open the upgrade window.
6. Choose the target tier and press Upgrade.

## Known Limitations

- Single-player is the tested path for this release.
- Multiplayer/dedicated-server support is not claimed yet.
- Storage/container upgrades are not the main release focus.
- This is a fork/port, so some original MassUpgrade UI and behavior parity work may continue in later versions.

## Attribution / License

Bulk Upgrade is derived from MassUpgrade by MarcioHuser and is distributed under GPLv3. The GPLv3 license text is included with the project.

## Changelog

### 1.0.0

- Ported MassUpgrade-style logistics upgrading to current Satisfactory/SML APIs.
- Added craftable/equippable Bulk Upgrade Tool.
- Added upgrade popup for selecting target tiers and connected upgrade scope.
- Reworked belt/lift replacement to avoid unsafe direct conveyor-chain state transfer.
- Added pipe and pump upgrade support.
- Fixed the hand-tool placement by matching the base-game Object Scanner equipment transform.
- Packaged for Satisfactory CL 463028 / SML 3.11.x local testing.
