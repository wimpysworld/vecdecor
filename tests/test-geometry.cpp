#include "deco-geometry.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

namespace geometry = wf::pixdecor::geometry;

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

geometry::geometry_input_t valid_geometry_input()
{
    geometry::geometry_input_t input;
    input.font_height     = 12;
    input.svg_proportions = geometry::full_box_svg_proportions();
    return input;
}

geometry::cache_key_input_t valid_cache_input()
{
    geometry::cache_key_input_t input;
    input.resolved_asset_identity = {0x1234, 1};
    input.colour = {0.1, 0.2, 0.3, 0.4};
    input.background_colour = {0.4, 0.3, 0.2, 0.1};
    input.logical_size   = {18, 18};
    input.line_thickness = 0.7;
    return input;
}

void test_automatic_sizes()
{
    expect(geometry::resolve_automatic_button_size(19) == 16,
        "automatic button size below the threshold is 16");
    expect(geometry::resolve_automatic_button_size(20) == 24,
        "automatic button size at the threshold is 24");
    expect(geometry::resolve_automatic_title_height(0) == 20,
        "automatic title height has a minimum of 20");
    expect(geometry::resolve_automatic_title_height(12) == 26,
        "automatic title height follows the font height");

    auto input  = valid_geometry_input();
    auto result = geometry::resolve_geometry(input);
    expect(result.button_size == geometry::logical_size_t{16, 16},
        "zero button size selects the automatic 16 size");
    expect(result.title_height == 26,
        "zero title height selects the font-derived height");
    expect(!result.used_automatic_fallback,
        "valid zero requests are automatic selections, not error fallbacks");

    input.font_height = 20;
    result = geometry::resolve_geometry(input);
    expect(result.button_size == geometry::logical_size_t{24, 24},
        "a large font selects the automatic 24 size");
    expect(result.title_height == 38,
        "a large font produces the corresponding title height");
}

void test_legacy_automatic_sizes_preserve_title_height()
{
    auto input = valid_geometry_input();
    input.requested_button_size  = 0;
    input.requested_title_height = 0;
    input.button_y_offset = 10;
    const auto result = geometry::resolve_geometry(input);

    expect((result.title_height == 26) &&
        (result.button_bounds == geometry::logical_bounds_t{0, 15, 16, 16}),
        "legacy automatic sizes preserve the font-derived title height with an offset");
}

void test_positive_inputs_and_containment()
{
    auto input = valid_geometry_input();
    input.requested_button_size  = 24;
    input.requested_title_height = 41;
    input.button_y_offset = 3;
    auto result = geometry::resolve_geometry(input);

    expect(result.button_size == geometry::logical_size_t{24, 24},
        "a positive button size is independent of the title request");
    expect(result.title_height == 41,
        "a positive title height is independent of the button request");
    expect(result.button_bounds == geometry::logical_bounds_t{0, 11, 24, 24},
        "the button offset applies after vertical centring");
    expect(geometry::contains(result.title_bounds, result.button_bounds),
        "the resolved title contains a positively offset button");

    input.requested_title_height = 20;
    input.button_y_offset = -5;
    result = geometry::resolve_geometry(input);
    expect(result.title_height == 34,
        "the title expands to contain a negatively offset button");
    expect(result.button_bounds == geometry::logical_bounds_t{0, 0, 24, 24},
        "the negative offset remains inside the expanded title");
    expect(geometry::contains(result.title_bounds, result.button_bounds),
        "the expanded title contains the negatively offset button");

    input.requested_button_size  = 30;
    input.requested_title_height = 0;
    input.button_y_offset = 0;
    result = geometry::resolve_geometry(input);
    expect(result.button_size == geometry::logical_size_t{30, 30},
        "a positive button request works with automatic title sizing");
    expect(result.title_height == 30,
        "automatic title sizing expands for an independent button request");

    input.requested_button_size  = 0;
    input.requested_title_height = 44;
    result = geometry::resolve_geometry(input);
    expect(result.button_size == geometry::logical_size_t{16, 16},
        "automatic button sizing works with an independent title request");
    expect(result.title_height == 44,
        "the independent title request remains unchanged");

    input.title_height_extension = 5;
    result = geometry::resolve_geometry(input);
    expect(result.title_height == 49,
        "the title height includes its runtime extension");
    expect(result.button_bounds == geometry::logical_bounds_t{0, 16, 16, 16},
        "the button remains centred in the extended title height");
}

