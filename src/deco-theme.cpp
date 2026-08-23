#include "deco-theme.hpp"
#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <mutex>
#include <stdlib.h>

namespace wf
{
namespace pixdecor
{
wf::option_wrapper_t<int> border_size{"vecdecor/border_size"};
wf::option_wrapper_t<bool> titlebar{"vecdecor/titlebar"};
wf::option_wrapper_t<wf::color_t> fg_color{"vecdecor/fg_color"};
wf::option_wrapper_t<wf::color_t> bg_color{"vecdecor/bg_color"};
wf::option_wrapper_t<wf::color_t> fg_text_color{"vecdecor/fg_text_color"};
wf::option_wrapper_t<wf::color_t> bg_text_color{"vecdecor/bg_text_color"};
wf::option_wrapper_t<int> button_size{"vecdecor/button_size"};
wf::option_wrapper_t<int> title_height{"vecdecor/title_height"};
wf::option_wrapper_t<int> button_y_offset{"vecdecor/button_y_offset"};
wf::option_wrapper_t<wf::color_t> button_color{"vecdecor/button_color"};
wf::option_wrapper_t<wf::color_t> button_inactive_color{"vecdecor/button_inactive_color"};
wf::option_wrapper_t<wf::color_t> button_hover_color{"vecdecor/button_hover_color"};
wf::option_wrapper_t<wf::color_t> button_pressed_color{"vecdecor/button_pressed_color"};
wf::option_wrapper_t<double> button_line_thickness{"vecdecor/button_line_thickness"};

namespace
{
bool same_button_prepare_config(
    const button_prepare_config_t& lhs, const button_prepare_config_t& rhs)
{
    return (lhs.logical_size == rhs.logical_size) &&
           (lhs.svg_proportions == rhs.svg_proportions) &&
           (lhs.output_scale == rhs.output_scale) &&
           (lhs.line_thickness == rhs.line_thickness) &&
           (lhs.theme_generation == rhs.theme_generation) &&
           (lhs.palette.active == rhs.palette.active) &&
           (lhs.palette.inactive == rhs.palette.inactive) &&
           (lhs.palette.hover == rhs.palette.hover) &&
           (lhs.palette.pressed == rhs.palette.pressed);
}
}

/** Create a new theme with the default parameters */
pixdecor_theme_t::pixdecor_theme_t(button_renderer_t& renderer, const std::uint64_t& generation) :
    button_renderer(renderer), generation(generation)
{
    gs = g_settings_new("org.gnome.desktop.interface");

    // read initial colours
    update_colors();
}

pixdecor_theme_t::~pixdecor_theme_t()
{
    g_object_unref(gs);
}

void pixdecor_theme_t::update_colors(void)
{
    fg = wf::color_t(fg_color);
    bg = wf::color_t(bg_color);
    fg_text = wf::color_t(fg_text_color);
    bg_text = wf::color_t(bg_text_color);
}

/**
 * @return A PangoFontDescription representing either a provided font from the Title Font option or the system
 * font, scaled by the text-scaling-factor GSetting.
 */
PangoFontDescription*pixdecor_theme_t::create_font_description()
{
    GSettings *gs = g_settings_new("org.gnome.desktop.interface");

    std::string title_font_val(title_font);
    bool using_system_font{};

    if (title_font_val.empty())
    {
        char *font = g_settings_get_string(gs, "font-name");
        title_font_val    = font;
        using_system_font = true;
        g_free(font);
    }

    PangoFontDescription *font_desc = pango_font_description_from_string(title_font_val.c_str());
    int font_size = pango_font_description_get_size(font_desc);
    bool font_size_absolute;

    // font size will be 0 if nothing is specified - grab from system font if this is the case
    // if the font is the system font, then just pray it works
    if (!font_size && !using_system_font)
    {
        char *font = g_settings_get_string(gs, "font-name");
        PangoFontDescription *sys_font_desc = pango_font_description_from_string(font);

        font_size = pango_font_description_get_size(sys_font_desc);
        font_size_absolute = pango_font_description_get_size_is_absolute(sys_font_desc);

        pango_font_description_free(sys_font_desc);
        g_free(font);
    } else
    {
        font_size_absolute = pango_font_description_get_size_is_absolute(font_desc);
    }

    // scale the font size if we got it by text-scaling-factor
    if (font_size)
    {
        double scaling_factor = g_settings_get_double(gs, "text-scaling-factor");
        if (!scaling_factor) // should default to 1.0, but better safe than sorry
        {
            scaling_factor = 1.0;
        }

        if (font_size_absolute)
        {
            pango_font_description_set_absolute_size(font_desc, font_size * scaling_factor);
        } else
        {
            pango_font_description_set_size(font_desc, font_size * scaling_factor);
        }
    }

    g_object_unref(gs);
    return font_desc;
}

/**
 * @return A PangoFontDescription from create_font_description(), originating from a global, internally
 * managed instance, freeing the data upon exit. It will also be updated with changes to the Title Font
 * option.
 */
PangoFontDescription*pixdecor_theme_t::get_font_description()
{
    static std::unique_ptr<PangoFontDescription, decltype(& pango_font_description_free)> font_desc(
        create_font_description(), &pango_font_description_free);

    static std::once_flag once_flag;
    std::call_once(once_flag, [&]
    {
        title_font.set_callback([&]
        {
            font_desc.reset(create_font_description());
        });
    });

    return font_desc.get();
}

/** @return The available height for displaying the title */
int pixdecor_theme_t::get_font_height_px()
{
    PangoFontDescription *font_desc = get_font_description();
    int font_height = pango_font_description_get_size(font_desc);

    if (!pango_font_description_get_size_is_absolute(font_desc))
    {
        font_height *= 4;
        font_height /= 3;
    }

    return font_height / PANGO_SCALE;
}

int pixdecor_theme_t::get_title_height()
{
    const int height = geometry::resolve_geometry({
                .font_height = get_font_height_px(),
                .requested_button_size  = button_size,
                .requested_title_height = title_height,
                .title_height_extension = (maximized && !maximized_borders) ? border_size : 0,
                .button_y_offset = button_y_offset,
                .output_scale    = 1.0,
                .svg_proportions = geometry::full_box_svg_proportions(),
            }).title_height;

    return titlebar ? height : 0;
}

geometry::logical_bounds_t pixdecor_theme_t::get_button_bounds()
{
    return geometry::resolve_geometry({
                .font_height = get_font_height_px(),
                .requested_button_size  = button_size,
                .requested_title_height = title_height,
                .title_height_extension = (maximized && !maximized_borders) ? border_size : 0,
                .button_y_offset = button_y_offset,
                .output_scale    = 1.0,
                .svg_proportions = geometry::full_box_svg_proportions(),
            }).button_bounds;
}

geometry::svg_proportions_t pixdecor_theme_t::get_svg_proportions() const
{
    return geometry::resolve_svg_proportions(geometry::full_box_svg_proportions());
}

std::uint64_t pixdecor_theme_t::get_generation() const
{
    return generation;
}

namespace
{
geometry::rgba_t to_rgba(const wf::color_t& colour)
{
    return {colour.r, colour.g, colour.b, colour.a};
}
}

button_prepare_config_t pixdecor_theme_t::get_button_prepare_config(
    geometry::logical_size_t logical_size, double output_scale) const
{
    return {
        .logical_size     = logical_size,
        .svg_proportions  = get_svg_proportions(),
        .output_scale     = output_scale,
        .line_thickness   = button_line_thickness,
        .theme_generation = generation,
        .palette    = {
            .active = to_rgba(wf::color_t(button_color)),
            .inactive = to_rgba(wf::color_t(button_inactive_color)),
            .hover    = to_rgba(wf::color_t(button_hover_color)),
            .pressed  = to_rgba(wf::color_t(button_pressed_color)),
        },
    };
}

bool pixdecor_theme_t::prepare_buttons(double output_scale)
{
    const auto bounds = get_button_bounds();
    const auto config = get_button_prepare_config(
        {bounds.width, bounds.height}, output_scale);
    buttons_prepared = button_renderer.prepare(config);
    if (buttons_prepared)
    {
        prepared_button_config = config;
    }

    return buttons_prepared;
}

const uploaded_button_texture_t*pixdecor_theme_t::get_prepared_fallback(
    const geometry::button_state_t& state, const button_prepare_config_t& requested) const
{
    auto fallback_state = state;
    fallback_state.interaction = geometry::interaction_state_t::normal;
    if (auto texture = button_renderer.lookup(fallback_state, requested))
    {
        return texture;
    }

    if (!buttons_prepared)
    {
        return nullptr;
    }

    if (auto texture = button_renderer.lookup(fallback_state, prepared_button_config))
    {
        return texture;
    }

    fallback_state = {};
    return button_renderer.lookup(fallback_state, prepared_button_config);
}

const wf::owned_texture_t*pixdecor_theme_t::get_button_texture(
    const geometry::button_state_t& state, geometry::logical_size_t logical_size,
    double output_scale, const std::function<void()>& damage_callback)
{
    const auto requested = get_button_prepare_config(logical_size, output_scale);
    auto texture = button_renderer.lookup(state, requested);
    const auto cache_decision = geometry::resolve_button_frame_cache_decision(texture != nullptr,
        [this, &requested, &damage_callback] (auto prepare)
    {
        auto pending = std::find_if(pending_button_prepares.begin(), pending_button_prepares.end(),
            [&requested] (const auto& item)
        {
            return same_button_prepare_config(item.config, requested);
        });
        if (pending == pending_button_prepares.end())
        {
            pending_button_prepares.push_back({requested, {}});
            pending = std::prev(pending_button_prepares.end());
        }

        pending->damage_callbacks.push_back(damage_callback);
        idle_prepare_buttons.run_once(std::move(prepare));
    }, [this]
    {
        auto pending = std::move(pending_button_prepares);
        pending_button_prepares.clear();
        for (auto& item : pending)
        {
            const bool prepared = button_renderer.prepare(item.config);
            if (prepared)
            {
                prepared_button_config = item.config;
                buttons_prepared = true;
                for (const auto& damage : item.damage_callbacks)
                {
                    damage();
                }
            }
        }
    });
    if (cache_decision == geometry::button_frame_cache_decision_t::schedule_idle_prepare)
    {
        texture = get_prepared_fallback(state, requested);
    }

    return texture ? texture->wayfire_texture() : nullptr;
}

/** @return The available border for resizing */
int pixdecor_theme_t::get_border_size() const
{
    return (!maximized_borders && maximized) ? 0 : border_size;
}

/** @return The input area for resizing */
int pixdecor_theme_t::get_input_size() const
{
    return std::max(get_border_size(), MIN_RESIZE_HANDLE_SIZE);
}

wf::color_t pixdecor_theme_t::get_decor_color(bool active) const
{
    return active ? fg : bg;
}

void pixdecor_theme_t::set_maximize(bool state)
{
    maximized = state;
}

/**
 * Fill the given rectangle with the background color(s).
 *
 * @param rectangle The rectangle to redraw.
 * @param active Whether to use active or inactive colors
 */
void pixdecor_theme_t::render_background(const wf::scene::render_instruction_t& data,
    wf::geometry_t rectangle, bool active, bool tiled)
{
    background_cache_key_t key;
    key.dimensions = {
        int(std::round(rectangle.width * data.target.scale)),
        int(std::round(rectangle.height * data.target.scale)),
    };
    key.active = active;
    key.color  = get_decor_color(active);
    key.radius = std::clamp(
        int(std::round(int(rounded_corner_radius) * data.target.scale)), 0,
        std::min(key.dimensions.width, key.dimensions.height) / 2);
    key.tiled = tiled;

    if ((key.dimensions.width <= 0) || (key.dimensions.height <= 0))
    {
        return;
    }

    // Avoid repeated Cairo drawing and texture uploads while the background properties stay unchanged.
    if (!background_key_matches(key))
    {
        update_background_texture(key);
    }

    OpenGL::render_texture(wf::gles_texture_t{background_texture.get_texture()}, data.target, rectangle,
        glm::vec4(1.0f), OpenGL::RENDER_FLAG_CACHED);

    data.pass->custom_gles_subpass(data.target, [&]
    {
        for (auto& box : data.damage)
        {
            wf::gles::render_target_logic_scissor(data.target, wlr_box_from_pixman_box(box));
            OpenGL::draw_cached();
        }
    });

    OpenGL::clear_cached();
}

bool pixdecor_theme_t::background_key_matches(const background_cache_key_t& key) const
{
    return background_texture_valid &&
           (background_cache_key.dimensions.width == key.dimensions.width) &&
           (background_cache_key.dimensions.height == key.dimensions.height) &&
           (background_cache_key.active == key.active) &&
           (background_cache_key.color.r == key.color.r) &&
           (background_cache_key.color.g == key.color.g) &&
           (background_cache_key.color.b == key.color.b) &&
           (background_cache_key.color.a == key.color.a) &&
           (background_cache_key.radius == key.radius) &&
           (background_cache_key.tiled == key.tiled);
}

void pixdecor_theme_t::update_background_texture(const background_cache_key_t& key)
{
    auto surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, key.dimensions.width, key.dimensions.height);
    auto cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    if (key.tiled || (key.radius == 0))
    {
        cairo_rectangle(cr, 0, 0, key.dimensions.width, key.dimensions.height);
    } else
    {
        constexpr double PI = 3.14159265358979323846;
        double radius = key.radius;
        double width  = key.dimensions.width;
        double height = key.dimensions.height;

        cairo_new_sub_path(cr);
        cairo_arc(cr, width - radius, radius, radius, -PI / 2.0, 0);
        cairo_arc(cr, width - radius, height - radius, radius, 0, PI / 2.0);
        cairo_arc(cr, radius, height - radius, radius, PI / 2.0, PI);
        cairo_arc(cr, radius, radius, radius, PI, 3.0 * PI / 2.0);
        cairo_close_path(cr);
    }

