# Bulk Upgrade

Bulk Upgrade is a GPLv3 fork/port of the original MassUpgrade mod for Satisfactory, rebuilt for current Satisfactory/SML APIs.

Equip the Bulk Upgrade Tool, aim at a supported logistics buildable, and open a MassUpgrade-style upgrade window. From there you can select target tiers and upgrade connected runs instead of rebuilding long factory lines by hand.

## Features

- Conveyor belt upgrades
- Conveyor lift upgrades
- Pipeline upgrades
- Pipeline pump upgrades
- Connected-line upgrades through splitters, mergers, and pipe junctions
- Craftable/equippable hand tool
- MassUpgrade-style popup with target selection, cost preview, build type list, refund view, and upgrade action
- Reused MassUpgrade visual lineage and assets where they are safe on current APIs

## Current Release Status

Version `3.0.1` is marked beta and targets Satisfactory 1.2 / SML 3.12.0.

Single-player testing on large local factories has been stable after the current conveyor-chain rewrite. Multiplayer and dedicated-server behavior are not claimed yet. Keep save backups before bulk-editing important worlds.

The Windows release package includes both Steam and Epic/EGS binaries.

Version `3.0.1` removes a second manual conveyor bucket rebuild that could cause a delayed conveyor item-subsystem crash after large belt upgrades.
It also limits each upgrade click to 100 buildables; repeat the operation for larger connected lines.

## In Game

1. Unlock/research the Bulk Upgrade Tool.
2. Craft it in the Equipment Workshop.
3. Equip it in a hand slot.
4. Aim at a belt, lift, pipe, or pump.
5. Click to open the upgrade window.
6. Choose the target tier and press Upgrade.

## Source Layout

- `BulkUpgrade.uplugin` - Satisfactory mod plugin descriptor.
- `Source/BulkUpgrade` - C++ runtime module.
- `Content` - packaged assets used by the tool and icons.
- `Config` - SML/access-transformer configuration.

## Attribution

Bulk Upgrade is derived from MassUpgrade by MarcioHuser.

The reference implementation and original product direction are MassUpgrade's GPLv3 lineage. This fork keeps attribution and continues the goal of a working MassUpgrade-style logistics upgrade tool on current Satisfactory builds.

## License

Bulk Upgrade is distributed under GPLv3. See `LICENSE`.
