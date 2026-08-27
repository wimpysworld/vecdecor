#include "wayfire/geometry.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel.hpp"
#include <memory>
#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include "deco-subsurface.hpp"
#include "deco-layout.hpp"
#include "deco-theme.hpp"
#include <wayfire/window-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/scene-render.hpp>

#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>
#include "shade.hpp"


namespace wf
{
namespace pixdecor
{
wf::option_wrapper_t<std::string> titlebar_opt{"vecdecor/titlebar"};
wf::option_wrapper_t<bool> enable_shade{"vecdecor/enable_shade"};
wf::option_wrapper_t<std::string> title_font{"vecdecor/title_font"};
wf::option_wrapper_t<bool> maximized_borders{"vecdecor/maximized_borders"};
wf::option_wrapper_t<int> title_text_align{"vecdecor/title_text_align"};

class simple_decoration_node_t : public wf::scene::node_t, public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> _view;
    wf::signal::connection_t<wf::view_title_changed_signal> title_set =
        [=] (wf::view_title_changed_signal *ev)
    {
        if (auto view = _view.lock())
        {
            view->damage();
        }
    };
    wf::signal::connection_t<wf::output_configuration_changed_signal> on_output_configuration_changed =
        [=] (wf::output_configuration_changed_signal *ev)
    {
        if (ev->changed_fields & wf::OUTPUT_SCALE_CHANGE)
        {
            prepare_buttons(ev->state.scale);
            if (auto view = _view.lock())
            {
                view->damage();
            }
        }
    };
    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output =
        [=] (wf::view_set_output_signal*)
    {
        if (auto view = _view.lock())
        {
            auto output = view->get_output();
            connect_output_configuration(output);
            const double scale = output ? output->handle->scale : 1.0;
            prepare_buttons(scale);
            view->damage();
        }
    };

    void connect_output_configuration(wf::output_t *output)
    {
        on_output_configuration_changed.disconnect();
        if (output)
        {
            output->connect(&on_output_configuration_changed);
        }
    }

    void update_title(int width, int height, int t_width, int border, int buttons_width, double scale)
    {
        if (auto view = _view.lock())
        {
            int target_width  = width * scale;
            int target_height = height * scale;

            if ((int(title_text_align) != title_texture.title_text_align) ||
                (view->get_title() != title_texture.current_text) ||
                (target_width != title_texture.tex.get_size().width) ||
                (std::string(title_font) != title_texture.title_font_string) ||
                (target_height != title_texture.tex.get_size().height) ||
                (view->activated != title_texture.rendered_for_activated_state))
            {
                auto surface = theme.render_text(view->get_title(),
                    target_width, target_height, t_width, border, buttons_width, view->activated);
                title_texture.tex = owned_texture_t{surface};
                cairo_surface_destroy(surface);
                title_texture.title_font_string = title_font;
                title_texture.current_text     = view->get_title();
                title_texture.title_text_align = int(title_text_align);
                title_texture.rendered_for_activated_state = view->activated;
            }
        }
    }

    struct
    {
        wf::owned_texture_t tex;
        std::string current_text = "";
        bool rendered_for_activated_state = false;
        int title_text_align = int(title_text_align);
        std::string title_font_string = title_font;
    } title_texture;

  public:
    pixdecor_theme_t theme;
    pixdecor_layout_t layout;
    layout_input_adapter_t input_adapter;
    wf::regionf_t cached_region;

    wf::dimensions_t size;

    int current_thickness;
    int current_titlebar;
    simple_decoration_node_t(wayfire_toplevel_view view, button_renderer_t& button_renderer,
        const std::uint64_t& theme_generation) :
        node_t(false),
        theme{button_renderer, theme_generation},
        layout{theme, [=] (wf::geometry_t box)
        {
            wf::scene::damage_node(shared_from_this(), box + wf::pointf_t{get_offset()});
        }},
        input_adapter{{
                    [this] (layout_input_point_t point)
            {
                const auto offset = get_offset();
                return layout_input_point_t{point.x - offset.x, point.y - offset.y};
            },
                    [this] (int x, int y) { return layout.handle_motion(x, y); },
                    [this] (bool pressed) { return layout.handle_press_event(pressed); },
                    [this] (int delta) { return layout.handle_axis_event(delta); },
                    [this] (bool clear_double_click)
            {
                return layout.handle_focus_lost(clear_double_click);
            },
                    [this] (const layout_button_update_t& update)
            {
                layout.apply_button_update(update);
            },
                    [this] (const layout_input_response_t& response) { handle_action(response); },
                }}
    {
        this->_view = view->weak_from_this();
        view->connect(&title_set);
        view->connect(&on_view_set_output);
        connect_output_configuration(view->get_output());

        // make sure to hide frame if the view is fullscreen
        update_decoration_size();
    }

