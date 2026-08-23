#include "deco-button-renderer.hpp"
#include "../vecdecor-config.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <librsvg/rsvg.h>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef VECDECOR_BUTTON_RENDERER_NO_WAYFIRE
    #include <wayfire/opengl.hpp>
    #include <wayfire/plugins/common/cairo-util.hpp>
#endif

namespace wf
{
namespace pixdecor
{
namespace
{
constexpr std::array<button_asset_t, 4> ALL_ASSETS = {
    button_asset_t::minimize,
    button_asset_t::maximize,
    button_asset_t::restore,
    button_asset_t::close,
};
constexpr std::array<geometry::button_kind_t, 3> ALL_KINDS = {
    geometry::button_kind_t::minimize,
    geometry::button_kind_t::maximize,
    geometry::button_kind_t::close,
};
constexpr std::array<geometry::focus_state_t, 2> ALL_FOCUS_STATES = {
    geometry::focus_state_t::inactive,
    geometry::focus_state_t::active,
};
constexpr std::array<geometry::interaction_state_t, 3> ALL_INTERACTION_STATES = {
    geometry::interaction_state_t::normal,
    geometry::interaction_state_t::hover,
    geometry::interaction_state_t::pressed,
};
constexpr std::array<geometry::maximize_state_t, 2> ALL_MAXIMIZE_STATES = {
    geometry::maximize_state_t::maximize,
    geometry::maximize_state_t::restore,
};
constexpr std::size_t UNIQUE_CONTROL_VARIANTS = ALL_KINDS.size() - 1 + ALL_MAXIMIZE_STATES.size();
constexpr std::size_t MAX_CACHE_ENTRIES = 8 * UNIQUE_CONTROL_VARIANTS *
    ALL_FOCUS_STATES.size() * ALL_INTERACTION_STATES.size();
constexpr std::array<std::array<const char*, 4>, 4> ASSET_FILENAMES = {{
    {{"minimize.svg", "minimize-hover.svg", "minimize-inactive.svg",
        "minimize-inactive-hover.svg"}},
    {{"maximize.svg", "maximize-hover.svg", "maximize-inactive.svg",
        "maximize-inactive-hover.svg"}},
    {{"restore.svg", "restore-hover.svg", "restore-inactive.svg",
        "restore-inactive-hover.svg"}},
    {{"close.svg", "close-hover.svg", "close-inactive.svg",
        "close-inactive-hover.svg"}},
}};
constexpr std::array<button_state_variant_t, 4> ALL_VARIANTS = {
    button_state_variant_t::active,
    button_state_variant_t::active_hover,
    button_state_variant_t::inactive,
    button_state_variant_t::inactive_hover,
};
constexpr double PI = 3.14159265358979323846;
constexpr double MINIMUM_GLYPH_CONTRAST = 4.5;

std::size_t asset_index(button_asset_t asset)
{
    return static_cast<std::size_t>(asset);
}

std::size_t variant_index(button_state_variant_t variant)
{
    return static_cast<std::size_t>(variant);
}

const loaded_button_asset_t& matrix_source(
    const std::array<std::array<loaded_button_asset_t, 4>, 4>& sources,
    button_asset_t asset, button_state_variant_t variant)
{
    return sources[asset_index(asset)][variant_index(variant)];
}

geometry::button_cache_key_t canonical_cache_key(geometry::button_cache_key_t key)
{
    if (key.state.kind != geometry::button_kind_t::maximize)
    {
        key.state.maximize = geometry::maximize_state_t::maximize;
    }

    return key;
}

std::uint64_t hash_bytes(const unsigned char *data, std::size_t size)
{
    constexpr std::uint64_t OFFSET = 14695981039346656037ULL;
    constexpr std::uint64_t PRIME  = 1099511628211ULL;
    std::uint64_t hash = OFFSET;
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= PRIME;
    }

