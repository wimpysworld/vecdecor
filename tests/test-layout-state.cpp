#include "deco-button.hpp"
#include "deco-layout-model.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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

class fake_timer_t : public pixdecor::layout_timer_t
{
  public:
    bool is_connected() override
    {
        if (deadline && (now >= *deadline))
        {
            deadline.reset();
        }

        return deadline.has_value();
    }

    void set_timeout(std::uint32_t timeout_ms) override
    {
        deadline     = now + timeout_ms;
        last_timeout = timeout_ms;
    }

    void advance(std::uint32_t milliseconds)
    {
        now += milliseconds;
    }

    std::uint32_t last_timeout = 0;

  private:
    std::uint32_t now = 0;
    std::optional<std::uint32_t> deadline;
};

struct button_backend_probe_t : public pixdecor::button_render_backend_t
{
    std::vector<geometry::button_state_t> requested_states;
    std::vector<std::pair<pixdecor::button_texture_slot_t, double>> draws;

    bool request_texture(pixdecor::button_texture_slot_t,
        const geometry::button_state_t& state, geometry::logical_size_t) override
    {
        requested_states.push_back(state);
        return true;
    }

    void draw_texture(pixdecor::button_texture_slot_t slot, double alpha) override
    {
        draws.emplace_back(slot, alpha);
    }
};

struct rendered_button_t
{
    geometry::button_state_t state;
    bool complete = false;
};

enum class input_source_t
{
    pointer,
    touch,
};

struct fixture_t
{
    static constexpr int width    = 300;
    static constexpr int offset_x = 13;
    static constexpr int offset_y = 17;

    struct button_entry_t
    {
        std::size_t target_id = 0;
        pixdecor::button_type_t type = pixdecor::BUTTON_CLOSE;
        int redraw_count = 0;
        std::unique_ptr<pixdecor::button_t> button;
    };

    fake_timer_t timer;
    pixdecor::layout_input_model_t model{timer};
    std::vector<pixdecor::layout_target_t> targets;
    std::vector<std::unique_ptr<button_entry_t>> buttons;
    std::unique_ptr<pixdecor::layout_input_adapter_t> input;
    std::vector<pixdecor::decoration_layout_action_t> actions;
    int coordinate_calls = 0;

    fixture_t()
    {
        const auto layout = pixdecor::parse_button_layout("minimize,maximize:close");
        geometry::button_group_input_t group;
        group.button_count  = layout.left.size();
        group.button_bounds = {0, 8, 20, 20};
        group.spacing = 4;
        group.corner_inset = 8;
        group.title_width  = width;

        const auto left = geometry::resolve_left_group_positions(group);
        group.button_count = layout.right.size();
        const auto right = geometry::resolve_right_group_positions(group);

        std::size_t id = 0;
        for (std::size_t i = 0; i < left.buttons.size(); ++i)
        {
            add_button(id++, layout.left[i], left.buttons[i]);
        }

        for (std::size_t i = 0; i < right.buttons.size(); ++i)
        {
            const auto type = layout.right[layout.right.size() - i - 1];
            add_button(id++, type, right.buttons[i]);
        }

        targets.push_back({id, pixdecor::layout_target_kind_t::move,
                {80, 0, 140, 32}, pixdecor::BUTTON_CLOSE, 0});
        model.set_targets(targets);
        input = std::make_unique<pixdecor::layout_input_adapter_t>(
            pixdecor::layout_input_adapter_dependencies_t{
                [this] (pixdecor::layout_input_point_t point)
            {
                ++coordinate_calls;
                return pixdecor::layout_input_point_t{
                    point.x - offset_x, point.y - offset_y};
            },
                [this] (int x, int y) { return model.motion(x, y); },
                [this] (bool pressed)
            {
                return pressed ? model.press() : model.release();
            },
                [this] (int delta) { return model.axis(delta); },
                [this] () { return model.focus_lost(); },
                [this] (const pixdecor::layout_button_update_t& update)
            {
                apply_button_update(update);
            },
                [this] (const pixdecor::layout_input_response_t& response)
            {
                if (response.action != pixdecor::DECORATION_ACTION_NONE)
                {
                    actions.push_back(response.action);
                }
            },
            });
        reset_observation();
    }

    std::pair<int, int> centre(pixdecor::button_type_t type) const
    {
        const auto target = std::find_if(targets.begin(), targets.end(),
            [=] (const auto& candidate)
        {
            return (candidate.kind == pixdecor::layout_target_kind_t::button) &&
                   (candidate.button == type);
        });
        return {target->bounds.x + target->bounds.width / 2,
            target->bounds.y + target->bounds.height / 2};
    }

