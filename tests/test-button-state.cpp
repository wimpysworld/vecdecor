#include "deco-button.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <wayfire/config/option-wrapper.hpp>

namespace pixdecor = wf::pixdecor;
namespace geometry = wf::pixdecor::geometry;

namespace
{
int failures = 0;

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

geometry::button_kind_t expected_kind(pixdecor::button_type_t type)
{
    switch (type)
    {
      case pixdecor::BUTTON_CLOSE:
        return geometry::button_kind_t::close;

      case pixdecor::BUTTON_TOGGLE_MAXIMIZE:
        return geometry::button_kind_t::maximize;

      case pixdecor::BUTTON_MINIMIZE:
        return geometry::button_kind_t::minimize;
    }

    return geometry::button_kind_t::close;
}

void expect_plan_states(const pixdecor::button_render_plan_t& plan,
    pixdecor::button_type_t type, bool active, bool maximized,
    geometry::interaction_state_t normal_interaction)
{
    const geometry::button_state_t expected_normal = {
        expected_kind(type),
        active ? geometry::focus_state_t::active : geometry::focus_state_t::inactive,
        normal_interaction,
        maximized ? geometry::maximize_state_t::restore :
        geometry::maximize_state_t::maximize,
    };
    auto expected_hover = expected_normal;
    expected_hover.interaction = geometry::interaction_state_t::hover;

    expect(plan.normal_texture_state == expected_normal,
        "the normal texture request has the selected button state");
    expect(plan.hover_texture_state == expected_hover,
        "the hover texture request has the selected button state");
}

void test_complete_state_selection()
{
    const std::array<pixdecor::button_type_t, 3> types = {
        pixdecor::BUTTON_CLOSE,
        pixdecor::BUTTON_TOGGLE_MAXIMIZE,
        pixdecor::BUTTON_MINIMIZE,
    };

    pixdecor::button_state_model_t model;
    for (const auto type : types)
    {
        for (const bool active : {false, true})
        {
            for (const bool maximized : {false, true})
            {
                const auto plan = model.render_plan(type, active, maximized, 0.25, false);
                expect_plan_states(plan, type, active, maximized,
                    geometry::interaction_state_t::normal);
                expect_close(plan.normal_alpha, 0.25,
                    "the normal texture uses the supplied progress");
                expect_close(plan.hover_alpha, 0.75,
                    "the hover texture uses the inverse progress");
            }
        }
    }

    model.set_pressed(true);
    for (const auto type : types)
    {
        for (const bool active : {false, true})
        {
            for (const bool maximized : {false, true})
            {
                const auto plan = model.render_plan(type, active, maximized, 1.0, false);
                expect_plan_states(plan, type, active, maximized,
                    geometry::interaction_state_t::pressed);
            }
        }
    }
}

void test_input_transitions()
{
    pixdecor::button_state_model_t model;
    int redraw_count = 0;
    auto apply = [&] (const pixdecor::button_transition_t& transition)
    {
        redraw_count += transition.needs_redraw ? 1 : 0;
    };

    auto transition = model.set_hover(true);
    apply(transition);
    expect(transition.animate,
        "set_hover starts the hover transition");
    expect_close(transition.target_normal_alpha, 0.0,
        "hover targets the hover texture");

    transition = model.set_pressed(true);
    apply(transition);
    expect(transition.animate &&
        model.render_plan(pixdecor::BUTTON_CLOSE, false, false, 1.0, false)
            .normal_texture_state.interaction == geometry::interaction_state_t::pressed,
        "set_pressed starts the pressed transition");
    expect_close(transition.target_normal_alpha, 1.0,
        "press targets the pressed normal texture");

    transition = model.set_hover(false);
    apply(transition);
    expect(!transition.animate &&
        model.render_plan(pixdecor::BUTTON_CLOSE, false, false, 1.0, false)
            .normal_texture_state.interaction == geometry::interaction_state_t::pressed,
        "leaving a pressed button keeps the pressed transition");
    expect_close(transition.target_normal_alpha, 1.0,
        "a pressed button keeps the normal texture target");

    transition = model.set_pressed(false);
    apply(transition);
    expect(transition.animate &&
        model.render_plan(pixdecor::BUTTON_CLOSE, false, false, 1.0, false)
            .normal_texture_state.interaction == geometry::interaction_state_t::normal,
        "release outside hover returns to normal");
    expect_close(transition.target_normal_alpha, 1.0,
        "release outside hover targets the normal texture");

    apply(model.set_hover(true));
    apply(model.set_pressed(true));
    transition = model.set_pressed(false);
    apply(transition);
    expect(transition.animate &&
        model.render_plan(pixdecor::BUTTON_CLOSE, false, false, 0.0, false)
            .normal_texture_state.interaction == geometry::interaction_state_t::normal,
        "release inside hover returns to hover");
    expect_close(transition.target_normal_alpha, 0.0,
        "release inside hover targets the hover texture");
    expect(redraw_count == 7,
        "each input change requests one idle redraw");
}

struct texture_probe_t
{
    bool normal_available = true;
    bool hover_available  = true;
    int redraw_count = 0;
    std::vector<pixdecor::button_texture_slot_t> slots;
    std::vector<geometry::button_state_t> states;

