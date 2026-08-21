#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/txn/transaction-manager.hpp>

#include "deco-subsurface.hpp"
#include "wayfire/core.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/toplevel.hpp"
#include <unistd.h>
#include <dlfcn.h>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include "shade.hpp"

namespace wf
{
namespace pixdecor
{
class wayfire_pixdecor : public wf::plugin_interface_t
{
    wf::option_wrapper_t<int> border_size{"vecdecor/border_size"};
    wf::option_wrapper_t<std::string> title_font{"vecdecor/title_font"};
    wf::option_wrapper_t<int> title_text_align{"vecdecor/title_text_align"};
    wf::option_wrapper_t<std::string> titlebar{"vecdecor/titlebar"};
    wf::option_wrapper_t<bool> maximized_borders{"vecdecor/maximized_borders"};
    wf::option_wrapper_t<wf::color_t> fg_color{"vecdecor/fg_color"};
    wf::option_wrapper_t<wf::color_t> bg_color{"vecdecor/bg_color"};
    wf::option_wrapper_t<wf::color_t> fg_text_color{"vecdecor/fg_text_color"};
    wf::option_wrapper_t<wf::color_t> bg_text_color{"vecdecor/bg_text_color"};
    wf::option_wrapper_t<wf::color_t> button_color{"vecdecor/button_color"};
    wf::option_wrapper_t<wf::color_t> button_inactive_color{"vecdecor/button_inactive_color"};
    wf::option_wrapper_t<wf::color_t> button_hover_color{"vecdecor/button_hover_color"};
    wf::option_wrapper_t<wf::color_t> button_pressed_color{"vecdecor/button_pressed_color"};
    wf::option_wrapper_t<double> button_line_thickness{"vecdecor/button_line_thickness"};
    wf::option_wrapper_t<int> left_button_spacing{"vecdecor/left_button_spacing"};
    wf::option_wrapper_t<int> right_button_spacing{"vecdecor/right_button_spacing"};
    wf::option_wrapper_t<int> left_button_x_offset{"vecdecor/left_button_x_offset"};
    wf::option_wrapper_t<int> right_button_x_offset{"vecdecor/right_button_x_offset"};
    wf::option_wrapper_t<int> button_y_offset{"vecdecor/button_y_offset"};
    wf::option_wrapper_t<std::string> button_minimize_svg{"vecdecor/button_minimize_svg"};
    wf::option_wrapper_t<std::string> button_maximize_svg{"vecdecor/button_maximize_svg"};
    wf::option_wrapper_t<std::string> button_restore_svg{"vecdecor/button_restore_svg"};
    wf::option_wrapper_t<std::string> button_close_svg{"vecdecor/button_close_svg"};
    wf::option_wrapper_t<std::string> button_layout{"vecdecor/button_layout"};
    wf::option_wrapper_t<std::string> ignore_views_string{"vecdecor/ignore_views"};
    wf::option_wrapper_t<std::string> always_decorate_string{"vecdecor/always_decorate"};
    wf::option_wrapper_t<int> rounded_corner_radius{"vecdecor/rounded_corner_radius"};
    wf::view_matcher_t ignore_views{"vecdecor/ignore_views"};
    wf::view_matcher_t always_decorate{"vecdecor/always_decorate"};
    wf::option_wrapper_t<wf::keybinding_t> shade_modifier{"vecdecor/shade_modifier"};
    wf::option_wrapper_t<int> csd_titlebar_height{"vecdecor/csd_titlebar_height"};
    wf::option_wrapper_t<bool> enable_shade{"vecdecor/enable_shade"};
    wf::ipc_activator_t pixdecor_toggle_shade{"vecdecor/shade_toggle"};
    wf::wl_idle_call idle_update_views;
    std::function<void(void)> update_event;
    wf::axis_callback shade_axis_cb;

    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        // For each transaction, we need to consider what happens with participating views
        for (const auto& obj : ev->tx->get_objects())
        {
            if (auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj))
            {
                // First check whether the toplevel already has decoration
                // In that case, we should just set the correct margins
                if (auto deco = toplevel->get_data<simple_decorator_t>())
                {
                    deco->update_decoration_size();
                    toplevel->pending().margins = deco->get_margins(toplevel->pending());
                    continue;
                }

                // Second case: the view is already mapped, or the transaction does not map it.
                // The view is not being decorated, so nothing to do here.
                if (toplevel->current().mapped || !toplevel->pending().mapped)
                {
                    continue;
                }

                // Third case: the transaction will map the toplevel.
                auto view = wf::find_view_for_toplevel(toplevel);
                wf::dassert(view != nullptr, "Mapping a toplevel means there must be a corresponding view!");
                if (should_decorate_view(view))
                {
                    if (auto deco = toplevel->get_data<simple_decorator_t>())
                    {
                        deco->update_decoration_size();
                    }

                    adjust_new_decorations(view);
                }
            }
        }
    };

