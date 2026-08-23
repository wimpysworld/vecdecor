#include "shade.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace pixdecor = wf::pixdecor;

namespace
{
int failures = 0;

class fake_animation_t : public pixdecor::shade_animation_backend_t
{
  public:
    bool running_value    = false;
    bool direction_value  = true;
    double progress_value = 0.0;
    int running_calls     = 0;
    int direction_calls   = 0;
    mutable int progress_calls = 0;
    int reverse_calls = 0;
    int start_calls   = 0;

    bool running() override
    {
        ++running_calls;
        return running_value;
    }

    bool direction() override
    {
        ++direction_calls;
        return direction_value;
    }

    double progress() const override
    {
        ++progress_calls;
        return progress_value;
    }

    void reverse() override
    {
        ++reverse_calls;
        direction_value = !direction_value;
    }

    void start() override
    {
        ++start_calls;
        running_value = true;
    }
};

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_close(double actual, double expected, const std::string& message)
{
    expect(std::abs(actual - expected) < 0.000001, message);
}

void test_control_gating()
{
    auto plan = pixdecor::shade_state_model_t::plan_control(false, true, true, false);
    expect(!plan.ensure_transformer && !plan.animate,
        "disabled shade does not create or animate a transformer");

    plan = pixdecor::shade_state_model_t::plan_control(false, false, true, true);
    expect(!plan.ensure_transformer && !plan.animate,
        "disabled unshade does not animate an existing transformer");

    plan = pixdecor::shade_state_model_t::plan_control(true, true, false, false);
    expect(!plan.ensure_transformer && !plan.animate,
        "shade ignores an unmapped view");

    plan = pixdecor::shade_state_model_t::plan_control(true, true, true, false);
    expect(plan.ensure_transformer && plan.animate,
        "shade creates and animates a transformer for a mapped view");

    plan = pixdecor::shade_state_model_t::plan_control(true, true, true, true);
    expect(!plan.ensure_transformer && plan.animate,
        "shade reuses an existing transformer");

    plan = pixdecor::shade_state_model_t::plan_control(true, false, true, false);
    expect(!plan.ensure_transformer && !plan.animate,
        "unshade ignores a view without a shade transformer");

    plan = pixdecor::shade_state_model_t::plan_control(true, false, true, true);
    expect(!plan.ensure_transformer && plan.animate,
        "unshade animates an existing shade transformer");
}

void test_production_control_gating()
{
    int ensure_calls  = 0;
    int animate_calls = 0;
    const pixdecor::shade_control_actions_t actions{
        [&] () { ++ensure_calls; },
        [&] () { ++animate_calls; },
    };

    pixdecor::apply_shade_control(false, true, true, false, actions);
    pixdecor::apply_shade_control(true, true, false, false, actions);
    expect(ensure_calls == 0 && animate_calls == 0,
        "the production control adapter gates disabled and unmapped shade requests");

    pixdecor::apply_shade_control(true, true, true, false, actions);
    expect(ensure_calls == 1 && animate_calls == 1,
        "the production control adapter creates and animates the shade transformer");

    pixdecor::apply_shade_control(true, false, true, true, actions);
    expect(ensure_calls == 1 && animate_calls == 2,
        "the production control adapter animates an existing unshade transformer");
}

void test_direction_and_reversal()
{
    pixdecor::shade_state_model_t model;
    auto transition = model.request(true, false, true);
    expect(transition.start && !transition.reverse && model.direction(),
        "shade starts a forward stopped transition");

    expect(!model.needs_transition(true),
        "the model rejects a repeated shade before the adapter reads its transition");
    transition = model.request(true, true, true);
    expect(!transition.start && !transition.reverse,
        "a repeated shade request keeps the current transition");

    transition = model.request(false, true, true);
    expect(!transition.start && transition.reverse && !model.direction(),
        "unshade reverses a running shade transition");

    transition = model.request(true, true, false);
    expect(!transition.start && transition.reverse && model.direction(),
        "shade reverses a running unshade transition");

    transition = model.request(false, false, true);
    expect(transition.start && transition.reverse && !model.direction(),
        "unshade reverses and starts a stopped forward transition");
}

void test_progress_duration_and_redraw()
{
    pixdecor::shade_state_model_t zero_duration_model;
    zero_duration_model.request(true, false, true);
    const int zero_duration_ms = 0;
    const auto zero_frame = zero_duration_model.frame(1.0, zero_duration_ms > 0);
    expect_close(zero_frame.progress, 1.0,
        "a zero-duration shade reaches its endpoint immediately");
    expect(!zero_frame.needs_damage && zero_frame.remove_frame_callback &&
        !zero_frame.remove_transformer,
        "a completed zero-duration shade stops frame damage and keeps the transformer");

    pixdecor::shade_state_model_t timed_model;
    timed_model.request(true, false, true);
    const int duration_ms = 500;
    struct sample_t
    {
        int elapsed_ms;
        double progress;
    };

    const std::array<sample_t, 3> samples = {{
        {0, 0.0},
        {duration_ms / 2, 0.5},
        {duration_ms, 1.0},
    }};
    int damage_count = 0;
    for (const auto& sample : samples)
    {
        const auto frame = timed_model.frame(sample.progress,
            sample.elapsed_ms < duration_ms);
        expect_close(frame.progress, sample.progress,
            "shade progress follows the configured transition");
        damage_count += frame.needs_damage ? 1 : 0;
        expect(frame.remove_frame_callback == (sample.elapsed_ms == duration_ms),
            "only a completed shade removes the frame callback");
    }

    expect(damage_count == 2,
        "each running shade frame requests damage");

    const auto completed_frame = timed_model.frame(1.0, false);
    expect(!completed_frame.needs_damage && completed_frame.remove_frame_callback,
        "a completed shade does not start a damage loop");

    auto transition = timed_model.request(false, false, true);
    expect(transition.start && transition.reverse,
        "unshade starts from the completed shade endpoint");
    const auto unshade_start    = timed_model.frame(1.0, true);
    const auto unshade_midpoint = timed_model.frame(0.5, true);
    const auto unshade_end = timed_model.frame(0.0, false);
    expect_close(unshade_start.progress, 1.0,
        "unshade starts at the shade endpoint");
    expect_close(unshade_midpoint.progress, 0.5,
        "unshade reaches its midpoint");
    expect_close(unshade_end.progress, 0.0,
        "unshade reaches the unshaded endpoint");
    expect(unshade_start.needs_damage && unshade_midpoint.needs_damage &&
        !unshade_end.needs_damage && unshade_end.remove_transformer,
        "unshade damages running frames and removes its transformer after completion");
}

void test_production_animation_and_frame_actions()
{
    fake_animation_t animation;
    int add_frame_calls    = 0;
    int remove_frame_calls = 0;
    int damage_calls = 0;
    int remove_transformer_calls = 0;
    pixdecor::shade_state_model_t state;
    pixdecor::shade_production_adapter_t production(state, animation,
        {
            [&] () { ++add_frame_calls; },
            [&] () { ++remove_frame_calls; },
            [&] () { ++damage_calls; },
            [&] () { ++remove_transformer_calls; },
        });

    pixdecor::pixdecor_shade::init_production_animation(production, true);
    expect(animation.running_calls == 1 && animation.direction_calls == 1 &&
        animation.start_calls == 1 && animation.reverse_calls == 0,
        "the production adapter forwards shade direction and start to the animation");
    expect(add_frame_calls == 1 && production.direction(),
        "shade enables the production frame callback");

    pixdecor::pixdecor_shade::init_production_animation(production, true);
    expect(animation.running_calls == 1 && animation.start_calls == 1 &&
        add_frame_calls == 1,
        "a repeated shade request does not restart the production animation");

    animation.progress_value = 0.4;
    animation.running_value  = true;
    const auto running_frame =
        pixdecor::pixdecor_shade::run_production_frame(production);
    expect(animation.progress_calls == 1 && animation.running_calls == 2,
        "the production frame reads progress and running state from the animation");
    expect_close(running_frame.progress, 0.4,
        "the production frame forwards animation progress");
    expect(damage_calls == 1 && remove_frame_calls == 0 &&
        remove_transformer_calls == 0,
        "a running production frame damages the view and keeps the callback");

    pixdecor::pixdecor_shade::init_production_animation(production, false);
    expect(animation.reverse_calls == 1 && animation.start_calls == 1 &&
        !production.direction(),
        "the production adapter reverses a running shade animation");

    animation.progress_value = 0.0;
    animation.running_value  = false;
    const auto completed_frame =
        pixdecor::pixdecor_shade::run_production_frame(production);
    expect_close(completed_frame.progress, 0.0,
        "the completed production frame forwards its endpoint");
    expect(damage_calls == 1 && remove_frame_calls == 1 &&
        remove_transformer_calls == 1,
        "the completed unshade frame keeps final-frame damage off and removes its resources");
}

void test_shade_animation_duration_wiring()
{
    const wf::animation_description_t duration{
        10000,
        wf::animation::smoothing::linear,
        "linear",
    };
    pixdecor::shade_animation_t animation(
        wf::create_option<wf::animation_description_t>(duration));
    animation.shade.set(0.0, 1.0);
    animation.start();

    expect(animation.running(),
        "shade_animation_t uses the configured non-zero shade duration");
    expect(animation.progress() < 1.0,
        "shade_animation_t forwards configured duration progress");
    animation.reverse();
    expect(!animation.direction(),
        "shade_animation_t forwards reversal to the Wayfire duration");
}
}

int main()
{
    test_control_gating();
    test_production_control_gating();
    test_direction_and_reversal();
    test_progress_duration_and_redraw();
    test_production_animation_and_frame_actions();
    test_shade_animation_duration_wiring();
    return failures == 0 ? 0 : 1;
}