    return hash == 0 ? 1 : hash;
}

std::uint64_t fallback_hash(button_asset_t asset, const std::string& source)
{
    auto hash = hash_bytes(reinterpret_cast<const unsigned char*>(source.data()), source.size());
    hash ^= 0xd6e8feb86659fd93ULL + static_cast<std::uint64_t>(asset);
    return hash == 0 ? 1 : hash;
}

std::uint64_t document_fallback_hash(const std::string& source)
{
    auto hash = hash_bytes(reinterpret_cast<const unsigned char*>(source.data()), source.size());
    hash ^= 0xd6e8feb86659fd93ULL;
    return hash == 0 ? 1 : hash;
}

std::uint64_t source_hash(std::uint64_t hash, button_asset_t asset,
    button_state_variant_t variant, button_render_mode_t mode)
{
    hash ^= 0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(asset);
    hash ^= 0xd6e8feb86659fd93ULL + static_cast<std::uint64_t>(variant);
    hash ^= 0xa5a35625e9537b4fULL + static_cast<std::uint64_t>(mode);
    return hash == 0 ? 1 : hash;
}

button_surface_t make_surface(int width, int height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (!surface || (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS))
    {
        if (surface)
        {
            cairo_surface_destroy(surface);
        }

        return {};
    }

    return button_surface_t(surface, cairo_surface_destroy);
}

bool clear_surface(cairo_t *cr)
{
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

bool surface_has_alpha(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
        return false;
    }

    const int width    = cairo_image_surface_get_width(surface);
    const int height   = cairo_image_surface_get_height(surface);
    const int stride   = cairo_image_surface_get_stride(surface) / sizeof(std::uint32_t);
    const auto *pixels = reinterpret_cast<const std::uint32_t*>(
        cairo_image_surface_get_data(surface));
    if (!pixels)
    {
        return false;
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (pixels[y * stride + x] & 0xff000000U)
            {
                return true;
            }
        }
    }

