#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wf
{
namespace pixdecor
{
namespace geometry
{
constexpr int AUTOMATIC_BUTTON_LARGE_FONT_THRESHOLD = 20;
constexpr int SMALL_AUTOMATIC_BUTTON_SIZE = 18;
constexpr int LARGE_AUTOMATIC_BUTTON_SIZE = 26;
constexpr int MINIMUM_TITLE_HEIGHT = 20;

struct logical_point_t
{
    int x = 0;
    int y = 0;
};

struct logical_size_t
{
    int width  = 0;
    int height = 0;
};

struct logical_bounds_t
{
    int x     = 0;
    int y     = 0;
    int width = 0;
    int height = 0;
};

struct svg_proportions_t
{
    double x     = 0.0;
    double y     = 0.0;
    double width = 1.0;
    double height = 1.0;
};

struct raster_size_t
{
    int width  = 1;
    int height = 1;
};

enum class button_kind_t
{
    minimize,
    maximize,
    close,
};

enum class focus_state_t
{
    inactive,
    active,
};

enum class interaction_state_t
{
    normal,
    hover,
    pressed,
};

enum class maximize_state_t
{
    maximize,
    restore,
};

struct button_state_t
{
    button_kind_t kind  = button_kind_t::close;
    focus_state_t focus = focus_state_t::inactive;
    interaction_state_t interaction = interaction_state_t::normal;
    maximize_state_t maximize = maximize_state_t::maximize;
};

struct geometry_input_t
{
    int font_height = 0;
    int requested_button_size  = 0;
    int requested_title_height = 0;
    int title_height_extension = 0;
    int button_y_offset = 0;
    double output_scale = 1.0;
    svg_proportions_t svg_proportions;
};

struct geometry_result_t
{
    logical_size_t button_size;
    int title_height = MINIMUM_TITLE_HEIGHT;
    logical_bounds_t button_bounds;
    logical_bounds_t title_bounds;
    svg_proportions_t svg_proportions;
    double output_scale = 1.0;
    bool used_automatic_fallback = false;
};

struct button_group_input_t
{
    std::size_t button_count = 0;
    logical_bounds_t button_bounds;
    int spacing  = 0;
    int x_offset = 0;
    int corner_inset = 0;
    int title_width  = 0;
};

struct button_group_positions_t
{
    std::vector<logical_bounds_t> buttons;
    logical_bounds_t bounds;
    bool valid = true;
};

struct cache_key_input_t
{
    button_state_t state;
    logical_size_t logical_size;
    svg_proportions_t svg_proportions;
    double output_scale = 1.0;
    std::uint64_t theme_generation = 0;
};

/**
 * Keep logical size and exact scale in the key because distinct render requests can round to the same raster
 * size.
 */
struct button_cache_key_t
{
    button_state_t state;
    logical_size_t logical_size;
    raster_size_t raster_size;
    svg_proportions_t svg_proportions;
    double output_scale = 1.0;
    std::uint64_t theme_generation = 0;
};

bool operator ==(const logical_size_t& lhs, const logical_size_t& rhs);
bool operator ==(const logical_bounds_t& lhs, const logical_bounds_t& rhs);
bool operator ==(const svg_proportions_t& lhs, const svg_proportions_t& rhs);
bool operator ==(const raster_size_t& lhs, const raster_size_t& rhs);
bool operator ==(const button_state_t& lhs, const button_state_t& rhs);
bool operator ==(const button_cache_key_t& lhs, const button_cache_key_t& rhs);

int resolve_automatic_button_size(int font_height);
int resolve_automatic_title_height(int font_height);

bool is_valid(const logical_size_t& size);
bool is_valid(const logical_bounds_t& bounds);
bool is_valid(const svg_proportions_t& proportions);
bool is_valid(const raster_size_t& size);
bool is_valid(const button_state_t& state);
bool is_valid(const geometry_input_t& input);
bool is_valid(const button_group_input_t& input);
bool is_valid(const cache_key_input_t& input);

svg_proportions_t full_box_svg_proportions();
/** Return full-box proportions when the supplied proportions are invalid. */
svg_proportions_t resolve_svg_proportions(const svg_proportions_t& proportions);
geometry_result_t resolve_geometry(const geometry_input_t& input);

bool contains(const logical_bounds_t& bounds, const logical_point_t& point);
bool contains(const logical_bounds_t& outer, const logical_bounds_t& inner);

button_group_positions_t resolve_left_group_positions(const button_group_input_t& input);
button_group_positions_t resolve_right_group_positions(const button_group_input_t& input);

raster_size_t resolve_raster_size(const logical_size_t& logical_size, double output_scale);
button_cache_key_t resolve_cache_key(const cache_key_input_t& input);
}
}
}