void test_invalid_inputs_and_view_boxes()
{
    auto input = valid_geometry_input();
    input.requested_button_size = -1;
    auto result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback,
        "a negative button request uses the automatic fallback");
    expect(result.button_size == geometry::logical_size_t{16, 16},
        "the negative button fallback uses a safe logical size");

    input = valid_geometry_input();
    input.requested_title_height = -1;
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback,
        "a negative title request uses the automatic fallback");

    input = valid_geometry_input();
    input.title_height_extension = -1;
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback,
        "a negative title height extension uses the automatic fallback");

    input = valid_geometry_input();
    input.font_height = -1;
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback && (result.title_height == 20),
        "a negative runtime font height uses the minimum title fallback");

    input = valid_geometry_input();
    input.font_height = std::numeric_limits<int>::max();
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback && (result.title_height == 20),
        "an overflowing runtime font height uses the minimum title fallback");

    input = valid_geometry_input();
    input.output_scale = std::numeric_limits<double>::quiet_NaN();
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback && (result.output_scale == 1.0),
        "a non-finite runtime scale uses scale 1");

    input = valid_geometry_input();
    input.output_scale = -1.0;
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback && (result.output_scale == 1.0),
        "a negative runtime scale uses scale 1");

    input = valid_geometry_input();
    input.output_scale = 0.0;
    result = geometry::resolve_geometry(input);
    expect(result.used_automatic_fallback && (result.output_scale == 1.0),
        "a zero runtime scale uses scale 1");

    const geometry::svg_proportions_t invalid_boxes[] = {
        {-0.1, 0.0, 1.0, 1.0},
        {0.0, 0.0, 0.0, 1.0},
        {0.0, 0.0, 1.1, 1.0},
        {0.5, 0.5, 0.6, 0.6},
        {0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0},
    };
    for (const auto& box : invalid_boxes)
    {
        expect(!geometry::is_valid(box), "an invalid SVG view box is rejected");
        expect(geometry::resolve_svg_proportions(box) ==
            geometry::full_box_svg_proportions(),
            "an invalid SVG view box resolves to the full box");
    }

    const geometry::svg_proportions_t inset{0.1, 0.2, 0.7, 0.6};
    expect(geometry::is_valid(inset), "a contained SVG view box is valid");
    expect(geometry::resolve_svg_proportions(inset) == inset,
        "a valid SVG view box retains its proportions");
}

