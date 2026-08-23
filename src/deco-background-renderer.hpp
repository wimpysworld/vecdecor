#pragma once

#include "deco-geometry.hpp"

#include <cairo.h>
#include <cstddef>

namespace wf
{
namespace pixdecor
{
enum class background_state_t
{
    floating,
    tiled,
    maximised,
    fullscreen,
};

struct background_render_input_t
{
    geometry::logical_size_t logical_size;
    double output_scale = 1.0;
    bool active = false;
    geometry::rgba_t colour;
    int corner_radius = 0;
    background_state_t state = background_state_t::floating;
};

struct background_cache_key_t
{
    geometry::logical_size_t logical_size;
    geometry::raster_size_t raster_size;
    double output_scale = 1.0;
    bool active = false;
    geometry::rgba_t colour;
    int corner_radius = 0;
    int raster_corner_radius = 0;
    background_state_t state = background_state_t::floating;
};

bool operator ==(const background_cache_key_t& lhs, const background_cache_key_t& rhs);

enum class background_prepare_result_t
{
    invalid,
    cache_hit,
    rendered,
};

class background_renderer_t
{
  public:
    background_renderer_t() = default;
    ~background_renderer_t();

    background_renderer_t(const background_renderer_t&) = delete;
    background_renderer_t& operator =(const background_renderer_t&) = delete;

    background_prepare_result_t prepare(const background_render_input_t& input);
    cairo_surface_t *surface() const;
    const background_cache_key_t *cache_key() const;
    std::size_t render_count() const;

  private:
    cairo_surface_t *cached_surface = nullptr;
    background_cache_key_t cached_key;
    bool cache_valid    = false;
    std::size_t renders = 0;
};
}
}
