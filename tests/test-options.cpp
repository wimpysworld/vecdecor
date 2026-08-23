#include "deco-options.hpp"

#include <functional>
#include <iostream>
#include <utility>

namespace
{
class fake_option_t
{
  public:
    template<class Callback>
    void set_callback(Callback new_callback)
    {
        callback = std::move(new_callback);
    }

    void change()
    {
        callback();
    }

  private:
    std::function<void()> callback;
};
}

int main()
{
    fake_option_t button_size;
    fake_option_t title_height;
    int recreation_count = 0;

    wf::pixdecor::register_size_option_callbacks(button_size, title_height, [&]
    {
        ++recreation_count;
    });

    button_size.change();
    if (recreation_count != 1)
    {
        std::cerr << "FAIL: the button-size callback must recreate frames exactly once\n";
        return 1;
    }

    title_height.change();
    if (recreation_count != 2)
    {
        std::cerr << "FAIL: the title-height callback must recreate frames exactly once\n";
        return 1;
    }

    fake_option_t active;
    fake_option_t inactive;
    fake_option_t hover;
    fake_option_t pressed;
    fake_option_t minimize;
    fake_option_t maximize;
    fake_option_t restore;
    fake_option_t close;
    int colour_invalidation_count = 0;
    int svg_reload_count = 0;
    int reloaded_source  = -1;

    wf::pixdecor::register_button_colour_option_callbacks(
        active, inactive, hover, pressed, [&]
    {
        ++colour_invalidation_count;
    });
    wf::pixdecor::register_button_svg_option_callbacks(
        minimize, maximize, restore, close, [&] (int source)
    {
        ++svg_reload_count;
        reloaded_source = source;
    });

    active.change();
    inactive.change();
    hover.change();
    pressed.change();
    if ((colour_invalidation_count != 4) || (svg_reload_count != 0))
    {
        std::cerr << "FAIL: colour callbacks must invalidate textures without reloading SVGs\n";
        return 1;
    }

    fake_option_t *svg_options[] = {&minimize, &maximize, &restore, &close};
    for (int source = 0; source < 4; ++source)
    {
        svg_options[source]->change();
        if ((svg_reload_count != source + 1) || (reloaded_source != source) ||
            (colour_invalidation_count != 4))
        {
            std::cerr << "FAIL: each SVG callback must identify only its selected source\n";
            return 1;
        }
    }

    return 0;
}
