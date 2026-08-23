#pragma once

#include "deco-geometry.hpp"

#include <array>
#include <cairo.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wf
{
struct owned_texture_t;

namespace pixdecor
{
enum class button_asset_t
{
    minimize,
    maximize,
    restore,
    close,
};

enum class button_state_variant_t
{
    active,
    active_hover,
    inactive,
    inactive_hover,
};

enum class button_render_mode_t
{
    full_colour,
    recoloured_mask,
};

struct button_source_spec_t
{
    std::string path;
    button_render_mode_t render_mode = button_render_mode_t::full_colour;
};

struct button_source_config_t
{
    std::array<std::array<button_source_spec_t, 4>, 4> sources;
    std::uint64_t generation = 0;
};

struct button_palette_t
{
    geometry::rgba_t active;
    geometry::rgba_t inactive;
    geometry::rgba_t hover;
    geometry::rgba_t pressed;
};

struct button_prepare_config_t
{
    geometry::logical_size_t logical_size;
    geometry::svg_proportions_t svg_proportions;
    double output_scale   = 1.0;
    double line_thickness = geometry::DEFAULT_BUTTON_LINE_THICKNESS;
    std::uint64_t theme_generation = 0;
    button_palette_t palette;
};

struct loaded_button_asset_t
{
    geometry::resolved_asset_identity_t identity;
    std::shared_ptr<void> payload;
    button_state_variant_t variant   = button_state_variant_t::active;
    button_render_mode_t render_mode = button_render_mode_t::full_colour;
    bool valid_svg = false;
    std::string error;
};

struct button_svg_document_t
{
    std::shared_ptr<void> payload;
    std::uint64_t content_hash = 0;
    bool valid_svg = false;
    std::string error;
};

using button_surface_t = std::shared_ptr<cairo_surface_t>;

class uploaded_button_texture_t
{
  public:
    virtual ~uploaded_button_texture_t() = default;
    virtual const wf::owned_texture_t *wayfire_texture() const = 0;
    virtual std::uint64_t identity() const = 0;
};

struct button_renderer_dependencies_t
{
    using decoder_t    = std::function<button_svg_document_t (const std::string&)>;
    using rasterizer_t = std::function<button_surface_t (
        const loaded_button_asset_t&, const geometry::button_cache_key_t&)>;
    using uploader_t = std::function<std::unique_ptr<uploaded_button_texture_t>(cairo_surface_t*)>;
    using context_runner_t = std::function<bool (const std::function<void ()>&)>;

    decoder_t decoder;
    rasterizer_t rasterizer;
    uploader_t uploader;
    context_runner_t run_in_context;
};

button_svg_document_t load_svg_button_document(const std::string& path);
loaded_button_asset_t load_svg_button_asset(
    const button_source_spec_t& source, button_asset_t asset,
    button_state_variant_t variant, std::uint64_t source_generation);
std::string default_button_asset_directory();
std::array<button_source_spec_t, 4> resolve_button_source_specs(
    const std::string& configured_path, button_asset_t asset);
button_state_variant_t resolve_button_state_variant(const geometry::button_state_t& state);
button_surface_t rasterize_button_asset(
    const loaded_button_asset_t& asset, const geometry::button_cache_key_t& key);
std::unique_ptr<uploaded_button_texture_t> upload_wayfire_button_texture(cairo_surface_t *surface);
button_renderer_dependencies_t wayfire_button_renderer_dependencies();

class button_renderer_t
{
  public:
    explicit button_renderer_t(
        button_renderer_dependencies_t dependencies = wayfire_button_renderer_dependencies());
    ~button_renderer_t();

    button_renderer_t(const button_renderer_t&) = delete;
    button_renderer_t& operator =(const button_renderer_t&) = delete;

    bool reload_sources(const button_source_config_t& config);
    bool reload_source(button_asset_t asset,
        const std::array<button_source_spec_t, 4>& sources,
        std::uint64_t source_generation);
    bool prepare(const button_prepare_config_t& config);

    geometry::button_cache_key_t resolve_key(
        const geometry::button_state_t& state, const button_prepare_config_t& config) const;
    const uploaded_button_texture_t *lookup(const geometry::button_cache_key_t& key) const;
    const uploaded_button_texture_t *lookup(
        const geometry::button_state_t& state, const button_prepare_config_t& config) const;

    std::size_t cache_size() const;
    const loaded_button_asset_t& source(
        button_asset_t asset, button_state_variant_t variant) const;

  private:
    struct cache_entry_t
    {
        geometry::button_cache_key_t key;
        std::unique_ptr<uploaded_button_texture_t> texture;
        std::uint64_t prepare_serial = 0;
    };

    button_asset_t asset_for_state(const geometry::button_state_t& state) const;
    const loaded_button_asset_t& source_for_state(
        const geometry::button_state_t& state) const;
    geometry::rgba_t colour_for_state(
        const geometry::button_state_t& state, const button_palette_t& palette) const;
    geometry::rgba_t background_colour_for_state(
        const geometry::button_state_t& state, const button_palette_t& palette) const;
    bool reload_source(button_asset_t asset,
        const std::array<button_source_spec_t, 4>& requested_sources,
        std::uint64_t source_generation,
        const button_renderer_dependencies_t::decoder_t& decoder);
    bool clear_cache();

    button_renderer_dependencies_t dependencies;
    button_source_config_t source_config;
    std::array<std::array<loaded_button_asset_t, 4>, 4> sources;
    std::array<std::array<bool, 4>, 4> source_loaded = {};
    std::vector<cache_entry_t> cache;
    bool sources_loaded = false;
    std::uint64_t prepare_serial = 0;
};
}
}