    ~simple_decoration_node_t()
    {
        remove_shade_transformers();
    }

    wf::point_t get_offset()
    {
        auto view = _view.lock();
        if (view && view->pending_tiled_edges() && !maximized_borders)
        {
            return {0, -current_titlebar};
        }

        return {-current_thickness, -current_titlebar};
    }

    void render_title(const wf::scene::render_instruction_t& data,
        const wf::geometry_t& geometry, int t_width, int border, int buttons_width)
    {
        update_title(geometry.width, geometry.height, t_width, border, buttons_width, data.target.scale);
        OpenGL::render_texture(wf::gles_texture_t{title_texture.tex.get_texture()}, data.target, geometry,
            glm::vec4(1.0f), OpenGL::RENDER_FLAG_CACHED);

        data.pass->custom_gles_subpass(data.target, [&]
        {
            wf::gles::for_each_scissor_rect(data.target, data.damage, [&]
            {
                OpenGL::draw_cached();
            });
        });

        OpenGL::clear_cached();
    }

    void render_region(const wf::scene::render_instruction_t& data, wf::point_t origin)
    {
        int border = theme.get_border_size();
        wf::geometry_t geometry = wf::construct_box(wf::pointf_t{origin}, size);

        bool activated  = false;
        bool fullscreen = false;
        uint32_t tiled_edges = 0;
        if (auto view = _view.lock())
        {
            activated   = view->activated;
            fullscreen  = view->toplevel()->pending().fullscreen;
            tiled_edges = view->pending_tiled_edges();
        }

        const bool maximized = tiled_edges != 0;
        const auto background_state = fullscreen ? background_state_t::fullscreen :
            (tiled_edges == wf::TILED_EDGES_ALL) ? background_state_t::maximised :
            tiled_edges ? background_state_t::tiled : background_state_t::floating;

        auto renderables = layout.get_renderable_areas();
        auto offset =
            wf::point_t{origin.x,
            origin.y -
            ((maximized && !maximized_borders) ? -border / 2 : border / 4)};

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(data.target);

            theme.render_background(data, geometry, activated, background_state);

            if (((std::string(titlebar_opt) == "never") ||
                 ((std::string(titlebar_opt) == "maximized") && !maximized) ||
                 ((std::string(titlebar_opt) == "windowed") && maximized)) &&
                (std::string(titlebar_opt) != "always"))
            {
                return;
            }

            int buttons_width = 0;
            for (auto item : renderables)
            {
                if (item->get_type() != DECORATION_AREA_TITLE)
                {
                    buttons_width += item->get_geometry().width;
                }
            }

            /* Draw title & buttons */
            for (auto item : renderables)
            {
                if (item->get_type() == DECORATION_AREA_TITLE)
                {
                    render_title(data,
                        item->get_geometry() + wf::pointf_t{offset}, size.width - border * 2, border,
                        buttons_width);
                } else // button
                {
                    item->as_button().render(data,
                        item->get_geometry() + wf::pointf_t{origin}, activated, maximized);
                }
            }
        });
    }

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        double border = theme.get_border_size();
        double r = std::min(border, double(MIN_RESIZE_HANDLE_SIZE)) - MIN_RESIZE_HANDLE_SIZE;
        wf::pointf_t local = at - wf::pointf_t{get_offset()};
        if (auto view = _view.lock())
        {
            if (view->toplevel()->current().fullscreen || view->toplevel()->pending().fullscreen)
            {
                return {};
            }

            wf::geometry_t g = view->get_geometry();
            g.x = g.y = 0;
            g   = wf::expand_geometry_by_margins(g, wf::decoration_margins_t{-r, -r, -r, -r});
            wf::regionf_t deco_region{g};

            if (deco_region.contains_pointf(local))
            {
                return wf::scene::input_node_t{
                    .node = this,
                    .local_coords = local,
                };
            }
        }

        return {};
    }

    pointer_interaction_t& pointer_interaction() override
    {
        return *this;
    }

    touch_interaction_t& touch_interaction() override
    {
        return *this;
    }

    class decoration_render_instance_t : public wf::scene::render_instance_t
    {
        simple_decoration_node_t *self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        decoration_render_instance_t(simple_decoration_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = self;
            this->push_damage = push_damage;
            self->connect(&on_surface_damage);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto our_region = self->cached_region + wf::pointf_t{self->get_offset()};
            wf::regionf_t our_damage = damage & our_region;
            if (!our_damage.empty())
            {
                instructions.push_back(wf::scene::render_instruction_t{
                            .instance = this,
                            .target   = target,
                            .damage   = std::move(our_damage),
                        });
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            auto offset = self->get_offset();
            self->render_region(data, offset);
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output = nullptr) override
    {
        instances.push_back(std::make_unique<decoration_render_instance_t>(this, push_damage));
    }

    wf::geometry_t get_bounding_box() override
    {
        return wf::construct_box(wf::pointf_t{get_offset()}, size);
    }

    /* wf::compositor_surface_t implementation */
    void handle_pointer_enter(wf::pointf_t point) override
    {
        input_adapter.pointer_motion({point.x, point.y});
    }

    void handle_pointer_leave() override
    {
        input_adapter.pointer_focus_lost();
    }

    void handle_pointer_motion(wf::pointf_t to, uint32_t) override
    {
        input_adapter.pointer_motion({to.x, to.y});
    }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        if (ev.button != BTN_LEFT)
        {
            return;
        }

        if (ev.state == WL_POINTER_BUTTON_STATE_PRESSED)
        {
            input_adapter.pointer_button(true);
        } else
        {
            input_adapter.pointer_button(false);
        }
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& ev) override
    {
        if (ev.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            input_adapter.pointer_axis(ev.delta);
        }
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(shade_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(shade_transformer_name);
        }
    }

    void remove_shade_transformers()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            pop_transformer(view);
        }
    }

    std::shared_ptr<pixdecor_shade> ensure_transformer(wayfire_view view, int titlebar_height)
    {
        auto tmgr = view->get_transformed_node();
        if (auto tr = tmgr->get_transformer<pixdecor_shade>(shade_transformer_name))
        {
            return tr;
        }

        auto node = std::make_shared<pixdecor_shade>(view, titlebar_height);
        tmgr->add_transformer(node, wf::TRANSFORMER_2D, shade_transformer_name);
        auto tr = tmgr->get_transformer<pixdecor_shade>(shade_transformer_name);

        return tr;
    }

    void init_shade(wayfire_view view, bool shade, int titlebar_height)
    {
        if (!view)
        {
            return;
        }

        auto tr = view->get_transformed_node()->get_transformer<pixdecor_shade>(
            shade_transformer_name);
        const shade_control_actions_t actions{
            [&] () { tr = ensure_transformer(view, titlebar_height); },
            [&] ()
            {
                tr->set_titlebar_height(titlebar_height);
                tr->init_animation(shade);
            },
        };
        apply_shade_control(bool(enable_shade), shade, view->is_mapped(), bool(tr),
            actions);
    }

    void handle_action(pixdecor_layout_t::action_response_t action)
    {
        if (auto view = _view.lock())
        {
            switch (action.action)
            {
              case DECORATION_ACTION_MOVE:
                return wf::get_core().default_wm->move_request(view);

              case DECORATION_ACTION_RESIZE:
                return wf::get_core().default_wm->resize_request(view, action.edges);

              case DECORATION_ACTION_CLOSE:
                return view->close();

              case DECORATION_ACTION_TOGGLE_MAXIMIZE:
                if (view->pending_tiled_edges())
                {
                    return wf::get_core().default_wm->tile_request(view, 0);
                } else
                {
                    return wf::get_core().default_wm->tile_request(view, wf::TILED_EDGES_ALL);
                }

                break;

              case DECORATION_ACTION_SHADE:
                init_shade(view, true, current_titlebar);
                break;

              case DECORATION_ACTION_UNSHADE:
                init_shade(view, false, current_titlebar);
                break;

              case DECORATION_ACTION_MINIMIZE:
                return wf::get_core().default_wm->minimize_request(view, true);
                break;

              default:
                break;
            }
        }
    }

    void handle_touch_down(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        input_adapter.touch_down({position.x, position.y});
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t lift_off_position) override
    {
        input_adapter.touch_up({lift_off_position.x, lift_off_position.y});
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        input_adapter.touch_motion({position.x, position.y});
    }

    void recreate_frame()
    {
        update_decoration_size();
        if (auto view = _view.lock())
        {
            resize(wf::dimensions(view->get_pending_geometry()));
            wf::get_core().tx_manager->schedule_object(view->toplevel());
        }
    }

    void prepare_buttons(double output_scale)
    {
        theme.prepare_buttons(output_scale);
    }

    void resize(wf::dimensions_t dims)
    {
        if (auto view = _view.lock())
        {
            theme.set_maximize(view->pending_tiled_edges());
            layout.set_maximize(view->pending_tiled_edges());
            view->damage();
            size = dims;
            layout.resize(size.width, size.height);
            const double scale = view->get_output() ? view->get_output()->handle->scale : 1.0;
            prepare_buttons(scale);
            if (!view->toplevel()->pending().fullscreen)
            {
                this->cached_region = layout.calculate_region();
            }

            view->damage();
        }
    }

    void update_decoration_size()
    {
        if (auto view = _view.lock())
        {
            view->damage();
            bool fullscreen = view->toplevel()->pending().fullscreen;
            bool maximized  = view->toplevel()->pending().tiled_edges;
            if (fullscreen)
            {
                current_thickness = 0;
                current_titlebar  = 0;
                this->cached_region.clear();
            } else
            {
                current_thickness = theme.get_border_size();
                current_titlebar  = theme.get_title_height() +
                    ((maximized && ((std::string(titlebar_opt) == "never" ||
                        (std::string(titlebar_opt) == "maximized" && !maximized) ||
                        (std::string(titlebar_opt) == "windowed" && maximized)) &&
                        (std::string(titlebar_opt) != "always")) &&
                        !maximized_borders) ? 0 : current_thickness);
                this->cached_region = layout.calculate_region();
            }

            if (auto tr =
                    view->get_transformed_node()->get_transformer<pixdecor_shade>(
                        shade_transformer_name))
            {
                tr->set_titlebar_height(current_titlebar);
            }

            view->damage();
        }
    }
};