    return false;
}

bool draw_interaction_background(cairo_t *cr, const geometry::button_cache_key_t& key)
{
    if (key.state.interaction == geometry::interaction_state_t::normal)
    {
        return true;
    }

    const double width  = key.raster_size.width;
    const double height = key.raster_size.height;
    const double radius = std::min(width, height) / 2.0;

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_sub_path(cr);
    if (width == height)
    {
        cairo_arc(cr, width / 2.0, height / 2.0, radius, 0.0, 2.0 * PI);
    } else if (width > height)
    {
        cairo_arc(cr, width - radius, radius, radius, -PI / 2.0, PI / 2.0);
        cairo_arc(cr, radius, radius, radius, PI / 2.0, 3.0 * PI / 2.0);
    } else
    {
        cairo_arc(cr, radius, radius, radius, PI, 2.0 * PI);
        cairo_arc(cr, radius, height - radius, radius, 0.0, PI);
    }

    cairo_close_path(cr);
    const auto& colour = key.background_colour;
    cairo_set_source_rgba(cr, colour.r, colour.g, colour.b, colour.a);
    cairo_fill(cr);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

double linear_colour_channel(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 :
           std::pow((channel + 0.055) / 1.055, 2.4);
}

double relative_luminance(const geometry::rgba_t& colour)
{
    return 0.2126 * linear_colour_channel(colour.r) +
           0.7152 * linear_colour_channel(colour.g) +
           0.0722 * linear_colour_channel(colour.b);
}

double contrast_ratio(const geometry::rgba_t& lhs, const geometry::rgba_t& rhs)
{
    const double lighter = std::max(relative_luminance(lhs), relative_luminance(rhs));
    const double darker  = std::min(relative_luminance(lhs), relative_luminance(rhs));
    return (lighter + 0.05) / (darker + 0.05);
}

geometry::rgba_t contrasting_glyph_colour(
    const geometry::rgba_t& configured, const geometry::rgba_t& background)
{
    if ((background.a < 1.0) || (configured.a < 1.0) ||
        (contrast_ratio(configured, background) >= MINIMUM_GLYPH_CONTRAST))
    {
        return configured;
    }

    const geometry::rgba_t black = {0.0, 0.0, 0.0, 1.0};
    const geometry::rgba_t white = {1.0, 1.0, 1.0, 1.0};
    return contrast_ratio(black, background) >= contrast_ratio(white, background) ? black : white;
}

bool draw_fallback(cairo_t *cr, const geometry::button_cache_key_t& key)
{
    const double width  = key.raster_size.width;
    const double height = key.raster_size.height;
    const auto& box     = key.svg_proportions;
    const double x = box.x * width;
    const double y = box.y * height;
    const double icon_width  = box.width * width;
    const double icon_height = box.height * height;
    const double line_width  = key.line_thickness * key.output_scale;

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgba(cr, key.colour.r, key.colour.g, key.colour.b, key.colour.a);

    switch (key.state.kind)
    {
      case geometry::button_kind_t::close:
        cairo_move_to(cr, x + icon_width / 4.0, y + icon_height / 4.0);
        cairo_line_to(cr, x + 3.0 * icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_move_to(cr, x + 3.0 * icon_width / 4.0, y + icon_height / 4.0);
        cairo_line_to(cr, x + icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_stroke(cr);
        break;

      case geometry::button_kind_t::minimize:
        cairo_move_to(cr, x + icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_line_to(cr, x + 3.0 * icon_width / 4.0, y + 3.0 * icon_height / 4.0);
        cairo_stroke(cr);
        break;

      case geometry::button_kind_t::maximize:
        if (key.state.maximize == geometry::maximize_state_t::restore)
        {
            cairo_rectangle(cr, x + 3.0 * icon_width / 8.0, y + icon_height / 4.0,
                3.0 * icon_width / 8.0, 3.0 * icon_height / 8.0);
            cairo_stroke(cr);
            cairo_rectangle(cr, x + icon_width / 4.0, y + 3.0 * icon_height / 8.0,
                3.0 * icon_width / 8.0, 3.0 * icon_height / 8.0);
            cairo_stroke(cr);
        } else
        {
            cairo_rectangle(cr, x + icon_width / 4.0, y + icon_height / 4.0,
                icon_width / 2.0, icon_height / 2.0);
            cairo_stroke(cr);
        }

        break;
    }

    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

bool render_svg(cairo_t *cr, RsvgHandle *handle,
    const geometry::button_cache_key_t& key, bool full_box)
{
    const RsvgRectangle viewport = {
        full_box ? 0.0 : key.svg_proportions.x * key.raster_size.width,
        full_box ? 0.0 : key.svg_proportions.y * key.raster_size.height,
        full_box ? static_cast<double>(key.raster_size.width) :
        key.svg_proportions.width * key.raster_size.width,
        full_box ? static_cast<double>(key.raster_size.height) :
        key.svg_proportions.height * key.raster_size.height,
    };
    GError *error = nullptr;
    const gboolean rendered = rsvg_handle_render_document(handle, cr, &viewport, &error);
    if (error)
    {
        g_error_free(error);
    }

    return rendered && (cairo_status(cr) == CAIRO_STATUS_SUCCESS);
}

#ifndef VECDECOR_BUTTON_RENDERER_NO_WAYFIRE
class wayfire_uploaded_texture_t : public uploaded_button_texture_t
{
  public:
    explicit wayfire_uploaded_texture_t(cairo_surface_t *surface) : texture(surface)
    {
        static std::atomic<std::uint64_t> next_identity{1};
        serial = next_identity.fetch_add(1);
    }

    const wf::owned_texture_t *wayfire_texture() const override
    {
        return &texture;
    }

    std::uint64_t identity() const override
    {
        return serial;
    }

  private:
    wf::owned_texture_t texture;
    std::uint64_t serial = 0;
};
#endif
}

button_svg_document_t load_svg_button_document(const std::string& path)
{
    button_svg_document_t result;

    gchar *contents = nullptr;
    gsize length    = 0;
    GError *error   = nullptr;
    if (!g_file_get_contents(path.c_str(), &contents, &length, &error))
    {
        result.error = error ? error->message : "Could not read the SVG source";
        if (error)
        {
            g_error_free(error);
        }

        result.content_hash = document_fallback_hash(path);
        return result;
    }

    result.content_hash = hash_bytes(reinterpret_cast<const unsigned char*>(contents), length);
    RsvgHandle *handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(contents), length, &error);
    g_free(contents);

    if (!handle)
    {
        result.error = error ? error->message : "Could not parse the SVG source";
        if (error)
        {
            g_error_free(error);
        }

        result.content_hash ^= document_fallback_hash(result.error);
        if (result.content_hash == 0)
        {
            result.content_hash = 1;
        }

        return result;
    }

    if (error)
    {
        g_error_free(error);
    }

    result.payload = std::shared_ptr<void>(handle, [] (void *value)
    {
        g_object_unref(value);
    });
    result.valid_svg = true;
    return result;
}

loaded_button_asset_t load_svg_button_asset(
    const button_source_spec_t& source, button_asset_t asset,
    button_state_variant_t variant, std::uint64_t source_generation)
{
    const auto document = load_svg_button_document(source.path);
    loaded_button_asset_t result;
    result.identity = {
        source_hash(document.content_hash, asset, variant, source.render_mode),
        source_generation,
    };
    result.payload     = document.payload;
    result.variant     = variant;
    result.render_mode = source.render_mode;
    result.valid_svg   = document.valid_svg;
    result.error = document.error;
    return result;
}

std::string default_button_asset_directory()
{
    return VECDECOR_ASSET_DIR;
}

std::array<button_source_spec_t, 4> resolve_button_source_specs(
    const std::string& configured_path, button_asset_t asset)
{
    std::array<button_source_spec_t, 4> result;
    if (!configured_path.empty())
    {
        for (auto& source : result)
        {
            source = {configured_path, button_render_mode_t::recoloured_mask};
        }
    } else
    {
        for (auto variant : ALL_VARIANTS)
        {
            const auto index = variant_index(variant);
            result[index] = {
                default_button_asset_directory() + "/" +
                ASSET_FILENAMES[asset_index(asset)][index],
                button_render_mode_t::full_colour,
            };
        }
    }

    return result;
}

button_state_variant_t resolve_button_state_variant(const geometry::button_state_t& state)
{
    const bool hover = state.interaction != geometry::interaction_state_t::normal;
    if (state.focus == geometry::focus_state_t::active)
    {
        return hover ? button_state_variant_t::active_hover :
               button_state_variant_t::active;
    }

    return hover ? button_state_variant_t::inactive_hover :
           button_state_variant_t::inactive;
}

button_surface_t rasterize_button_asset(
    const loaded_button_asset_t& asset, const geometry::button_cache_key_t& key)
{
    if (!geometry::is_valid(key.raster_size) || !geometry::is_valid(key.colour) ||
        !geometry::is_valid(key.background_colour) ||
        !geometry::is_valid(key.svg_proportions) || !std::isfinite(key.line_thickness) ||
        (key.line_thickness < 0.0))
    {
        return {};
    }

    auto surface = make_surface(key.raster_size.width, key.raster_size.height);
    if (!surface)
    {
        return {};
    }

    cairo_t *cr = cairo_create(surface.get());
    if (!cr || (cairo_status(cr) != CAIRO_STATUS_SUCCESS) || !clear_surface(cr))
    {
        if (cr)
        {
            cairo_destroy(cr);
        }

        return {};
    }

    bool rendered = false;
    if (asset.valid_svg && asset.payload)
    {
        if (asset.render_mode == button_render_mode_t::full_colour)
        {
            rendered = render_svg(cr, static_cast<RsvgHandle*>(asset.payload.get()), key, true) &&
                surface_has_alpha(surface.get());
        } else
        {
            if (!draw_interaction_background(cr, key))
            {
                cairo_destroy(cr);
                return {};
            }

            auto mask = make_surface(key.raster_size.width, key.raster_size.height);
            if (mask)
            {
                cairo_t *mask_cr = cairo_create(mask.get());
                if (mask_cr && (cairo_status(mask_cr) == CAIRO_STATUS_SUCCESS) &&
                    clear_surface(mask_cr))
                {
                    rendered = render_svg(mask_cr,
                        static_cast<RsvgHandle*>(asset.payload.get()), key, false);
                }

                if (mask_cr)
                {
                    cairo_destroy(mask_cr);
                }

                rendered = rendered && surface_has_alpha(mask.get());
                if (rendered)
                {
                    cairo_set_source_rgba(cr, key.colour.r, key.colour.g, key.colour.b,
                        key.colour.a);
                    cairo_mask_surface(cr, mask.get(), 0, 0);
                    rendered = cairo_status(cr) == CAIRO_STATUS_SUCCESS;
                }
            }
        }
    }

    if (!rendered)
    {
        if (!clear_surface(cr) || !draw_interaction_background(cr, key))
        {
            cairo_destroy(cr);
            return {};
        }

        if (!draw_fallback(cr, key))
        {
            cairo_destroy(cr);
            return {};
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface.get());
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS)
    {
        return {};
    }

    return surface;
}

std::unique_ptr<uploaded_button_texture_t> upload_wayfire_button_texture(cairo_surface_t *surface)
{
#ifdef VECDECOR_BUTTON_RENDERER_NO_WAYFIRE
    (void)surface;
    return {};
#else
    if (!surface || (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS))
    {
        return {};
    }

    return std::make_unique<wayfire_uploaded_texture_t>(surface);
#endif
}

button_renderer_dependencies_t wayfire_button_renderer_dependencies()
{
#ifdef VECDECOR_BUTTON_RENDERER_NO_WAYFIRE
    return {
        load_svg_button_document,
        rasterize_button_asset,
        upload_wayfire_button_texture,
        [] (const std::function<void()>& callback)
        {
            callback();
            return true;
        },
    };
#else
    return {
        load_svg_button_document,
        rasterize_button_asset,
        upload_wayfire_button_texture,
        [] (const std::function<void()>& callback)
        {
            return wf::gles::run_in_context(callback);
        },
    };
#endif
}

button_renderer_t::button_renderer_t(button_renderer_dependencies_t dependencies) :
    dependencies(std::move(dependencies))
{}

button_renderer_t::~button_renderer_t()
{
    if (!clear_cache())
    {
        // Uploaded Wayfire textures must be destroyed in a GL context. Context entry can fail
        // during compositor teardown, so leak the bounded cache instead of destroying it.
        for (auto& entry : cache)
        {
            (void)entry.texture.release();
        }

        cache.clear();
    }
}

bool button_renderer_t::reload_sources(const button_source_config_t& config)
{
    if (!dependencies.decoder)
    {
        return false;
    }

    std::unordered_map<std::string, button_svg_document_t> decoded;
    button_renderer_dependencies_t::decoder_t decode_cached = [&] (const std::string& path)
    {
        const auto found = decoded.find(path);
        if (found != decoded.end())
        {
            return found->second;
        }

        auto document = dependencies.decoder(path);
        decoded.emplace(path, document);
        return document;
    };

    for (auto asset : ALL_ASSETS)
    {
        const auto index = asset_index(asset);
        bool changed     = false;
        for (auto variant : ALL_VARIANTS)
        {
            const auto variant_value = variant_index(variant);
            const auto& current   = source_config.sources[index][variant_value];
            const auto& requested = config.sources[index][variant_value];
            changed = changed || !source_loaded[index][variant_value] ||
                (current.path != requested.path) ||
                (current.render_mode != requested.render_mode) ||
                (sources[index][variant_value].identity.source_generation != config.generation);
        }

        if (changed && !reload_source(
            asset, config.sources[index], config.generation, decode_cached))
        {
            return false;
        }
    }

    source_config  = config;
    sources_loaded = std::all_of(source_loaded.begin(), source_loaded.end(), [] (const auto& row)
    {
        return std::all_of(row.begin(), row.end(), [] (bool loaded)
        {
            return loaded;
        });
    });
    return true;
}

bool button_renderer_t::reload_source(button_asset_t asset,
    const std::array<button_source_spec_t, 4>& requested_sources,
    std::uint64_t source_generation)
{
    if (!dependencies.decoder)
    {
        return false;
    }

    std::unordered_map<std::string, button_svg_document_t> decoded;
    button_renderer_dependencies_t::decoder_t decode_cached = [&] (const std::string& path)
    {
        const auto found = decoded.find(path);
        if (found != decoded.end())
        {
            return found->second;
        }

        auto document = dependencies.decoder(path);
        decoded.emplace(path, document);
        return document;
    };
    return reload_source(asset, requested_sources, source_generation, decode_cached);
}

bool button_renderer_t::reload_source(button_asset_t asset,
    const std::array<button_source_spec_t, 4>& requested_sources,
    std::uint64_t source_generation,
    const button_renderer_dependencies_t::decoder_t& decoder)
{
    const auto index = asset_index(asset);
    bool changed     = false;
    for (auto variant : ALL_VARIANTS)
    {
        const auto variant_value = variant_index(variant);
        const auto& current   = source_config.sources[index][variant_value];
        const auto& requested = requested_sources[variant_value];
        changed = changed || !source_loaded[index][variant_value] ||
            (current.path != requested.path) ||
            (current.render_mode != requested.render_mode) ||
            (sources[index][variant_value].identity.source_generation != source_generation);
    }

    if (!changed)
    {
        return true;
    }

    if (!decoder)
    {
        return false;
    }

    std::array<loaded_button_asset_t, 4> loaded_sources;
    for (auto variant : ALL_VARIANTS)
    {
        const auto variant_value = variant_index(variant);
        const auto& requested    = requested_sources[variant_value];
        const auto document = decoder(requested.path);
        loaded_button_asset_t loaded;
        loaded.identity = {
            source_hash(document.content_hash, asset, variant, requested.render_mode),
            source_generation,
        };
        loaded.payload     = document.payload;
        loaded.variant     = variant;
        loaded.render_mode = requested.render_mode;
        loaded.valid_svg   = document.valid_svg;
        loaded.error = document.error;
        if (!geometry::is_valid(loaded.identity))
        {
            loaded.valid_svg = false;
            loaded.payload.reset();
            loaded.identity = {
                source_hash(fallback_hash(asset, requested.path), asset, variant,
                    requested.render_mode),
                source_generation,
            };
        }

        loaded_sources[variant_value] = std::move(loaded);
    }

    if (!cache.empty())
    {
        if (!dependencies.run_in_context || !dependencies.run_in_context([&]
        {
            cache.erase(std::remove_if(cache.begin(), cache.end(), [&] (const cache_entry_t& entry)
            {
                return asset_for_state(entry.key.state) == asset;
            }), cache.end());
        }))
        {
            return false;
        }
    }

    sources[index] = std::move(loaded_sources);
    source_loaded[index].fill(true);
    source_config.sources[index] = requested_sources;
    source_config.generation     = source_generation;
    sources_loaded = std::all_of(source_loaded.begin(), source_loaded.end(), [] (const auto& row)
    {
        return std::all_of(row.begin(), row.end(), [] (bool loaded)
        {
            return loaded;
        });
    });
    return true;
}

bool button_renderer_t::prepare(const button_prepare_config_t& config)
{
    if (!sources_loaded || !dependencies.rasterizer || !dependencies.uploader ||
        !dependencies.run_in_context)
    {
        return false;
    }

    std::vector<geometry::button_cache_key_t> desired;
    desired.reserve(ALL_KINDS.size() * ALL_FOCUS_STATES.size() *
        ALL_INTERACTION_STATES.size() * ALL_MAXIMIZE_STATES.size());
    for (auto kind : ALL_KINDS)
    {
        for (auto focus : ALL_FOCUS_STATES)
        {
            for (auto interaction : ALL_INTERACTION_STATES)
            {
                for (auto maximize : ALL_MAXIMIZE_STATES)
                {
                    const auto key = resolve_key({kind, focus, interaction, maximize}, config);
                    if (std::find(desired.begin(), desired.end(), key) == desired.end())
                    {
                        desired.push_back(key);
                    }
                }
            }
        }
    }

    struct pending_entry_t
    {
        geometry::button_cache_key_t key;
        button_surface_t surface;
    };

    std::vector<pending_entry_t> pending;
    for (const auto& key : desired)
    {
        if (lookup(key))
        {
            continue;
        }

        const auto& source = source_for_state(key.state);
        auto surface = dependencies.rasterizer(source, key);
        if (!surface || (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS))
        {
            return false;
        }

        pending.push_back({key, std::move(surface)});
    }

    bool uploaded = true;
    const auto current_prepare_serial = ++prepare_serial;
    const bool context_ready = dependencies.run_in_context([&]
    {
        cache.erase(std::remove_if(cache.begin(), cache.end(), [&] (const cache_entry_t& entry)
        {
            const auto& source = source_for_state(entry.key.state);
            return ((entry.key.theme_generation != 0) &&
                (entry.key.theme_generation != config.theme_generation)) ||
                   !(entry.key.resolved_asset_identity == source.identity);
        }), cache.end());

        for (auto& entry : cache)
        {
            if (std::find(desired.begin(), desired.end(), entry.key) != desired.end())
            {
                entry.prepare_serial = current_prepare_serial;
            }
        }

        for (auto& item : pending)
        {
            auto texture = dependencies.uploader(item.surface.get());
            if (!texture)
            {
                uploaded = false;
                continue;
            }

            cache.push_back({item.key, std::move(texture), current_prepare_serial});
        }

        while (cache.size() > MAX_CACHE_ENTRIES)
        {
            const auto oldest = std::min_element(cache.begin(), cache.end(),
                [] (const cache_entry_t& lhs, const cache_entry_t& rhs)
            {
                return lhs.prepare_serial < rhs.prepare_serial;
            })->prepare_serial;
            cache.erase(std::remove_if(cache.begin(), cache.end(), [oldest] (const cache_entry_t& entry)
            {
                return entry.prepare_serial == oldest;
            }), cache.end());
        }
    });

    return context_ready && uploaded && std::all_of(desired.begin(), desired.end(), [&] (const auto& key)
    {
        return lookup(key) != nullptr;
    });
}

geometry::button_cache_key_t button_renderer_t::resolve_key(
    const geometry::button_state_t& state, const button_prepare_config_t& config) const
{
    const auto& source = source_for_state(state);
    const bool fixed_full_colour = source.valid_svg &&
        (source.render_mode == button_render_mode_t::full_colour);
    geometry::cache_key_input_t input;
    input.state = state;
    if (fixed_full_colour &&
        (input.state.interaction == geometry::interaction_state_t::pressed))
    {
        input.state.interaction = geometry::interaction_state_t::hover;
    }

    input.resolved_asset_identity = source.identity;
    input.colour = fixed_full_colour ? geometry::rgba_t{} :
    colour_for_state(state, config.palette);
    input.background_colour = fixed_full_colour ? geometry::rgba_t{} :
    background_colour_for_state(state, config.palette);
    input.logical_size     = config.logical_size;
    input.svg_proportions  = config.svg_proportions;
    input.output_scale     = config.output_scale;
    input.line_thickness   = fixed_full_colour ? 0.0 : config.line_thickness;
    input.theme_generation = fixed_full_colour ? 0 : config.theme_generation;
    return canonical_cache_key(geometry::resolve_cache_key(input));
}

const uploaded_button_texture_t*button_renderer_t::lookup(
    const geometry::button_cache_key_t& key) const
{
    const auto canonical_key = canonical_cache_key(key);
    const auto found = std::find_if(cache.begin(), cache.end(), [&] (const cache_entry_t& entry)
    {
        return entry.key == canonical_key;
    });
    return found == cache.end() ? nullptr : found->texture.get();
}

const uploaded_button_texture_t*button_renderer_t::lookup(
    const geometry::button_state_t& state, const button_prepare_config_t& config) const
{
    return lookup(resolve_key(state, config));
}

std::size_t button_renderer_t::cache_size() const
{
    return cache.size();
}

const loaded_button_asset_t& button_renderer_t::source(
    button_asset_t asset, button_state_variant_t variant) const
{
    return matrix_source(sources, asset, variant);
}

button_asset_t button_renderer_t::asset_for_state(const geometry::button_state_t& state) const
{
    switch (state.kind)
    {
      case geometry::button_kind_t::minimize:
        return button_asset_t::minimize;

      case geometry::button_kind_t::close:
        return button_asset_t::close;

      case geometry::button_kind_t::maximize:
        return state.maximize == geometry::maximize_state_t::restore ?
               button_asset_t::restore : button_asset_t::maximize;
    }

    return button_asset_t::close;
}

const loaded_button_asset_t& button_renderer_t::source_for_state(
    const geometry::button_state_t& state) const
{
    return source(asset_for_state(state), resolve_button_state_variant(state));
}

geometry::rgba_t button_renderer_t::colour_for_state(
    const geometry::button_state_t& state, const button_palette_t& palette) const
{
    const auto configured = state.focus == geometry::focus_state_t::active ?
        palette.active : palette.inactive;
    return contrasting_glyph_colour(configured, background_colour_for_state(state, palette));
}

geometry::rgba_t button_renderer_t::background_colour_for_state(
    const geometry::button_state_t& state, const button_palette_t& palette) const
{
    switch (state.interaction)
    {
      case geometry::interaction_state_t::hover:
        return palette.hover;

      case geometry::interaction_state_t::pressed:
        return palette.pressed;

      case geometry::interaction_state_t::normal:
        return {};
    }

    return {};
}

bool button_renderer_t::clear_cache()
{
    if (cache.empty())
    {
        return true;
    }

    if (!dependencies.run_in_context)
    {
        return false;
    }

    return dependencies.run_in_context([this]
    {
        cache.clear();
    });
}
}
}
