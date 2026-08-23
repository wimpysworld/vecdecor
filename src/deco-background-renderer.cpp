#include "deco-background-renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wf
{
namespace pixdecor
{
namespace
{
bool valid_state(background_state_t state)
{
    switch (state)
    {
      case background_state_t::floating:
      case background_state_t::tiled:
      case background_state_t::maximised:
      case background_state_t::fullscreen:
        return true;
    }

    return false;
}

bool resolve_key(const background_render_input_t& input, background_cache_key_t& key)
{
    if (!geometry::is_valid(input.logical_size) || (input.logical_size.width <= 0) ||
        (input.logical_size.height <= 0) || !std::isfinite(input.output_scale) ||
        (input.output_scale <= 0.0) || !geometry::is_valid(input.colour) ||
        (input.corner_radius < 0) || !valid_state(input.state))
    {
        return false;
    }

    const auto raster_size     = geometry::resolve_raster_size(input.logical_size, input.output_scale);
    const double scaled_radius = static_cast<double>(input.corner_radius) * input.output_scale;
    if (!std::isfinite(scaled_radius) ||
        (scaled_radius > static_cast<double>(std::numeric_limits<int>::max())))
    {
        return false;
    }

    key.logical_size = input.logical_size;
    key.raster_size  = raster_size;
    key.output_scale = input.output_scale;
    key.active = input.active;
    key.colour = input.colour;
    key.corner_radius = input.corner_radius;
    key.raster_corner_radius = std::clamp(
        static_cast<int>(std::lround(scaled_radius)), 0,
        std::min(raster_size.width, raster_size.height) / 2);
    key.state = input.state;
    return true;
}

cairo_surface_t *render_surface(const background_cache_key_t& key)
{
    auto surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, key.raster_size.width, key.raster_size.height);
    if (!surface || (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS))
    {
        if (surface)
        {
            cairo_surface_destroy(surface);
        }

        return nullptr;
    }

    auto cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
    {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return nullptr;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    if ((key.state != background_state_t::floating) || (key.raster_corner_radius == 0))
    {
        cairo_rectangle(cr, 0, 0, key.raster_size.width, key.raster_size.height);
    } else
    {
        constexpr double PI = 3.14159265358979323846;
        const double radius = key.raster_corner_radius;
        const double width  = key.raster_size.width;
        const double height = key.raster_size.height;

        cairo_new_sub_path(cr);
        cairo_arc(cr, width - radius, radius, radius, -PI / 2.0, 0);
        cairo_arc(cr, width - radius, height - radius, radius, 0, PI / 2.0);
        cairo_arc(cr, radius, height - radius, radius, PI / 2.0, PI);
        cairo_arc(cr, radius, radius, radius, PI, 3.0 * PI / 2.0);
        cairo_close_path(cr);
    }

    cairo_set_source_rgba(cr, key.colour.r, key.colour.g, key.colour.b, key.colour.a);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    return surface;
}
}

bool operator ==(const background_cache_key_t& lhs, const background_cache_key_t& rhs)
{
    return (lhs.logical_size == rhs.logical_size) &&
           (lhs.raster_size == rhs.raster_size) &&
           (lhs.output_scale == rhs.output_scale) &&
           (lhs.active == rhs.active) &&
           (lhs.colour == rhs.colour) &&
           (lhs.corner_radius == rhs.corner_radius) &&
           (lhs.raster_corner_radius == rhs.raster_corner_radius) &&
           (lhs.state == rhs.state);
}

background_renderer_t::~background_renderer_t()
{
    if (cached_surface)
    {
        cairo_surface_destroy(cached_surface);
    }
}

background_prepare_result_t background_renderer_t::prepare(
    const background_render_input_t& input)
{
    background_cache_key_t key;
    if (!resolve_key(input, key))
    {
        return background_prepare_result_t::invalid;
    }

    if (cache_valid && (cached_key == key))
    {
        return background_prepare_result_t::cache_hit;
    }

    auto surface = render_surface(key);
    if (!surface)
    {
        return background_prepare_result_t::invalid;
    }

    if (cached_surface)
    {
        cairo_surface_destroy(cached_surface);
    }

    cached_surface = surface;
    cached_key     = key;
    cache_valid    = true;
    ++renders;
    return background_prepare_result_t::rendered;
}

cairo_surface_t*background_renderer_t::surface() const
{
    return cached_surface;
}

const background_cache_key_t*background_renderer_t::cache_key() const
{
    return cache_valid ? &cached_key : nullptr;
}

std::size_t background_renderer_t::render_count() const
{
    return renders;
}
}
}