    bool execute(pixdecor::button_render_plan_t& plan)
    {
        return pixdecor::request_button_textures(plan,
            [&] (pixdecor::button_texture_slot_t slot,
                 const geometry::button_state_t& state)
        {
            slots.push_back(slot);
            states.push_back(state);
            return slot == pixdecor::button_texture_slot_t::normal ?
                   normal_available : hover_available;
        }, [&]
        {
            ++redraw_count;
        });
    }
};

void test_deterministic_progress_and_redraws()
{
    pixdecor::button_state_model_t zero_duration_model;
    const int zero_duration_ms = 0;
    const auto zero_transition = zero_duration_model.set_hover(true);
    auto zero_plan = zero_duration_model.render_plan(pixdecor::BUTTON_CLOSE, true, false,
        zero_transition.target_normal_alpha, zero_duration_ms > 0);
    texture_probe_t zero_probe;
    expect(zero_probe.execute(zero_plan),
        "a zero-duration render requests both available textures");
    expect_close(zero_plan.normal_alpha, 0.0,
        "a zero-duration transition reaches its target immediately");
    expect_close(zero_plan.hover_alpha, 1.0,
        "a zero-duration transition displays the hover texture");
    expect(zero_probe.redraw_count == 0,
        "a completed zero-duration transition needs no frame redraw");

    pixdecor::button_state_model_t timed_model;
    timed_model.set_hover(true);
    const int duration_ms = 500;
    struct sample_t
    {
        int elapsed_ms;
        double normal_alpha;
    };

    const std::array<sample_t, 3> samples = {{
        {0, 1.0},
        {duration_ms / 2, 0.5},
        {duration_ms, 0.0},
    }};
    texture_probe_t timed_probe;
    for (const auto& sample : samples)
    {
        auto plan = timed_model.render_plan(pixdecor::BUTTON_TOGGLE_MAXIMIZE,
            false, true, sample.normal_alpha, sample.elapsed_ms < duration_ms);
        const std::size_t first_request = timed_probe.states.size();
        expect(timed_probe.execute(plan),
            "a timed render requests both available textures");
        expect(timed_probe.states.size() == first_request + 2,
            "each frame makes exactly two texture requests");
        expect(timed_probe.slots[first_request] == pixdecor::button_texture_slot_t::normal &&
            timed_probe.slots[first_request + 1] == pixdecor::button_texture_slot_t::hover,
            "each frame requests the normal texture before the hover texture");
        expect(timed_probe.states[first_request] == plan.normal_texture_state &&
            timed_probe.states[first_request + 1] == plan.hover_texture_state,
            "both texture requests use the render plan states");
        expect_close(plan.normal_alpha, sample.normal_alpha,
            "the normal texture weight follows animation progress");
        expect_close(plan.hover_alpha, 1.0 - sample.normal_alpha,
            "the hover texture weight follows inverse animation progress");
    }

    expect(timed_probe.redraw_count == 2,
        "only the start and midpoint schedule per-frame redraws");
}

void test_missing_textures_request_redraw()
{
    pixdecor::button_state_model_t model;

    texture_probe_t normal_missing;
    normal_missing.normal_available = false;
    auto plan = model.render_plan(pixdecor::BUTTON_MINIMIZE, true, false, 1.0, false);
    expect(!normal_missing.execute(plan),
        "a missing normal texture prevents rendering");
    expect(normal_missing.states.size() == 2 && normal_missing.redraw_count == 1,
        "a missing normal texture still requests both textures and one redraw");

    texture_probe_t hover_missing;
    hover_missing.hover_available = false;
    plan = model.render_plan(pixdecor::BUTTON_MINIMIZE, true, false, 1.0, false);
    expect(!hover_missing.execute(plan),
        "a missing hover texture prevents rendering");
    expect(hover_missing.states.size() == 2 && hover_missing.redraw_count == 1,
        "a missing hover texture requests one redraw");

    texture_probe_t both_missing;
    both_missing.normal_available = false;
    both_missing.hover_available  = false;
    plan = model.render_plan(pixdecor::BUTTON_MINIMIZE, true, false, 1.0, true);
    expect(!both_missing.execute(plan),
        "two missing textures prevent rendering");
    expect(both_missing.states.size() == 2 && both_missing.redraw_count == 1,
        "animation and two cache misses coalesce into one idle redraw");
}

struct button_backend_probe_t : public pixdecor::button_render_backend_t
{
    bool normal_available = true;
    bool hover_available  = true;
    std::vector<pixdecor::button_texture_slot_t> requested_slots;
    std::vector<geometry::button_state_t> requested_states;
    std::vector<geometry::logical_size_t> requested_sizes;
    std::vector<std::pair<pixdecor::button_texture_slot_t, double>> draws;

