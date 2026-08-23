#include "deco-background-renderer.hpp"
#include "test-cairo-support.hpp"

#include <cairo.h>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
using namespace wf::pixdecor;
using namespace test_cairo;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

background_render_input_t floating_input()
{
    return {
        .logical_size = {34, 46},
        .output_scale = 1.5,
        .active = true,
        .colour = {0.25, 0.5, 0.75, 1.0},
        .corner_radius = 8,
        .state = background_state_t::floating,
    };
}

bool transparent(cairo_surface_t *surface, int x, int y)
{
    return (surface_pixel(surface, x, y) & 0xff000000U) == 0;
}

void require_transparent_corners(cairo_surface_t *surface, bool expected,
    const std::string& state)
{
    const int right  = cairo_image_surface_get_width(surface) - 1;
    const int bottom = cairo_image_surface_get_height(surface) - 1;
    require(transparent(surface, 0, 0) == expected &&
        transparent(surface, right, 0) == expected &&
        transparent(surface, 0, bottom) == expected &&
        transparent(surface, right, bottom) == expected,
        state + " has the wrong corner pixels");
}

void verify_state_sequence()
{
    background_renderer_t renderer;
    auto input = floating_input();

    require(renderer.prepare(input) == background_prepare_result_t::rendered,
        "the floating background did not render");
    require_transparent_corners(renderer.surface(), true, "floating");
    const auto floating_digest = surface_digest(renderer.surface());

    for (const auto state : {background_state_t::tiled, background_state_t::maximised,
         background_state_t::fullscreen})
    {
        input.state = state;
        require(renderer.prepare(input) == background_prepare_result_t::rendered,
            "a non-floating state reused the previous cache entry");
        require_transparent_corners(renderer.surface(), false, "non-floating");
    }

    input.state = background_state_t::floating;
    require(renderer.prepare(input) == background_prepare_result_t::rendered,
        "restored floating state reused the fullscreen cache entry");
    require_transparent_corners(renderer.surface(), true, "restored floating");
    require(surface_digest(renderer.surface()) == floating_digest,
        "restored floating state lost its rounded output");

    const int width  = cairo_image_surface_get_width(renderer.surface());
    const int height = cairo_image_surface_get_height(renderer.surface());
    require((width == 51) && (height == 69),
        "the renderer did not reuse the WW-234 logical size and scale contract");
    require(visible_pixels(renderer.surface()) < static_cast<std::size_t>(width * height),
        "a built-in shadow filled the transparent rounded-corner area");
}

void require_invalidation(const background_render_input_t& base,
    const background_render_input_t& changed, const std::string& field)
{
    background_renderer_t renderer;
    require(renderer.prepare(base) == background_prepare_result_t::rendered,
        "the " + field + " baseline did not render");
    require(renderer.prepare(changed) == background_prepare_result_t::rendered,
        "a changed " + field + " reused the background cache");
    require(renderer.render_count() == 2,
        "a changed " + field + " did not create one background surface");
}

void verify_cache_key()
{
    background_renderer_t renderer;
    const auto base = floating_input();
    require(renderer.prepare(base) == background_prepare_result_t::rendered,
        "the cache baseline did not render");
    const auto *surface = renderer.surface();
    require(renderer.prepare(base) == background_prepare_result_t::cache_hit &&
        (renderer.surface() == surface) && (renderer.render_count() == 1),
        "an identical background request missed the retained cache");

    auto changed = base;
    changed.logical_size.width = 35;
    require_invalidation(base, changed, "logical dimensions");

    changed = base;
    changed.output_scale = 1.5001;
    require_invalidation(base, changed, "exact output scale");

    changed = base;
    changed.colour.r = 0.5;
    require_invalidation(base, changed, "active colour");

    changed = base;
    changed.active = false;
    require_invalidation(base, changed, "active state");

    changed = base;
    changed.corner_radius = 9;
    require_invalidation(base, changed, "corner radius");

    changed = base;
    changed.state = background_state_t::tiled;
    require_invalidation(base, changed, "window state");

    changed = base;
    changed.output_scale = 2.0;
    require_invalidation(base, changed, "raster dimensions");
    background_renderer_t key_renderer;
    require(key_renderer.prepare(changed) == background_prepare_result_t::rendered &&
        key_renderer.cache_key()->logical_size == base.logical_size &&
        key_renderer.cache_key()->raster_size == geometry::raster_size_t{68, 92} &&
        (key_renderer.cache_key()->output_scale == 2.0),
        "the cache key lost its logical, raster, or scale dimensions");

    changed = base;
    changed.output_scale = 1.5001;
    require(key_renderer.prepare(base) == background_prepare_result_t::rendered &&
        key_renderer.prepare(changed) == background_prepare_result_t::rendered &&
        key_renderer.cache_key()->raster_size == geometry::raster_size_t{51, 69},
        "the exact-scale test did not preserve equal raster dimensions");
}
}

int main()
{
    verify_state_sequence();
    verify_cache_key();
    std::cout << "background renderer tests passed\n";
    return 0;
}