    std::pair<int, int> global(std::pair<int, int> point) const
    {
        return {point.first + offset_x, point.second + offset_y};
    }

    std::pair<int, int> move_centre() const
    {
        return {150, 16};
    }

    button_entry_t& button(pixdecor::button_type_t type)
    {
        const auto found = std::find_if(buttons.begin(), buttons.end(), [=] (const auto& entry)
        {
            return entry->type == type;
        });
        return **found;
    }

    rendered_button_t render(pixdecor::button_type_t type, bool maximized = false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        button_backend_probe_t backend;
        button(type).button->render(backend, {20, 20}, true, maximized);
        if ((backend.requested_states.size() != 2) || (backend.draws.size() != 2))
        {
            return {};
        }

        const std::size_t visible = backend.draws[1].second > backend.draws[0].second ? 1 : 0;
        return {backend.requested_states[visible], true};
    }

    int redraw_count() const
    {
        int result = 0;
        for (const auto& entry : buttons)
        {
            result += entry->redraw_count;
        }

        return result;
    }

    void reset_observation()
    {
        actions.clear();
        for (auto& entry : buttons)
        {
            entry->redraw_count = 0;
        }
    }

  private:
    void add_button(std::size_t id, pixdecor::button_type_t type,
        const geometry::logical_bounds_t& bounds)
    {
        targets.push_back({id, pixdecor::layout_target_kind_t::button,
                {bounds.x, bounds.y, bounds.width, bounds.height}, type, 0});
        auto entry = std::make_unique<button_entry_t>();
        entry->target_id = id;
        entry->type = type;
        auto *stored = entry.get();
        entry->button = std::make_unique<pixdecor::button_t>(pixdecor::button_runtime_t{
                [] (const std::string&) { return wf::create_option(1); },
                [bounds] () { return bounds; },
                [stored] () { ++stored->redraw_count; },
            });
        entry->button->set_button_type(type);
        buttons.push_back(std::move(entry));
    }

    void apply_button_update(const pixdecor::layout_button_update_t& update)
    {
        const auto found = std::find_if(buttons.begin(), buttons.end(), [&] (const auto& entry)
        {
            return entry->target_id == update.state.target_id;
        });
        if (found == buttons.end())
        {
            return;
        }

        if (update.hover_changed)
        {
            (*found)->button->set_hover(update.state.hovered);
        }

        if (update.pressed_changed)
        {
            (*found)->button->set_pressed(update.state.pressed);
        }
    }
};

void motion(input_source_t source, fixture_t& fixture, std::pair<int, int> point)
{
    const auto [x, y] = fixture.global(point);
    if (source == input_source_t::pointer)
    {
        fixture.input->pointer_motion({double(x), double(y)});
    } else
    {
        fixture.input->touch_motion({double(x), double(y)});
    }
}

void press(input_source_t source, fixture_t& fixture, std::pair<int, int> point)
{
    const auto [x, y] = fixture.global(point);
    if (source == input_source_t::pointer)
    {
        fixture.input->pointer_motion({double(x), double(y)});
        fixture.input->pointer_button(true);
    } else
    {
        fixture.input->touch_down({double(x), double(y)});
    }
}

void release(input_source_t source, fixture_t& fixture)
{
    if (source == input_source_t::pointer)
    {
        fixture.input->pointer_button(false);
    } else
    {
        fixture.input->touch_up();
    }
}

geometry::button_kind_t expected_kind(pixdecor::button_type_t button)
{
    switch (button)
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

void expect_transition(fixture_t& fixture, pixdecor::button_type_t button, bool maximized,
    geometry::interaction_state_t interaction,
    pixdecor::decoration_layout_action_t action, int redraw_count, const std::string& name)
{
    const int event_redraw_count = fixture.redraw_count();
    const auto rendered = fixture.render(button, maximized);
    expect(rendered.complete && (rendered.state.kind == expected_kind(button)) &&
        (rendered.state.maximize == (maximized ? geometry::maximize_state_t::restore :
            geometry::maximize_state_t::maximize)) &&
        (rendered.state.interaction == interaction),
        name + " selects the expected production texture output");
    expect((action == pixdecor::DECORATION_ACTION_NONE) ? fixture.actions.empty() :
        (fixture.actions == std::vector<pixdecor::decoration_layout_action_t>{action}),
        name + " emits the expected action");
    expect(event_redraw_count == redraw_count,
        name + " requests " + std::to_string(redraw_count) + " redraws, got " +
        std::to_string(event_redraw_count));
}

bool same_state(const std::vector<pixdecor::layout_button_state_t>& lhs,
    const std::vector<pixdecor::layout_button_state_t>& rhs, bool compare_hover)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if ((lhs[i].target_id != rhs[i].target_id) || (lhs[i].button != rhs[i].button) ||
            (lhs[i].pressed != rhs[i].pressed) ||
            (compare_hover && (lhs[i].hovered != rhs[i].hovered)))
        {
            return false;
        }
    }

    return true;
}

