#include "deco-button-renderer.hpp"

#include <array>
#include <cairo.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace
{
using namespace wf::pixdecor;
using namespace wf::pixdecor::geometry;

struct lifecycle_t
{
    int loads   = 0;
    int rasters = 0;
    int uploads = 0;
    int context_runs  = 0;
    int live_textures = 0;
    int destroyed_outside_context = 0;
    bool in_context = false;
    std::uint64_t next_identity = 1;
};

std::uint64_t surface_digest(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    std::uint64_t digest = 14695981039346656037ULL;
    for (int i = 0; i < height * stride; ++i)
    {
        digest ^= data[i];
        digest *= 1099511628211ULL;
    }

    return digest;
}

class fake_texture_t : public uploaded_button_texture_t
{
  public:
    fake_texture_t(lifecycle_t& lifecycle, cairo_surface_t *surface) : lifecycle(lifecycle)
    {
        serial = lifecycle.next_identity++;
        digest = surface_digest(surface);
        width  = cairo_image_surface_get_width(surface);
        height = cairo_image_surface_get_height(surface);
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
    std::uint64_t serial = 0;
    std::uint64_t digest = 0;
    int width  = 0;
    int height = 0;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

const fake_texture_t& texture_for(button_renderer_t& renderer,
    const button_state_t& state, const button_prepare_config_t& config)
{
    auto texture = renderer.lookup(state, config);
    require(texture != nullptr, "The prepared matrix has a missing texture");
    auto fake = dynamic_cast<const fake_texture_t*>(texture);
    require(fake != nullptr, "The prepared texture has an unexpected type");
    return *fake;
}

button_source_config_t valid_sources(const std::string& directory, std::uint64_t generation)
{
    return {{
        directory + "/minimize.svg",
        directory + "/maximize.svg",
        directory + "/restore.svg",
        directory + "/close.svg",
    }, generation};
}

button_prepare_config_t preparation(std::uint64_t generation = 1)
{
    return {
        .logical_size     = {18, 18},
        .svg_proportions  = full_box_svg_proportions(),
        .output_scale     = 1.0,
        .line_thickness   = DEFAULT_BUTTON_LINE_THICKNESS,
        .theme_generation = generation,
        .palette = {
            {0.9, 0.1, 0.2, 1.0},
            {0.2, 0.3, 0.4, 0.7},
            {0.1, 0.8, 0.2, 0.9},
            {0.2, 0.1, 0.9, 1.0},
        },
    };
}

std::size_t visible_pixels(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    const int width   = cairo_image_surface_get_width(surface);
    const int height  = cairo_image_surface_get_height(surface);
    const int stride  = cairo_image_surface_get_stride(surface) / sizeof(std::uint32_t);
    const auto pixels = reinterpret_cast<const std::uint32_t*>(
        cairo_image_surface_get_data(surface));
    std::size_t count = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            count += (pixels[y * stride + x] & 0xff000000U) != 0;
        }
    }

    return count;
}

void verify_default_source_paths()
{
    constexpr std::array<button_asset_t, 4> assets = {
        button_asset_t::minimize,
        button_asset_t::maximize,
        button_asset_t::restore,
        button_asset_t::close,
    };
    constexpr std::array<const char*, 4> names = {
        "minimize.svg",
        "maximize.svg",
        "restore.svg",
        "close.svg",
    };

    const auto asset_directory = default_button_asset_directory();
    require(!asset_directory.empty() && (asset_directory[0] == '/'),
        "The installed owned SVG directory is not absolute");

    std::array<std::string, 4> resolved;
    for (std::size_t index = 0; index < assets.size(); ++index)
    {
        resolved[index] = resolve_button_source_path("", assets[index]);
        require(resolved[index] == asset_directory + "/" + names[index],
            "An empty option did not resolve its installed owned SVG");
        require(!resolved[index].empty() && (resolved[index][0] == '/'),
            "An empty option did not resolve to an absolute path");
    }

    std::array<char, 4096> working_directory{};
    require(getcwd(working_directory.data(), working_directory.size()) != nullptr,
        "Could not read the test working directory");
    require(chdir("/") == 0, "Could not change the test working directory");
    for (std::size_t index = 0; index < assets.size(); ++index)
    {
        require(resolve_button_source_path("", assets[index]) == resolved[index],
            "The working directory changed an owned SVG path");
    }

    require(chdir(working_directory.data()) == 0,
        "Could not restore the test working directory");
    require(resolve_button_source_path("relative/custom.svg", button_asset_t::close) ==
        "relative/custom.svg", "A configured SVG path was not preserved");
}

void verify_staged_default_sources(const std::string& staged_root)
{
    constexpr std::array<button_asset_t, 4> assets = {
        button_asset_t::minimize,
        button_asset_t::maximize,
        button_asset_t::restore,
        button_asset_t::close,
    };
    for (auto asset : assets)
    {
        const auto resolved = resolve_button_source_path("", asset);
        const auto loaded   = load_svg_button_asset(staged_root + resolved, asset, 1);
        require(loaded.valid_svg,
            "An empty option did not reach its installed owned SVG in the staged root");
    }
}

void verify_configured_fallback_thickness()
{
    loaded_button_asset_t fallback;
    button_cache_key_t key;
    key.state.kind = button_kind_t::minimize;
    key.colour     = {1.0, 1.0, 1.0, 1.0};
    key.logical_size    = {64, 64};
    key.raster_size     = {64, 64};
    key.svg_proportions = full_box_svg_proportions();
    key.line_thickness  = 0.7;
    auto thin = rasterize_button_asset(fallback, key);
    require(thin != nullptr, "The thin procedural fallback did not rasterise");

    key.line_thickness = 4.0;
    auto thick = rasterize_button_asset(fallback, key);
    require(thick != nullptr, "The thick procedural fallback did not rasterise");
    require(visible_pixels(thick.get()) > visible_pixels(thin.get()),
        "The configured line thickness did not change the procedural fallback width");
}

std::string make_malformed_svg()
{
    std::array<char, 64> path{};
    const std::string pattern = "/tmp/vecdecor-malformed-svg-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), path.begin());
    const int descriptor = mkstemp(path.data());
    require(descriptor >= 0, "Could not create the malformed SVG fixture");
    close(descriptor);

    std::ofstream output(path.data());
    output << "<svg><path";
    output.close();
    require(output.good(), "Could not write the malformed SVG fixture");
    return path.data();
}

void verify_full_matrix(button_renderer_t& renderer, const button_prepare_config_t& config)
{
    constexpr std::array<button_kind_t, 3> kinds = {
        button_kind_t::minimize,
        button_kind_t::maximize,
        button_kind_t::close,
    };
    constexpr std::array<focus_state_t, 2> focuses = {
        focus_state_t::inactive,
        focus_state_t::active,
    };
    constexpr std::array<interaction_state_t, 3> interactions = {
        interaction_state_t::normal,
        interaction_state_t::hover,
        interaction_state_t::pressed,
    };
    constexpr std::array<maximize_state_t, 2> maximize_states = {
        maximize_state_t::maximize,
        maximize_state_t::restore,
    };

    std::size_t count = 0;
    for (auto kind : kinds)
    {
        for (auto focus : focuses)
        {
            for (auto interaction : interactions)
            {
                for (auto maximize : maximize_states)
                {
                    const auto& texture = texture_for(
                        renderer, {kind, focus, interaction, maximize}, config);
                    require((texture.width == 18) && (texture.height == 18),
                        "A prepared texture has the wrong raster size");
                    require(texture.digest != 0, "A prepared texture has no pixel digest");
                    ++count;
                }
            }
        }
    }

    require(count == 36, "The prepared matrix has the wrong size");
    require(renderer.cache_size() == count, "The cache does not contain the full matrix");
}
}

