#include "deco-layout.hpp"
#include "deco-theme.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <wayfire/core.hpp>
#include <wlr/xcursor.h>
#include <wayfire/toplevel.hpp>
#include <wayfire/util.hpp>

namespace wf
{
namespace pixdecor
{
namespace
{
button_layout_t read_button_layout()
{
    wf::option_wrapper_t<std::string> button_layout{"vecdecor/button_layout"};
    std::string layout = button_layout;

    return parse_button_layout(layout);
}

wf::geometry_t to_geometry(const geometry::logical_bounds_t& bounds)
{
    return {
        static_cast<double>(bounds.x),
        static_cast<double>(bounds.y),
        static_cast<double>(bounds.width),
        static_cast<double>(bounds.height),
    };
}

wf::geometry_t expanded_move_geometry(const geometry::logical_bounds_t& bounds,
    int outer_edge, int title_y, int title_height)
{
    const int64_t group_right  = static_cast<int64_t>(bounds.x) + bounds.width;
    const int64_t group_bottom = static_cast<int64_t>(bounds.y) + bounds.height;
    const int64_t title_bottom = static_cast<int64_t>(title_y) + title_height;
    const int64_t left   = std::min<int64_t>(bounds.x, outer_edge);
    const int64_t right  = std::max<int64_t>(group_right, outer_edge);
    const int64_t top    = std::min<int64_t>(bounds.y, title_y);
    const int64_t bottom = std::max(group_bottom, title_bottom);

    if (((right - left) > std::numeric_limits<int>::max()) ||
        ((bottom - top) > std::numeric_limits<int>::max()))
    {
        return to_geometry(bounds);
    }

    return {
        static_cast<double>(left), static_cast<double>(top),
        static_cast<double>(right - left), static_cast<double>(bottom - top),
    };
}
}

/**
 * Represents an area of the decoration which reacts to input events.
 */
decoration_area_t::decoration_area_t(decoration_area_type_t type, wf::geometry_t g)
{
    this->type     = type;
    this->geometry = g;

    assert(type != DECORATION_AREA_BUTTON);
}

/**
 * Initialize a new decoration area holding a button
 */
decoration_area_t::decoration_area_t(wf::geometry_t g,
    std::function<void(wf::geometry_t)> damage_callback,
    pixdecor_theme_t& theme)
{
    this->type     = DECORATION_AREA_BUTTON;
    this->geometry = g;

    this->button = std::make_unique<button_t>(theme,
        std::bind(damage_callback, g));
}

wf::geometry_t decoration_area_t::get_geometry() const
{
    return geometry;
}

button_t& decoration_area_t::as_button()
{
    assert(button);

    return *button;
}

decoration_area_type_t decoration_area_t::get_type() const
{
    return type;
}

pixdecor_layout_t::pixdecor_layout_t(pixdecor_theme_t& th,
    std::function<void(wf::geometry_t)> callback) :
    theme(th),
    damage_callback(callback),
    input_model(click_timer)
{}

bool pixdecor_layout_t::wayfire_layout_timer_t::is_connected()
{
    return timer.is_connected();
}

void pixdecor_layout_t::wayfire_layout_timer_t::set_timeout(std::uint32_t timeout_ms)
{
    timer.set_timeout(timeout_ms, [] () {});
}

pixdecor_layout_t::~pixdecor_layout_t()
{
    this->layout_areas.clear();
}

void pixdecor_layout_t::create_buttons(const std::vector<button_type_t>& buttons,
    const geometry::button_group_positions_t& positions, bool reverse_order)
{
    if (!positions.valid || (positions.buttons.size() != buttons.size()))
    {
        return;
    }

    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        const auto type_index = reverse_order ? buttons.size() - index - 1 : index;
        auto button_area = std::make_unique<decoration_area_t>(
            to_geometry(positions.buttons[index]), damage_callback, theme);
        button_area->as_button().set_button_type(buttons[type_index]);
        this->layout_areas.push_back(std::move(button_area));
    }
}

/** Regenerate layout using the new size */
void pixdecor_layout_t::resize(int width, int height)
{
    wf::option_wrapper_t<bool> maximized_borders{"vecdecor/maximized_borders"};
    wf::option_wrapper_t<int> left_button_spacing{"vecdecor/left_button_spacing"};
    wf::option_wrapper_t<int> right_button_spacing{"vecdecor/right_button_spacing"};
    wf::option_wrapper_t<int> left_button_x_offset{"vecdecor/left_button_x_offset"};
    wf::option_wrapper_t<int> right_button_x_offset{"vecdecor/right_button_x_offset"};

    const int layout_width  = std::max(0, width);
    const int layout_height = std::max(0, height);
    int border = theme.get_border_size();
    int corner_inset = maximized ? 4 : std::max(border,
        std::clamp(int(theme.rounded_corner_radius), 0,
            std::min(layout_width, layout_height) / 2));

    this->layout_areas.clear();

    if (this->theme.get_title_height() > 0)
    {
        const auto button_layout = read_button_layout();
        auto button_bounds = theme.get_button_bounds();
        button_bounds.y += border / 2;

        geometry::button_group_input_t left_input;
        left_input.button_count  = button_layout.left.size();
        left_input.button_bounds = button_bounds;
        left_input.spacing  = left_button_spacing;
        left_input.x_offset = left_button_x_offset;
        left_input.corner_inset = corner_inset;
        left_input.title_width  = layout_width;

        auto right_input = left_input;
        right_input.button_count = button_layout.right.size();
        right_input.spacing  = right_button_spacing;
        right_input.x_offset = right_button_x_offset;

        const auto left_positions  = geometry::resolve_left_group_positions(left_input);
        const auto right_positions = geometry::resolve_right_group_positions(right_input);
        create_buttons(button_layout.left, left_positions, false);
        create_buttons(button_layout.right, right_positions, true);

        /* Padding around the buttons, allows move */
        const int group_outer_padding = maximized ? 4 : border;
        const int group_y = maximized ? 4 : border;
        if (left_positions.valid && !left_positions.buttons.empty())
        {
            auto move_geometry = expanded_move_geometry(left_positions.bounds,
                group_outer_padding, group_y, theme.get_title_height());
            this->layout_areas.push_back(std::make_unique<decoration_area_t>(
                DECORATION_AREA_MOVE, move_geometry));
        }

        if (right_positions.valid && !right_positions.buttons.empty())
        {
            auto move_geometry = expanded_move_geometry(right_positions.bounds,
                layout_width - group_outer_padding, group_y, theme.get_title_height());
            this->layout_areas.push_back(std::make_unique<decoration_area_t>(
                DECORATION_AREA_MOVE, move_geometry));
        }

        /* Titlebar dragging area (for move) */
        const int64_t title_left = static_cast<int64_t>(border) +
            ((!left_positions.valid || left_positions.buttons.empty()) ?
                0 : group_outer_padding);
        int64_t title_width = static_cast<int64_t>(layout_width) - 2LL * border;
        if (right_positions.valid && !right_positions.buttons.empty())
        {
            title_width = static_cast<int64_t>(right_positions.bounds.x) - title_left;
        }

        const int title_x = static_cast<int>(std::clamp<int64_t>(title_left, 0,
            std::max(0, layout_width - 1)));
        title_width = std::clamp<int64_t>(title_width, 1,
            std::max(1, layout_width - title_x));
        const int64_t title_area_height = static_cast<int64_t>(theme.get_title_height()) +
            (maximized ? 0 : static_cast<int64_t>(border) / 2 + 1);
        wf::geometry_t title_geometry = {
            static_cast<double>(title_x),
            static_cast<double>(maximized ? 0 : border / 2),
            static_cast<double>(title_width),
            static_cast<double>(std::clamp<int64_t>(title_area_height, 1,
                std::numeric_limits<int>::max())),
        };
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_TITLE, title_geometry));

