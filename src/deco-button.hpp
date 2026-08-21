#pragma once

#include "deco-geometry.hpp"

#include <functional>
#include <optional>
#include <wayfire/util.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

namespace wf
{
namespace pixdecor
{
class pixdecor_theme_t;

enum button_type_t
{
    BUTTON_CLOSE,
    BUTTON_TOGGLE_MAXIMIZE,
    BUTTON_MINIMIZE,
};

class button_t
{
  public:
    /**
     * Create a new button with the given theme.
     * @param theme  The theme to use.
     * @param damage_callback   A callback to execute when the button needs a repaint. Damage won't be
     * reported while render() is being called.
     */
    button_t(pixdecor_theme_t& theme,
        std::function<void()> damage_callback);

    /**
     * Set the type of the button. This will affect the displayed icon and potentially other appearance like
     * colors.
     */
    geometry::logical_bounds_t set_button_type(button_type_t type);

    /** @return The type of the button */
    button_type_t get_button_type() const;

    /**
     * Set the button hover state. Affects appearance.
     */
    void set_hover(bool is_hovered);

    /**
     * Set whether the button is pressed or not. Affects appearance.
     */
    void set_pressed(bool is_pressed);

    /**
     * Render the button on the given framebuffer at the given coordinates. Precondition: set_button_type()
     * has been called, otherwise result is no-op
     *
     * @param data The render instruction.
     * @param button_geometry The button bounds in logical coordinates.
     * @param active Whether the view has focus.
     * @param maximized Whether the button selects restore.
     */
    void render(const wf::scene::render_instruction_t& data, wf::geometry_t button_geometry,
        bool active, bool maximized);

    pixdecor_theme_t& theme;
    std::function<void()> damage_callback;

  private:

    wf::option_wrapper_t<int> button_hover_duration{"vecdecor/button_hover_duration"};
    button_type_t type;
    wf::owned_texture_t button_texture;
    wf::owned_texture_t button_texture_hovered;
    std::optional<geometry::button_cache_key_t> button_texture_key;
    std::optional<geometry::button_cache_key_t> button_texture_hovered_key;
    bool type_set = false;

    /* Whether the button is currently being hovered */
    bool is_hovered = false;
    /* Whether the button is currently being held */
    bool is_pressed = false;
    /* The shade of button background to use. */
    wf::animation::simple_animation_t hover{button_hover_duration};

    wf::wl_idle_call idle_damage;
    /** Damage button the next time the main loop goes idle */
    void add_idle_damage();

    /**
     * Redraw the button surface and store it as a texture
     */
    void update_texture(const geometry::button_cache_key_t& key,
        wf::owned_texture_t& texture,
        std::optional<geometry::button_cache_key_t>& texture_key);
};
}
}
