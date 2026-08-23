#include "deco-button.hpp"
#include "deco-theme.hpp"
#include <wayfire/opengl.hpp>

#define NORMAL   1.0
#define HOVERED  0.0

namespace wf
{
namespace pixdecor
{
button_t::button_t(pixdecor_theme_t& t, std::function<void()> damage) :
    theme(t), damage_callback(damage)
{}

geometry::logical_bounds_t button_t::set_button_type(button_type_t type)
{
    this->type     = type;
    this->type_set = true;
    this->hover.animate(NORMAL, NORMAL);
    add_idle_damage();

    return theme.get_button_bounds();
}

button_type_t button_t::get_button_type() const
{
    return this->type;
}

void button_t::set_hover(bool is_hovered)
{
    this->is_hovered = is_hovered;
    if (!this->is_pressed)
    {
        if (is_hovered)
        {
            this->hover.animate(HOVERED);
        } else
        {
            this->hover.animate(NORMAL);
        }
    }

    add_idle_damage();
}

/**
 * Set whether the button is pressed or not. Affects appearance.
 */
void button_t::set_pressed(bool is_pressed)
{
    this->is_pressed = is_pressed;
    if (is_pressed)
    {
        this->hover.animate(NORMAL);
    } else
    {
        this->hover.animate(is_hovered ? HOVERED : NORMAL);
    }

    add_idle_damage();
}

void button_t::render(const wf::scene::render_instruction_t& data, wf::geometry_t button_geometry,
    bool active, bool maximized)
{
    if (!type_set || (button_geometry.width <= 0) || (button_geometry.height <= 0))
    {
        return;
    }

    if (this->hover.running())
    {
        add_idle_damage();
    }

    geometry::button_state_t state;
    switch (type)
    {
      case BUTTON_CLOSE:
        state.kind = geometry::button_kind_t::close;
        break;

      case BUTTON_TOGGLE_MAXIMIZE:
        state.kind = geometry::button_kind_t::maximize;
        break;

      case BUTTON_MINIMIZE:
        state.kind = geometry::button_kind_t::minimize;
        break;
    }

    state.focus    = active ? geometry::focus_state_t::active : geometry::focus_state_t::inactive;
    state.maximize = maximized ? geometry::maximize_state_t::restore :
        geometry::maximize_state_t::maximize;

    state.interaction = is_pressed ? geometry::interaction_state_t::pressed :
        geometry::interaction_state_t::normal;
    const geometry::logical_size_t logical_size = {button_geometry.width, button_geometry.height};
    const auto normal_texture = theme.get_button_texture(state, logical_size, data.target.scale);

    state.interaction = geometry::interaction_state_t::hover;
    const auto hovered_texture = theme.get_button_texture(state, logical_size, data.target.scale);
    if (!normal_texture || !hovered_texture)
    {
        add_idle_damage();
        return;
    }

    OpenGL::render_texture(wf::gles_texture_t{normal_texture->get_texture()}, data.target, button_geometry,
        {1, 1, 1, this->hover},
        OpenGL::RENDER_FLAG_CACHED);
    data.pass->custom_gles_subpass(data.target, [&]
    {
        wf::gles::for_each_scissor_rect(data.target, data.damage, [&]
        {
            OpenGL::draw_cached();
        });
    });
    OpenGL::clear_cached();

    OpenGL::render_texture(wf::gles_texture_t{hovered_texture->get_texture()}, data.target,
        button_geometry,
        {1, 1, 1, 1.0 - this->hover},
        OpenGL::RENDER_FLAG_CACHED);
    data.pass->custom_gles_subpass(data.target, [&]
    {
        wf::gles::for_each_scissor_rect(data.target, data.damage, [&]
        {
            OpenGL::draw_cached();
        });
    });
    OpenGL::clear_cached();
}

void button_t::add_idle_damage()
{
    this->idle_damage.run_once([=] ()
    {
        this->damage_callback();
    });
}
}
}