        const int cached_width = static_cast<int>(std::clamp<int64_t>(
            static_cast<int64_t>(layout_width) - 2LL * border, 0,
            std::numeric_limits<int>::max()));
        const int cached_height = static_cast<int>(std::clamp<int64_t>(
            static_cast<int64_t>(layout_height) - 2LL * border, 0,
            std::numeric_limits<int>::max()));
        this->cached_titlebar = {
            static_cast<double>(border),
            static_cast<double>(border),
            static_cast<double>(cached_width),
            static_cast<double>(cached_height),
        };
    }

    border = MIN_RESIZE_HANDLE_SIZE - theme.get_input_size();
    auto inverse_border = MIN_RESIZE_HANDLE_SIZE - theme.get_border_size();

    if (!maximized || maximized_borders)
    {
        double w = layout_width;
        double h = layout_height;

        /* Resizing edges - top */
        wf::geometry_t border_geometry =
        {0, -double(inverse_border), w + MIN_RESIZE_HANDLE_SIZE, static_cast<double>(border)};
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_RESIZE_TOP, border_geometry));

        /* Resizing edges - bottom */
        border_geometry =
        {0, h - border + inverse_border,
            w + MIN_RESIZE_HANDLE_SIZE, static_cast<double>(border)};
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_RESIZE_BOTTOM, border_geometry));

        /* Resizing edges - left */
        border_geometry =
        {-double(inverse_border), 0, static_cast<double>(border), h + MIN_RESIZE_HANDLE_SIZE};
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_RESIZE_LEFT, border_geometry));

        /* Resizing edges - right */
        border_geometry =
        {w - border + inverse_border, 0, static_cast<double>(border),
            h + MIN_RESIZE_HANDLE_SIZE};
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_RESIZE_RIGHT, border_geometry));
    }

    rebuild_input_targets();
}

