#pragma once

#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>

#include "deco-subsurface.hpp"
#include "shade-state.hpp"

const std::string shade_transformer_name = "pixdecor_shade";

namespace wf
{
namespace pixdecor
{
using namespace wf::scene;
using namespace wf::animation;
class shade_animation_t : public duration_t, public shade_animation_backend_t
{
  public:
    explicit shade_animation_t(
        std::shared_ptr<wf::config::option_t<wf::animation_description_t>> duration) :
        duration_t(duration)
    {}

    timed_transition_t shade{*this};

    bool running() override
    {
        return duration_t::running();
    }

    bool direction() override
    {
        return duration_t::get_direction();
    }

    double progress() const override
    {
        return shade;
    }

    void reverse() override
    {
        duration_t::reverse();
    }

    void start() override
    {
        duration_t::start();
    }
};
class pixdecor_shade : public wf::scene::view_2d_transformer_t
{
    nonstd::observer_ptr<simple_decorator_t> deco = nullptr;
    wayfire_view view;
    wf::output_t *output;
    int titlebar_height;
    wf::option_wrapper_t<wf::animation_description_t> shade_duration{"vecdecor/shade_duration"};
    shade_state_model_t state;
    bool frame_callback_active = false;

  public:
    bool last_direction = false;
    shade_animation_t progression{shade_duration};
    shade_production_adapter_t production{
        state,
        progression,
        {
            [this] () { add_frame_callback(); },
            [this] () { remove_frame_callback(); },
            [this] () { view->damage(); },
            [this] () { pop_transformer(view); },
        },
    };

    static void init_production_animation(shade_production_adapter_t& production,
        bool shade)
    {
        production.init_animation(shade);
    }

    static shade_frame_plan_t run_production_frame(
        shade_production_adapter_t& production,
        const std::function<void(const shade_frame_plan_t&)>& before_actions = {})
    {
        return production.frame(before_actions);
    }

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        pixdecor_shade *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(pixdecor_shade *self, damage_callback push_damage,
            wayfire_view view) : wf::scene::transformer_render_instance_t<transformer_base_node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage)
        {
            // We want to render ourselves only, the node does not have children
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
        }

        void render(const wf::scene::render_instruction_t& data)
        {
            auto src_box = self->get_children_bounding_box();
            gl_geometry src_geometry = {float(src_box.x), float(src_box.y),
                float(src_box.x + src_box.width), float(src_box.y + src_box.height)};
            auto src_tex = wf::scene::transformer_render_instance_t<transformer_base_node_t>::get_texture(
                1.0);
            auto shade_region = data.damage;
            int height    = src_box.height;
            auto titlebar = self->titlebar_height + self->get_shadow_margin();
            src_box.y += self->titlebar_height;
            src_box.height *= 1.0 -
                (self->get_progress() *
                    ((src_box.height - titlebar) / float(src_box.height)));
            auto progress_height = src_box.height;
            shade_region &= src_box;
            wf::gles::run_in_context([&]
            {
                wf::gles::bind_render_buffer(data.target);
                data.pass->custom_gles_subpass(data.target, [&]
                {
                    wf::gles::for_each_scissor_rect(data.target, shade_region, [&]
                    {
                        OpenGL::render_transformed_texture(wf::gles_texture_t{src_tex},
                            {src_geometry.x1, src_geometry.y1 - float(height - progress_height),
                                src_geometry.x2,
                                src_geometry.y2 - float(height - progress_height)}, {},
                            wf::gles::render_target_orthographic_projection(data.target), glm::vec4(1.0), 0);
                    });
                });

                shade_region = data.damage;
                src_box = self->get_children_bounding_box();
                src_box.height = self->titlebar_height;
                shade_region  &= src_box;
                data.pass->custom_gles_subpass(data.target, [&]
                {
                    wf::gles::for_each_scissor_rect(data.target, shade_region, [&]
                    {
                        OpenGL::render_transformed_texture(wf::gles_texture_t{src_tex}, src_geometry, {},
                            wf::gles::render_target_orthographic_projection(data.target), glm::vec4(1.0), 0);
                    });
                });
            });
        }
    };

    pixdecor_shade(wayfire_view view, int titlebar_height) : wf::scene::view_2d_transformer_t(view)
    {
        this->view   = view;
        this->output = view->get_output();
        this->titlebar_height = titlebar_height;
        this->progression.shade.set(0.0, 1.0);
        add_frame_callback();

        if (auto toplevel = wf::toplevel_cast(view))
        {
            this->deco = toplevel->toplevel()->get_data<simple_decorator_t>();
        }
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(shade_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(shade_transformer_name);
        }

        if (!deco && view->has_data(custom_data_name))
        {
            view->erase_data(custom_data_name);
        }
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        run_production_frame(production, [&] (const shade_frame_plan_t& frame)
        {
            last_direction = production.direction();
            if (auto toplevel = wf::toplevel_cast(view))
            {
                if (deco)
                {
                    /* SSD */
                    deco->get_margins(toplevel->toplevel()->pending());
                } else
                {
                    /* CSD */
                    auto bg = view->get_surface_root_node()->get_bounding_box();
                    auto vg = toplevel->get_geometry();
                    auto margins =
                        wf::decoration_margins_t{vg.x - bg.x, vg.y - bg.y,
                        bg.width - ((vg.x - bg.x) + vg.width),
                        bg.height - ((vg.y - bg.y) + vg.height)};
                    if (!view->has_data(custom_data_name))
                    {
                        view->store_data(std::make_unique<wf_shadow_margin_t>(), custom_data_name);
                    }

                    view->get_data<wf_shadow_margin_t>(custom_data_name)->set_margins(
                        {0, 0, 0,
                            double(((toplevel->get_geometry().height + margins.bottom) - titlebar_height) *
                                frame.progress)});
                }
            }
        });
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        auto bbox = this->get_children_bounding_box();
        if (((at.y - bbox.y) <
             ((1.0 -
               (get_progress() * ((bbox.height - this->titlebar_height) / float(bbox.height)))) *
              bbox.height)) &&
            ((at.y - bbox.y) > 0.0))
        {
            return floating_inner_node_t::find_node_at(at);
        }

        return {};
    }

    void init_animation(bool shade)
    {
        init_production_animation(production, shade);
        last_direction = production.direction();
    }

    void add_frame_callback()
    {
        if (output && !frame_callback_active)
        {
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
            frame_callback_active = true;
        }
    }

    void remove_frame_callback()
    {
        if (output && frame_callback_active)
        {
            output->render->rem_effect(&pre_hook);
            frame_callback_active = false;
        }
    }

    double get_progress() const
    {
        return production.progress();
    }

    void set_titlebar_height(int titlebar_height)
    {
        if (this->titlebar_height == titlebar_height)
        {
            return;
        }

        this->titlebar_height = titlebar_height;
        production.request_refresh();
    }

    int get_shadow_margin()
    {
        if (deco)
        {
            return 0;
        } else
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                auto bg = view->get_surface_root_node()->get_bounding_box();
                auto vg = toplevel->get_geometry();
                return bg.height - ((vg.y - bg.y) + vg.height);
            }
        }

        return 0;
    }

    bool get_direction()
    {
        return production.direction();
    }

    virtual ~pixdecor_shade()
    {
        remove_frame_callback();
    }
};
}
}
