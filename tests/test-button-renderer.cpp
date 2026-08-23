#include "deco-button-renderer.hpp"
#include "test-cairo-support.hpp"

#include <algorithm>
#include <array>
#include <cairo.h>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
using namespace wf::pixdecor;
using namespace wf::pixdecor::geometry;
using namespace test_cairo;

constexpr std::array<button_asset_t, 4> ASSETS = {
    button_asset_t::minimize,
    button_asset_t::maximize,
    button_asset_t::restore,
    button_asset_t::close,
};
constexpr std::array<button_state_variant_t, 4> VARIANTS = {
    button_state_variant_t::active,
    button_state_variant_t::active_hover,
    button_state_variant_t::inactive,
    button_state_variant_t::inactive_hover,
};
constexpr std::array<std::array<const char*, 4>, 4> FILENAMES = {{
    {{"minimize.svg", "minimize-hover.svg", "minimize-inactive.svg",
        "minimize-inactive-hover.svg"}},
    {{"maximize.svg", "maximize-hover.svg", "maximize-inactive.svg",
        "maximize-inactive-hover.svg"}},
    {{"restore.svg", "restore-hover.svg", "restore-inactive.svg",
        "restore-inactive-hover.svg"}},
    {{"close.svg", "close-hover.svg", "close-inactive.svg",
        "close-inactive-hover.svg"}},
}};

std::size_t index(button_asset_t asset)
{
    return static_cast<std::size_t>(asset);
}

