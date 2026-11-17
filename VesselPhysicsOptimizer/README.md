# Vessel Physics Optimizer — KSP 1.12.5.3190

This first compiled plugin build is a **visualization prototype**, not the physics
aggregation implementation.

- Purple: parts in currently connected multi-part components.
- Green: parts previously in a multi-part component that have returned to an
  individual state following a separation.
- Normal appearance: parts never detected as grouped.
- F9 toggles debug colors; F10 toggles the in-flight window.

The scanner checks loaded vessels so detached subassemblies can remain purple when
they still contain multiple connected parts. Original renderer property blocks are
saved and restored when the overlay is disabled.

It does not yet merge rigidbodies or remove KSP joints, so it does not currently
reduce physics body count or improve simulation performance.