    bool request_texture(pixdecor::button_texture_slot_t slot,
        const geometry::button_state_t& state,
        geometry::logical_size_t logical_size) override
    {
        requested_slots.push_back(slot);
        requested_states.push_back(state);
        requested_sizes.push_back(logical_size);
        return slot == pixdecor::button_texture_slot_t::normal ?
               normal_available : hover_available;
    }

    void draw_texture(pixdecor::button_texture_slot_t slot, double alpha) override
    {
        draws.emplace_back(slot, alpha);
    }

    void clear()
    {
        requested_slots.clear();
        requested_states.clear();
        requested_sizes.clear();
        draws.clear();
    }
};

class fake_button_animation_t : public pixdecor::button_animation_backend_t
{
  public:
    bool complete_immediately = true;
    bool running_value   = false;
    double current_value = 1.0;

    void animate(double end) override
    {
        if (complete_immediately)
        {
            current_value = end;
        }
    }

    void animate(double start, double end) override
    {
        current_value = complete_immediately ? end : start;
    }

    bool running() override
    {
        return running_value;
    }

    double value() const override
    {
        return current_value;
    }
};

pixdecor::button_t make_button(int duration_ms, int& redraw_count,
    std::string& loaded_duration_name,
    std::unique_ptr<pixdecor::button_animation_backend_t> animation = {})
{
    return pixdecor::button_t({
            [duration_ms, &loaded_duration_name] (const std::string& option_name)
        {
            loaded_duration_name = option_name;
            return wf::create_option(duration_ms);
        },
            []
        {
            return geometry::logical_bounds_t{2, 3, 18, 20};
        },
            [&redraw_count]
        {
            ++redraw_count;
        },
            std::move(animation),
        });
}

void expect_two_texture_requests(const button_backend_probe_t& backend,
    const std::string& message)
{
    expect(backend.requested_slots.size() == 2 &&
        backend.requested_slots[0] == pixdecor::button_texture_slot_t::normal &&
        backend.requested_slots[1] == pixdecor::button_texture_slot_t::hover,
        message);
}

void expect_two_draws(const button_backend_probe_t& backend,
    const std::string& message)
{
    expect(backend.draws.size() == 2 &&
        backend.draws[0].first == pixdecor::button_texture_slot_t::normal &&
        backend.draws[1].first == pixdecor::button_texture_slot_t::hover,
        message);
}

void test_production_button_animation_wiring()
{
    expect(pixdecor::button_animation_t::duration_option_name() ==
        "vecdecor/button_hover_duration",
        "the production animation wrapper loads button_hover_duration");

    int redraw_count = 0;
    std::string loaded_duration_name;
    auto button = make_button(1, redraw_count, loaded_duration_name);
    expect(loaded_duration_name == "vecdecor/button_hover_duration",
        "the production animation wrapper loads button_hover_duration");
    const auto bounds = button.set_button_type(pixdecor::BUTTON_TOGGLE_MAXIMIZE);
    expect(bounds == geometry::logical_bounds_t{2, 3, 18, 20} && redraw_count == 1,
        "set_button_type uses the injected bounds and requests a redraw");

    button_backend_probe_t backend;
    button.render(backend, {18, 20}, true, true);
    expect_two_draws(backend,
        "the production animation wrapper supplies both texture weights");
    expect_close(backend.draws[0].second, 1.0,
        "the production animation keeps a constant normal texture weight");
    expect_close(backend.draws[1].second, 0.0,
        "the production animation keeps a constant hover texture weight");
}

void test_button_adapter_state_and_render()
{
    int redraw_count = 0;
    std::string loaded_duration_name;
    auto button = make_button(1, redraw_count, loaded_duration_name,
        std::make_unique<fake_button_animation_t>());
    const auto bounds = button.set_button_type(pixdecor::BUTTON_TOGGLE_MAXIMIZE);
    expect(bounds == geometry::logical_bounds_t{2, 3, 18, 20} && redraw_count == 1,
        "set_button_type uses the injected bounds and requests a redraw");

    button.set_hover(true);
    expect(redraw_count == 2,
        "button_t::set_hover requests a redraw");

    button_backend_probe_t backend;
    button.render(backend, {18, 20}, true, true);
    expect_two_texture_requests(backend,
        "button_t::render requests both production texture states");
    expect_two_draws(backend,
        "button_t::render draws both production texture slots");
    expect(backend.requested_states[0].interaction == geometry::interaction_state_t::normal &&
        backend.requested_states[1].interaction == geometry::interaction_state_t::hover,
        "button_t::set_hover reaches the production render state");
    expect(backend.requested_states[0].focus == geometry::focus_state_t::active &&
        backend.requested_states[0].maximize == geometry::maximize_state_t::restore,
        "button_t::render forwards focus and maximise state");
    expect_close(backend.draws[0].second, 0.0,
        "the configured production animation reaches the hover texture");
    expect_close(backend.draws[1].second, 1.0,
        "the configured production animation displays the hover texture");

    backend.clear();
    const int before_press = redraw_count;
    button.set_pressed(true);
    expect(redraw_count == before_press + 1,
        "button_t::set_pressed requests a redraw");
    button.render(backend, {18, 20}, false, false);
    expect(backend.requested_states[0].interaction == geometry::interaction_state_t::pressed,
        "button_t::set_pressed reaches the production texture request");
    expect_close(backend.draws[0].second, 1.0,
        "press uses the normal texture through the production animation");

    backend.clear();
    const int before_release = redraw_count;
    button.set_pressed(false);
    expect(redraw_count == before_release + 1,
        "button_t::set_pressed requests a redraw on release");
    button.render(backend, {18, 20}, false, false);
    expect(backend.requested_states[0].interaction == geometry::interaction_state_t::normal,
        "release restores the normal production texture state");
    expect_close(backend.draws[0].second, 0.0,
        "release while hovered restores the hover texture");

    backend.clear();
    backend.hover_available = false;
    const int before_missing = redraw_count;
    button.render(backend, {18, 20}, false, false);
    expect_two_texture_requests(backend,
        "a completed transition still requests both textures");
    expect(backend.draws.empty() && redraw_count == before_missing + 1,
        "a missing texture requests a redraw after the animation completes");
}

void test_button_adapter_animation_redraws()
{
    int redraw_count = 0;
    std::string loaded_duration_name;
    auto animation = std::make_unique<fake_button_animation_t>();
    animation->complete_immediately = false;
    auto *animation_probe = animation.get();
    auto button = make_button(10000, redraw_count, loaded_duration_name, std::move(animation));
    button.set_button_type(pixdecor::BUTTON_CLOSE);
    button.set_hover(true);
    animation_probe->current_value = 0.5;
    animation_probe->running_value = true;

    button_backend_probe_t backend;
    const int before_first_frame = redraw_count;
    button.render(backend, {24, 24}, false, false);
    expect_two_draws(backend,
        "a cross-fade draws both textures");
    expect(backend.draws[0].second > 0.0 && backend.draws[0].second<1.0 &&
        backend.draws[1].second> 0.0 && backend.draws[1].second < 1.0,
        "draw weights come from simple_animation_t progress");
    expect_close(backend.draws[0].second + backend.draws[1].second, 1.0,
        "the production animation weights remain complementary");
    expect(redraw_count == before_first_frame + 1,
        "a running cross-fade requests the next frame");

    backend.clear();
    const int before_second_frame = redraw_count;
    button.render(backend, {24, 24}, false, false);
    expect(redraw_count == before_second_frame + 1,
        "each running cross-fade frame requests another redraw");

    backend.clear();
    backend.normal_available = false;
    const int before_missing = redraw_count;
    button.render(backend, {24, 24}, false, false);
    expect_two_texture_requests(backend,
        "a missing texture does not skip the other texture request");
    expect(backend.draws.empty(),
        "a missing texture prevents partial drawing");
    expect(redraw_count == before_missing + 1,
        "a cache miss and animation coalesce into one redraw request");
}
}

int main()
{
    test_complete_state_selection();
    test_input_transitions();
    test_deterministic_progress_and_redraws();
    test_missing_textures_request_redraw();
    test_production_button_animation_wiring();
    test_button_adapter_state_and_render();
    test_button_adapter_animation_redraws();
    return failures == 0 ? 0 : 1;
}
