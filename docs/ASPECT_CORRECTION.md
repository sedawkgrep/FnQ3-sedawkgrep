# Aspect Correction

FnQuake3 keeps the classic Quake III `640x480` UI and HUD assumptions, then maps them onto modern displays with opt-in aspect correction controls. The goal is to preserve retail-compatible behavior by default, while allowing widescreen-safe presentation when you want it.

## Overview

- `cl_hudAspect 0`: Stretch legacy HUD output to the framebuffer, matching the original wide-screen stretching behavior.
- `cl_hudAspect 1`: Keep HUD output in centered 4:3 space. If `fnq3-hud.json` exists in the active game directory, matching rules are applied automatically.
- `cl_menuAspect 0`: Stretch menu widgets to the framebuffer.
- `cl_menuAspect 1`: Keep menu widgets in centered 4:3 space, including 3D model preview viewports rendered through the UI VM.
- `cl_cinematicAspect 0`: Stretch UI and fullscreen cinematics to the framebuffer.
- `cl_cinematicAspect 1`: Keep UI and fullscreen cinematics in centered 4:3 space.
- `con_screenExtents 0`: Use the full screen width for the console display.
- `con_screenExtents 1`: Keep the entire console display in centered 4:3 space.
- `con_scaleUniform 0`: Keep the console font in native pixel sizing.
- `con_scaleUniform 1`: Scale the console font from centered 4:3 space and relayout it within the current console extents.
- `con_speed`: Controls how quickly the console opens and closes.
- `con_scrollLines`: Controls how many lines a normal console scroll step moves.
- `con_scrollSmooth 0`: Keep console scrollback movement immediate.
- `con_scrollSmooth 1`: Smoothly animate scrollback movement and new-line pushes.
- `con_fade 0`: Keep console open and close transitions fully opaque.
- `con_fade 1`: Fade console background and text in and out while it opens or closes.

## HUD Rules

When `cl_hudAspect 1` is enabled, FnQuake3 always performs centered uniform HUD placement first. If a `fnq3-hud.json` file exists, the engine also looks for matching rules to override alignment or force individual elements back to stretch mode.

- The script is read from the active game filesystem location.
- `hud_reload` reparses `fnq3-hud.json` without restarting the game.
- Missing or invalid scripts fall back to centered uniform HUD placement.

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
- Each dumped group includes a suggested transform mode and alignment.
- The dump format matches the loader format, so it can be used as a starting point for a real `fnq3-hud.json`.

## Menus And 3D Widgets

`cl_menuAspect 1` affects both UI quads and UI-rendered scenes.

- Traditional menu art is remapped into centered 4:3 space.
- UI `refdef` viewports are remapped as well, so player models and other 3D menu widgets render into the corrected 4:3 viewport instead of a stretched one.
- This keeps 2D framing and 3D widget projection in the same coordinate space.

## Cinematics

`cl_cinematicAspect` is separate from menu correction on purpose.

- Use it when you want centered 4:3 cinematics but do not want corrected menu widgets.
- Use `0` if you prefer fullscreen stretch for splash videos, intro movies, or UI-driven cinematics.
- Use `1` if you want black-bar style 4:3 presentation inside widescreen framebuffers.

## Console Layout And Appearance

FnQuake3 splits console customization into three groups: layout, motion, and appearance. The goal is to let you choose whether the console behaves like the classic full-width Quake III console, a centered 4:3 console, or something in between.

### Layout

`con_screenExtents` and `con_scaleUniform` control different parts of the console.

- `con_screenExtents` chooses whether the whole console display uses full-screen width or centered 4:3 width.
- `con_scaleUniform` changes the console from native-pixel font sizing to centered 4:3 uniform sizing without forcing the console to use centered extents.
- `con_scale` still controls the base font size before the chosen scaling mode is applied.

- Character width and height are derived from the active console scaling mode instead of being treated as fixed.
- Console line width, prompt width, visible page size, scroll paging, and input field width are recomputed from the current character metrics and the active console extents.
- With `con_screenExtents 0`, uniform font scaling still uses the full console width.
- With `con_screenExtents 1`, the entire console display, including its background and text block, is centered in 4:3 space.
- The console relayout updates automatically when screen size, internal scaling, `con_scale`, `con_scaleUniform`, or `con_screenExtents` changes.

### Motion

`con_speed`, `con_scrollLines`, `con_scrollSmooth`, and `con_scrollSmoothSpeed` control how the console moves.

- `con_speed` controls how fast the console opens and closes.
- `con_scrollLines` sets the default number of lines moved by normal scroll steps such as `PgUp`, `PgDn`, or mouse wheel scrolling.
- `Ctrl+PgUp` and `Ctrl+PgDn` still scroll by one visible console page.
- `con_scrollLines` is clamped to the current visible console page, so it automatically respects the current console height and text size.
- `con_scrollSmooth 1` interpolates scrollback movement instead of snapping it immediately.
- `con_scrollSmoothSpeed` defines the smooth scroll speed in lines per second.
- Smooth scrolling applies both when you manually scroll the console and when new console output pushes older lines upward.

### Appearance

The console background and accent colors can now be customized directly.

- `con_backgroundStyle 0`: Use the classic textured console background.
- `con_backgroundStyle 1`: Use a flat shaded console background.
- `con_backgroundColor`: Override the console background RGB color with `R G B` values from `0-255`.
- `con_backgroundOpacity`: Set console background opacity from `0` to `1`.
- `con_lineColor`: Set the separator line and scrollback marker color with `R G B` values from `0-255`.
- `con_versionColor`: Set the version text color with `R G B` values from `0-255`.
- `con_fade 1`: Fade the console background, text, and accents in and out during open and close transitions.

Legacy compatibility notes:

- `cl_conColor` is still honored as a legacy fallback for background tinting.
- If `con_backgroundColor` is empty, the console uses the legacy `cl_conColor` value when one is set.
- If neither override is set, textured mode uses its default look and flat mode uses a plain black background.

## Recommended Starting Point

- `cl_hudAspect 1`
- `cl_menuAspect 1`
- `cl_cinematicAspect 1` if you prefer pillarboxed cinematics
- `con_screenExtents 1` if you want the whole console centered in 4:3 space
- `con_scaleUniform 1`
- `con_scrollSmooth 1` if you want console scrollback to move instead of snap
- `con_fade 1` if you want softer console open and close transitions

That setup keeps gameplay HUDs, menus, UI model widgets, cinematics, and console text closer to their original 4:3 presentation on modern displays while still letting you opt out per subsystem.