simple_decorator_t::simple_decorator_t(wayfire_toplevel_view view,
    button_renderer_t& button_renderer, const std::uint64_t& theme_generation)
{
    this->view = view;
    deco = std::make_shared<simple_decoration_node_t>(view, button_renderer, theme_generation);
    deco->resize(wf::dimensions(view->get_pending_geometry()));
    wf::scene::add_back(view->get_surface_root_node(), deco);

    view->connect(&on_view_activated);
    view->connect(&on_view_geometry_changed);
    view->connect(&on_view_fullscreen);
    view->connect(&on_view_tiled);

    on_view_activated = [this] (auto)
    {
        wf::scene::damage_node(deco, deco->get_bounding_box());
    };

    on_view_geometry_changed = [this] (auto)
    {
        deco->resize(wf::dimensions(this->view->get_geometry()));
        wf::get_core().tx_manager->schedule_object(this->view->toplevel());
    };

    on_view_tiled = [this] (auto)
    {
        deco->resize(wf::dimensions(this->view->get_geometry()));
        wf::get_core().tx_manager->schedule_object(this->view->toplevel());
    };

    on_view_fullscreen = [this] (auto)
    {
        if (!this->view->toplevel()->pending().fullscreen)
        {
            deco->resize(wf::dimensions(this->view->get_geometry()));
            wf::get_core().tx_manager->schedule_object(this->view->toplevel());
        }
    };
}