void test_group_positions()
{
    geometry::button_group_input_t input;
    input.button_count  = 3;
    input.button_bounds = {0, 4, 18, 18};
    input.spacing  = 5;
    input.x_offset = 2;
    input.corner_inset = 7;
    input.title_width  = 200;

    const auto left  = geometry::resolve_left_group_positions(input);
    const auto right = geometry::resolve_right_group_positions(input);
    expect(left.valid && right.valid, "valid button groups resolve on both sides");
    expect(left.buttons.size() == 3 && right.buttons.size() == 3,
        "both groups contain every button");
    expect(left.buttons[0] == geometry::logical_bounds_t{9, 4, 18, 18} &&
        left.buttons[1] == geometry::logical_bounds_t{32, 4, 18, 18} &&
        left.buttons[2] == geometry::logical_bounds_t{55, 4, 18, 18},
        "left positions include the spacing and positive offset");
    expect(right.buttons[0] == geometry::logical_bounds_t{177, 4, 18, 18} &&
        right.buttons[1] == geometry::logical_bounds_t{154, 4, 18, 18} &&
        right.buttons[2] == geometry::logical_bounds_t{131, 4, 18, 18},
        "right positions mirror spacing and the positive offset");
    expect(left.bounds.width == right.bounds.width &&
        left.bounds.height == right.bounds.height,
        "left and right group bounds are symmetric");

    for (const auto& button_bounds : left.buttons)
    {
        expect(button_bounds.width == input.button_bounds.width &&
            button_bounds.height == input.button_bounds.height,
            "group positioning preserves each canonical button size");
    }

    input.button_count = 0;
    const auto empty_left  = geometry::resolve_left_group_positions(input);
    const auto empty_right = geometry::resolve_right_group_positions(input);
    expect(empty_left.valid && empty_left.buttons.empty() &&
        empty_left.bounds == geometry::logical_bounds_t{},
        "an empty left group has valid empty bounds");
    expect(empty_right.valid && empty_right.buttons.empty() &&
        empty_right.bounds == geometry::logical_bounds_t{},
        "an empty right group has valid empty bounds");

    input.button_count = 2;
    input.title_width  = 10;
    input.x_offset     = -3;
    const auto narrow_right = geometry::resolve_right_group_positions(input);
    expect(narrow_right.valid, "a narrow title still has defined pure-model positions");
    expect(narrow_right.buttons[0] == geometry::logical_bounds_t{-18, 4, 18, 18} &&
        narrow_right.buttons[1] == geometry::logical_bounds_t{-41, 4, 18, 18},
        "narrow-window positions retain spacing, inset, and negative offset");

    input.spacing = -1;
    expect(!geometry::resolve_left_group_positions(input).valid,
        "a negative runtime spacing is rejected");
    input.spacing = 5;
    input.corner_inset = -1;
    expect(!geometry::resolve_right_group_positions(input).valid,
        "a negative runtime corner inset is rejected");
}

void test_svg_cache_contracts()
{
    auto base_input = valid_cache_input();
    base_input.svg_proportions = {0.1, 0.2, 0.6, 0.5};
    const auto base = geometry::resolve_cache_key(base_input);

    struct proportion_case_t
    {
        geometry::svg_proportions_t proportions;
        const char *field;
    };

    const proportion_case_t valid_changes[] = {
        {{0.15, 0.2, 0.6, 0.5}, "SVG view-box x proportions"},
        {{0.1, 0.25, 0.6, 0.5}, "SVG view-box y proportions"},
        {{0.1, 0.2, 0.55, 0.5}, "SVG view-box width proportions"},
        {{0.1, 0.2, 0.6, 0.45}, "SVG view-box height proportions"},
    };

    for (const auto& item : valid_changes)
    {
        auto changed = base_input;
        changed.svg_proportions = item.proportions;
        expect(!(base == geometry::resolve_cache_key(changed)),
            std::string{"the cache key separates "} + item.field);
    }

    const auto full_box = geometry::full_box_svg_proportions();
    auto fallback_input = valid_cache_input();
    fallback_input.svg_proportions = full_box;
    const auto fallback = geometry::resolve_cache_key(fallback_input);
    const geometry::svg_proportions_t invalid_proportions[] = {
        {-0.1, 0.0, 1.0, 1.0},
        {0.0, -0.1, 1.0, 1.0},
        {0.0, 0.0, 0.0, 1.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.5, 0.0, 0.6, 1.0},
        {0.0, 0.5, 1.0, 0.6},
        {0.0, 0.0, std::numeric_limits<double>::quiet_NaN(), 1.0},
    };

    for (const auto& proportions : invalid_proportions)
    {
        auto invalid_input = valid_cache_input();
        invalid_input.svg_proportions = proportions;
        const auto resolved = geometry::resolve_cache_key(invalid_input);
        expect((resolved.svg_proportions == full_box) && (resolved == fallback),
            "invalid SVG proportions use the full-box cache key");
    }
}

