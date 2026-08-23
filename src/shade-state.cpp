#include "shade-state.hpp"

#include <algorithm>
#include <utility>

namespace wf
{
namespace pixdecor
{
shade_control_plan_t shade_state_model_t::plan_control(bool enabled, bool shade,
    bool view_mapped, bool transformer_present)
{
    if (!enabled)
    {
        return {};
    }

    if (shade)
    {
        return {view_mapped && !transformer_present, view_mapped};
    }

    return {false, transformer_present};
}

shade_transition_plan_t shade_state_model_t::request(bool shade,
    bool transition_running, bool transition_direction)
{
    if (!needs_transition(shade))
    {
        return {};
    }

    last_direction = shade;
    if (transition_running)
    {
        return {true, false};
    }

    return {shade != transition_direction, true};
}

shade_frame_plan_t shade_state_model_t::frame(double transition_progress,
    bool transition_running)
{
    current_progress = std::clamp(transition_progress, 0.0, 1.0);
    return {
        current_progress,
        transition_running,
        !transition_running,
        !transition_running && !last_direction,
    };
}

bool shade_state_model_t::needs_transition(bool shade) const
{
    return shade != last_direction;
}

bool shade_state_model_t::direction() const
{
    return last_direction;
}

double shade_state_model_t::progress() const
{
    return current_progress;
}

shade_production_adapter_t::shade_production_adapter_t(
    shade_state_model_t& state, shade_animation_backend_t& animation,
    shade_runtime_actions_t actions) :
    animation(animation), actions(std::move(actions)), state(state)
{}

void shade_production_adapter_t::init_animation(bool shade)
{
    if (!state.needs_transition(shade))
    {
        return;
    }

    const auto transition = state.request(shade, animation.running(),
        animation.direction());
    if (transition.reverse)
    {
        animation.reverse();
    }

    if (transition.start)
    {
        animation.start();
    }

    if ((transition.reverse || transition.start) && actions.add_frame_callback)
    {
        actions.add_frame_callback();
    }
}

shade_frame_plan_t shade_production_adapter_t::frame(
    const std::function<void(const shade_frame_plan_t&)>& before_actions)
{
    const auto result = state.frame(animation.progress(), animation.running());
    if (before_actions)
    {
        before_actions(result);
    }

    if (result.needs_damage && actions.damage)
    {
        actions.damage();
    }

    if (result.remove_frame_callback && actions.remove_frame_callback)
    {
        actions.remove_frame_callback();
    }

    if (result.remove_transformer && actions.remove_transformer)
    {
        actions.remove_transformer();
    }

    return result;
}

bool shade_production_adapter_t::direction() const
{
    return state.direction();
}

double shade_production_adapter_t::progress() const
{
    return state.progress();
}

void apply_shade_control(bool enabled, bool shade, bool view_mapped,
    bool transformer_present, const shade_control_actions_t& actions)
{
    const auto plan = shade_state_model_t::plan_control(enabled, shade,
        view_mapped, transformer_present);
    if (plan.ensure_transformer && actions.ensure_transformer)
    {
        actions.ensure_transformer();
    }

    if (plan.animate && actions.animate)
    {
        actions.animate();
    }
}
}
}
