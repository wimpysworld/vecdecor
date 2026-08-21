#include "deco-theme.hpp"
#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdlib.h>

namespace wf
{
namespace pixdecor
{
wf::option_wrapper_t<int> border_size{"vecdecor/border_size"};
wf::option_wrapper_t<std::string> titlebar{"vecdecor/titlebar"};
wf::option_wrapper_t<wf::color_t> fg_color{"vecdecor/fg_color"};
wf::option_wrapper_t<wf::color_t> bg_color{"vecdecor/bg_color"};
wf::option_wrapper_t<wf::color_t> fg_text_color{"vecdecor/fg_text_color"};
wf::option_wrapper_t<wf::color_t> bg_text_color{"vecdecor/bg_text_color"};
wf::option_wrapper_t<std::string> button_minimize_svg{"vecdecor/button_minimize_svg"};
wf::option_wrapper_t<std::string> button_maximize_svg{"vecdecor/button_maximize_svg"};
wf::option_wrapper_t<std::string> button_restore_svg{"vecdecor/button_restore_svg"};
wf::option_wrapper_t<std::string> button_close_svg{"vecdecor/button_close_svg"};
wf::option_wrapper_t<int> button_size{"vecdecor/button_size"};
wf::option_wrapper_t<int> title_height{"vecdecor/title_height"};
wf::option_wrapper_t<int> button_y_offset{"vecdecor/button_y_offset"};
wf::option_wrapper_t<wf::color_t> button_color{"vecdecor/button_color"};
wf::option_wrapper_t<wf::color_t> button_inactive_color{"vecdecor/button_inactive_color"};
wf::option_wrapper_t<wf::color_t> button_hover_color{"vecdecor/button_hover_color"};
wf::option_wrapper_t<wf::color_t> button_pressed_color{"vecdecor/button_pressed_color"};
wf::option_wrapper_t<double> button_line_thickness{"vecdecor/button_line_thickness"};
/** Create a new theme with the default parameters */
pixdecor_theme_t::pixdecor_theme_t() : font_description(nullptr, pango_font_description_free)
{
    // read initial colours
    update_colors();
}

pixdecor_theme_t::~pixdecor_theme_t()
{}

void pixdecor_theme_t::update_colors(void)
{
    fg = wf::color_t(fg_color);
    bg = wf::color_t(bg_color);
    fg_text = wf::color_t(fg_text_color);
    bg_text = wf::color_t(bg_text_color);
    ++generation;
}

std::unique_ptr<PangoFontDescription,
    decltype(& pango_font_description_free)> pixdecor_theme_t::get_font_description()
{
    font_description.reset(pango_font_description_from_string(title_font.value().c_str()));
    return std::move(font_description);
}

/** @return The available height for displaying the title */
int pixdecor_theme_t::get_font_height_px()
{
    auto font_desc  = get_font_description();
    int font_height = pango_font_description_get_size(font_desc.get());

    if (!pango_font_description_get_size_is_absolute(font_desc.get()))
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

    return ((std::string(titlebar) == "always" ||
        (std::string(titlebar) == "windowed" && !maximized) ||
        (std::string(titlebar) == "maximized" && maximized)) &&
        (std::string(titlebar) !=
            "never")) ? height : 0;
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
            wf::gles::render_target_logic_scissor(data.target, box);
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

    PangoLayout *layout;
    int x, w, h;

    // render text
    auto font_desc = get_font_description();

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc.get());
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

static cairo_surface_t *get_cairo_surface(const geometry::button_cache_key_t& key)
{
    const int w  = key.raster_size.width;
    const int h  = key.raster_size.height;
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);

    auto cr = cairo_create(surface);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /** Draw the button  */
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const float alpha = key.state.interaction == geometry::interaction_state_t::hover ? 0.25 : 1.0;
    cairo_set_source_rgba(cr,
        wf::color_t(button_color).r,
        wf::color_t(button_color).g,
        wf::color_t(button_color).b,
        wf::color_t(button_color).a * alpha);
    const auto& proportions = key.svg_proportions;
    const double x = proportions.x * w;
    const double y = proportions.y * h;
    const double icon_width  = proportions.width * w;
    const double icon_height = proportions.height * h;
    const double line_width  = double(button_line_thickness) * key.output_scale;
    switch (key.state.kind)
    {
      case geometry::button_kind_t::close:
        cairo_set_line_width(cr, line_width);
        cairo_move_to(cr, x + icon_width / 4.0, y + icon_height / 4.0);
        cairo_line_to(cr, x + 3.0 * icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_move_to(cr, x + 3.0 * icon_width / 4.0, y + icon_height / 4.0);
        cairo_line_to(cr, x + icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_stroke(cr);
        break;

      case geometry::button_kind_t::maximize:
        cairo_set_line_width(cr, line_width);
        cairo_rectangle(cr, x + icon_width / 4.0, y + icon_height / 4.0,
            icon_width / 2.0, icon_height / 2.0);
        cairo_stroke(cr);
        break;

      case geometry::button_kind_t::minimize:
        cairo_set_line_width(cr, line_width);
        cairo_move_to(cr, x + icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_line_to(cr, x + 3.0 * icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_stroke(cr);
        break;
    }

    cairo_destroy(cr);

    return surface;
}

cairo_surface_t*pixdecor_theme_t::get_button_surface(const geometry::button_cache_key_t& key) const
{
    std::string button_svg;
    switch (key.state.kind)
    {
      case geometry::button_kind_t::close:
        button_svg = button_close_svg;
        break;

      case geometry::button_kind_t::maximize:
        button_svg = key.state.maximize == geometry::maximize_state_t::restore ?
            std::string(button_restore_svg) : std::string(button_maximize_svg);
        break;

      case geometry::button_kind_t::minimize:
        button_svg = button_minimize_svg;
        break;
    }

    (void)button_svg;
    return get_cairo_surface(key);
}
}
}