void test_scale_and_cache_contracts()
{
    const geometry::logical_size_t logical{18, 26};
    struct scale_case_t
    {
        double scale;
        geometry::raster_size_t raster;
    };

    const scale_case_t cases[] = {
        {1.0, {18, 26}},
        {1.25, {23, 33}},
        {2.0, {36, 52}},
    };

    for (const auto& item : cases)
    {
        auto geometry_input = valid_geometry_input();
        geometry_input.requested_button_size  = logical.width;
        geometry_input.requested_title_height = logical.height;
        geometry_input.output_scale = item.scale;
        const auto resolved = geometry::resolve_geometry(geometry_input);
        expect(resolved.button_size == geometry::logical_size_t{18, 18} &&
            resolved.title_height == 26,
            "output scale does not change logical geometry");
        expect(geometry::resolve_raster_size(logical, item.scale) == item.raster,
            "the raster resolver performs scale conversion");

        auto cache_input = valid_cache_input();
        cache_input.logical_size = logical;
        cache_input.output_scale = item.scale;
        const auto key = geometry::resolve_cache_key(cache_input);
        expect(key.logical_size == logical && key.raster_size == item.raster,
            "a cache key gets raster dimensions from the raster resolver");
    }

    auto base_input = valid_cache_input();
    const auto base = geometry::resolve_cache_key(base_input);
    auto expect_key_separate = [&] (const geometry::button_cache_key_t& changed,
                                    const std::string& field)
    {
        expect(!(base == changed), "the cache key separates " + field);
    };
    auto expect_separate = [&] (const geometry::cache_key_input_t& changed,
                                const std::string& field)
    {
        expect(!(base == geometry::resolve_cache_key(changed)),
            "the cache key separates " + field);
    };

    auto changed = base_input;
    changed.logical_size = {19, 18};
    expect_separate(changed, "logical sizes");

    auto changed_key = base;
    changed_key.logical_size = {19, 18};
    expect_key_separate(changed_key, "the logical-size field");

    changed_key = base;
    changed_key.raster_size = {19, 18};
    expect_key_separate(changed_key, "the raster-size field");

    changed = base_input;
    changed.logical_size = {19, 19};
    changed.output_scale = 0.95;
    const auto same_raster = geometry::resolve_cache_key(changed);
    expect(same_raster.raster_size == base.raster_size && !(same_raster == base),
        "the cache key separates logical sizes with equal raster sizes");

    changed = base_input;
    changed.output_scale = 1.25;
    expect_separate(changed, "raster sizes");

    changed = base_input;
    changed.logical_size = {1, 1};
    changed.output_scale = 1.25;
    const auto exact_scale_a = geometry::resolve_cache_key(changed);
    changed.output_scale = 1.3;
    const auto exact_scale_b = geometry::resolve_cache_key(changed);
    expect(exact_scale_a.raster_size == exact_scale_b.raster_size &&
        !(exact_scale_a == exact_scale_b),
        "the cache key separates exact scales with equal raster sizes");

    changed = base_input;
    changed.state.focus = geometry::focus_state_t::active;
    expect_separate(changed, "active and inactive focus");

    changed = base_input;
    changed.state.kind = geometry::button_kind_t::minimize;
    expect_separate(changed, "close and minimise buttons");
    changed.state.kind = geometry::button_kind_t::maximize;
    expect_separate(changed, "close and maximise buttons");

    changed = base_input;
    changed.state.interaction = geometry::interaction_state_t::hover;
    expect_separate(changed, "normal and hover interaction");
    changed.state.interaction = geometry::interaction_state_t::pressed;
    expect_separate(changed, "normal and pressed interaction");

    changed = base_input;
    changed.state.maximize = geometry::maximize_state_t::restore;
    expect_separate(changed, "maximise and restore states");

    changed = base_input;
    changed.theme_generation = 1;
    expect_separate(changed, "theme generations");

    changed = base_input;
    changed.line_thickness = 1.4;
    expect_separate(changed, "button line thicknesses");

    changed = base_input;
    changed.output_scale = -1.0;
    const auto invalid_scale = geometry::resolve_cache_key(changed);
    expect(invalid_scale.output_scale == 1.0 && invalid_scale.raster_size == base.raster_size,
        "an invalid runtime cache scale uses the safe scale");

    changed.output_scale = 0.0;
    const auto zero_scale = geometry::resolve_cache_key(changed);
    expect(zero_scale.output_scale == 1.0 && zero_scale.raster_size == base.raster_size,
        "a zero runtime cache scale uses the safe scale");

    changed = base_input;
    changed.state.kind = static_cast<geometry::button_kind_t>(-1);
    const auto invalid_state = geometry::resolve_cache_key(changed);
    expect(invalid_state.state == geometry::button_state_t{},
        "an invalid runtime button state uses the safe state");
}

