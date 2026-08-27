#pragma once

#include <functional>

namespace wf
{
namespace pixdecor
{
struct shade_control_plan_t
{
    bool ensure_transformer = false;
    bool animate = false;
};

struct shade_transition_plan_t
{
    bool reverse = false;
    bool start   = false;
};

struct shade_frame_plan_t
{
    double progress   = 0.0;
    bool needs_damage = false;
    bool remove_frame_callback = false;
    bool remove_transformer    = false;
};

class shade_animation_backend_t
{
  public:
    virtual ~shade_animation_backend_t() = default;

    virtual bool running()   = 0;
    virtual bool direction() = 0;
    virtual double progress() const = 0;
    virtual void reverse() = 0;
    virtual void start()   = 0;
};

struct shade_runtime_actions_t
{
    std::function<void()> add_frame_callback;
    std::function<void()> remove_frame_callback;
    std::function<void()> damage;
    std::function<void()> remove_transformer;
};

struct shade_control_actions_t
{
    std::function<void()> ensure_transformer;
    std::function<void()> animate;
};

class shade_state_model_t
{
  public:
    static shade_control_plan_t plan_control(bool enabled, bool shade,
        bool view_mapped, bool transformer_present);

    shade_transition_plan_t request(bool shade, bool transition_running,
        bool transition_direction);
    shade_frame_plan_t frame(double transition_progress, bool transition_running);

    bool needs_transition(bool shade) const;
    bool direction() const;
    double progress() const;

  private:
    bool last_direction     = false;
    double current_progress = 0.0;
};

class shade_production_adapter_t
{
  public:
    shade_production_adapter_t(shade_state_model_t& state,
        shade_animation_backend_t& animation, shade_runtime_actions_t actions);

    void init_animation(bool shade);
    void request_refresh();
    shade_frame_plan_t frame(
        const std::function<void(const shade_frame_plan_t&)>& before_actions = {});
    bool direction() const;
    double progress() const;

  private:
    shade_animation_backend_t& animation;
    shade_runtime_actions_t actions;
    shade_state_model_t& state;
};

void apply_shade_control(bool enabled, bool shade, bool view_mapped,
    bool transformer_present, const shade_control_actions_t& actions);
}
}