std::size_t index(button_state_variant_t variant)
{
    return static_cast<std::size_t>(variant);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct lifecycle_t
{
    int parses  = 0;
    int rasters = 0;
    int uploads = 0;
    int context_runs  = 0;
    int live_textures = 0;
    int destroyed_outside_context = 0;
    bool context_available = true;
    bool in_context = false;
    std::uint64_t serial = 1;
};

class fake_texture_t : public uploaded_button_texture_t
{
  public:
    fake_texture_t(lifecycle_t& lifecycle, cairo_surface_t *surface,
        std::uint64_t serial) : lifecycle(lifecycle), serial(serial)
    {
        digest = surface_digest(surface);
        width  = cairo_image_surface_get_width(surface);
        height = cairo_image_surface_get_height(surface);
        centre = surface_pixel(surface, width / 2, height / 3);
        corner = surface_pixel(surface, width / 2, std::min(3, height - 1));
        visible_pixel_count = visible_pixels(surface);
        ++lifecycle.live_textures;
    }

    ~fake_texture_t() override
    {
        if (!lifecycle.in_context)
        {
            ++lifecycle.destroyed_outside_context;
        }

        --lifecycle.live_textures;
    }

    const wf::owned_texture_t *wayfire_texture() const override
    {
        return nullptr;
    }

    std::uint64_t identity() const override
    {
        return serial;
    }

    lifecycle_t& lifecycle;
    std::uint64_t serial;
    std::uint64_t digest;
    std::uint32_t centre;
    std::uint32_t corner;
    std::size_t visible_pixel_count;
    int width;
    int height;
};

button_renderer_dependencies_t dependencies(lifecycle_t& lifecycle)
{
    return {
        [&] (const std::string& path)
        {
            ++lifecycle.parses;
            return load_svg_button_document(path);
        },
        [&] (const loaded_button_asset_t& asset, const button_cache_key_t& key)
        {
            ++lifecycle.rasters;
            return rasterize_button_asset(asset, key);
        },
        [&] (cairo_surface_t *surface)
        {
            ++lifecycle.uploads;
            return std::make_unique<fake_texture_t>(lifecycle, surface, lifecycle.serial++);
        },
        [&] (const std::function<void()>& callback)
        {
            ++lifecycle.context_runs;
            if (!lifecycle.context_available)
            {
                return false;
            }

            lifecycle.in_context = true;
            callback();
            lifecycle.in_context = false;
            return true;
        },
    };
}

button_source_config_t bundled_sources(const std::string& directory,
    std::uint64_t generation = 1)
{
    button_source_config_t result;
    result.generation = generation;
    for (auto asset : ASSETS)
    {
        for (auto variant : VARIANTS)
        {
            result.sources[index(asset)][index(variant)] = {
                directory + "/" + FILENAMES[index(asset)][index(variant)],
                button_render_mode_t::full_colour,
            };
        }
    }

    return result;
}

button_prepare_config_t preparation(std::uint64_t generation = 1)
{
    return {
        .logical_size     = {34, 34},
        .svg_proportions  = full_box_svg_proportions(),
        .output_scale     = 1.0,
        .line_thickness   = DEFAULT_BUTTON_LINE_THICKNESS,
        .theme_generation = generation,
        .palette = {
            {0.9, 0.1, 0.2, 1.0},
            {0.2, 0.3, 0.4, 1.0},
            {0.1, 0.8, 0.2, 1.0},
            {0.2, 0.1, 0.9, 1.0},
        },
    };
}

std::string make_svg_fixture(const std::string& contents)
{
    std::array<char, 64> path{};
    const std::string pattern = "/tmp/vecdecor-button-svg-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), path.begin());
    const int descriptor = mkstemp(path.data());
    require(descriptor >= 0, "Could not create the SVG fixture");
    close(descriptor);

    std::ofstream output(path.data());
    output << contents;
    output.close();
    require(output.good(), "Could not write the SVG fixture");
    return path.data();
}

button_state_t state_for(button_asset_t asset, button_state_variant_t variant,
    interaction_state_t interaction = interaction_state_t::normal)
{
    button_state_t state;
    state.kind = asset == button_asset_t::minimize ? button_kind_t::minimize :
        asset == button_asset_t::close ? button_kind_t::close : button_kind_t::maximize;
    state.maximize = asset == button_asset_t::restore ? maximize_state_t::restore :
        maximize_state_t::maximize;
    state.focus = (variant == button_state_variant_t::active ||
        variant == button_state_variant_t::active_hover) ?
        focus_state_t::active : focus_state_t::inactive;
    state.interaction = interaction;
    if (((variant == button_state_variant_t::active_hover) ||
         (variant == button_state_variant_t::inactive_hover)) &&
        (interaction == interaction_state_t::normal))
    {
        state.interaction = interaction_state_t::hover;
    }

    return state;
}

const fake_texture_t& texture_for(button_renderer_t& renderer, const button_state_t& state,
    const button_prepare_config_t& config)
{
    const auto *texture = dynamic_cast<const fake_texture_t*>(renderer.lookup(state, config));
    require(texture != nullptr, "A prepared state has no texture");
    return *texture;
}

void verify_source_mapping()
{
    const auto directory = default_button_asset_directory();
    require(!directory.empty() && directory.front() == '/',
        "The installed button directory is not absolute");
    for (auto asset : ASSETS)
    {
        const auto bundled = resolve_button_source_specs("", asset);
        const auto custom  = resolve_button_source_specs("custom.svg", asset);
        for (auto variant : VARIANTS)
        {
            const auto variant_value = index(variant);
            require(bundled[variant_value].path == directory + "/" +
                FILENAMES[index(asset)][variant_value], "A bundled source path is wrong");
            require(bundled[variant_value].render_mode == button_render_mode_t::full_colour,
                "A bundled source does not use full-colour mode");
            require(custom[variant_value].path == "custom.svg" &&
                custom[variant_value].render_mode == button_render_mode_t::recoloured_mask,
                "A custom source does not override every variant as a mask");
        }
    }

    for (auto focus : {focus_state_t::active, focus_state_t::inactive})
    {
        for (auto interaction : {interaction_state_t::normal, interaction_state_t::hover,
             interaction_state_t::pressed})
        {
            button_state_t state = {button_kind_t::close, focus, interaction,
                maximize_state_t::maximize};
            const bool active   = focus == focus_state_t::active;
            const bool hover    = interaction != interaction_state_t::normal;
            const auto expected = active ?
                (hover ? button_state_variant_t::active_hover : button_state_variant_t::active) :
                (hover ? button_state_variant_t::inactive_hover :
                    button_state_variant_t::inactive);
            require(resolve_button_state_variant(state) == expected,
                "A button state selected the wrong source variant");
        }
    }
}

void verify_asset_directory(const std::string& directory)
{
    DIR *handle = opendir(directory.c_str());
    require(handle != nullptr, "The button asset directory cannot be opened");
    std::vector<std::string> files;
    while (auto *entry = readdir(handle))
    {
        const std::string name = entry->d_name;
        if ((name.size() > 4) && (name.substr(name.size() - 4) == ".svg"))
        {
            files.push_back(name);
        }
    }

    closedir(handle);
    require(files.size() == 16, "The button asset directory does not contain exactly 16 SVGs");

    const auto key = resolve_cache_key({
            .state = {},
            .resolved_asset_identity = {1, 1},
            .colour = {1.0, 1.0, 1.0, 1.0},
            .background_colour = {},
            .logical_size    = {34, 34},
            .svg_proportions = full_box_svg_proportions(),
        });
    for (auto asset : ASSETS)
    {
        for (auto variant : VARIANTS)
        {
            button_source_spec_t source = {
                directory + "/" + FILENAMES[index(asset)][index(variant)],
                button_render_mode_t::full_colour,
            };
            const auto loaded = load_svg_button_asset(source, asset, variant, 1);
            require(loaded.valid_svg, "A bundled SVG did not load");
            const auto surface = rasterize_button_asset(loaded, key);
            require(surface && (cairo_surface_status(surface.get()) == CAIRO_STATUS_SUCCESS),
                "A bundled SVG did not render");
        }
    }
}

void verify_svg_fallbacks(const std::string& directory)
{
    const auto transparent_path = make_svg_fixture(
        "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'>"
        "<rect width='32' height='32' fill='none'/></svg>");
    const std::array<std::pair<std::string, bool>, 3> SOURCES = {{
        {directory + "/missing.svg", false},
        {directory + "/../../tests/fixtures/malformed.svg", false},
        {transparent_path, true},
    }};
    constexpr std::array<button_render_mode_t, 2> MODES = {
        button_render_mode_t::full_colour,
        button_render_mode_t::recoloured_mask,
    };
    for (const auto& source_case : SOURCES)
    {
        for (auto mode : MODES)
        {
            for (auto asset : ASSETS)
            {
                for (auto focus : {focus_state_t::active, focus_state_t::inactive})
                {
                    for (auto interaction : {interaction_state_t::normal,
                         interaction_state_t::hover, interaction_state_t::pressed})
                    {
                        const auto variant = focus == focus_state_t::active ?
                            (interaction == interaction_state_t::normal ?
                                button_state_variant_t::active :
                                button_state_variant_t::active_hover) :
                            (interaction == interaction_state_t::normal ?
                                button_state_variant_t::inactive :
                                button_state_variant_t::inactive_hover);
                        const auto state = state_for(asset, variant, interaction);
                        const button_source_spec_t source = {source_case.first, mode};
                        const auto loaded = load_svg_button_asset(source, asset, variant, 1);
                        require(loaded.valid_svg == source_case.second,
                            "An SVG fallback source has the wrong parse state");
                        require(loaded.render_mode == mode,
                            "An SVG fallback source lost its render mode");

                        loaded_button_asset_t fallback;
                        fallback.identity    = loaded.identity;
                        fallback.variant     = variant;
                        fallback.render_mode = mode;
                        const auto key = resolve_cache_key({
                                .state = state,
                                .resolved_asset_identity = loaded.identity,
                                .colour = {0.8, 0.2, 0.1, 1.0},
                                .background_colour = interaction == interaction_state_t::pressed ?
                                    rgba_t{0.3, 0.1, 0.6, 0.7} : rgba_t{0.1, 0.2, 0.3, 0.7},
                                .logical_size    = {34, 34},
                                .svg_proportions = full_box_svg_proportions(),
                                .line_thickness  = DEFAULT_BUTTON_LINE_THICKNESS,
                            });
                        const auto source_surface = rasterize_button_asset(loaded, key);
                        const auto direct_surface = rasterize_button_asset(fallback, key);
                        require(source_surface && direct_surface,
                            "An SVG procedural fallback did not render");
                        require(visible_pixels(source_surface.get()) > 0,
                            "An SVG procedural fallback produced a transparent control");
                        require(surface_digest(source_surface.get()) ==
                            surface_digest(direct_surface.get()),
                            "An SVG source did not select its exact-state procedural fallback");
                    }
                }
            }
        }
    }

    unlink(transparent_path.c_str());
}

void verify_fallback_line_thickness()
{
    loaded_button_asset_t fallback;
    fallback.identity = {1, 1};
    button_cache_key_t key = resolve_cache_key({
            .state = state_for(button_asset_t::minimize, button_state_variant_t::active),
            .resolved_asset_identity = fallback.identity,
            .colour = {1.0, 1.0, 1.0, 1.0},
            .background_colour = {},
            .logical_size    = {64, 64},
            .svg_proportions = full_box_svg_proportions(),
            .line_thickness  = 0.7,
        });
    const auto thin = rasterize_button_asset(fallback, key);
    key.line_thickness = 4.0;
    const auto thick = rasterize_button_asset(fallback, key);
    require(thin && thick && (visible_pixels(thick.get()) > visible_pixels(thin.get())),
        "The procedural fallback ignored the configured line thickness");
}

button_source_config_t custom_sources(const std::string& path, std::uint64_t generation)
{
    button_source_config_t result;
    result.generation = generation;
    for (auto asset : ASSETS)
    {
        result.sources[index(asset)] = resolve_button_source_specs(path, asset);
    }

    return result;
}

void verify_parse_cache_and_state_keys()
{
    const auto first_path = make_svg_fixture(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
        "<path fill='white' d='M2 7h12v2H2z'/></svg>");
    const auto second_path = make_svg_fixture(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
        "<path fill='white' d='M3 3h10v10H3z'/></svg>");
    lifecycle_t lifecycle;
    {
        button_renderer_t renderer(dependencies(lifecycle));
        const auto shared = custom_sources(first_path, 30);
        require(renderer.reload_sources(shared), "The shared custom source matrix did not load");
        require(lifecycle.parses == 1,
            "A shared custom source was parsed more than once during reload");
        require(!(renderer.source(button_asset_t::minimize,
            button_state_variant_t::active).identity == renderer.source(
                button_asset_t::close, button_state_variant_t::active).identity),
            "A texture source identity omitted the control asset");
        require(!(renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity == renderer.source(
                button_asset_t::close, button_state_variant_t::inactive).identity),
            "A texture source identity omitted the state variant");

        const auto base = preparation(30);
        require(renderer.prepare(base), "The shared custom source matrix did not prepare");
        const int prepared_parses = lifecycle.parses;
        require(renderer.prepare(base), "The shared custom source matrix did not prepare twice");
        const auto lookup_state = state_for(
            button_asset_t::close, button_state_variant_t::active);
        for (int frame = 0; frame < 120; ++frame)
        {
            require(renderer.lookup(lookup_state, base) != nullptr,
                "An animation-frame lookup missed a prepared texture");
        }

        require(lifecycle.parses == prepared_parses,
            "A repeated prepare or animation-frame lookup parsed an SVG");

        const auto base_key    = renderer.resolve_key(lookup_state, base);
        const auto base_digest = texture_for(renderer, lookup_state, base).digest;

        auto sized = base;
        sized.logical_size = {41, 29};
        require(renderer.prepare(sized), "The changed-dimensions matrix did not prepare");
        require(!(renderer.resolve_key(lookup_state, sized) == base_key) &&
            (texture_for(renderer, lookup_state, sized).digest != base_digest),
            "Changed dimensions did not change the texture key and digest");

        auto scaled = base;
        scaled.output_scale = 1.5;
        require(renderer.prepare(scaled), "The changed-scale matrix did not prepare");
        require(!(renderer.resolve_key(lookup_state, scaled) == base_key) &&
            (texture_for(renderer, lookup_state, scaled).digest != base_digest),
            "Changed output scale did not change the texture key and digest");

        auto themed = base;
        themed.theme_generation = 31;
        themed.palette.active   = {0.1, 0.7, 0.9, 1.0};
        require(renderer.prepare(themed), "The changed-theme matrix did not prepare");
        require(!(renderer.resolve_key(lookup_state, themed) == base_key) &&
            (texture_for(renderer, lookup_state, themed).digest != base_digest),
            "Changed theme colours did not change the texture key and digest");
        require(renderer.prepare(base), "The baseline matrix did not restore after a theme change");

        const auto inactive_state = state_for(
            button_asset_t::close, button_state_variant_t::inactive);
        require(!(renderer.resolve_key(inactive_state, base) == base_key) &&
            (texture_for(renderer, inactive_state, base).digest != base_digest),
            "Changed focus did not change the texture key and digest");

        const auto hover_state = state_for(button_asset_t::close,
            button_state_variant_t::active_hover, interaction_state_t::hover);
        require(!(renderer.resolve_key(hover_state, base) == base_key) &&
            (texture_for(renderer, hover_state, base).digest != base_digest),
            "Changed interaction did not change the texture key and digest");

        const auto maximize_state = state_for(
            button_asset_t::maximize, button_state_variant_t::active);
        const auto restore_state = state_for(
            button_asset_t::restore, button_state_variant_t::active);
        require(!(renderer.resolve_key(maximize_state, base) ==
            renderer.resolve_key(restore_state, base)) &&
            (texture_for(renderer, maximize_state, base).identity() !=
                texture_for(renderer, restore_state, base).identity()),
            "Changed maximise state did not change the texture key and texture");

        const auto old_source_identity = renderer.source(
            button_asset_t::close, button_state_variant_t::active).identity;
        const auto changed_specs = resolve_button_source_specs(
            second_path, button_asset_t::close);
        const int source_parses = lifecycle.parses;
        require(renderer.reload_source(button_asset_t::close, changed_specs, 30),
            "The changed glyph source did not load");
        require(lifecycle.parses == source_parses + 1,
            "One changed custom asset did not add exactly one parse");
        require(!(renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity == old_source_identity),
            "A texture source identity omitted the content hash");
        require(renderer.prepare(base), "The changed glyph source did not prepare");
        require(!(renderer.resolve_key(lookup_state, base) == base_key) &&
            (texture_for(renderer, lookup_state, base).digest != base_digest),
            "Changed glyph source did not change the texture key and digest");

        auto full_colour_specs = changed_specs;
        for (auto& spec : full_colour_specs)
        {
            spec.render_mode = button_render_mode_t::full_colour;
        }

        const auto mask_identity = renderer.source(
            button_asset_t::close, button_state_variant_t::active).identity;
        require(renderer.reload_source(button_asset_t::close, full_colour_specs, 30),
            "The changed render mode did not load");
        require(!(renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity == mask_identity),
            "A texture source identity omitted the render mode");
        const auto generation_identity = renderer.source(
            button_asset_t::close, button_state_variant_t::active).identity;
        require(renderer.reload_source(button_asset_t::close, full_colour_specs, 31),
            "The changed source generation did not load");
        require(!(renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity == generation_identity),
            "A texture source identity omitted the source generation");
    }

    unlink(first_path.c_str());
    unlink(second_path.c_str());
}

void verify_palette_cache_keys(const std::string& directory)
{
    enum class palette_member_t
    {
        active,
        inactive,
        hover,
        pressed,
    };

    struct palette_case_t
    {
        palette_member_t member;
        button_state_t affected;
        button_state_t unaffected;
    };

    const auto active_normal = state_for(button_asset_t::close,
        button_state_variant_t::active);
    const auto inactive_normal = state_for(button_asset_t::close,
        button_state_variant_t::inactive);
    const auto active_hover = state_for(button_asset_t::close,
        button_state_variant_t::active_hover, interaction_state_t::hover);
    const auto active_pressed = state_for(button_asset_t::close,
        button_state_variant_t::active_hover, interaction_state_t::pressed);
    const std::array<palette_case_t, 4> CASES = {{
        {palette_member_t::active, active_normal, inactive_normal},
        {palette_member_t::inactive, inactive_normal, active_normal},
        {palette_member_t::hover, active_hover, active_pressed},
        {palette_member_t::pressed, active_pressed, active_hover},
    }};

    for (const auto& item : CASES)
    {
        lifecycle_t lifecycle;
        button_renderer_t renderer(dependencies(lifecycle));
        require(renderer.reload_sources(custom_sources(directory + "/minimize.svg", 7)),
            "The custom palette matrix did not load");
        auto before = preparation(7);
        before.palette.active.a   = 0.8;
        before.palette.inactive.a = 0.8;
        before.palette.hover.a    = 0.8;
        before.palette.pressed.a  = 0.8;
        require(renderer.prepare(before), "The custom palette matrix did not prepare");
        const auto affected_before    = renderer.resolve_key(item.affected, before);
        const auto unaffected_before  = renderer.resolve_key(item.unaffected, before);
        const auto unaffected_texture = texture_for(renderer, item.unaffected, before).identity();

        auto after = before;
        switch (item.member)
        {
          case palette_member_t::active:
            after.palette.active = {0.1, 0.7, 0.9, 0.8};
            break;

          case palette_member_t::inactive:
            after.palette.inactive = {0.8, 0.6, 0.1, 0.8};
            break;

          case palette_member_t::hover:
            after.palette.hover = {0.7, 0.1, 0.5, 0.8};
            break;

          case palette_member_t::pressed:
            after.palette.pressed = {0.6, 0.2, 0.7, 0.8};
            break;
        }

        require(after.theme_generation == before.theme_generation,
            "A palette-only change altered the theme generation");
        require(!(renderer.resolve_key(item.affected, after) == affected_before),
            "A palette member did not change its custom mask cache key");
        require(renderer.resolve_key(item.unaffected, after) == unaffected_before,
            "A palette member changed an unrelated custom mask cache key");
        require(renderer.prepare(after), "A changed custom palette matrix did not prepare");
        require(texture_for(renderer, item.unaffected, after).identity() == unaffected_texture,
            "A palette member replaced an unrelated custom mask texture");
    }
}

void verify_renderer_regressions(const std::string& directory)
{
    lifecycle_t lifecycle;
    {
        button_renderer_t renderer(dependencies(lifecycle));
        auto sources = custom_sources(directory + "/minimize.svg", 10);
        require(renderer.reload_sources(sources), "The regression source matrix did not load");
        auto config = preparation(10);
        require(renderer.prepare(config), "The regression matrix did not prepare");
        const int exact_parses  = lifecycle.parses;
        const int exact_rasters = lifecycle.rasters;
        const int exact_uploads = lifecycle.uploads;
        require(renderer.prepare(config), "The repeated regression matrix did not prepare");
        require((lifecycle.parses == exact_parses) && (lifecycle.rasters == exact_rasters) &&
            (lifecycle.uploads == exact_uploads), "An exact repeated prepare performed work");

        const auto active = state_for(button_asset_t::close, button_state_variant_t::active);
        auto scaled = config;
        scaled.output_scale = 1.5;
        require(renderer.prepare(scaled), "The scaled regression matrix did not prepare");
        require((texture_for(renderer, active, scaled).width == 51) &&
            (texture_for(renderer, active, scaled).height == 51),
            "The scaled regression matrix has the wrong raster size");
        const auto scaled_identity = texture_for(renderer, active, scaled).identity();

        auto sized = config;
        sized.logical_size = {20, 22};
        sized.output_scale = 1.25;
        require(renderer.prepare(sized), "The sized regression matrix did not prepare");
        require((texture_for(renderer, active, sized).width == 25) &&
            (texture_for(renderer, active, sized).height == 28),
            "The sized regression matrix has the wrong raster size");
        const int matrix_rasters = lifecycle.rasters;
        const int matrix_uploads = lifecycle.uploads;
        require(renderer.prepare(scaled) && renderer.prepare(config),
            "A prepared output matrix could not be reused");
        require((lifecycle.rasters == matrix_rasters) && (lifecycle.uploads == matrix_uploads) &&
            (texture_for(renderer, active, scaled).identity() == scaled_identity),
            "Reusing a prepared output matrix performed work");

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            auto bounded = config;
            bounded.logical_size     = {20 + iteration, 22 + iteration};
            bounded.output_scale     = 1.0 + iteration * 0.125;
            bounded.palette.active.r = 0.05 * iteration;
            require(renderer.prepare(bounded), "A bounded cache matrix did not prepare");
            require(renderer.cache_size() <= 192, "A matrix change exceeded the cache bound");
        }

        for (std::uint64_t generation = 11; generation < 17; ++generation)
        {
            auto themed = config;
            themed.theme_generation = generation;
            require(renderer.prepare(themed), "A theme generation matrix did not prepare");
            require(renderer.cache_size() <= 192, "A theme change exceeded the cache bound");
        }

        auto current = config;
        current.theme_generation = 16;
        require(renderer.prepare(current), "The reload baseline did not prepare");
        const auto old_maximize_key = renderer.resolve_key(
            state_for(button_asset_t::maximize, button_state_variant_t::active), current);
        const auto close_source_identity = renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity;
        const auto close_texture_identity = texture_for(renderer, active, current).identity();
        const auto maximize_specs = resolve_button_source_specs(
            directory + "/maximize.svg", button_asset_t::maximize);
        require(renderer.reload_source(button_asset_t::maximize, maximize_specs, 17),
            "A single control matrix did not reload");
        require(renderer.lookup(old_maximize_key) == nullptr,
            "A single control reload retained a stale cache entry");
        require(renderer.source(button_asset_t::close,
            button_state_variant_t::active).identity == close_source_identity,
            "A single control reload replaced an unrelated source identity");
        require(texture_for(renderer, active, current).identity() == close_texture_identity,
            "A single control reload replaced an unrelated texture");
        require(renderer.cache_size() <= 192, "A source change exceeded the cache bound");

        for (std::uint64_t generation = 18; generation < 24; ++generation)
        {
            const auto path = generation % 2 == 0 ? directory + "/minimize.svg" :
                directory + "/maximize.svg";
            const auto repeated_specs = resolve_button_source_specs(path,
                button_asset_t::maximize);
            const auto stale_key = renderer.resolve_key(
                state_for(button_asset_t::maximize, button_state_variant_t::active), current);
            require(renderer.reload_source(button_asset_t::maximize, repeated_specs, generation),
                "A repeated control source change did not load");
            require((renderer.lookup(stale_key) == nullptr) &&
                (renderer.cache_size() <= 192),
                "A repeated source change retained stale or excessive cache entries");
            require(renderer.prepare(current), "A repeated source matrix did not prepare");
            require(texture_for(renderer, active, current).identity() == close_texture_identity,
                "A repeated source change replaced an unrelated texture");
        }
    }

    require((lifecycle.live_textures == 0) &&
        (lifecycle.destroyed_outside_context == 0),
        "The renderer destroyed a texture outside its context");

    lifecycle_t failed_context;
    {
        button_renderer_t renderer(dependencies(failed_context));
        require(renderer.reload_sources(bundled_sources(directory, 20)),
            "The failed-context source matrix did not load");
        require(renderer.prepare(preparation(20)),
            "The failed-context texture matrix did not prepare");
        require(failed_context.live_textures == 16,
            "The failed-context texture matrix is incomplete");
        failed_context.context_available = false;
    }

    require((failed_context.live_textures == 16) &&
        (failed_context.destroyed_outside_context == 0),
        "Failed context entry did not retain all textures safely");
}

