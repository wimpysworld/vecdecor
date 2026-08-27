# Vecdecor

![Pixdecor window decorations](https://github.com/soreau/pixdecor/assets/1450125/af891554-8eeb-4769-b571-fa587afd8350)

Vecdecor is a configurable decorator plugin for Wayfire. It renders rounded corners on floating windows and square corners on tiled or maximised windows. It no longer provides titlebar background effects or built-in window shadows. Use Wayfire's external `winshadows` plugin for shadows.

## Install

Set `--prefix` to the prefix of your Wayfire installation.

```console
meson setup build --prefix=/usr
ninja -C build
sudo ninja -C build install
```

The build installs the Wayfire plugin as `libvecdecor.so` and its metadata as `vecdecor.xml`. Restart Wayfire after installation.

## Configure

Disable other decorator plugins. Keep your existing `core.plugins` entries and add `vecdecor`:

```ini
[core]
plugins = command move resize vecdecor
```

Put Vecdecor options in the `[vecdecor]` section of `wayfire.ini`. All option names use the `vecdecor/` prefix in Wayfire's option API.

```ini
[vecdecor]
title_height = 0
button_size = 0

button_minimize_svg = /path/to/minimize.svg
button_maximize_svg = /path/to/maximize.svg
button_restore_svg = /path/to/restore.svg
button_close_svg = /path/to/close.svg

button_color = 0.803922 0.839216 0.956863 1.0
button_inactive_color = 0.529412 0.533333 0.572549 1.0
button_hover_color = 0.192157 0.196078 0.266667 1.0
button_pressed_color = 0.270588 0.278431 0.352941 1.0
```

| Option | Role |
| --- | --- |
| `title_height` | Non-negative titlebar height in logical pixels. `0` uses an automatic height derived from the title font. CSD Shade uses this resolved height. |
| `button_size` | Non-negative square button size in logical pixels. `0` selects `18` or `26`, based on the title font. |
| `button_minimize_svg` | Custom SVG mask for every minimise state. Leave empty to use the bundled controls. |
| `button_maximize_svg` | Custom SVG mask for every maximise state. Leave empty to use the bundled controls. |
| `button_restore_svg` | Custom SVG mask for every restore state. Leave empty to use the bundled controls. |
| `button_close_svg` | Custom SVG mask for every close state. Leave empty to use the bundled controls. |
| `button_color` | Active glyph colour for custom masks and procedural fallbacks |
| `button_inactive_color` | Inactive glyph colour for custom masks and procedural fallbacks |
| `button_hover_color` | Hover background colour for custom masks and procedural fallbacks |
| `button_pressed_color` | Pressed background colour for custom masks and procedural fallbacks |

`title_height` and `button_size` are independent. If either value is positive, Vecdecor expands the titlebar when needed to contain the button after applying `button_y_offset`. Changes to either option recreate the decoration frames, so the new size applies without restarting Wayfire.

The logical geometry stays constant across output scales. Vecdecor rasterises each button for the output scale and keeps the rendered and pointer bounds equal.

Empty SVG options use 16 full-colour controls installed with the plugin. Each minimise, maximise, restore, and close control has active, active hover, inactive, and inactive hover assets. Pressed states use the matching hover asset. The bundled colours do not use the palette options.

A configured SVG path overrides all four states of its control. Vecdecor treats the custom SVG as an alpha mask and recolours it with the palette options. If one source is missing, blank, or invalid, Vecdecor uses the procedural fallback for that exact state. Changes to an SVG option path or a colour option apply without restarting Wayfire.

The bundled controls use the Catppuccin Mocha palette. Active minimise circles use `#f9e2af`, maximise and restore circles use `#a6e3a1`, and close circles use `#f38ba8`. Inactive circles use `#45475a`. Active glyphs use `#cdd6f4`, inactive glyphs use `#878892`, and hover backgrounds use `#313244`.

> [!WARNING]
> This version removes the `overlay_engine`, `effect_type`, `effect_color`, `animate`, `beveled_glass`, and `beveled_glass_overlay` options. It also removes `shadow_radius`, `shadow_color`, and `maximized_shadows`. Remove these keys from `[vecdecor]`. Enable Wayfire's external `winshadows` plugin if you need window shadows.
>
> Vecdecor also removes Pixdecor's PNG button options. It has no compatibility reader, so existing PNG options do not migrate automatically.

## Licence and credit

Vecdecor is a fork of [Pixdecor](https://github.com/soreau/pixdecor) by Scott Moreau. Pixdecor also credits Ilia Bozhinov and Andrew Pliatsikas. Vecdecor keeps Pixdecor's [MIT licence](LICENSE).
