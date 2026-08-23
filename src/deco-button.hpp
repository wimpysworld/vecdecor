#pragma once

#include "deco-button-state.hpp"

#include <functional>
#include <memory>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

namespace wf
{
namespace pixdecor
{
class pixdecor_theme_t;

class button_render_backend_t
{
  public:
    virtual ~button_render_backend_t() = default;

    virtual bool request_texture(button_texture_slot_t slot,
        const geometry::button_state_t& state, geometry::logical_size_t logical_size) = 0;
    virtual void draw_texture(button_texture_slot_t slot, double alpha) = 0;
};

struct button_runtime_t
{
    button_animation_t::duration_loader_t load_hover_duration;
    std::function<geometry::logical_bounds_t()> get_button_bounds;
    std::function<void()> schedule_redraw;
    std::unique_ptr<button_animation_backend_t> hover_animation;
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

    button_t(button_runtime_t runtime,
        std::function<void()> damage_callback = {});

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

    void render(button_render_backend_t& backend, geometry::logical_size_t logical_size,
        bool active, bool maximized);

    std::function<void()> damage_callback;

  private:
    pixdecor_theme_t *theme = nullptr;
    std::function<geometry::logical_bounds_t()> get_button_bounds;
    std::function<void()> schedule_redraw;
    button_type_t type;
    bool type_set = false;

    button_state_model_t state_model;
    std::unique_ptr<button_animation_backend_t> hover;
};
}
}