void test_layout_parser()
{
    const auto mixed = pixdecor::parse_button_layout(
        "appmenu,unknown,minimize:maximize,spacer,close");
    expect(mixed.left == std::vector<pixdecor::button_type_t>{pixdecor::BUTTON_MINIMIZE},
        "a mixed layout ignores unknown left tokens");
    expect(mixed.right == std::vector<pixdecor::button_type_t>{
            pixdecor::BUTTON_TOGGLE_MAXIMIZE, pixdecor::BUTTON_CLOSE},
        "a mixed layout keeps known right tokens in order");

    const auto fallback = pixdecor::parse_button_layout("appmenu,menu:spacer,unknown");
    const auto expected = pixdecor::default_button_layout();
    expect(fallback.left == expected.left && fallback.right == expected.right &&
        fallback.left.empty() &&
        fallback.right == std::vector<pixdecor::button_type_t>{pixdecor::BUTTON_CLOSE},
        "an all-unknown layout selects the appmenu:close production default");

    const auto explicit_fallback = pixdecor::parse_button_layout("appmenu:close");
    expect(explicit_fallback.left.empty() &&
        explicit_fallback.right == std::vector<pixdecor::button_type_t>{pixdecor::BUTTON_CLOSE},
        "the explicit appmenu:close fallback keeps the close button");
}

void test_motion_press_and_same_control_actions()
{
    struct activation_t
    {
        const char *name;
        pixdecor::button_type_t button;
        pixdecor::decoration_layout_action_t action;
        bool maximized;
    };

    const std::vector<activation_t> activations = {
        {"close", pixdecor::BUTTON_CLOSE, pixdecor::DECORATION_ACTION_CLOSE, false},
        {"minimise", pixdecor::BUTTON_MINIMIZE, pixdecor::DECORATION_ACTION_MINIMIZE, false},
        {"maximise", pixdecor::BUTTON_TOGGLE_MAXIMIZE,
            pixdecor::DECORATION_ACTION_TOGGLE_MAXIMIZE, false},
        {"restore", pixdecor::BUTTON_TOGGLE_MAXIMIZE,
            pixdecor::DECORATION_ACTION_TOGGLE_MAXIMIZE, true},
    };

    for (const auto& activation : activations)
    {
        fixture_t pointer;
        motion(input_source_t::pointer, pointer, pointer.centre(activation.button));
        expect_transition(pointer, activation.button, activation.maximized,
            geometry::interaction_state_t::hover, pixdecor::DECORATION_ACTION_NONE, 1,
            std::string{"pointer "} + activation.name + " motion");
        pointer.reset_observation();
        pointer.input->pointer_button(true);
        expect_transition(pointer, activation.button, activation.maximized,
            geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 1,
            std::string{"pointer "} + activation.name + " press");
        pointer.reset_observation();
        release(input_source_t::pointer, pointer);
        expect_transition(pointer, activation.button, activation.maximized,
            geometry::interaction_state_t::hover, activation.action, 1,
            std::string{"pointer "} + activation.name + " release");

        fixture_t touch;
        press(input_source_t::touch, touch, touch.centre(activation.button));
        expect_transition(touch, activation.button, activation.maximized,
            geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 2,
            std::string{"touch "} + activation.name + " press");
        touch.reset_observation();
        release(input_source_t::touch, touch);
        expect_transition(touch, activation.button, activation.maximized,
            geometry::interaction_state_t::normal, activation.action, 2,
            std::string{"touch "} + activation.name + " release");

        expect(pointer.actions == touch.actions &&
            same_state(pointer.model.button_states(), touch.model.button_states(), false),
            std::string{activation.name} + " has pointer and touch action and pressed-state parity");
        expect(std::none_of(pointer.model.button_states().begin(),
            pointer.model.button_states().end(), [] (const auto& state) { return state.pressed; }) &&
            std::none_of(touch.model.button_states().begin(),
                touch.model.button_states().end(), [] (const auto& state) { return state.pressed; }),
            std::string{activation.name} + " clears pointer and touch pressed state");
        expect(pointer.coordinate_calls == 1 && touch.coordinate_calls == 1,
            std::string{activation.name} + " uses the injected production coordinate conversion");
    }
}