/**
 * @return The decoration areas which need to be rendered, in top to bottom order.
 */
std::vector<nonstd::observer_ptr<decoration_area_t>> pixdecor_layout_t::get_renderable_areas()
{
    std::vector<nonstd::observer_ptr<decoration_area_t>> renderable;
    for (auto& area : layout_areas)
    {
        if (area->get_type() & DECORATION_AREA_RENDERABLE_BIT)
        {
            renderable.push_back({area});
        }
    }

    return renderable;
}

wf::regionf_t pixdecor_layout_t::calculate_region() const
{
    wf::regionf_t r{};
    for (auto& area : layout_areas)
    {
        auto g   = area->get_geometry();
        double b = theme.get_input_size();
        if (maximized && (area->get_type() & DECORATION_AREA_MOVE_BIT))
        {
            g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, b, b});
        }

        if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            if (b <= MIN_RESIZE_HANDLE_SIZE)
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_TOP) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_BOTTOM))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{0, 0, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_LEFT) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_RIGHT))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, 0, 0});
            }
        }

        if ((g.width > 0) && (g.height > 0))
        {
            r |= g;
        }
    }

    return r;
}

wf::regionf_t pixdecor_layout_t::limit_region(wf::regionf_t & region) const
{
    wf::regionf_t out = region & this->cached_titlebar;
    return out;
}

void pixdecor_layout_t::rebuild_input_targets()
{
    std::vector<layout_target_t> targets;
    auto add_target = [&] (std::size_t id, decoration_area_t& area, wf::geometry_t geometry)
    {
        layout_target_t target;
        target.id     = id;
        target.bounds = {geometry.x, geometry.y, geometry.width, geometry.height};
        if (area.get_type() == DECORATION_AREA_BUTTON)
        {
            target.kind   = layout_target_kind_t::button;
            target.button = area.as_button().get_button_type();
        } else if (area.get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            target.kind  = layout_target_kind_t::resize;
            target.edges = area.get_type() & ~DECORATION_AREA_RESIZE_BIT;
        } else
        {
            target.kind = layout_target_kind_t::move;
        }

        targets.push_back(target);
    };

    for (std::size_t id = 0; id < layout_areas.size(); ++id)
    {
        auto& area = *layout_areas[id];
        if (area.get_type() & DECORATION_AREA_MOVE_BIT)
        {
            continue;
        }

        auto geometry = area.get_geometry();
        const auto input_size = theme.get_input_size();
        if (area.get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            if (input_size <= MIN_RESIZE_HANDLE_SIZE)
            {
                geometry = wf::expand_geometry_by_margins(geometry,
                    wf::decoration_margins_t{input_size, input_size, input_size, input_size});
            } else if ((area.get_type() == DECORATION_AREA_RESIZE_TOP) ||
                       (area.get_type() == DECORATION_AREA_RESIZE_BOTTOM))
            {
                geometry = wf::expand_geometry_by_margins(geometry,
                    wf::decoration_margins_t{0, 0, input_size, input_size});
            } else
            {
                geometry = wf::expand_geometry_by_margins(geometry,
                    wf::decoration_margins_t{input_size, input_size, 0, 0});
            }
        }

        add_target(id, area, geometry);
    }

    for (std::size_t id = 0; id < layout_areas.size(); ++id)
    {
        auto& area = *layout_areas[id];
        if (!(area.get_type() & DECORATION_AREA_MOVE_BIT))
        {
            continue;
        }

        auto geometry = area.get_geometry();
        if (maximized)
        {
            geometry.height += theme.get_input_size();
        }

        add_target(id, area, geometry);
    }

    input_model.set_targets(std::move(targets));
}

