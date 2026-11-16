# Elemental Fusion Casting

An experimental SKSE plugin for Skyrim AE that combines the three vanilla Destruction concentration beams when the player presses both hand controls together.

## Supported combinations

- Flames + Frostbite
- Flames + Sparks
- Frostbite + Sparks

Same-spell dual casting, other spells, and gamepad input are passed through unchanged.

## What it does

The plugin creates three runtime-only duplicate spells. Each duplicate contains the effects from both source spells, with the magnitude of each source effect scaled to 50%. The original spell records are not edited.

When two different supported beams are equipped and the player presses both hand attack controls within 0.18 seconds, the plugin temporarily selects the matching combined spell in both hand slots and sends the two attack-down events to Skyrim's original input handler in the same frame. Skyrim then handles the cast and dual-cast animation. Once both physical controls are released, the plugin sends a balanced pair of releases and restores the original spell selections.

A single supported beam pressed alone is delayed briefly while the plugin waits to see whether the other hand is pressed. If it is not, the original single-hand input is replayed. This is an early test build; input timing and casting behavior need in-game validation.

## Requirements

- Skyrim AE runtime 1.7.104.0.8
- SKSE for that runtime
- Address Library for SKSE Plugins
- Windows x64

No ESP/ESL or Papyrus scripts are included.

## Install

1. Download the ElementalFusionCasting-test GitHub Actions artifact.
2. In Vortex, choose Install From File and select the ZIP.
3. Enable the mod and launch Skyrim through SKSE.

The archive contains Data/SKSE/Plugins/ElementalFusionCasting.dll.

## Test procedure

1. Use a disposable save and make sure the Destruction Dual Casting perk is unlocked.
2. Equip Flames in one hand and Frostbite in the other.
3. Press and hold both hand attack buttons together.
4. Check whether Skyrim starts one normal dual-cast beam and applies both elemental effects.
5. Repeat with Flames + Sparks, then Frostbite + Sparks.
6. Confirm that a single-hand cast still works and that matching spells in both hands still use ordinary vanilla dual casting.

The plugin writes diagnostics to:

Documents/My Games/Skyrim Special Edition/SKSE/ElementalFusionCasting.log

## Current limitations

- The plugin has not yet been tested in a running Skyrim process.
- The first prototype supports keyboard/mouse input; gamepad events are deliberately passed through.
- The brief single-hand delay is currently fixed at 0.18 seconds.
- The exact visual appearance of a beam carrying two separate Magic Effects depends on Skyrim's effect rendering. Both effects are placed in one runtime spell; a blended visual is not guaranteed.
- Only the three vanilla concentration-beam spell forms listed above are recognized.

Please test on a copy of a save. Do not treat a successful compile as proof that the in-game behavior is correct.