void verify_renderer(const std::string& directory)
{
    lifecycle_t lifecycle;
    button_renderer_t renderer(dependencies(lifecycle));
    auto sources = bundled_sources(directory);
    require(renderer.reload_sources(sources), "The bundled matrix did not load");
    require(lifecycle.parses == 16, "The renderer did not parse all 16 bundled sources");
    for (auto asset : ASSETS)
    {
        for (auto variant : VARIANTS)
        {
            const auto& source = renderer.source(asset, variant);
            require(source.valid_svg && source.variant == variant &&
                source.render_mode == button_render_mode_t::full_colour,
                "A loaded bundled source has the wrong identity");
        }
    }

    auto config = preparation();
    require(renderer.prepare(config), "The bundled matrix did not prepare");
    require((lifecycle.rasters == 16) && (lifecycle.uploads == 16) &&
        (renderer.cache_size() == 16), "The bundled matrix did not create 16 textures");

    constexpr std::array<std::uint32_t, 4> ACTIVE_CIRCLES = {
        0xfff9e2afU, 0xffa6e3a1U, 0xffa6e3a1U, 0xfff38ba8U,
    };
    std::array<std::array<std::uint64_t, 4>, 4> digests;
    for (auto asset : ASSETS)
    {
        for (auto variant : VARIANTS)
        {
            const auto state    = state_for(asset, variant);
            const auto& texture = texture_for(renderer, state, config);
            digests[index(asset)][index(variant)] = texture.digest;
            const bool active = variant == button_state_variant_t::active ||
                variant == button_state_variant_t::active_hover;
            require(texture.centre == (active ? ACTIVE_CIRCLES[index(asset)] : 0xff45475aU),
                "A bundled circle colour changed during rendering");
            const bool hover = variant == button_state_variant_t::active_hover ||
                variant == button_state_variant_t::inactive_hover;
            require(texture.corner == (hover ? 0xff313244U : 0U),
                "A bundled hover background colour changed during rendering");
        }

        for (auto focus : {focus_state_t::active, focus_state_t::inactive})
        {
            const auto variant = focus == focus_state_t::active ?
                button_state_variant_t::active_hover : button_state_variant_t::inactive_hover;
            const auto hover   = state_for(asset, variant, interaction_state_t::hover);
            const auto pressed = state_for(asset, variant, interaction_state_t::pressed);
            require(texture_for(renderer, hover, config).identity() ==
                texture_for(renderer, pressed, config).identity(),
                "A bundled pressed state did not reuse its hover texture");
        }
    }

    for (auto variant : VARIANTS)
    {
        for (std::size_t first = 0; first < ASSETS.size(); ++first)
        {
            for (std::size_t second = first + 1; second < ASSETS.size(); ++second)
            {
                require(digests[first][index(variant)] != digests[second][index(variant)],
                    "Distinct bundled controls produced the same full-surface digest");
            }
        }
    }

    auto recoloured = config;
    recoloured.theme_generation = 2;
    recoloured.palette = {
        {0.0, 1.0, 0.0, 1.0},
        {1.0, 0.0, 0.0, 1.0},
        {1.0, 1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0, 1.0},
    };
    require(renderer.prepare(recoloured), "The palette-independent matrix did not prepare");
    require((lifecycle.rasters == 16) && (lifecycle.uploads == 16),
        "A palette change rerendered full-colour bundled assets");
    for (auto asset : ASSETS)
    {
        for (auto variant : VARIANTS)
        {
            require(texture_for(renderer, state_for(asset, variant), recoloured).digest ==
                digests[index(asset)][index(variant)],
                "A palette change altered a bundled full-colour texture");
        }
    }

    const auto custom = resolve_button_source_specs(directory + "/minimize.svg",
        button_asset_t::close);
    const int custom_parses = lifecycle.parses;
    require(renderer.reload_source(button_asset_t::close, custom, 2),
        "The custom control did not reload");
    require(lifecycle.parses == custom_parses + 1,
        "Reloading one custom option did not parse its shared path exactly once");
    for (auto variant : VARIANTS)
    {
        require(renderer.source(button_asset_t::close, variant).render_mode ==
            button_render_mode_t::recoloured_mask,
            "A custom option variant did not use mask recolouring");
    }

    auto custom_config = preparation(3);
    require(renderer.prepare(custom_config), "The custom mask matrix did not prepare");
    const auto custom_active = texture_for(renderer,
        state_for(button_asset_t::close, button_state_variant_t::active), custom_config);
    const auto custom_hover = texture_for(renderer,
        state_for(button_asset_t::close, button_state_variant_t::active_hover), custom_config);
    require(((custom_active.centre >> 24) == 0xffU) &&
        (((custom_active.centre >> 16) & 0xffU) > 200U) &&
        (((custom_active.centre >> 8) & 0xffU) < 50U) &&
        ((custom_active.centre & 0xffU) < 80U),
        "The active custom mask did not use the configured glyph colour");
    require(custom_hover.corner != 0xff313244U,
        "A custom mask retained the bundled hover background");
    const auto custom_active_digest = custom_active.digest;
    auto changed_custom = custom_config;
    changed_custom.theme_generation = 4;
    changed_custom.palette.active   = {0.0, 1.0, 0.0, 1.0};
    require(renderer.prepare(changed_custom), "The recoloured custom mask did not prepare");
    require(texture_for(renderer,
        state_for(button_asset_t::close, button_state_variant_t::active),
        changed_custom).digest != custom_active_digest,
        "A custom mask ignored the configured palette");

    auto damaged = sources;
    damaged.generation = 5;
    damaged.sources[index(button_asset_t::maximize)]
    [index(button_state_variant_t::active_hover)].path = directory + "/missing.svg";
    damaged.sources[index(button_asset_t::restore)]
    [index(button_state_variant_t::inactive)].path = directory + "/../meson.build";
    damaged.sources[index(button_asset_t::minimize)]
    [index(button_state_variant_t::inactive)].path = "";
    require(renderer.reload_sources(damaged), "The damaged variant matrix did not load");
    require(!renderer.source(button_asset_t::maximize,
        button_state_variant_t::active_hover).valid_svg,
        "A missing variant did not select the procedural fallback");
    require(!renderer.source(button_asset_t::restore,
        button_state_variant_t::inactive).valid_svg,
        "A malformed variant did not select the procedural fallback");
    require(!renderer.source(button_asset_t::minimize,
        button_state_variant_t::inactive).valid_svg,
        "A blank variant did not select the procedural fallback");
    require(renderer.source(button_asset_t::maximize,
        button_state_variant_t::active).valid_svg &&
        renderer.source(button_asset_t::restore,
            button_state_variant_t::inactive_hover).valid_svg,
        "A damaged variant changed another variant");

    auto fallback_config = preparation(5);
    require(renderer.prepare(fallback_config), "The damaged variant matrix did not prepare");
    require(texture_for(renderer,
        state_for(button_asset_t::maximize, button_state_variant_t::active),
        fallback_config).digest == digests[index(button_asset_t::maximize)]
        [index(button_state_variant_t::active)],
        "A missing hover variant changed the normal variant");
    require(texture_for(renderer,
        state_for(button_asset_t::maximize, button_state_variant_t::active_hover),
        fallback_config).digest != digests[index(button_asset_t::maximize)]
        [index(button_state_variant_t::active_hover)],
        "A missing hover variant did not use the exact-state fallback");
}
}

int main(int argc, char **argv)
{
    try {
        require((argc == 2) || (argc == 3),
            "The test needs the source and optional staged button directories");
        verify_source_mapping();
        verify_asset_directory(argv[1]);
        verify_svg_fallbacks(argv[1]);
        verify_fallback_line_thickness();
        if (argc == 3)
        {
            verify_asset_directory(std::string(argv[2]) + "/usr/share/vecdecor/buttons");
        }

        verify_renderer(argv[1]);
        verify_parse_cache_and_state_keys();
        verify_palette_cache_keys(argv[1]);
        verify_renderer_regressions(argv[1]);
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