void pixdecor_layout_t::apply_button_update(const layout_button_update_t& update)
{
    if (update.state.target_id >= layout_areas.size())
    {
        return;
    }

    auto& area = *layout_areas[update.state.target_id];
    if (area.get_type() != DECORATION_AREA_BUTTON)
    {
        return;
    }

    if (update.hover_changed)
    {
        area.as_button().set_hover(update.state.hovered);
    }

    if (update.pressed_changed)
    {
        area.as_button().set_pressed(update.state.pressed);
    }
}

/** Handle motion event to (x, y) relative to the decoration */
pixdecor_layout_t::action_response_t pixdecor_layout_t::handle_motion(
    int x, int y)
{
    this->current_input = {x, y};
    update_cursor();
    return input_model.motion(x, y);
}

/**
 * Handle press or release event.
 * @param pressed Whether the event is a press(true) or release(false) event.
 * @return The action which needs to be carried out in response to this event.
 * */
pixdecor_layout_t::action_response_t pixdecor_layout_t::handle_press_event(
    bool pressed)
{
    return pressed ? input_model.press() : input_model.release();
}

pixdecor_layout_t::action_response_t pixdecor_layout_t::handle_axis_event(
    int delta)
{
    return input_model.axis(delta);
}

/**
 * Find the layout area at the given coordinates, if any
 * @return The layout area or null on failure
 */
nonstd::observer_ptr<decoration_area_t> pixdecor_layout_t::find_area_at(
    wf::point_t point)
{
    for (auto& area : this->layout_areas)
    {
        auto g   = area->get_geometry();
        double b = theme.get_input_size();
        if (area->get_type() & DECORATION_AREA_MOVE_BIT)
        {
            continue;
        }

        if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            if (b <= MIN_RESIZE_HANDLE_SIZE)
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_TOP) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_BOTTOM))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{0, 0, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_LEFT) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_RIGHT))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, 0, 0});
            }
        }

        if (maximized && (area->get_type() & DECORATION_AREA_MOVE_BIT))
        {
            g.height += b;
        }

        if (g & point)
        {
            return {area};
        }
    }

    for (auto& area : this->layout_areas)
    {
        auto g = area->get_geometry();
        auto b = theme.get_input_size();
        if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            continue;
        }

        if (maximized && (area->get_type() & DECORATION_AREA_MOVE_BIT))
        {
            g.height += b;
        }

        if (g & point)
        {
            return {area};
        }
    }

    return nullptr;
}

/** Calculate resize edges based on @current_input */
uint32_t pixdecor_layout_t::calculate_resize_edges() const
{
    uint32_t edges = 0;
    for (auto& area : layout_areas)
    {
        auto g   = area->get_geometry();
        double b = theme.get_input_size();
        g.width  = g.width ?: 1;
        g.height = g.height ?: 1;
        if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
        {
            if (b <= MIN_RESIZE_HANDLE_SIZE)
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_TOP) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_BOTTOM))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{0, 0, b, b});
            } else if ((area->get_type() == DECORATION_AREA_RESIZE_LEFT) ||
                       (area->get_type() == DECORATION_AREA_RESIZE_RIGHT))
            {
                g = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{b, b, 0, 0});
            }
        }

        if (g & this->current_input)
        {
            if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
            {
                edges |= (area->get_type() & ~DECORATION_AREA_RESIZE_BIT);
            }
        }
    }

    return edges;
}

/** Update the cursor based on @current_input */
void pixdecor_layout_t::update_cursor()
{
    uint32_t edges = calculate_resize_edges();
    auto area = find_area_at(this->current_input);
    if (area && (area->get_type() == DECORATION_AREA_BUTTON))
    {
        wf::get_core().set_cursor("default");
        return;
    }

    auto cursor_name = edges > 0 ?
        wlr_xcursor_get_resize_name((wlr_edges)edges) : "default";
    wf::get_core().set_cursor(cursor_name);
}

void pixdecor_layout_t::set_maximize(bool state)
{
    maximized = state;
}

pixdecor_layout_t::action_response_t pixdecor_layout_t::handle_focus_lost(
    bool clear_double_click)
{
    return input_model.focus_lost(clear_double_click);
}
}
}
