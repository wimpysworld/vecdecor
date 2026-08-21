#pragma once
#include "deco-geometry.hpp"

#include <cairo.h>
#include <cstdint>
#include <gio/gio.h>
#include <pango/pangocairo.h>
#include <string>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>

#define MIN_RESIZE_HANDLE_SIZE 5

namespace wf
{
namespace pixdecor
{
/**
 * A  class which manages the outlook of decorations. It is responsible for determining the background colors,
 * sizes, etc.
 */
class pixdecor_theme_t
{
  public:
    wf::option_wrapper_t<std::string> title_font{"vecdecor/title_font"};
    wf::option_wrapper_t<int> rounded_corner_radius{"vecdecor/rounded_corner_radius"};
    wf::option_wrapper_t<bool> maximized_borders{"vecdecor/maximized_borders"};
    wf::option_wrapper_t<int> title_text_align{"vecdecor/title_text_align"};
    /** Create a new theme with the default parameters */
    pixdecor_theme_t();
    ~pixdecor_theme_t();

    /** @return The height of the system font in pixels */
    int get_font_height_px();
    /** @return The available height for displaying the title */
    int get_title_height();
    /** @return The logical bounds for a button in the titlebar */
    geometry::logical_bounds_t get_button_bounds();
    /** @return The proportions for the button icon view box */
    geometry::svg_proportions_t get_svg_proportions() const;
    /** @return The current theme generation */
    std::uint64_t get_generation() const;
    /** @return The available border for rendering */
    int get_border_size() const;
    /** @return The available border for resizing */
    int get_input_size() const;
    /** @return The decoration color */
    wf::color_t get_decor_color(bool active) const;
    std::unique_ptr<PangoFontDescription, decltype(& pango_font_description_free)> get_font_description();

    void update_colors(void);

    /**
     * Fill the given rectangle with the background color(s).
     *
     * @param rectangle The rectangle to redraw.
     * @param active Whether to use active or inactive colors
     * @param tiled Whether the view has any tiled edges
     */
    void render_background(const wf::scene::render_instruction_t& data,
        wf::geometry_t rectangle, bool active, bool tiled);

    /**
     * Render the given text on a cairo_surface_t with the given size. The caller is responsible for freeing
     * the memory afterwards.
     */
    cairo_surface_t *render_text(std::string text, int width, int height, int t_width, int border,
        int buttons_width, bool active);

    /**
     * Get the icon for the given button. The caller is responsible for freeing the memory afterwards.
     *
     * @param key The resolved button texture cache key.
     */
    cairo_surface_t *get_button_surface(const geometry::button_cache_key_t& key) const;

    void set_maximize(bool state);

  private:

    struct background_cache_key_t
    {
        wf::dimensions_t dimensions = {0, 0};
        bool active = false;
        wf::color_t color;
        int radius = 0;
        bool tiled = false;
    };

    bool background_key_matches(const background_cache_key_t& key) const;
    void update_background_texture(const background_cache_key_t& key);

    wf::color_t fg;
    wf::color_t bg;
    wf::color_t fg_text;
    wf::color_t bg_text;
    bool maximized = false;
    std::uint64_t generation = 0;
    background_cache_key_t background_cache_key;
    wf::owned_texture_t background_texture;
    bool background_texture_valid = false;
};
}
}
