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

template<class ActiveOption, class InactiveOption, class HoverOption, class PressedOption,
    class InvalidateColours>
void register_button_colour_option_callbacks(ActiveOption& active, InactiveOption& inactive,
    HoverOption& hover, PressedOption& pressed, InvalidateColours invalidate_colours)
{
    auto callback = [invalidate_colours] () mutable
    {
        invalidate_colours();
    };

    active.set_callback(callback);
    inactive.set_callback(callback);
    hover.set_callback(callback);
    pressed.set_callback(callback);
}

template<class MinimizeOption, class MaximizeOption, class RestoreOption, class CloseOption,
    class ReloadSource>
void register_button_svg_option_callbacks(MinimizeOption& minimize, MaximizeOption& maximize,
    RestoreOption& restore, CloseOption& close, ReloadSource reload_source)
{
    minimize.set_callback([reload_source] () mutable
    {
        reload_source(0);
    });
    maximize.set_callback([reload_source] () mutable
    {
        reload_source(1);
    });
    restore.set_callback([reload_source] () mutable
    {
        reload_source(2);
    });
    close.set_callback([reload_source] () mutable
    {
        reload_source(3);
    });
}
}
}
