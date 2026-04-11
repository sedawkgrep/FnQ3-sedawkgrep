# Display Guide

FnQuake3 splits display settings across three layers: how the window or fullscreen mode is created, how the scene is rendered internally, and which post-processing controls are applied to the final image. This guide covers renderer choice, video mode selection, framebuffer-based rendering, anti-aliasing, bloom, and the related scene presentation controls.

For HUD, menu, and cinematic layout on widescreen displays, use the separate [Aspect Correction Guide](ASPECT_CORRECTION.md). For screenshot output and capture-specific options, use the [Screenshot Guide](SCREENSHOTS.md).

## Renderer Choice

`cl_renderer` selects the rendering backend and requires `vid_restart`.

- `cl_renderer opengl`: Best choice if you want the full bloom feature set. OpenGL exposes the extra bloom shape controls, the lens reflection effect, and the HUD-inclusive `r_bloom 2` mode.
- `cl_renderer vulkan`: Modern backend with the same core display path controls for FBO rendering, HDR, multisampling, supersampling, render scaling, gamma, and greyscale. Bloom is available here too, but the exposed control set is smaller.

If you are tuning bloom heavily, start on `opengl`. If you want the simpler cross-platform path and do not need the OpenGL-only bloom extras, `vulkan` is fine.

## Display Modes And Window Behavior

These settings control the actual game window or fullscreen mode. Treat them as `vid_restart` settings.

- `r_mode`: Main video mode selector.
  - `-2`: Use the current desktop resolution.
  - `-1`: Use `r_customWidth` and `r_customHeight`.
  - `0..N`: Use a predefined mode from `\modelist`.
- `r_modeFullscreen`: Optional dedicated fullscreen override. Leave it empty to reuse `r_mode`, or set it to one of the same values you would normally use for `r_mode`.
- `r_fullscreen`: `1` for fullscreen, `0` for windowed mode.
- `r_customWidth` and `r_customHeight`: Custom resolution used by `r_mode -1`.
- `r_customPixelAspect`: Advanced custom pixel aspect value for `r_mode -1` or `r_mode -2`. Leave this at `1` unless you intentionally need non-square pixel behavior.
- `r_displayRefresh`: Fullscreen refresh-rate override. `0` uses the current monitor refresh rate.
- `r_noborder`: Removes the title bar and borders in windowed mode.
- `vid_xpos` and `vid_ypos`: Saved window position in windowed mode.
- `r_swapInterval`: V-Sync control.
  - `0`: Do not wait for v-blank.
  - `1`: Sync swaps to the monitor refresh rate.

Practical setups:

- Exclusive fullscreen at desktop resolution:

```cfg
seta r_fullscreen "1"
seta r_mode "-2"
vid_restart
```

- Borderless desktop-sized window:

```cfg
seta r_fullscreen "0"
seta r_mode "-2"
seta r_noborder "1"
vid_restart
```

- Custom fixed window size:

```cfg
seta r_fullscreen "0"
seta r_mode "-1"
seta r_customWidth "1600"
seta r_customHeight "900"
vid_restart
```

## Framebuffer Path, Internal Resolution, And Anti-Aliasing

These settings control the render path behind the display output.

- `r_fbo`: Enables framebuffer-object rendering. This is the foundation for the modern display path and is required for bloom, HDR, multisample anti-aliasing, supersampling, greyscale, and arbitrary internal render resolutions.
- `r_hdr`: Controls framebuffer precision.
  - `-1`: 4-bit, mainly for testing and likely to band heavily.
  - `0`: 8-bit, the default.
  - `1`: 16-bit, higher precision with a possible performance cost.
- `r_ext_multisample`: Geometry-edge anti-aliasing. The practical values are `0`, `2`, `4`, `6`, and `8`.
- `r_ext_supersample`: Enables supersample anti-aliasing.
- `r_renderWidth` and `r_renderHeight`: Internal render resolution when `r_renderScale > 0`.
- `r_renderScale`: Controls how the internal render image is scaled to the actual window or fullscreen size.
  - `0`: Disabled.
  - `1`: Nearest-neighbor stretch.
  - `2`: Nearest-neighbor with preserved aspect ratio.
  - `3`: Linear stretch.
  - `4`: Linear with preserved aspect ratio.

Guidance:

- Use `r_fbo 1` if you want any modern post-processing or internal render scaling.
- Use `r_ext_multisample` for cleaner geometry edges.
- Use `r_ext_supersample 1` when you want higher image quality and can afford the extra GPU cost.
- Use `r_renderWidth`, `r_renderHeight`, and `r_renderScale` when you want a lower or higher internal render resolution than the actual window size.