void test_complete_cache_key_contract()
{
    const auto input = valid_cache_input();
    const auto base  = geometry::resolve_cache_key(input);
    expect(geometry::is_valid(input), "the complete cache-key input is valid");
    expect((base.resolved_asset_identity == input.resolved_asset_identity) &&
        (base.colour == input.colour) &&
        (base.background_colour == input.background_colour),
        "the cache key preserves the asset identity and both RGBA colours");

    struct key_change_t
    {
        const char *field;
        void (*apply)(geometry::button_cache_key_t& key);
    };

    const key_change_t changes[] = {
        {"button kind", [] (auto& key) {key.state.kind = geometry::button_kind_t::minimize;}},
        {"focus state", [] (auto& key) {key.state.focus = geometry::focus_state_t::active;}},
        {"interaction state", [] (auto& key)
            {key.state.interaction = geometry::interaction_state_t::hover;}
        },
        {"maximise state", [] (auto& key)
            {key.state.maximize = geometry::maximize_state_t::restore;}
        },
        {"asset content hash", [] (auto& key)
            {key.resolved_asset_identity.content_hash += 1;}
        },
        {"asset source generation", [] (auto& key)
            {key.resolved_asset_identity.source_generation += 1;}
        },
        {"red channel", [] (auto& key) {key.colour.r += 0.01;}},
        {"green channel", [] (auto& key) {key.colour.g += 0.01;}},
        {"blue channel", [] (auto& key) {key.colour.b += 0.01;}},
        {"alpha channel", [] (auto& key) {key.colour.a += 0.01;}},
        {"background red channel", [] (auto& key) {key.background_colour.r += 0.01;}},
        {"background green channel", [] (auto& key) {key.background_colour.g += 0.01;}},
        {"background blue channel", [] (auto& key) {key.background_colour.b += 0.01;}},
        {"background alpha channel", [] (auto& key) {key.background_colour.a += 0.01;}},
        {"logical width", [] (auto& key) {key.logical_size.width += 1;}},
        {"logical height", [] (auto& key) {key.logical_size.height += 1;}},
        {"raster width", [] (auto& key) {key.raster_size.width += 1;}},
        {"raster height", [] (auto& key) {key.raster_size.height += 1;}},
        {"SVG x proportion", [] (auto& key) {key.svg_proportions.x += 0.01;}},
        {"SVG y proportion", [] (auto& key) {key.svg_proportions.y += 0.01;}},
        {"SVG width proportion", [] (auto& key) {key.svg_proportions.width -= 0.01;}},
        {"SVG height proportion", [] (auto& key) {key.svg_proportions.height -= 0.01;}},
        {"exact output scale", [] (auto& key) {key.output_scale += 0.01;}},
        {"button line thickness", [] (auto& key) {key.line_thickness += 0.1;}},
        {"theme generation", [] (auto& key) {key.theme_generation += 1;}},
    };

    for (const auto& item : changes)
    {
        auto changed = base;
        item.apply(changed);
        expect(!(base == changed),
            std::string{"the cache-key equality separates the "} + item.field);
    }

    auto changed_input = input;
    changed_input.resolved_asset_identity.content_hash += 1;
    expect(!(base == geometry::resolve_cache_key(changed_input)),
        "different asset content has a different stable cache identity");

    changed_input = input;
    changed_input.resolved_asset_identity.source_generation += 1;
    expect(!(base == geometry::resolve_cache_key(changed_input)),
        "a reloaded asset generation has a different stable cache identity");

    const geometry::rgba_t invalid_colours[] = {
        {-0.1, 0.2, 0.3, 0.4},
        {0.1, 1.1, 0.3, 0.4},
        {0.1, 0.2, std::numeric_limits<double>::infinity(), 0.4},
        {0.1, 0.2, 0.3, std::numeric_limits<double>::quiet_NaN()},
    };
    for (const auto& colour : invalid_colours)
    {
        changed_input = input;
        changed_input.colour = colour;
        const auto resolved = geometry::resolve_cache_key(changed_input);
        expect(!geometry::is_valid(colour) && (resolved.colour == geometry::rgba_t{}),
            "an invalid cache colour uses transparent black");
    }

    for (const auto& colour : invalid_colours)
    {
        changed_input = input;
        changed_input.background_colour = colour;
        const auto resolved = geometry::resolve_cache_key(changed_input);
        expect(!geometry::is_valid(colour) &&
            (resolved.background_colour == geometry::rgba_t{}),
            "an invalid cache background colour uses transparent black");
    }

    changed_input = input;
    changed_input.resolved_asset_identity.content_hash = 0;
    const auto invalid_identity = geometry::resolve_cache_key(changed_input);
    expect(!geometry::is_valid(changed_input.resolved_asset_identity) &&
        (invalid_identity.resolved_asset_identity == geometry::resolved_asset_identity_t{}),
        "an unresolved asset identity uses the safe identity");
}

