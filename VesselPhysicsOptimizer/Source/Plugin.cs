using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace VesselPhysicsOptimizer
{
    [KSPAddon(KSPAddon.Startup.Flight, false)]
    public sealed class Plugin : MonoBehaviour
    {
        private bool debugColors;
        private bool showWindow = true;
        private Rect windowRect = new Rect(25, 80, 365, 185);
        private float nextScan;
        private string status = "Waiting for vessel";
        private int groupedPartCount;
        private int groupCount;
        private int restoredPartCount;

        private readonly HashSet<Part> wasGrouped = new HashSet<Part>();
        private readonly HashSet<Part> restoredParts = new HashSet<Part>();
        private readonly Dictionary<Renderer, MaterialPropertyBlock> originalBlocks =
            new Dictionary<Renderer, MaterialPropertyBlock>();

        private static readonly Color GroupedColor = new Color(0.65f, 0.25f, 0.9f, 1f);
        private static readonly Color RestoredColor = new Color(0.2f, 0.85f, 0.35f, 1f);

        private void Update()
        {
            if (Input.GetKeyDown(KeyCode.F9))
            {
                debugColors = !debugColors;
                if (!debugColors) RestoreAllOriginalBlocks();
            }
            if (Input.GetKeyDown(KeyCode.F10)) showWindow = !showWindow;

            if (Time.realtimeSinceStartup < nextScan) return;
            nextScan = Time.realtimeSinceStartup + 0.5f;
            ScanLoadedVessels();
        }

        private void ScanLoadedVessels()
        {
            var currentParts = new HashSet<Part>();
            var currentGrouped = new HashSet<Part>();
            int currentGroupCount = 0;

            foreach (Vessel vessel in FlightGlobals.Vessels)
            {
                if (vessel == null || vessel.parts == null) continue;
                foreach (Part part in vessel.parts)
                    if (part != null) currentParts.Add(part);

                foreach (List<Part> component in FindConnectedComponents(vessel))
                {
                    if (component.Count <= 1) continue;
                    currentGroupCount++;
                    foreach (Part part in component) currentGrouped.Add(part);
                }
            }

            foreach (Part part in wasGrouped)
                if (part != null && !currentGrouped.Contains(part))
                    restoredParts.Add(part);

            foreach (Part part in currentGrouped)
                restoredParts.Remove(part);

            wasGrouped.Clear();
            foreach (Part part in currentGrouped) wasGrouped.Add(part);

            groupedPartCount = currentGrouped.Count;
            groupCount = currentGroupCount;
            restoredParts.RemoveWhere(part => part == null);
            restoredPartCount = restoredParts.Count;

            status = FlightGlobals.ActiveVessel == null
                ? "No active vessel (scanning loaded vessels)"
                : "Visualization only — physics unchanged";

            if (debugColors) ApplyDebugColors(currentParts, currentGrouped);
            else RestoreAllOriginalBlocks();
        }

        private static List<List<Part>> FindConnectedComponents(Vessel vessel)
        {
            var result = new List<List<Part>>();
            var vesselParts = new HashSet<Part>(vessel.parts.Where(part => part != null));
            var visited = new HashSet<Part>();

            foreach (Part start in vesselParts)
            {
                if (!visited.Add(start)) continue;
                var component = new List<Part>();
                var queue = new Queue<Part>();
                queue.Enqueue(start);

                while (queue.Count > 0)
                {
                    Part current = queue.Dequeue();
                    component.Add(current);
                    if (current.parent != null && vesselParts.Contains(current.parent) &&
                        visited.Add(current.parent))
                        queue.Enqueue(current.parent);

                    if (current.children == null) continue;
                    foreach (Part child in current.children)
                        if (child != null && vesselParts.Contains(child) && visited.Add(child))
                            queue.Enqueue(child);
                }
                if (component.Count > 1) result.Add(component);
            }
            return result;
        }

        private void ApplyDebugColors(HashSet<Part> currentParts, HashSet<Part> currentGrouped)
        {
            foreach (KeyValuePair<Renderer, MaterialPropertyBlock> entry in originalBlocks.ToArray())
            {
                Renderer renderer = entry.Key;
                if (renderer == null) continue;
                if (!IsRendererOnStatePart(renderer, currentParts, currentGrouped))
                    renderer.SetPropertyBlock(entry.Value);
            }

            foreach (Part part in currentParts)
            {
                if (part == null) continue;
                Color? color = currentGrouped.Contains(part)
                    ? (Color?)GroupedColor
                    : restoredParts.Contains(part) ? (Color?)RestoredColor : null;
                if (!color.HasValue) continue;
                foreach (Renderer renderer in part.FindModelComponents<Renderer>())
                    ApplyColor(renderer, color.Value);
            }

            foreach (Part part in restoredParts)
            {
                if (part == null || currentParts.Contains(part)) continue;
                foreach (Renderer renderer in part.FindModelComponents<Renderer>())
                    ApplyColor(renderer, RestoredColor);
            }
        }

        private bool IsRendererOnStatePart(Renderer renderer, HashSet<Part> currentParts,
            HashSet<Part> currentGrouped)
        {
            foreach (Part part in currentParts)
            {
                if (part == null || (!currentGrouped.Contains(part) && !restoredParts.Contains(part)))
                    continue;
                foreach (Renderer candidate in part.FindModelComponents<Renderer>())
                    if (candidate == renderer) return true;
            }
            return false;
        }

        private void ApplyColor(Renderer renderer, Color color)
        {
            if (renderer == null) return;
            if (!originalBlocks.ContainsKey(renderer))
            {
                var original = new MaterialPropertyBlock();
                renderer.GetPropertyBlock(original);
                originalBlocks.Add(renderer, original);
            }
            var block = new MaterialPropertyBlock();
            renderer.GetPropertyBlock(block);
            block.SetColor("_Color", color);
            block.SetColor("_BaseColor", color);
            renderer.SetPropertyBlock(block);
        }

        private void RestoreAllOriginalBlocks()
        {
            foreach (KeyValuePair<Renderer, MaterialPropertyBlock> entry in originalBlocks)
                if (entry.Key != null) entry.Key.SetPropertyBlock(entry.Value);
            originalBlocks.Clear();
        }

        private void OnGUI()
        {
            if (!showWindow || HighLogic.LoadedScene != GameScenes.FLIGHT) return;
            windowRect = GUILayout.Window(194028, windowRect, DrawWindow, "Vessel Physics Optimizer");
        }

        private void DrawWindow(int id)
        {
            GUILayout.Label("KSP 1.12.5 — visualization build");
            GUILayout.Label("Connected multi-part groups: " + groupCount);
            GUILayout.Label("Purple / grouped parts: " + groupedPartCount);
            GUILayout.Label("Green / restored parts: " + restoredPartCount);
            GUILayout.Label(status);
            debugColors = GUILayout.Toggle(debugColors, "Debug colors (F9)");
            if (!debugColors) RestoreAllOriginalBlocks();
            GUILayout.Label("F10: show/hide this window");
            if (GUILayout.Button("Close")) showWindow = false;
            GUI.DragWindow();
        }

        private void OnDestroy()
        {
            RestoreAllOriginalBlocks();
        }
    }
}