Example: render internally at `1280x720` and scale to your current display with linear filtering and preserved aspect ratio:

```cfg
seta r_fbo "1"
seta r_renderWidth "1280"
seta r_renderHeight "720"
seta r_renderScale "4"
vid_restart
```

## Scene Presentation Controls

These settings affect the rendered scene itself rather than the window mode.

- `r_gamma`: Gamma correction factor. This is one of the first settings to check if the whole frame looks too dark or too washed out.
- `r_fovCorrection`: Auto-corrects classic `4:3` scene FOV values for the current viewport aspect. This is for world rendering, not HUD layout.
- `r_greyscale`: Full-frame desaturation. Requires `r_fbo 1`.

Use [ASPECT_CORRECTION.md](ASPECT_CORRECTION.md) for HUD, menu, and cinematic layout. That guide is intentionally separate because those settings solve a different problem than scene FOV, render scaling, or bloom.

## Cel Shading

Cel shading is available for model entities in all supported renderers. In practice that means map world models, player models, and the first-person weapon can use banded lighting and an optional silhouette shell, while the main BSP world remains unchanged.

### Controls

- `r_celShading`: Master toggle.
  - `0`: Disabled.
  - `1`: Enable cel shading on model entities.
- `r_celShadingSteps`: Number of diffuse lighting bands. Lower values push a more stylized hard-step look. Higher values keep more intermediate tone.
- `r_celOutline`: Enables the silhouette shell around cel-shaded model entities.
- `r_celOutlineScale`: Outline shell expansion amount. Values just above `1.0` keep the outline tight. Larger values make it thicker.
- `r_celOutlineColor`: Outline color as `"r g b a"`.

Notes:

- `r_celOutline` depends on a stencil buffer being available.
- The effect is meant for model presentation, not full-world BSP flat shading.
- The outline color is shared across eligible model entities.

Recommended starting point:

```cfg
seta r_celShading "1"
seta r_celShadingSteps "4"
seta r_celOutline "1"
seta r_celOutlineScale "1.02"
seta r_celOutlineColor "0 0 0 255"
```

For a harsher stylized look, reduce `r_celShadingSteps` to `2` or `3`. For a softer result, keep the outline enabled but raise `r_celShadingSteps` to `5` or `6`.

## Bloom

Bloom extracts bright areas from the rendered frame, blurs them through a downsampled chain, and blends the result back over the original image. It is a post-processing effect, so it depends on the framebuffer path.

### Requirements

- `r_fbo 1` is required.
- OpenGL and Vulkan both support bloom.
- OpenGL exposes the largest bloom control set.
- Vulkan currently exposes the shared extraction and intensity controls, but not the OpenGL-only shape controls.

### Shared Bloom Controls

These settings are available in both renderers.

- `r_bloom`: Master bloom toggle.
  - On OpenGL, use `0`, `1`, or `2`.
    - `0`: Disabled.
    - `1`: Bloom the rendered 3D scene.
    - `2`: Also let bloom affect 2D and HUD elements drawn before the final post-process pass.
  - On Vulkan, treat `r_bloom` as `0` or `1`.
- `r_bloom_threshold`: Brightness cutoff for extraction. Higher values restrict bloom to stronger highlights. Lower values let more of the frame glow.
- `r_bloom_threshold_mode`: How brightness is measured.
  - `0`: Trigger when any color channel reaches the threshold.
  - `1`: Trigger when the average of `r`, `g`, and `b` reaches the threshold.
  - `2`: Trigger using luma weighting. This is usually the cleanest and most predictable mode.
- `r_bloom_modulate`: How strongly extracted color is biased toward already-bright areas.
  - `0`: Leave extracted color unchanged.
  - `1`: Multiply the color by itself for a more aggressive highlight push.
  - `2`: Multiply the color by its luma for a cleaner brightness-weighted result.
- `r_bloom_intensity`: Final bloom blend strength.

Recommended tuning order:

1. Enable `r_bloom 1`.
2. Set `r_bloom_threshold_mode 2`.
3. Adjust `r_bloom_threshold` until only the highlights you want are being extracted.
4. Adjust `r_bloom_modulate` if you want a tighter or more contrast-driven response.
5. Adjust `r_bloom_intensity` last.

### OpenGL-Only Bloom Controls

These settings are currently specific to the OpenGL renderer.

- `r_bloom_passes`: Number of downsampled bloom levels used in the effect. More passes generally create a wider haze and cost more GPU time. The engine may clamp the effective chain length based on hardware limits or very small internal render sizes.
- `r_bloom_blend_base`: Which downsampled level to start blending from. Higher values skip the tighter levels and bias the result toward a broader, softer haze.
- `r_bloom_filter_size`: Blur filter size per bloom level. Higher values widen the blur and cost more.
- `r_bloom_reflection`: Lens reflection effect intensity.
  - Positive values add the reflection on top of the main bloom.
  - Negative values keep only the reflection path and skip the main bloom texture.

