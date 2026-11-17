Vessel Physics Optimizer — KSP 1.12.5.3190

INSTALLATION
Copy the GameData folder from this archive into your Kerbal Space Program directory.
The plugin DLL must end up at:
GameData/VesselPhysicsOptimizer/Plugins/VesselPhysicsOptimizer.dll

CONTROLS
F9: Toggle debug colors.
F10: Show/hide the in-flight diagnostic window.

DEBUG COLORS
Purple: part is in a connected multi-part component.
Green: part was previously in a multi-part component and has since returned to a
       single-part state after a separation.
Normal appearance: part has never been part of a detected multi-part component.

LIMITATIONS
This build is a visualization/prototype only. It detects connected part components
and colors their renderers, but it does not merge rigidbodies, remove joints, reduce
the physics body count, or restore actual physics bodies. No physics aggregation is
performed in this build.