simple_decorator_t::~simple_decorator_t()
{
    wf::scene::remove_child(deco);
    deco.reset();
}

int simple_decorator_t::get_titlebar_height()
{
    return deco->current_titlebar;
}

void simple_decorator_t::recreate_frame()
{
    deco->recreate_frame();
}

void simple_decorator_t::update_decoration_size()
{
    deco->update_decoration_size();
}

void simple_decorator_t::update_colors()
{
    deco->theme.update_colors();
}

void simple_decorator_t::prepare_buttons(double output_scale)
{
    deco->prepare_buttons(output_scale);
}

wf::decoration_margins_t simple_decorator_t::get_margins(const wf::toplevel_state_t& state)
{
    if (state.fullscreen)
    {
        return {0, 0, 0, 0};
    }

    bool maximized = state.tiled_edges;
    deco->theme.set_maximize(maximized);

    double thickness = deco->theme.get_border_size();
    double titlebar  = deco->theme.get_title_height() +
        ((state.tiled_edges && ((std::string(titlebar_opt) == "never" ||
            (std::string(titlebar_opt) == "maximized" && !maximized) ||
            (std::string(titlebar_opt) == "windowed" && maximized)) &&
            (std::string(titlebar_opt) != "always")) && !maximized_borders) ? 0 : thickness);
    if (state.tiled_edges && !maximized_borders)
    {
        thickness = 0;
    }

    double shade_progress = 0.0;
    if (auto tr =
            view->get_transformed_node()->get_transformer<pixdecor_shade>(
                shade_transformer_name))
    {
        tr->set_titlebar_height(titlebar);
        shade_progress = tr->get_progress();
    }

    if (view->has_data(custom_data_name))
    {
        view->get_data<wf_shadow_margin_t>(custom_data_name)->set_margins(
            {0, 0, 0, double((view->get_geometry().height - titlebar) * shade_progress)});
    } else
    {
        view->store_data(std::make_unique<wf_shadow_margin_t>(), custom_data_name);
        view->get_data<wf_shadow_margin_t>(custom_data_name)->set_margins(
            {0, 0, 0, double((view->get_geometry().height - titlebar) * shade_progress)});
    }

    return wf::decoration_margins_t{
        .left   = thickness,
        .right  = thickness,
        .bottom = thickness,
        .top    = titlebar,
    };
}
}
}