int main(int argc, char **argv)
{
    try {
        require((argc == 2) || (argc == 3),
            "The test needs the source button asset directory");
        verify_default_source_paths();
        if (argc == 3)
        {
            verify_staged_default_sources(argv[2]);
        }

        verify_configured_fallback_thickness();
        lifecycle_t lifecycle;

        button_renderer_dependencies_t dependencies;
        dependencies.loader = [&] (const std::string& path, button_asset_t asset,
                                   std::uint64_t generation)
        {
            ++lifecycle.loads;
            return load_svg_button_asset(path, asset, generation);
        };
        dependencies.rasterizer = [&] (const loaded_button_asset_t& asset,
                                       const button_cache_key_t& key)
        {
            ++lifecycle.rasters;
            return rasterize_button_asset(asset, key);
        };
        dependencies.uploader = [&] (cairo_surface_t *surface)
        {
            ++lifecycle.uploads;
            return std::unique_ptr<uploaded_button_texture_t>(
                std::make_unique<fake_texture_t>(lifecycle, surface));
        };
        dependencies.run_in_context = [&] (const std::function<void()>& callback)
        {
            ++lifecycle.context_runs;
            lifecycle.in_context = true;
            callback();
            lifecycle.in_context = false;
            return true;
        };

        const std::string malformed_path = make_malformed_svg();
        {
            button_renderer_t renderer(dependencies);
            auto sources = valid_sources(argv[1], 1);
            require(renderer.reload_sources(sources), "The initial SVG load failed");
            require(lifecycle.loads == 4, "Each initial SVG source must load once");
            const auto initial_maximize_identity = renderer.source(button_asset_t::maximize).identity;
            require(is_valid(initial_maximize_identity), "The initial SVG identity is invalid");
            require(!(initial_maximize_identity == renderer.source(button_asset_t::restore).identity),
                "Different SVG content produced the same identity");
            require(renderer.reload_sources(sources), "The exact source reload failed");
            require(lifecycle.loads == 4, "An exact source reload parsed an SVG again");
            require(renderer.source(button_asset_t::maximize).identity == initial_maximize_identity,
                "An exact source reload changed the SVG identity");

            auto config = preparation();
            require(renderer.prepare(config), "The initial matrix preparation failed");
            require((lifecycle.rasters == 36) && (lifecycle.uploads == 36),
                "The initial matrix did not rasterise and upload each key once");
            verify_full_matrix(renderer, config);

            const button_state_t active_normal = {
                button_kind_t::close,
                focus_state_t::active,
                interaction_state_t::normal,
                maximize_state_t::maximize,
            };
            const button_state_t inactive_normal = {
                button_kind_t::close,
                focus_state_t::inactive,
                interaction_state_t::normal,
                maximize_state_t::maximize,
            };
            const auto first_identity  = texture_for(renderer, active_normal, config).identity();
            const auto active_digest   = texture_for(renderer, active_normal, config).digest;
            const auto inactive_digest = texture_for(renderer, inactive_normal, config).digest;
            require(active_digest != inactive_digest,
                "The active and inactive colours produced the same pixels");
            const auto hover_digest = texture_for(renderer, {
                button_kind_t::close,
                focus_state_t::active,
                interaction_state_t::hover,
                maximize_state_t::maximize,
            }, config).digest;
            const auto pressed_digest = texture_for(renderer, {
                button_kind_t::close,
                focus_state_t::active,
                interaction_state_t::pressed,
                maximize_state_t::maximize,
            }, config).digest;
            require((active_digest != hover_digest) && (hover_digest != pressed_digest),
                "The interaction colours produced equal pixels");

            const int exact_rasters = lifecycle.rasters;
            const int exact_uploads = lifecycle.uploads;
            require(renderer.prepare(config), "The exact matrix preparation failed");
            require((lifecycle.rasters == exact_rasters) && (lifecycle.uploads == exact_uploads),
                "The exact matrix preparation repeated work");
            require(texture_for(renderer, active_normal, config).identity() == first_identity,
                "The exact matrix preparation replaced a texture");

            const int frame_loads   = lifecycle.loads;
            const int frame_rasters = lifecycle.rasters;
            const int frame_uploads = lifecycle.uploads;
            for (int i = 0; i < 100; ++i)
            {
                require(renderer.lookup(active_normal, config) != nullptr,
                    "A frame lookup missed a prepared texture");
            }

            require((lifecycle.loads == frame_loads) && (lifecycle.rasters == frame_rasters) &&
                (lifecycle.uploads == frame_uploads), "A frame lookup performed renderer work");

            auto recoloured = config;
            recoloured.palette.active = {0.7, 0.6, 0.1, 1.0};
            require(renderer.prepare(recoloured), "The colour matrix preparation failed");
            require(lifecycle.loads == frame_loads, "A colour change reloaded an SVG");
            require((lifecycle.rasters == exact_rasters + 6) &&
                (lifecycle.uploads == exact_uploads + 6),
                "A colour change did not replace only active normal textures");
            require(texture_for(renderer, active_normal, recoloured).digest != active_digest,
                "A colour change did not change the pixels");
            require(renderer.cache_size() == 42,
                "A colour change removed reusable entries from the current generation");

            const int before_scale_rasters = lifecycle.rasters;
            const int before_scale_uploads = lifecycle.uploads;
            auto scaled = recoloured;
            scaled.output_scale = 1.5;
            require(renderer.prepare(scaled), "The scaled matrix preparation failed");
            require((lifecycle.rasters == before_scale_rasters + 36) &&
                (lifecycle.uploads == before_scale_uploads + 36),
                "A scale change did not replace the complete matrix");
            require(texture_for(renderer, active_normal, scaled).width == 27,
                "A scale change produced the wrong raster width");
            const auto scaled_identity = texture_for(renderer, active_normal, scaled).identity();
            require(renderer.lookup(active_normal, recoloured) != nullptr,
                "A scale change removed the texture for another output");

            auto sized = recoloured;
            sized.logical_size = {20, 22};
            sized.output_scale = 1.25;
            require(renderer.prepare(sized), "The second size matrix preparation failed");
            require(texture_for(renderer, active_normal, sized).width == 25,
                "A logical size change produced the wrong raster width");
            require(texture_for(renderer, active_normal, sized).height == 28,
                "A logical size change produced the wrong raster height");
            const int multiple_rasters = lifecycle.rasters;
            const int multiple_uploads = lifecycle.uploads;
            require(renderer.prepare(scaled) && renderer.prepare(recoloured),
                "A prepared output matrix could not be selected again");
            require((lifecycle.rasters == multiple_rasters) &&
                (lifecycle.uploads == multiple_uploads),
                "Switching between prepared output matrices repeated renderer work");
            require(texture_for(renderer, active_normal, scaled).identity() == scaled_identity,
                "Switching output matrices replaced a prepared texture");

            const int before_theme_rasters = lifecycle.rasters;
            auto regenerated = scaled;
            regenerated.theme_generation = 2;
            require(renderer.prepare(regenerated), "The theme matrix preparation failed");
            require(lifecycle.rasters == before_theme_rasters + 36,
                "A theme generation did not replace the complete matrix");
            require(lifecycle.loads == frame_loads, "A theme generation reloaded an SVG");
            require(renderer.cache_size() == 36, "A theme generation left stale cache entries");

            auto fallback_sources = valid_sources(argv[1], 2);
            fallback_sources.paths[1] = std::string(argv[1]) + "/missing-maximize.svg";
            const auto minimize_identity = renderer.source(button_asset_t::minimize).identity;
            const auto restore_identity  = renderer.source(button_asset_t::restore).identity;
            const auto close_identity    = renderer.source(button_asset_t::close).identity;
            const auto close_texture_identity = texture_for(renderer, active_normal,
                regenerated).identity();
            const button_state_t maximize_normal = {
                button_kind_t::maximize,
                focus_state_t::active,
                interaction_state_t::normal,
                maximize_state_t::maximize,
            };
            const auto old_maximize_key   = renderer.resolve_key(maximize_normal, regenerated);
            const int before_reload_loads = lifecycle.loads;
            require(renderer.reload_sources(fallback_sources), "The fallback source reload failed");
            require(lifecycle.loads == before_reload_loads + 1,
                "One changed source loaded more than one SVG");
            require(renderer.source(button_asset_t::maximize).identity.source_generation == 2,
                "The SVG identity did not store the source generation");
            require(!renderer.source(button_asset_t::maximize).valid_svg,
                "The missing maximise SVG did not select the procedural fallback");
            require(renderer.source(button_asset_t::minimize).identity == minimize_identity,
                "A maximise reload replaced the minimise source identity");
            require(renderer.source(button_asset_t::restore).identity == restore_identity,
                "A maximise reload replaced the restore source identity");
            require(renderer.source(button_asset_t::close).identity == close_identity,
                "A maximise reload replaced the close source identity");
            require(renderer.lookup(old_maximize_key) == nullptr,
                "A replaced source identity remained in the cache");
            require(texture_for(renderer, active_normal, regenerated).identity() ==
                close_texture_identity, "A maximise reload replaced a reusable close texture");
            require(renderer.cache_size() == 30,
                "A source reload removed cache entries for other controls");

            const int before_direct_reload = lifecycle.loads;
            require(renderer.reload_source(button_asset_t::restore, malformed_path, 3),
                "The per-source reload failed");
            require(lifecycle.loads == before_direct_reload + 1,
                "A per-source reload loaded more than one SVG");
            require(!renderer.source(button_asset_t::restore).valid_svg,
                "The malformed SVG did not select the procedural fallback");
            require(renderer.source(button_asset_t::close).identity == close_identity,
                "A restore reload replaced the close source identity");
            require(renderer.cache_size() == 24,
                "A second source reload removed cache entries for other controls");

            auto fallback_config = preparation(3);
            require(renderer.prepare(fallback_config), "The fallback matrix preparation failed");
            verify_full_matrix(renderer, fallback_config);
            const auto maximize_digest = texture_for(renderer, {
                button_kind_t::maximize,
                focus_state_t::active,
                interaction_state_t::normal,
                maximize_state_t::maximize,
            }, fallback_config).digest;
            const auto restore_digest = texture_for(renderer, {
                button_kind_t::maximize,
                focus_state_t::active,
                interaction_state_t::normal,
                maximize_state_t::restore,
            }, fallback_config).digest;
            require(maximize_digest != restore_digest,
                "The restore fallback is not distinct from maximise");

            const int thickness_loads   = lifecycle.loads;
            const auto fallback_texture = texture_for(renderer, maximize_normal,
                fallback_config).identity();
            auto thick_fallback_config = fallback_config;
            thick_fallback_config.line_thickness = 3.0;
            require(renderer.prepare(thick_fallback_config),
                "The thick fallback matrix preparation failed");
            require(lifecycle.loads == thickness_loads,
                "A line thickness change reloaded an SVG source");
            require(texture_for(renderer, maximize_normal,
                thick_fallback_config).identity() != fallback_texture,
                "A line thickness change reused a stale fallback texture");
            require(texture_for(renderer, maximize_normal,
                thick_fallback_config).digest != maximize_digest,
                "A line thickness change did not change fallback pixels");
            require(renderer.lookup(maximize_normal, fallback_config) != nullptr,
                "A line thickness change did not separate fallback cache entries");

            for (int index = 0; index < 12; ++index)
            {
                auto output_config = fallback_config;
                output_config.logical_size = {20 + index, 22 + index};
                output_config.output_scale = 1.0 + index * 0.125;
                require(renderer.prepare(output_config),
                    "A bounded output matrix preparation failed");
                require(renderer.cache_size() <= 288,
                    "Output matrix changes exceeded the cache bound");
                require(renderer.lookup(active_normal, output_config) != nullptr,
                    "Cache eviction removed the current output matrix");
            }

            auto current_config = fallback_config;
            for (std::uint64_t generation = 4; generation < 14; ++generation)
            {
                current_config.theme_generation = generation;
                require(renderer.prepare(current_config),
                    "A repeated theme generation preparation failed");
                require(renderer.cache_size() == 36,
                    "A repeated theme generation retained stale entries");
            }

            const auto reusable_close_texture =
                texture_for(renderer, active_normal, current_config).identity();
            for (std::uint64_t generation = 10; generation < 20; ++generation)
            {
                const std::string path = generation % 2 == 0 ?
                    std::string(argv[1]) + "/maximize.svg" :
                    std::string(argv[1]) + "/missing-maximize.svg";
                const int loads = lifecycle.loads;
                require(renderer.reload_source(button_asset_t::maximize, path, generation),
                    "A repeated source generation reload failed");
                require(lifecycle.loads == loads + 1,
                    "A repeated source generation loaded more than one SVG");
                require(renderer.cache_size() == 30,
                    "A repeated source generation removed another control");
                require(renderer.prepare(current_config),
                    "A repeated source generation preparation failed");
                require(renderer.cache_size() == 36,
                    "A repeated source generation retained stale entries");
                require(texture_for(renderer, active_normal, current_config).identity() ==
                    reusable_close_texture,
                    "A repeated source generation replaced a reusable close texture");
            }
        }

        unlink(malformed_path.c_str());
        require(lifecycle.live_textures == 0, "The renderer leaked an uploaded texture");
        require(lifecycle.destroyed_outside_context == 0,
            "The renderer destroyed an uploaded texture outside the GL context");
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