void test_move_action()
{
    for (const auto source : {input_source_t::pointer, input_source_t::touch})
    {
        fixture_t fixture;
        press(source, fixture, fixture.move_centre());
        fixture.reset_observation();
        motion(source, fixture, fixture.move_centre());
        expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
            geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_MOVE, 0,
            source == input_source_t::pointer ? "pointer move" : "touch move");
    }
}

void test_button_grab_does_not_move()
{
    fixture_t fixture;
    press(input_source_t::pointer, fixture, fixture.centre(pixdecor::BUTTON_CLOSE));
    fixture.reset_observation();
    motion(input_source_t::pointer, fixture, fixture.move_centre());
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 1,
        "pointer button grab entering move target");
    fixture.reset_observation();
    motion(input_source_t::pointer, fixture, fixture.move_centre());
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 0,
        "pointer button grab remaining in move target");
}

void test_cross_control_and_release_outside()
{
    for (const auto source : {input_source_t::pointer, input_source_t::touch})
    {
        const std::string source_name = source == input_source_t::pointer ? "pointer" : "touch";
        fixture_t cross;
        press(source, cross, cross.centre(pixdecor::BUTTON_CLOSE));
        cross.reset_observation();
        motion(source, cross, cross.centre(pixdecor::BUTTON_TOGGLE_MAXIMIZE));
        expect_transition(cross, pixdecor::BUTTON_CLOSE, false,
            geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 2,
            source_name + " cross-control motion");
        cross.reset_observation();
        release(source, cross);
        expect_transition(cross, pixdecor::BUTTON_CLOSE, false,
            geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_NONE,
            source == input_source_t::pointer ? 1 : 2,
            source_name + " cross-control release");

        fixture_t outside;
        press(source, outside, outside.centre(pixdecor::BUTTON_CLOSE));
        outside.reset_observation();
        motion(source, outside, {-20, -20});
        expect_transition(outside, pixdecor::BUTTON_CLOSE, false,
            geometry::interaction_state_t::pressed, pixdecor::DECORATION_ACTION_NONE, 1,
            source_name + " move outside");
        outside.reset_observation();
        release(source, outside);
        expect_transition(outside, pixdecor::BUTTON_CLOSE, false,
            geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_NONE, 1,
            source_name + " release outside");
    }
}

void test_focus_loss()
{
    fixture_t fixture;
    press(input_source_t::pointer, fixture, fixture.centre(pixdecor::BUTTON_CLOSE));
    fixture.reset_observation();
    fixture.input->pointer_focus_lost();
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_NONE, 2,
        "pointer focus loss");
    fixture.reset_observation();
    fixture.input->pointer_button(false);
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_NONE, 0,
        "release after focus loss");
}

void test_axis_actions()
{
    fixture_t fixture;
    fixture.input->pointer_axis(-1);
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_SHADE, 0,
        "vertical shade axis");
    fixture.reset_observation();
    fixture.input->pointer_axis(1);
    expect_transition(fixture, pixdecor::BUTTON_CLOSE, false,
        geometry::interaction_state_t::normal, pixdecor::DECORATION_ACTION_UNSHADE, 0,
        "vertical unshade axis");
}

pixdecor::decoration_layout_action_t double_click_after(input_source_t source,
    std::uint32_t delay)
{
    fixture_t fixture;
    const auto point = fixture.move_centre();
    press(source, fixture, point);
    release(source, fixture);
    fixture.timer.advance(delay);
    press(source, fixture, point);
    release(source, fixture);
    expect(fixture.timer.last_timeout == pixdecor::layout_input_model_t::DOUBLE_CLICK_TIMEOUT_MS,
        "the production adapter sends the 300 ms timeout to the timer");
    return fixture.actions.empty() ? pixdecor::DECORATION_ACTION_NONE : fixture.actions.back();
}

void test_double_click_boundary()
{
    for (const auto delay : {299u, 300u, 301u})
    {
        const auto pointer  = double_click_after(input_source_t::pointer, delay);
        const auto touch    = double_click_after(input_source_t::touch, delay);
        const auto expected = delay < 300 ? pixdecor::DECORATION_ACTION_TOGGLE_MAXIMIZE :
            pixdecor::DECORATION_ACTION_NONE;
        expect(pointer == expected && touch == expected,
            "pointer and touch use the 300 ms double-click boundary at " +
            std::to_string(delay) + " ms");
    }
}
}

int main()
{
    test_layout_parser();
    test_motion_press_and_same_control_actions();
    test_move_action();
    test_button_grab_does_not_move();
    test_cross_control_and_release_outside();
    test_focus_loss();
    test_axis_actions();
    test_double_click_boundary();
    return failures == 0 ? 0 : 1;
}