    cairo_set_source_rgba(cr, key.color.r, key.color.g, key.color.b, key.color.a);
    cairo_fill(cr);
    cairo_surface_flush(surface);

    background_texture   = wf::owned_texture_t{surface};
    background_cache_key = key;
    background_texture_valid = true;

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

/**
 * Render the given text on a cairo_surface_t with the given size. The caller is responsible for freeing the
 * memory afterwards.
 */
cairo_surface_t*pixdecor_theme_t::render_text(std::string text,
    int width, int height, int t_width, int border, int buttons_width, bool active)
{
    const auto format = CAIRO_FORMAT_ARGB32;
    auto surface = cairo_image_surface_create(format, width, height);

    if (height == 0)
    {
        return surface;
    }

    auto cr = cairo_create(surface);

    PangoFontDescription *font_desc;
    PangoLayout *layout;
    int x, w, h;

    // render text
    font_desc = get_font_description();

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), text.size());
    cairo_set_source_rgba(cr, active ? fg_text.r : bg_text.r, active ? fg_text.g : bg_text.g,
        active ? fg_text.b : bg_text.b, 1);
    pango_layout_get_pixel_size(layout, &w, &h);
    switch (int(title_text_align))
    {
      // left
      case 0:
        x = border;
        break;

      // right
      case 2:
        x = t_width - (w + buttons_width + border);
        break;

      // center
      case 1:
      default:
        x = (t_width - w) / 2;
        break;
    }

    cairo_translate(cr, x, (height - h) / 2);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    cairo_destroy(cr);

    return surface;
}
}
}