void test_button_frame_cache_decision()
{
    int idle_schedules    = 0;
    int prepare_calls     = 0;
    int damage_requests   = 0;
    bool prepare_succeeds = true;
    std::function<void()> deferred_prepare;
    auto schedule_idle_prepare = [&] (auto prepare)
    {
        ++idle_schedules;
        deferred_prepare = std::move(prepare);
    };
    auto prepare = [&]
    {
        ++prepare_calls;
        if (prepare_succeeds)
        {
            ++damage_requests;
        }
    };

    expect(geometry::resolve_button_frame_cache_decision(true,
        schedule_idle_prepare, prepare) ==
        geometry::button_frame_cache_decision_t::use_cached_texture,
        "a frame cache hit uses the cached texture");
    expect((idle_schedules == 0) && (prepare_calls == 0) &&
        (damage_requests == 0) && !deferred_prepare,
        "a frame cache hit neither schedules nor prepares a texture");

    expect(geometry::resolve_button_frame_cache_decision(false,
        schedule_idle_prepare, prepare) ==
        geometry::button_frame_cache_decision_t::schedule_idle_prepare,
        "a frame cache miss schedules preparation for idle time");
    expect((idle_schedules == 1) && (prepare_calls == 0) &&
        (damage_requests == 0) && bool(deferred_prepare),
        "a frame cache miss does not prepare a texture synchronously");

    deferred_prepare();
    expect((prepare_calls == 1) && (damage_requests == 1),
        "successful deferred preparation requests damage after the idle callback");

    prepare_succeeds = false;
    deferred_prepare = {};
    expect(geometry::resolve_button_frame_cache_decision(false,
        schedule_idle_prepare, prepare) ==
        geometry::button_frame_cache_decision_t::schedule_idle_prepare,
        "another frame cache miss schedules another idle preparation");
    deferred_prepare();
    expect((prepare_calls == 2) && (damage_requests == 1),
        "failed deferred preparation does not request damage");
}
}

int main()
{
    test_automatic_sizes();
    test_legacy_automatic_sizes_preserve_title_height();
    test_positive_inputs_and_containment();
    test_invalid_inputs_and_view_boxes();
    test_group_positions();
    test_svg_cache_contracts();
    test_scale_and_cache_contracts();
    test_complete_cache_key_contract();
    test_button_frame_cache_decision();
    return failures == 0 ? 0 : 1;
}