Practical interpretation:

- Increase `r_bloom_passes` if you want bloom to spread farther from bright sources.
- Increase `r_bloom_blend_base` if you want less tight glow and more broad atmosphere.
- Increase `r_bloom_filter_size` if the bloom still feels too sharp.
- Use `r_bloom_reflection` carefully. It is a stylized effect and becomes obvious quickly.

### Bloom Tuning Recipes

Subtle highlight bloom:

```cfg
seta r_fbo "1"
seta r_bloom "1"
seta r_bloom_threshold "0.75"
seta r_bloom_threshold_mode "2"
seta r_bloom_modulate "2"
seta r_bloom_intensity "0.25"
```

Balanced bloom for bright maps and effects:

```cfg
seta r_fbo "1"
seta r_bloom "1"
seta r_bloom_threshold "0.60"
seta r_bloom_threshold_mode "2"
seta r_bloom_modulate "2"
seta r_bloom_intensity "0.45"
```

OpenGL haze-heavy bloom:

```cfg
seta cl_renderer "opengl"
seta r_fbo "1"
seta r_bloom "1"
seta r_bloom_threshold "0.55"
seta r_bloom_threshold_mode "2"
seta r_bloom_modulate "2"
seta r_bloom_intensity "0.45"
seta r_bloom_passes "6"
seta r_bloom_blend_base "2"
seta r_bloom_filter_size "8"
vid_restart
```

OpenGL HUD-inclusive bloom:

```cfg
seta cl_renderer "opengl"
seta r_fbo "1"
seta r_bloom "2"
seta r_bloom_threshold "0.65"
seta r_bloom_threshold_mode "2"
seta r_bloom_intensity "0.35"
```

OpenGL lens reflection add-on:

```cfg
seta cl_renderer "opengl"
seta r_fbo "1"
seta r_bloom "1"
seta r_bloom_threshold "0.60"
seta r_bloom_threshold_mode "2"
seta r_bloom_intensity "0.40"
seta r_bloom_reflection "0.25"
```

### Bloom Troubleshooting

If bloom seems too weak:

- Lower `r_bloom_threshold`.
- Use `r_bloom_threshold_mode 0` or `2`.
- Raise `r_bloom_intensity`.
- On OpenGL, increase `r_bloom_passes` or `r_bloom_filter_size`.

If bloom is making the whole frame look milky:

- Raise `r_bloom_threshold`.
- Lower `r_bloom_intensity`.
- Use `r_bloom_threshold_mode 2`.
- On OpenGL, lower `r_bloom_passes`, lower `r_bloom_filter_size`, or reduce `r_bloom_blend_base`.

If HUD elements are glowing and you do not want that:

- On OpenGL, use `r_bloom 1` instead of `r_bloom 2`.

If nothing happens at all:

- Confirm `r_fbo 1`.
- Confirm bloom is actually enabled with `r_bloom`.
- Lower `r_bloom_threshold` to test.
- If you just changed renderer, display mode, or other latched video settings, use `vid_restart`.

## When To Use `vid_restart`

Use `vid_restart` after changes to:

- `cl_renderer`
- `r_mode`, `r_modeFullscreen`, `r_fullscreen`
- `r_customWidth`, `r_customHeight`, `r_customPixelAspect`
- `r_displayRefresh`
- `r_noborder`
- `r_fbo`
- `r_hdr`
- `r_ext_multisample`
- `r_renderWidth`, `r_renderHeight`, `r_renderScale`
- `r_ext_supersample`
- OpenGL `r_bloom_passes`
- Vulkan `r_bloom`

Settings that are usually safe to tune live:

- `r_swapInterval`
- `r_gamma`
- `r_fovCorrection`
- `r_greyscale`
- `r_bloom_threshold`
- `r_bloom_threshold_mode`
- `r_bloom_modulate`
- `r_bloom_intensity`
- OpenGL `r_bloom_blend_base`
- OpenGL `r_bloom_filter_size`
- OpenGL `r_bloom_reflection`

If a live change does not seem to take effect immediately, `vid_restart` is the safe fallback.

## Related Guides

- [ASPECT_CORRECTION.md](ASPECT_CORRECTION.md) for HUD, menu, and cinematic presentation.
- [VISUALS.md](VISUALS.md) for player highlighting and other visual presentation controls.
- [SCREENSHOTS.md](SCREENSHOTS.md) for screenshot and capture output options.
