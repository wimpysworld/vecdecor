#include "deco-geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wf
{
namespace pixdecor
{
namespace geometry
{
namespace
{
constexpr logical_size_t AUTOMATIC_FALLBACK_SIZE = {
    SMALL_AUTOMATIC_BUTTON_SIZE,
    SMALL_AUTOMATIC_BUTTON_SIZE,
};

bool valid_enum(button_kind_t value)
{
    switch (value)
    {
      case button_kind_t::minimize:
      case button_kind_t::maximize:
      case button_kind_t::close:
        return true;
    }

    return false;
}

bool valid_enum(focus_state_t value)
{
    switch (value)
    {
      case focus_state_t::inactive:
      case focus_state_t::active:
        return true;
    }

    return false;
}

bool valid_enum(interaction_state_t value)
{
    switch (value)
    {
      case interaction_state_t::normal:
      case interaction_state_t::hover:
      case interaction_state_t::pressed:
        return true;
    }

    return false;
}

bool valid_enum(maximize_state_t value)
{
    switch (value)
    {
      case maximize_state_t::maximize:
      case maximize_state_t::restore:
        return true;
    }

    return false;
}

bool to_int(std::int64_t value, int& result)
{
    if ((value < std::numeric_limits<int>::min()) ||
        (value > std::numeric_limits<int>::max()))
    {
        return false;
    }

    result = static_cast<int>(value);
    return true;
}

bool add(int lhs, int rhs, int& result)
{
    return to_int(static_cast<std::int64_t>(lhs) + rhs, result);
}

bool subtract(int lhs, int rhs, int& result)
{
    return to_int(static_cast<std::int64_t>(lhs) - rhs, result);
}

bool multiply(std::size_t lhs, int rhs, int& result)
{
    if (rhs < 0)
    {
        return false;
    }

    if (lhs > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() /
                                       std::max(rhs, 1)))
    {
        return false;
    }

    return to_int(static_cast<std::int64_t>(lhs) * rhs, result);
}

bool valid_font_height(int font_height)
{
    if (font_height < 0)
    {
        return false;
    }

    const std::int64_t title_height = static_cast<std::int64_t>(font_height) * 3 / 2 + 8;
    return title_height <= std::numeric_limits<int>::max();
}

bool resolve_raster_dimension(int logical, double scale, int& raster)
{
    if ((logical < 0) || !std::isfinite(scale) || (scale <= 0.0))
    {
        return false;
    }

    const double scaled = static_cast<double>(logical) * scale;
    if (!std::isfinite(scaled) ||
        (scaled > static_cast<double>(std::numeric_limits<int>::max())))
    {
        return false;
    }

    const long rounded = std::lround(scaled);
    if ((rounded < 0) || (rounded > std::numeric_limits<int>::max()))
    {
        return false;
    }

    raster = std::max(1L, rounded);
    return true;
}

bool can_rasterize(const logical_size_t& logical_size, double output_scale)
{
    int width, height;
    return resolve_raster_dimension(logical_size.width, output_scale, width) &&
           resolve_raster_dimension(logical_size.height, output_scale, height);
}

geometry_result_t automatic_fallback(const geometry_input_t& input)
{
    geometry_input_t fallback;
    fallback.font_height  = valid_font_height(input.font_height) ? input.font_height : 0;
    fallback.output_scale = std::isfinite(input.output_scale) && (input.output_scale > 0.0) ?
        input.output_scale : 1.0;

    const int button = resolve_automatic_button_size(fallback.font_height);
    const int title  = resolve_automatic_title_height(fallback.font_height);
    if (!can_rasterize({button, title}, fallback.output_scale))
    {
        fallback.output_scale = 1.0;
    }

    auto result = resolve_geometry(fallback);
    result.used_automatic_fallback = true;
    return result;
}

button_group_positions_t invalid_group()
{
    button_group_positions_t result;
    result.valid = false;
    return result;
}
}

bool operator ==(const logical_size_t& lhs, const logical_size_t& rhs)
{
    return (lhs.width == rhs.width) && (lhs.height == rhs.height);
}

bool operator ==(const logical_bounds_t& lhs, const logical_bounds_t& rhs)
{
    return (lhs.x == rhs.x) && (lhs.y == rhs.y) &&
           (lhs.width == rhs.width) && (lhs.height == rhs.height);
}

bool operator ==(const svg_proportions_t& lhs, const svg_proportions_t& rhs)
{
    return (lhs.x == rhs.x) && (lhs.y == rhs.y) &&
           (lhs.width == rhs.width) && (lhs.height == rhs.height);
}

bool operator ==(const raster_size_t& lhs, const raster_size_t& rhs)
{
    return (lhs.width == rhs.width) && (lhs.height == rhs.height);
}

bool operator ==(const resolved_asset_identity_t& lhs, const resolved_asset_identity_t& rhs)
{
    return (lhs.content_hash == rhs.content_hash) &&
           (lhs.source_generation == rhs.source_generation);
}

bool operator ==(const rgba_t& lhs, const rgba_t& rhs)
{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) &&
           (lhs.b == rhs.b) && (lhs.a == rhs.a);
}

bool operator ==(const button_state_t& lhs, const button_state_t& rhs)
{
    return (lhs.kind == rhs.kind) && (lhs.focus == rhs.focus) &&
           (lhs.interaction == rhs.interaction) && (lhs.maximize == rhs.maximize);
}

bool operator ==(const button_cache_key_t& lhs, const button_cache_key_t& rhs)
{
    return (lhs.state == rhs.state) &&
           (lhs.resolved_asset_identity == rhs.resolved_asset_identity) &&
           (lhs.colour == rhs.colour) &&
           (lhs.logical_size == rhs.logical_size) &&
           (lhs.raster_size == rhs.raster_size) &&
           (lhs.svg_proportions == rhs.svg_proportions) &&
           (lhs.output_scale == rhs.output_scale) &&
           (lhs.line_thickness == rhs.line_thickness) &&
           (lhs.theme_generation == rhs.theme_generation);
}

int resolve_automatic_button_size(int font_height)
{
    return font_height >= AUTOMATIC_BUTTON_LARGE_FONT_THRESHOLD ?
           LARGE_AUTOMATIC_BUTTON_SIZE : SMALL_AUTOMATIC_BUTTON_SIZE;
}

int resolve_automatic_title_height(int font_height)
{
    if (font_height < 0)
    {
        return MINIMUM_TITLE_HEIGHT;
    }

    const std::int64_t height = static_cast<std::int64_t>(font_height) * 3 / 2 + 8;
    if (height > std::numeric_limits<int>::max())
    {
        return MINIMUM_TITLE_HEIGHT;
    }

    return std::max(static_cast<int>(height), MINIMUM_TITLE_HEIGHT);
}

bool is_valid(const logical_size_t& size)
{
    return (size.width > 0) && (size.height > 0);
}

bool is_valid(const logical_bounds_t& bounds)
{
    int right, bottom;
    return (bounds.width >= 0) && (bounds.height >= 0) &&
           add(bounds.x, bounds.width, right) && add(bounds.y, bounds.height, bottom);
}

bool is_valid(const svg_proportions_t& proportions)
{
    if (!std::isfinite(proportions.x) || !std::isfinite(proportions.y) ||
        !std::isfinite(proportions.width) || !std::isfinite(proportions.height) ||
        (proportions.x < 0.0) || (proportions.y < 0.0) ||
        (proportions.width <= 0.0) || (proportions.height <= 0.0))
    {
        return false;
    }

    const double right  = proportions.x + proportions.width;
    const double bottom = proportions.y + proportions.height;
    return std::isfinite(right) && std::isfinite(bottom) && (right <= 1.0) && (bottom <= 1.0);
}

bool is_valid(const raster_size_t& size)
{
    return (size.width > 0) && (size.height > 0);
}

bool is_valid(const resolved_asset_identity_t& identity)
{
    return identity.content_hash != 0;
}

bool is_valid(const rgba_t& colour)
{
    return std::isfinite(colour.r) && std::isfinite(colour.g) &&
           std::isfinite(colour.b) && std::isfinite(colour.a) &&
           (colour.r >= 0.0) && (colour.r <= 1.0) &&
           (colour.g >= 0.0) && (colour.g <= 1.0) &&
           (colour.b >= 0.0) && (colour.b <= 1.0) &&
           (colour.a >= 0.0) && (colour.a <= 1.0);
}

bool is_valid(const button_state_t& state)
{
    return valid_enum(state.kind) && valid_enum(state.focus) &&
           valid_enum(state.interaction) && valid_enum(state.maximize);
}

bool is_valid(const geometry_input_t& input)
{
    return valid_font_height(input.font_height) && (input.requested_button_size >= 0) &&
           (input.requested_title_height >= 0) && (input.title_height_extension >= 0) &&
           std::isfinite(input.output_scale) &&
           (input.output_scale > 0.0) && is_valid(input.svg_proportions);
}

bool is_valid(const button_group_input_t& input)
{
    const bool settings_valid = (input.spacing >= 0) &&
        (input.corner_inset >= 0) && (input.title_width >= 0);
    return settings_valid && ((input.button_count == 0) ||
        (is_valid(input.button_bounds) && (input.button_bounds.width > 0) &&
            (input.button_bounds.height > 0)));
}

bool is_valid(const cache_key_input_t& input)
{
    return is_valid(input.state) && is_valid(input.resolved_asset_identity) &&
           is_valid(input.colour) && is_valid(input.logical_size) &&
           is_valid(input.svg_proportions) &&
           std::isfinite(input.output_scale) && (input.output_scale > 0.0) &&
           std::isfinite(input.line_thickness) && (input.line_thickness >= 0.0) &&
           can_rasterize(input.logical_size, input.output_scale);
}

svg_proportions_t full_box_svg_proportions()
{
    return {};
}

svg_proportions_t resolve_svg_proportions(const svg_proportions_t& proportions)
{
    return is_valid(proportions) ? proportions : full_box_svg_proportions();
}

geometry_result_t resolve_geometry(const geometry_input_t& input)
{
    if (!is_valid(input))
    {
        return automatic_fallback(input);
    }

    const int automatic_button = resolve_automatic_button_size(input.font_height);
    const int automatic_title  = resolve_automatic_title_height(input.font_height);
    const int button = input.requested_button_size > 0 ?
        input.requested_button_size : automatic_button;
    int title = input.requested_title_height > 0 ?
        input.requested_title_height : automatic_title;

    const std::int64_t offset = input.button_y_offset;
    const std::int64_t absolute_offset    = offset < 0 ? -offset : offset;
    const std::int64_t containment_height =
        static_cast<std::int64_t>(button) + 2 * absolute_offset;
    if ((containment_height > std::numeric_limits<int>::max()) || (button <= 0))
    {
        return automatic_fallback(input);
    }

    // Preserve legacy zero/zero sizing, where the title stays font-derived despite the button offset.
    if ((input.requested_button_size > 0) || (input.requested_title_height > 0))
    {
        title = std::max(title, static_cast<int>(containment_height));
    }

    if (!add(title, input.title_height_extension, title))
    {
        return automatic_fallback(input);
    }

    if (!can_rasterize({button, title}, input.output_scale))
    {
        return automatic_fallback(input);
    }

    int button_y;
    if (!to_int((static_cast<std::int64_t>(title) - button) / 2 + offset, button_y))
    {
        return automatic_fallback(input);
    }

    geometry_result_t result;
    result.button_size     = {button, button};
    result.title_height    = title;
    result.button_bounds   = {0, button_y, button, button};
    result.title_bounds    = {0, 0, button, title};
    result.svg_proportions = input.svg_proportions;
    result.output_scale    = input.output_scale;
    return result;
}

bool contains(const logical_bounds_t& bounds, const logical_point_t& point)
{
    int right, bottom;
    return is_valid(bounds) && add(bounds.x, bounds.width, right) &&
           add(bounds.y, bounds.height, bottom) &&
           (point.x >= bounds.x) && (point.x < right) &&
           (point.y >= bounds.y) && (point.y < bottom);
}

bool contains(const logical_bounds_t& outer, const logical_bounds_t& inner)
{
    int outer_right, outer_bottom, inner_right, inner_bottom;
    return is_valid(outer) && is_valid(inner) &&
           add(outer.x, outer.width, outer_right) &&
           add(outer.y, outer.height, outer_bottom) &&
           add(inner.x, inner.width, inner_right) &&
           add(inner.y, inner.height, inner_bottom) &&
           (inner.x >= outer.x) && (inner.y >= outer.y) &&
           (inner_right <= outer_right) && (inner_bottom <= outer_bottom);
}

button_group_positions_t resolve_left_group_positions(const button_group_input_t& input)
{
    if (!is_valid(input))
    {
        return invalid_group();
    }

    button_group_positions_t result;
    if (input.button_count == 0)
    {
        return result;
    }

    int step, span, group_width, start_x, end_x;
    if (!add(input.button_bounds.width, input.spacing, step) ||
        !multiply(input.button_count - 1, step, span) ||
        !add(span, input.button_bounds.width, group_width) ||
        !add(input.corner_inset, input.x_offset, start_x) ||
        !add(start_x, group_width, end_x))
    {
        return invalid_group();
    }

    result.buttons.reserve(input.button_count);
    int x = start_x;
    for (std::size_t index = 0; index < input.button_count; ++index)
    {
        result.buttons.push_back({
                        x,
                        input.button_bounds.y,
                        input.button_bounds.width,
                        input.button_bounds.height,
                    });

        if ((index + 1 < input.button_count) && !add(x, step, x))
        {
            return invalid_group();
        }
    }

    result.bounds = {
        start_x,
        input.button_bounds.y,
        group_width,
        input.button_bounds.height,
    };
    return result;
}

button_group_positions_t resolve_right_group_positions(const button_group_input_t& input)
{
    if (!is_valid(input))
    {
        return invalid_group();
    }

    button_group_positions_t result;
    if (input.button_count == 0)
    {
        return result;
    }

    int step, span, group_width, right_edge, rightmost_x, leftmost_x;
    if (!add(input.button_bounds.width, input.spacing, step) ||
        !multiply(input.button_count - 1, step, span) ||
        !add(span, input.button_bounds.width, group_width) ||
        !subtract(input.title_width, input.corner_inset, right_edge) ||
        !add(right_edge, input.x_offset, right_edge) ||
        !subtract(right_edge, input.button_bounds.width, rightmost_x) ||
        !subtract(rightmost_x, span, leftmost_x))
    {
        return invalid_group();
    }

    result.buttons.reserve(input.button_count);
    int x = rightmost_x;
    for (std::size_t index = 0; index < input.button_count; ++index)
    {
        result.buttons.push_back({
                        x,
                        input.button_bounds.y,
                        input.button_bounds.width,
                        input.button_bounds.height,
                    });

        if ((index + 1 < input.button_count) && !subtract(x, step, x))
        {
            return invalid_group();
        }
    }

    result.bounds = {
        leftmost_x,
        input.button_bounds.y,
        group_width,
        input.button_bounds.height,
    };
    return result;
}

raster_size_t resolve_raster_size(const logical_size_t& logical_size, double output_scale)
{
    raster_size_t result;
    if (!resolve_raster_dimension(logical_size.width, output_scale, result.width) ||
        !resolve_raster_dimension(logical_size.height, output_scale, result.height))
    {
        return {1, 1};
    }

    return result;
}

button_cache_key_t resolve_cache_key(const cache_key_input_t& input)
{
    button_cache_key_t result;
    result.state = is_valid(input.state) ? input.state : button_state_t{};
    result.resolved_asset_identity = is_valid(input.resolved_asset_identity) ?
        input.resolved_asset_identity : resolved_asset_identity_t{};
    result.colour = is_valid(input.colour) ? input.colour : rgba_t{};
    result.svg_proportions = resolve_svg_proportions(input.svg_proportions);
    result.line_thickness  = std::isfinite(input.line_thickness) &&
        (input.line_thickness >= 0.0) ? input.line_thickness : DEFAULT_BUTTON_LINE_THICKNESS;
    result.theme_generation = input.theme_generation;

    logical_size_t logical_size = is_valid(input.logical_size) ?
        input.logical_size : AUTOMATIC_FALLBACK_SIZE;
    double output_scale = std::isfinite(input.output_scale) && (input.output_scale > 0.0) ?
        input.output_scale : 1.0;
    if (!can_rasterize(logical_size, output_scale))
    {
        logical_size = AUTOMATIC_FALLBACK_SIZE;
        if (!can_rasterize(logical_size, output_scale))
        {
            output_scale = 1.0;
        }
    }

    result.logical_size = logical_size;
    result.raster_size  = resolve_raster_size(logical_size, output_scale);
    result.output_scale = output_scale;
    return result;
}
}
}
}
