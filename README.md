# Elemental Fusion Casting

An experimental SKSE plugin for Skyrim AE 1.7.104 that attempts to merge two different vanilla Destruction concentration beams into one beam carrying both elemental effects.

## Current target

- Fire + Frost
- Fire + Shock
- Frost + Shock

Same-element combinations and spells other than the vanilla aimed concentration elemental beams are left alone.

## Important

This is an experimental first implementation. It modifies the live effect arrays of the equipped spell forms while the merged cast is active, then restores them when casting ends or a save/load transition occurs. Back up saves and test on a disposable save first. It has not been validated in a running game yet.

A successful CI build only proves that the DLL compiles; it does not prove the in-game hook behaves correctly.

## Install

Install the generated `ElementalFusionCasting-test.zip` with Vortex as a mod. It places the DLL under `Data/SKSE/Plugins`.

Requires SKSE64 and Address Library for SKSE Plugins.

## Test

Equip two different vanilla concentration beams (Flames, Frostbite, or Sparks), one in each hand. Hold both attack buttons together. The intended result is one beam from the first hand, containing both elemental effects at half magnitude each. Release both buttons to end the cast.

Check `Documents\My Games\Skyrim Special Edition\SKSE\ElementalFusionCasting.log` for detection and restoration messages.

## Development status

Experimental native hook. No ESP/ESL is required. See the workflow artifact for the installable test archive.
