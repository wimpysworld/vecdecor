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

    return 0;
}
