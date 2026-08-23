#pragma once

#include "deco-button-state.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wf
{
namespace pixdecor
{
struct button_layout_t
{
    std::vector<button_type_t> left;
    std::vector<button_type_t> right;
};

button_layout_t default_button_layout();
button_layout_t parse_button_layout(const std::string& layout);

enum decoration_layout_action_t
{
    DECORATION_ACTION_NONE            = 0,
    DECORATION_ACTION_MOVE            = 1,
    DECORATION_ACTION_RESIZE          = 2,
    DECORATION_ACTION_CLOSE           = 3,
    DECORATION_ACTION_TOGGLE_MAXIMIZE = 4,
    DECORATION_ACTION_MINIMIZE        = 5,
    DECORATION_ACTION_SHADE           = 6,
    DECORATION_ACTION_UNSHADE         = 7,
};

class layout_timer_t
{
  public:
    virtual ~layout_timer_t()   = default;
    virtual bool is_connected() = 0;
    virtual void set_timeout(std::uint32_t timeout_ms) = 0;
};

enum class layout_target_kind_t
{
    button,
    move,
    resize,
};

struct layout_bounds_t
{
    int x     = 0;
    int y     = 0;
    int width = 0;
    int height = 0;

    bool contains(int px, int py) const;
};

struct layout_target_t
{
    std::size_t id = 0;
    layout_target_kind_t kind = layout_target_kind_t::move;
    layout_bounds_t bounds;
    button_type_t button = BUTTON_CLOSE;
    std::uint32_t edges  = 0;
};

struct layout_button_state_t
{
    std::size_t target_id = 0;
    button_type_t button  = BUTTON_CLOSE;
    bool hovered = false;
    bool pressed = false;
};

struct layout_button_update_t
{
    layout_button_state_t state;
    bool hover_changed   = false;
    bool pressed_changed = false;
};

struct layout_input_response_t
{
    decoration_layout_action_t action = DECORATION_ACTION_NONE;
    std::uint32_t edges = 0;
    std::vector<layout_button_update_t> changed_buttons;
};

struct layout_input_point_t
{
    double x = 0.0;
    double y = 0.0;
};

struct layout_input_adapter_dependencies_t
{
    std::function<layout_input_point_t(layout_input_point_t)> to_local;
    std::function<layout_input_response_t(int, int)> motion;
    std::function<layout_input_response_t(bool)> button;
    std::function<layout_input_response_t(int)> axis;
    std::function<layout_input_response_t()> focus_lost;
    std::function<void(const layout_button_update_t&)> update_button;
    std::function<void(const layout_input_response_t&)> handle_action;
};

class layout_input_adapter_t
{
  public:
    explicit layout_input_adapter_t(layout_input_adapter_dependencies_t dependencies);

    void pointer_motion(layout_input_point_t point);
    void pointer_button(bool pressed);
    void pointer_axis(int delta);
    void pointer_focus_lost();

    void touch_down(layout_input_point_t point);
    void touch_motion(layout_input_point_t point);
    void touch_up(layout_input_point_t point);

  private:
    layout_input_adapter_dependencies_t dependencies;
    layout_input_point_t last_touch_point;
    bool touch_active = false;

    void dispatch(layout_input_response_t response);
    void motion(layout_input_point_t point);
    void button(bool pressed);
    void focus_lost();
};

class layout_input_model_t
{
  public:
    static constexpr std::uint32_t DOUBLE_CLICK_TIMEOUT_MS = 300;

    explicit layout_input_model_t(layout_timer_t& timer);

    void set_targets(std::vector<layout_target_t> targets);
    layout_input_response_t motion(int x, int y);
    layout_input_response_t press();
    layout_input_response_t release();
    layout_input_response_t axis(int delta) const;
    layout_input_response_t focus_lost();

    const std::vector<layout_button_state_t>& button_states() const;

  private:
    layout_timer_t& timer;
    std::vector<layout_target_t> targets;
    std::vector<layout_button_state_t> buttons;
    const layout_target_t *current_target = nullptr;
    const layout_target_t *grab_target    = nullptr;
    bool grabbed = false;
    bool double_click_pending    = false;
    bool double_click_at_release = false;

    const layout_target_t *find_target(int x, int y) const;
    layout_button_state_t *find_button(std::size_t target_id);
    void update_button(std::size_t target_id, bool hovered, bool pressed,
        layout_input_response_t& response);
    std::uint32_t resize_edges_at(int x, int y) const;
    int current_x = 0;
    int current_y = 0;
};
}
}
