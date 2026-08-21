#pragma once

namespace wf
{
namespace pixdecor
{
template<class ButtonSizeOption, class TitleHeightOption, class RecreateFrames>
void register_size_option_callbacks(ButtonSizeOption& button_size, TitleHeightOption& title_height,
    RecreateFrames recreate_frames)
{
    auto callback = [recreate_frames] () mutable
    {
        recreate_frames();
    };

    button_size.set_callback(callback);
    title_height.set_callback(callback);
}
}
}
