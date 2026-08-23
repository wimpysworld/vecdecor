#include "deco-layout-model.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace wf
{
namespace pixdecor
{
namespace
{
std::vector<button_type_t> parse_buttons(const std::string& layout)
{
    std::stringstream stream(layout);
    std::vector<button_type_t> buttons;
    std::string button_name;
    while (stream >> button_name)
    {
        if (button_name == "minimize")
        {
            buttons.push_back(BUTTON_MINIMIZE);
        } else if (button_name == "maximize")
        {
            buttons.push_back(BUTTON_TOGGLE_MAXIMIZE);
        } else if (button_name == "close")
        {
            buttons.push_back(BUTTON_CLOSE);
        }
    }

    return buttons;
}

decoration_layout_action_t button_action(button_type_t button)
{
    switch (button)
    {
      case BUTTON_CLOSE:
        return DECORATION_ACTION_CLOSE;

      case BUTTON_TOGGLE_MAXIMIZE:
        return DECORATION_ACTION_TOGGLE_MAXIMIZE;

      case BUTTON_MINIMIZE:
        return DECORATION_ACTION_MINIMIZE;
    }

    return DECORATION_ACTION_NONE;
}
}

button_layout_t default_button_layout()
{
    return {{}, {BUTTON_CLOSE}};
}

button_layout_t parse_button_layout(const std::string& configured_layout)
{
    std::string layout = configured_layout;
    std::replace(layout.begin(), layout.end(), ',', ' ');
    const auto first_separator = layout.find(':');
    const auto last_separator  = layout.rfind(':');

    button_layout_t result;
    result.left  = parse_buttons(layout.substr(0, first_separator));
    result.right = parse_buttons(last_separator == std::string::npos ?
        std::string{} : layout.substr(last_separator + 1));
    if (result.left.empty() && result.right.empty())
    {
        return default_button_layout();
    }

    return result;
}

bool layout_bounds_t::contains(int px, int py) const
{
    return (width > 0) && (height > 0) && (px >= x) && (py >= y) &&
           (static_cast<std::int64_t>(px) < static_cast<std::int64_t>(x) + width) &&
           (static_cast<std::int64_t>(py) < static_cast<std::int64_t>(y) + height);
}

layout_input_adapter_t::layout_input_adapter_t(
    layout_input_adapter_dependencies_t dependencies) :
    dependencies(std::move(dependencies))
{}

void layout_input_adapter_t::dispatch(layout_input_response_t response)
{
    for (const auto& update : response.changed_buttons)
    {
        dependencies.update_button(update);
    }

    dependencies.handle_action(response);
}

void layout_input_adapter_t::motion(layout_input_point_t point)
{
    const auto local = dependencies.to_local(point);
    dispatch(dependencies.motion(local.x, local.y));
}

void layout_input_adapter_t::button(bool pressed)
{
    dispatch(dependencies.button(pressed));
}

void layout_input_adapter_t::focus_lost()
{
    dispatch(dependencies.focus_lost());
}

void layout_input_adapter_t::pointer_motion(layout_input_point_t point)
{
    motion(point);
}

void layout_input_adapter_t::pointer_button(bool pressed)
{
    button(pressed);
}

void layout_input_adapter_t::pointer_axis(int delta)
{
    dispatch(dependencies.axis(delta));
}

void layout_input_adapter_t::pointer_focus_lost()
{
    focus_lost();
}

void layout_input_adapter_t::touch_down(layout_input_point_t point)
{
    last_touch_point = point;
    touch_active     = true;
    motion(point);
    button(true);
}

void layout_input_adapter_t::touch_motion(layout_input_point_t point)
{
    last_touch_point = point;
    motion(point);
}

void layout_input_adapter_t::touch_up(layout_input_point_t point)
{
    const auto local = dependencies.to_local(point);
    if (!touch_active || (point.x != last_touch_point.x) ||
        (point.y != last_touch_point.y))
    {
        auto response = dependencies.motion(local.x, local.y);
        response.action = DECORATION_ACTION_NONE;
        dispatch(std::move(response));
    }

    button(false);
    focus_lost();
    touch_active = false;
}

layout_input_model_t::layout_input_model_t(layout_timer_t& timer) : timer(timer)
{}

void layout_input_model_t::set_targets(std::vector<layout_target_t> new_targets)
{
    targets = std::move(new_targets);
    buttons.clear();
    for (const auto& target : targets)
    {
        if (target.kind == layout_target_kind_t::button)
        {
            buttons.push_back({target.id, target.button, false, false});
        }
    }

    current_target = nullptr;
    grab_target    = nullptr;
    grabbed = false;
    double_click_pending    = false;
    double_click_at_release = false;
}

const layout_target_t*layout_input_model_t::find_target(int x, int y) const
{
    for (const auto& target : targets)
    {
        if (target.bounds.contains(x, y))
        {
            return &target;
        }
    }

    return nullptr;
}

layout_button_state_t*layout_input_model_t::find_button(std::size_t target_id)
{
    for (auto& button : buttons)
    {
        if (button.target_id == target_id)
        {
            return &button;
        }
    }

    return nullptr;
}

void layout_input_model_t::update_button(std::size_t target_id, bool hovered,
    bool pressed, layout_input_response_t& response)
{
    auto button = find_button(target_id);
    if (!button)
    {
        return;
    }

    const bool hover_changed   = button->hovered != hovered;
    const bool pressed_changed = button->pressed != pressed;
    if (!hover_changed && !pressed_changed)
    {
        return;
    }

    button->hovered = hovered;
    button->pressed = pressed;
    response.changed_buttons.push_back({*button, hover_changed, pressed_changed});
}

std::uint32_t layout_input_model_t::resize_edges_at(int x, int y) const
{
    std::uint32_t edges = 0;
    for (const auto& target : targets)
    {
        if ((target.kind == layout_target_kind_t::resize) && target.bounds.contains(x, y))
        {
            edges |= target.edges;
        }
    }

    return edges;
}

layout_input_response_t layout_input_model_t::motion(int x, int y)
{
    layout_input_response_t response;
    const auto previous_target = current_target;
    const auto next_target     = find_target(x, y);

    if ((previous_target == next_target) && grabbed && grab_target &&
        (grab_target->kind == layout_target_kind_t::move) && next_target &&
        (next_target->kind == layout_target_kind_t::move))
    {
        grabbed     = false;
        grab_target = nullptr;
        double_click_pending    = false;
        double_click_at_release = false;
        response.action = DECORATION_ACTION_MOVE;
    } else if (previous_target != next_target)
    {
        if (previous_target && (previous_target->kind == layout_target_kind_t::button))
        {
            auto button = find_button(previous_target->id);
            update_button(previous_target->id, false, button && button->pressed, response);
        }

        if (next_target && (next_target->kind == layout_target_kind_t::button))
        {
            auto button = find_button(next_target->id);
            update_button(next_target->id, true, button && button->pressed, response);
        }
    }

    current_x = x;
    current_y = y;
    current_target = next_target;
    return response;
}

layout_input_response_t layout_input_model_t::press()
{
    layout_input_response_t response;
    if (current_target && (current_target->kind == layout_target_kind_t::move))
    {
        if (double_click_pending && timer.is_connected())
        {
            double_click_pending    = false;
            double_click_at_release = true;
        } else
        {
            timer.set_timeout(DOUBLE_CLICK_TIMEOUT_MS);
            double_click_pending = true;
        }
    }

    if (current_target && (current_target->kind == layout_target_kind_t::resize))
    {
        response.action = DECORATION_ACTION_RESIZE;
        response.edges  = resize_edges_at(current_x, current_y);
        return response;
    }

    if (current_target && (current_target->kind == layout_target_kind_t::button))
    {
        auto button = find_button(current_target->id);
        update_button(current_target->id, button && button->hovered, true, response);
    }

    grabbed     = true;
    grab_target = current_target;
    return response;
}

layout_input_response_t layout_input_model_t::release()
{
    layout_input_response_t response;
    if (double_click_at_release)
    {
        double_click_at_release = false;
        grabbed     = false;
        grab_target = nullptr;
        response.action = DECORATION_ACTION_TOGGLE_MAXIMIZE;
        return response;
    }

    if (!grabbed)
    {
        return response;
    }

    grabbed = false;
    const auto begin_target = grab_target;
    grab_target = nullptr;
    if (begin_target && (begin_target->kind == layout_target_kind_t::button))
    {
        auto button = find_button(begin_target->id);
        update_button(begin_target->id, button && button->hovered, false, response);
        if (current_target && (begin_target->id == current_target->id))
        {
            response.action = button_action(begin_target->button);
        }
    }

    return response;
}

layout_input_response_t layout_input_model_t::axis(int delta) const
{
    return {delta < 0 ? DECORATION_ACTION_SHADE : DECORATION_ACTION_UNSHADE, 0, {}};
}

layout_input_response_t layout_input_model_t::focus_lost()
{
    layout_input_response_t response;
    if (grab_target && (grab_target->kind == layout_target_kind_t::button))
    {
        auto button = find_button(grab_target->id);
        update_button(grab_target->id, button && button->hovered, false, response);
    }

    if (current_target && (current_target->kind == layout_target_kind_t::button))
    {
        auto button = find_button(current_target->id);
        update_button(current_target->id, false, button && button->pressed, response);
    }

    grabbed     = false;
    grab_target = nullptr;
    current_target = nullptr;
    double_click_at_release = false;
    return response;
}

const std::vector<layout_button_state_t>& layout_input_model_t::button_states() const
{
    return buttons;
}
}
}
