#pragma once

#include "deco-geometry.hpp"

#include <functional>
#include <memory>
#include <string>

namespace wf
{
namespace config
{
template<class Type>
class option_t;
}
}

namespace wf
{
namespace pixdecor
{
enum button_type_t
{
    BUTTON_CLOSE,
    BUTTON_TOGGLE_MAXIMIZE,
    BUTTON_MINIMIZE,
};

struct button_transition_t
{
    double target_normal_alpha = 1.0;
    bool animate = false;
    bool needs_redraw = true;
};

struct button_render_plan_t
{
    geometry::button_state_t normal_texture_state;
    geometry::button_state_t hover_texture_state;
    double normal_alpha = 1.0;
    double hover_alpha  = 0.0;
    bool needs_redraw   = false;

    void set_texture_availability(bool normal_available, bool hover_available);
};

enum class button_texture_slot_t
{
    normal,
    hover,
};

template<class RequestTexture, class ScheduleRedraw>
bool request_button_textures(button_render_plan_t& plan, RequestTexture&& request_texture,
    ScheduleRedraw&& schedule_redraw)
{
    const bool normal_available = request_texture(
        button_texture_slot_t::normal, plan.normal_texture_state);
    const bool hover_available = request_texture(
        button_texture_slot_t::hover, plan.hover_texture_state);
    plan.set_texture_availability(normal_available, hover_available);
    if (plan.needs_redraw)
    {
        schedule_redraw();
    }

    return normal_available && hover_available;
}

class button_state_model_t
{
  public:
    button_transition_t set_hover(bool hovered);
    button_transition_t set_pressed(bool pressed);

    button_render_plan_t render_plan(button_type_t type, bool active, bool maximized,
        double normal_alpha, bool transition_running) const;

  private:
    bool is_hovered = false;
    bool is_pressed = false;
};

/**
 * The hover cross-fade easing: CSS cubic-bezier(0, 0, 0.2, 1), the ease-out curve the catppuccin-gtk theme
 * uses for titlebutton transitions.
 */
double button_hover_easing(double progress);

class button_animation_backend_t
{
  public:
    virtual ~button_animation_backend_t() = default;

    virtual void animate(double end) = 0;
    virtual void animate(double start, double end) = 0;
    virtual bool running() = 0;
    virtual double value() const = 0;
};

class button_animation_t : public button_animation_backend_t
{
  public:
    using duration_loader_t = std::function<std::shared_ptr<wf::config::option_t<int>>(
        const std::string&)>;

    explicit button_animation_t(duration_loader_t load_duration);
    ~button_animation_t();

    button_animation_t(button_animation_t&&) noexcept;
    button_animation_t& operator =(button_animation_t&&) noexcept;

    void animate(double end) override;
    void animate(double start, double end) override;
    bool running() override;
    double value() const override;

    static const std::string& duration_option_name();

  private:
    class impl;
    std::unique_ptr<impl> priv;
};
}
}
