# Aspect Correction

FnQuake3 keeps the classic Quake III `640x480` UI and HUD assumptions, then remaps them to modern displays with optional aspect correction controls. By default, the goal is to stay faithful to retail behavior. When you want a cleaner widescreen presentation, the controls here let you get there without guessing.

## Overview

These settings are split by subsystem, so you can correct the HUD, menus, and cinematics independently instead of forcing one global choice.

- `cl_hudAspect 0`: Stretch legacy HUD output to the framebuffer, matching the original wide-screen stretching behavior.
- `cl_hudAspect 1`: Keep HUD output in centered 4:3 space. If `fnq3-hud.json` exists in the active game directory, matching rules are applied automatically, including HUD 3D model viewports rendered through cgame.
- `cl_menuAspect 0`: Stretch menu widgets to the framebuffer.
- `cl_menuAspect 1`: Keep menu widgets in centered 4:3 space, including 3D model preview viewports rendered through the UI VM. Those adjusted UI scenes keep their authored menu FOV instead of taking an extra `r_fovCorrection` pass.
- `cl_cinematicAspect 0`: Stretch UI and fullscreen cinematics to the framebuffer.
- `cl_cinematicAspect 1`: Keep UI and fullscreen cinematics in centered 4:3 space.

## HUD Rules

When `cl_hudAspect 1` is enabled, FnQuake3 starts from a centered 4:3 HUD layout. If a `fnq3-hud.json` file exists, the engine can then use your rules to realign specific elements or push selected pieces back to stretch mode.

- The script is read from the active game filesystem location.
- `hud_reload` reparses `fnq3-hud.json` without restarting the game.
- Missing or invalid scripts fall back to centered uniform HUD placement.
- HUD `refdef` viewports use the same rect transform path as HUD quads, so score heads and other 3D HUD widgets stay aligned with the corrected HUD layout.

Supported rule fields:

- `name`: Optional label for the rule.
- `match.shader`: Match a registered shader name.
- `match.textLike`: Match likely text groups.
- `match.region`: Match elements by virtual `640x480` region.
- `mode`: `uniform` or `stretch`.
- `align.x`: `left`, `center`, or `right`.
- `align.y`: `top`, `middle`, or `bottom`.

Example:

```json
{
  "version": 1,
  "rules": [
    {
      "name": "status_right",
      "match": {
        "region": { "x": 500, "y": 420, "w": 140, "h": 60 }
      },
      "mode": "uniform",
      "align": {
        "x": "right",
        "y": "bottom"
      }
    }
  ]
}
```

## HUD Script Dumping

`cl_hudDump 1` writes `fnq3-hud-dump.json` into the active game directory while the cgame HUD is being drawn.

- Output is deduplicated across frames.
- Nearby primitives are grouped into likely widgets, including text runs.
- HUD 3D model viewports are included as region-based groups, so they can be targeted with the same rule file.
- Each dumped group includes a suggested transform mode and alignment.
- The dump format matches the loader format, so it works well as a starting point for a real `fnq3-hud.json`.

## Menus And 3D Widgets

`cl_menuAspect 1` affects both UI quads and UI-rendered scenes.

- Traditional menu art is remapped into centered 4:3 space.
- UI `refdef` viewports are remapped as well, so player models and other 3D menu widgets render into the corrected 4:3 viewport instead of a stretched one.
- When that remap is active, the UI scene keeps its original menu-authored FOV and is not widened again by `r_fovCorrection`.
- That keeps the 2D framing and 3D widget projection in the same coordinate space, so the whole menu feels consistent.

## Cinematics

`cl_cinematicAspect` is separate from menu correction on purpose.

- Use it when you want centered 4:3 cinematics without also forcing corrected menu widgets.
- Use `0` if you prefer fullscreen stretch for splash videos, intro movies, or UI-driven cinematics.
- Use `1` if you want black-bar style 4:3 presentation inside widescreen framebuffers.

## Console

Console layout, scaling, interaction, completion, and appearance live in the separate [Console Guide](CONSOLE.md). This page stays focused on HUD, menu, and cinematic presentation.

## Recommended Starting Point

- `cl_hudAspect 1`
- `cl_menuAspect 1`
- `cl_cinematicAspect 1` if you prefer pillarboxed cinematics

That setup gives you a strong "classic Quake III, but cleaner on a modern display" baseline while still letting you opt out per subsystem.
