#include "deco-button.hpp"
#include "deco-theme.hpp"
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

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

    geometry::cache_key_input_t key_input;
    key_input.state = state;
    key_input.logical_size = {
        static_cast<int>(button_geometry.width),
        static_cast<int>(button_geometry.height),
    };
    key_input.svg_proportions  = theme.get_svg_proportions();
    key_input.output_scale     = data.target.scale;
    key_input.theme_generation = theme.get_generation();

    key_input.state.interaction = is_pressed ? geometry::interaction_state_t::pressed :
        geometry::interaction_state_t::normal;
    const auto normal_key = geometry::resolve_cache_key(key_input);
    update_texture(normal_key, button_texture, button_texture_key);

    auto hovered_key = normal_key;
    hovered_key.state.interaction = geometry::interaction_state_t::hover;
    update_texture(hovered_key, button_texture_hovered, button_texture_hovered_key);

    OpenGL::render_texture(wf::gles_texture_t{button_texture.get_texture()}, data.target, button_geometry,
        {1, 1, 1, this->hover},
        OpenGL::RENDER_FLAG_CACHED);
    data.pass->custom_gles_subpass(data.target, [&]
    {
        for (auto& box : data.damage)
        {
            wf::gles::render_target_logic_scissor(data.target, box);
            OpenGL::draw_cached();
        }
    });
    OpenGL::clear_cached();

    OpenGL::render_texture(wf::gles_texture_t{button_texture_hovered.get_texture()}, data.target,
        button_geometry,
        {1, 1, 1, 1.0 - this->hover},
        OpenGL::RENDER_FLAG_CACHED);
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

void button_t::update_texture(const geometry::button_cache_key_t& key,
    wf::owned_texture_t& texture,
    std::optional<geometry::button_cache_key_t>& texture_key)
{
    if (texture_key && (*texture_key == key))
    {
        return;
    }

    auto surface = theme.get_button_surface(key);
    wf::gles::run_in_context([&]
    {
        texture = owned_texture_t{surface};
    });

    cairo_surface_destroy(surface);
    texture_key = key;
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
