#include "deco-button.hpp"
#include "deco-theme.hpp"
#include <utility>
#include <wayfire/opengl.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util.hpp>

namespace wf
{
namespace pixdecor
{
button_t::button_t(pixdecor_theme_t& t, std::function<void()> damage) :
    damage_callback(std::move(damage)), theme(&t),
    get_button_bounds([this] { return theme->get_button_bounds(); }),
    hover(std::make_unique<button_animation_t>([] (const std::string& option_name)
{
    wf::option_wrapper_t<int> duration(option_name);
    return std::shared_ptr<wf::config::option_t<int>>(duration);
}))
{
    auto idle_damage = std::make_shared<wf::wl_idle_call>();
    schedule_redraw = [this, idle_damage]
    {
        idle_damage->run_once([this]
        {
            damage_callback();
        });
    };
}

void button_t::render(const wf::scene::render_instruction_t& data, wf::geometry_t button_geometry,
    bool active, bool maximized)
{
    if (!type_set || (button_geometry.width <= 0) || (button_geometry.height <= 0))
    {
        return;
    }

    class wayfire_button_backend_t : public button_render_backend_t
    {
      public:
        pixdecor_theme_t& theme;
        const wf::scene::render_instruction_t& data;
        wf::geometry_t button_geometry;
        const std::function<void()>& damage_callback;
        const wf::owned_texture_t *normal_texture  = nullptr;
        const wf::owned_texture_t *hovered_texture = nullptr;

        wayfire_button_backend_t(pixdecor_theme_t& theme,
            const wf::scene::render_instruction_t& data, wf::geometry_t button_geometry,
            const std::function<void()>& damage_callback) :
            theme(theme), data(data), button_geometry(button_geometry),
            damage_callback(damage_callback)
        {}

        bool request_texture(button_texture_slot_t slot,
            const geometry::button_state_t& state,
            geometry::logical_size_t logical_size) override
        {
            auto texture = theme.get_button_texture(
                state, logical_size, data.target.scale, damage_callback);
            if (slot == button_texture_slot_t::normal)
            {
                normal_texture = texture;
            } else
            {
                hovered_texture = texture;
            }

            return bool(texture);
        }

        void draw_texture(button_texture_slot_t slot, double alpha) override
        {
            const auto texture = slot == button_texture_slot_t::normal ?
                normal_texture : hovered_texture;
            OpenGL::render_texture(wf::gles_texture_t{texture->get_texture()}, data.target,
                button_geometry, {1, 1, 1, alpha}, OpenGL::RENDER_FLAG_CACHED);
            data.pass->custom_gles_subpass(data.target, [&]
            {
                wf::gles::for_each_scissor_rect(data.target, data.damage, [&]
                {
                    OpenGL::draw_cached();
                });
            });
            OpenGL::clear_cached();
        }
    };

    wayfire_button_backend_t backend(*theme, data, button_geometry, damage_callback);
    const geometry::logical_size_t logical_size = {
        static_cast<int>(button_geometry.width),
        static_cast<int>(button_geometry.height),
    };
    render(backend, logical_size, active, maximized);
}
}
}
