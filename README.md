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

button_color = 0.0 0.0 0.0 1.0
button_inactive_color = 0.4 0.4 0.4 1.0
button_hover_color = 0.2 0.2 0.2 1.0
button_pressed_color = 0.8 0.8 0.8 1.0
```

| Option | Role |
| --- | --- |
| `title_height` | Non-negative titlebar height in logical pixels. `0` uses an automatic height derived from the title font. |
| `button_size` | Non-negative square button size in logical pixels. `0` selects `18` or `26`, based on the title font. |
| `button_minimize_svg` | SVG file override for the minimise button. Leave empty to use Vecdecor's control. |
| `button_maximize_svg` | SVG file override for the maximise button. Leave empty to use Vecdecor's control. |
| `button_restore_svg` | SVG file override for the restore button. Leave empty to use Vecdecor's control. |
| `button_close_svg` | SVG file override for the close button. Leave empty to use Vecdecor's control. |
| `button_color` | Colour of active buttons |
| `button_inactive_color` | Colour of inactive buttons |
| `button_hover_color` | Colour of buttons under the pointer |
| `button_pressed_color` | Colour of pressed buttons |

`title_height` and `button_size` are independent. If either value is positive, Vecdecor expands the titlebar when needed to contain the button after applying `button_y_offset`. Changes to either option recreate the decoration frames, so the new size applies without restarting Wayfire.

The logical geometry stays constant across output scales. Vecdecor rasterises each button for the output scale and keeps the rendered and pointer bounds equal.

Empty SVG options use the Vecdecor-owned controls installed with the plugin. A configured file overrides the matching Vecdecor control. The configured colours recolour the controls, so each colour does not need a separate asset file. If a configured file is missing or invalid, Vecdecor uses the matching procedural fallback control. Changes to an SVG option path or a colour option apply without restarting Wayfire.

> [!WARNING]
> This version removes the `overlay_engine`, `effect_type`, `effect_color`, `animate`, `shadow_radius`, `shadow_color`, and `maximized_shadows` options. Remove these keys from `[vecdecor]`, then configure and enable Wayfire's `winshadows` plugin if you need window shadows.
>
> Vecdecor also removes Pixdecor's PNG button options. It has no compatibility reader, so existing PNG options do not migrate automatically.

## Licence and credit

Vecdecor is a fork of [Pixdecor](https://github.com/soreau/pixdecor) by Scott Moreau. Pixdecor also credits Ilia Bozhinov and Andrew Pliatsikas. Vecdecor keeps Pixdecor's [MIT licence](LICENSE).
