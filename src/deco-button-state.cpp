#include "deco-button-state.hpp"
#include "deco-button.hpp"

#include <algorithm>
#include <utility>
#include <wayfire/util/duration.hpp>

namespace wf
{
namespace pixdecor
{
void button_render_plan_t::set_texture_availability(bool normal_available, bool hover_available)
{
    needs_redraw = needs_redraw || !normal_available || !hover_available;
}

button_transition_t button_state_model_t::set_hover(bool hovered)
{
    is_hovered = hovered;
    return {
        (is_pressed || !hovered) ? 1.0 : 0.0,
        !is_pressed,
        true,
    };
}

button_transition_t button_state_model_t::set_pressed(bool pressed)
{
    is_pressed = pressed;
    return {
        (pressed || !is_hovered) ? 1.0 : 0.0,
        true,
        true,
    };
}

button_render_plan_t button_state_model_t::render_plan(button_type_t type, bool active,
    bool maximized, double normal_alpha, bool transition_running) const
{
    geometry::button_state_t normal_state;
    switch (type)
    {
      case BUTTON_CLOSE:
        normal_state.kind = geometry::button_kind_t::close;
        break;

      case BUTTON_TOGGLE_MAXIMIZE:
        normal_state.kind = geometry::button_kind_t::maximize;
        break;

      case BUTTON_MINIMIZE:
        normal_state.kind = geometry::button_kind_t::minimize;
        break;
    }

    normal_state.focus = active ? geometry::focus_state_t::active :
        geometry::focus_state_t::inactive;
    normal_state.maximize = maximized ? geometry::maximize_state_t::restore :
        geometry::maximize_state_t::maximize;
    normal_state.interaction = is_pressed ? geometry::interaction_state_t::pressed :
        geometry::interaction_state_t::normal;

    auto hover_state = normal_state;
    hover_state.interaction = geometry::interaction_state_t::hover;

    const double resolved_normal_alpha = std::clamp(normal_alpha, 0.0, 1.0);
    return {
        normal_state,
        hover_state,
        resolved_normal_alpha,
        1.0 - resolved_normal_alpha,
        transition_running,
    };
}

class button_animation_t::impl
{
  public:
    wf::animation::simple_animation_t animation;

    explicit impl(std::shared_ptr<wf::config::option_t<int>> duration) :
        animation(std::move(duration))
    {}
};

button_animation_t::button_animation_t(duration_loader_t load_duration) :
    priv(std::make_unique<impl>(load_duration(duration_option_name())))
{}

button_animation_t::~button_animation_t() = default;
button_animation_t::button_animation_t(button_animation_t&&) noexcept = default;
button_animation_t& button_animation_t::operator =(button_animation_t&&) noexcept = default;

void button_animation_t::animate(double end)
{
    priv->animation.animate(end);
}

void button_animation_t::animate(double start, double end)
{
    priv->animation.animate(start, end);
}

bool button_animation_t::running()
{
    return priv->animation.running();
}

double button_animation_t::value() const
{
    return priv->animation;
}

const std::string& button_animation_t::duration_option_name()
{
    static const std::string name = "vecdecor/button_hover_duration";
    return name;
}

button_t::button_t(button_runtime_t runtime, std::function<void()> damage) :
    damage_callback(std::move(damage)),
    get_button_bounds(std::move(runtime.get_button_bounds)),
    schedule_redraw(std::move(runtime.schedule_redraw)),
    hover(runtime.hover_animation ? std::move(runtime.hover_animation) :
        std::make_unique<button_animation_t>(std::move(runtime.load_hover_duration)))
{}

geometry::logical_bounds_t button_t::set_button_type(button_type_t type)
{
    this->type     = type;
    this->type_set = true;
    hover->animate(1.0, 1.0);
    schedule_redraw();
    return get_button_bounds();
}

button_type_t button_t::get_button_type() const
{
    return type;
}

void button_t::set_hover(bool is_hovered)
{
    const auto transition = state_model.set_hover(is_hovered);
    if (transition.animate)
    {
        hover->animate(transition.target_normal_alpha);
    }

    if (transition.needs_redraw)
    {
        schedule_redraw();
    }
}

void button_t::set_pressed(bool is_pressed)
{
    const auto transition = state_model.set_pressed(is_pressed);
    if (transition.animate)
    {
        hover->animate(transition.target_normal_alpha);
    }

    if (transition.needs_redraw)
    {
        schedule_redraw();
    }
}

void button_t::render(button_render_backend_t& backend,
    geometry::logical_size_t logical_size, bool active, bool maximized)
{
    if (!type_set || (logical_size.width <= 0) || (logical_size.height <= 0))
    {
        return;
    }

    const bool transition_running = hover->running();
    auto plan = state_model.render_plan(type, active, maximized, hover->value(),
        transition_running);
    const bool textures_available = request_button_textures(plan,
        [&] (button_texture_slot_t slot, const geometry::button_state_t& state)
    {
        return backend.request_texture(slot, state, logical_size);
    }, schedule_redraw);

    if (!textures_available)
    {
        return;
    }

    backend.draw_texture(button_texture_slot_t::normal, plan.normal_alpha);
    backend.draw_texture(button_texture_slot_t::hover, plan.hover_alpha);
}
}
}