    wf::signal::connection_t<wf::view_decoration_state_updated_signal> on_decoration_state_changed =
        [=] (wf::view_decoration_state_updated_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);
        if (toplevel)
        {
            remove_decoration(toplevel);
        }

        update_view_decoration(ev->view);
    };

    // allows criteria containing maximized or floating check
    wf::signal::connection_t<wf::view_tiled_signal> on_view_tiled =
        [=] (wf::view_tiled_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed =
        [=] (wf::view_app_id_changed_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed =
        [=] (wf::view_title_changed_signal *ev)
    {
        update_view_decoration(ev->view);
    };

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
        if (!bool(enable_shade))
        {
            return;
        }

        if (shade)
        {
            if (view && view->is_mapped())
            {
                auto tr = ensure_transformer(view, titlebar_height);
                tr->set_titlebar_height(titlebar_height);
                tr->init_animation(shade);
            }
        } else
        {
            if (auto tr =
                    view->get_transformed_node()->get_transformer<pixdecor_shade>(
                        shade_transformer_name))
            {
                tr->set_titlebar_height(titlebar_height);
                tr->init_animation(shade);
            }
        }
    }

  public:

    void init() override
    {
        wf::get_core().connect(&on_decoration_state_changed);
        wf::get_core().tx_manager->connect(&on_new_tx);
        wf::get_core().connect(&on_app_id_changed);
        wf::get_core().connect(&on_title_changed);
        wf::get_core().connect(&on_view_tiled);

        if (bool(enable_shade))
        {
            wf::get_core().bindings->add_axis(shade_modifier, &shade_axis_cb);
        }

        pixdecor_toggle_shade.set_handler([=] (wf::output_t *output, wayfire_view view)
        {
            if (!bool(enable_shade))
            {
                return false;
            }

            if (auto toplevel = wf::toplevel_cast(view))
            {
                bool direction = true;
                auto deco = toplevel->toplevel()->get_data<simple_decorator_t>();
                if (auto tr =
                        view->get_transformed_node()->get_transformer<pixdecor_shade>(
                            shade_transformer_name))
                {
                    direction = !tr->get_direction();
                }

                init_shade(view, direction,
                    deco ? deco->get_titlebar_height() : csd_titlebar_height);
                return true;
            }

            return false;
        });

        shade_axis_cb = [=] (wlr_pointer_axis_event *ev)
        {
            auto v = wf::get_core().get_cursor_focus_view();
            if (ev->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
            {
                if (auto toplevel = wf::toplevel_cast(v))
                {
                    auto deco = toplevel->toplevel()->get_data<simple_decorator_t>();
                    init_shade(v, ev->delta < 0 ? true : false,
                        deco ? deco->get_titlebar_height() : csd_titlebar_height);
                    return true;
                }

                return true;
            }

            return false;
        };

        enable_shade.set_callback([=]
        {
            if (bool(enable_shade))
            {
                wf::get_core().bindings->add_axis(shade_modifier, &shade_axis_cb);
            } else
            {
                wf::get_core().bindings->rem_binding(&shade_axis_cb);
                remove_shade_transformers();
            }
        });

        csd_titlebar_height.set_callback([=] ()
        {
            for (auto& view : wf::get_core().get_all_views())
            {
                if (auto tr =
                        view->get_transformed_node()->get_transformer<pixdecor_shade>(
                            shade_transformer_name))
                {
                    auto toplevel = toplevel_cast(view);
                    if (toplevel)
                    {
                        if (!toplevel->toplevel()->get_data<simple_decorator_t>())
                        {
                            tr->set_titlebar_height(csd_titlebar_height);
                        }
                    }
                }
            }
        });

        for (auto& view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }

        border_size.set_callback([=]
        {
            for (auto& view : wf::get_core().get_all_views())
            {
                auto toplevel = wf::toplevel_cast(view);
                if (!toplevel || !toplevel->toplevel()->get_data<simple_decorator_t>())
                {
                    continue;
                }

                remove_decoration(toplevel);
                adjust_new_decorations(toplevel);
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        });

        fg_color.set_callback([=] { update_colors(); });
        bg_color.set_callback([=] { update_colors(); });
        fg_text_color.set_callback([=] { update_colors(); });
        bg_text_color.set_callback([=] { update_colors(); });
        ignore_views_string.set_callback([=]
        {
            idle_update_views.run_once([=] ()
            {
                for (auto& view : wf::get_core().get_all_views())
                {
                    auto toplevel = wf::toplevel_cast(view);
                    if (!toplevel)
                    {
                        continue;
                    }

                    update_view_decoration(view);
                }
            });
        });
        always_decorate_string.set_callback([=]
        {
            idle_update_views.run_once([=] ()
            {
                for (auto& view : wf::get_core().get_all_views())
                {
                    auto toplevel = wf::toplevel_cast(view);
                    if (!toplevel)
                    {
                        continue;
                    }

                    update_view_decoration(view);
                }
            });
        });

        titlebar.set_callback([=] {recreate_decorations();});
        button_color.set_callback([=] {recreate_frames();});
        button_inactive_color.set_callback([=] {recreate_frames();});
        button_hover_color.set_callback([=] {recreate_frames();});
        button_pressed_color.set_callback([=] {recreate_frames();});
        button_line_thickness.set_callback([=] {recreate_frames();});
        left_button_spacing.set_callback([=] {recreate_frames();});
        right_button_spacing.set_callback([=] {recreate_frames();});
        left_button_x_offset.set_callback([=] {recreate_frames();});
        right_button_x_offset.set_callback([=] {recreate_frames();});
        button_y_offset.set_callback([=] {recreate_frames();});
        button_minimize_svg.set_callback([=] {recreate_frames();});
        button_maximize_svg.set_callback([=] {recreate_frames();});
        button_restore_svg.set_callback([=] {recreate_frames();});
        button_close_svg.set_callback([=] {recreate_frames();});
        button_layout.set_callback([=] {recreate_frames();});
        title_text_align.set_callback([=]
        {
            for (auto& view : wf::get_core().get_all_views())
            {
                view->damage();
            }
        });
        title_font.set_callback([=] {recreate_frames();});
        rounded_corner_radius.set_callback([=] {recreate_frames();});
        maximized_borders.set_callback([=]
        {
            for (auto& view : wf::get_core().get_all_views())
            {
                auto toplevel = wf::toplevel_cast(view);
                if (!toplevel || !toplevel->toplevel()->get_data<simple_decorator_t>() ||
                    !toplevel->pending_tiled_edges())
                {
                    continue;
                }

                view->damage();
                remove_decoration(toplevel);
                adjust_new_decorations(toplevel);
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        });
        update_event = [=] (void)
        {
            update_colors();
        };

        dlopen("libpangocairo-1.0.so", RTLD_LAZY);
    }

    void fini() override
    {
        for (auto view : wf::get_core().get_all_views())
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                remove_decoration(toplevel);
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }

        on_decoration_state_changed.disconnect();
        on_new_tx.disconnect();
        on_app_id_changed.disconnect();
        on_title_changed.disconnect();

        wf::get_core().bindings->rem_binding(&shade_axis_cb);
        remove_shade_transformers();
    }

    void recreate_frame(wayfire_toplevel_view toplevel)
    {
        auto deco = toplevel->toplevel()->get_data<simple_decorator_t>();
        if (!deco)
        {
            return;
        }

        deco->recreate_frame();
    }

    void recreate_frames()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                continue;
            }

            recreate_frame(toplevel);
        }
    }

    void recreate_decorations()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel || !toplevel->toplevel()->get_data<simple_decorator_t>())
            {
                continue;
            }

            auto deco     = toplevel->toplevel()->get_data<simple_decorator_t>();
            auto& pending = toplevel->toplevel()->pending();
            if (!pending.fullscreen && !pending.tiled_edges)
            {
                pending.geometry = wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
            }

            pending.margins = deco->get_margins(pending);
            if (!pending.fullscreen && !pending.tiled_edges)
            {
                pending.geometry = wf::expand_geometry_by_margins(pending.geometry, pending.margins);
            }

            deco->recreate_frame();
            wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
        }
    }

    void update_colors()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel || !toplevel->toplevel()->get_data<simple_decorator_t>())
            {
                continue;
            }

            auto deco = toplevel->toplevel()->get_data<simple_decorator_t>();
            deco->update_colors();
            view->damage();
        }
    }

    /**
     * Uses view_matcher_t to match whether the given view needs to be ignored for decoration
     *
     * @param view The view to match
     * @return Whether the given view should be decorated?
     */
    bool ignore_decoration_of_view(wayfire_view view)
    {
        return ignore_views.matches(view);
    }

    bool should_decorate_view(wayfire_toplevel_view view)
    {
        return (view->should_be_decorated() && !ignore_decoration_of_view(view)) || always_decorate.matches(
            view);
    }

    void adjust_new_decorations(wayfire_toplevel_view view)
    {
        auto toplevel = view->toplevel();

        if (!toplevel->get_data<simple_decorator_t>())
        {
            toplevel->store_data(std::make_unique<simple_decorator_t>(view));
        }

        auto deco     = toplevel->get_data<simple_decorator_t>();
        auto& pending = toplevel->pending();
        pending.margins = deco->get_margins(pending);

        if (!pending.fullscreen && !pending.tiled_edges)
        {
            toplevel->pending().geometry = wf::expand_geometry_by_margins(
                toplevel->pending().geometry, pending.margins);
        }
    }

    void remove_decoration(wayfire_toplevel_view view)
    {
        view->toplevel()->erase_data<simple_decorator_t>();
        auto& pending = view->toplevel()->pending();
        if (!pending.fullscreen && !pending.tiled_edges)
        {
            pending.geometry = wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
        }

        pending.margins = {0, 0, 0, 0};

        std::string custom_data_name = "wf-decoration-shadow-margin";
        if (view->has_data(custom_data_name))
        {
            view->erase_data(custom_data_name);
        }
    }

    void update_view_decoration(wayfire_view view)
    {
        if (auto toplevel = wf::toplevel_cast(view))
        {
            if (!toplevel)
            {
                return;
            }

            if (!toplevel->toplevel()->get_data<simple_decorator_t>() && (always_decorate.matches(view) ||
                                                                          (should_decorate_view(toplevel)
                                                                           &&
                                                                           !ignore_views.matches(view))))
            {
                adjust_new_decorations(toplevel);
            } else if ((!always_decorate.matches(view) &&
                        (!should_decorate_view(toplevel) || ignore_views.matches(view))))
            {
                remove_decoration(toplevel);
            }

            wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
        }
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::pixdecor::wayfire_pixdecor);
