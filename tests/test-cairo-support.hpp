#pragma once

#include <cairo.h>
#include <cstddef>
#include <cstdint>

namespace test_cairo
{
inline std::uint64_t surface_digest(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    std::uint64_t digest = 14695981039346656037ULL;
    for (int byte = 0; byte < height * stride; ++byte)
    {
        digest ^= data[byte];
        digest *= 1099511628211ULL;
    }

    return digest;
}

inline std::uint32_t surface_pixel(cairo_surface_t *surface, int x, int y)
{
    cairo_surface_flush(surface);
    const int stride   = cairo_image_surface_get_stride(surface) / sizeof(std::uint32_t);
    const auto *pixels = reinterpret_cast<const std::uint32_t*>(
        cairo_image_surface_get_data(surface));
    return pixels[y * stride + x];
}

inline std::size_t visible_pixels(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    const int width    = cairo_image_surface_get_width(surface);
    const int height   = cairo_image_surface_get_height(surface);
    const int stride   = cairo_image_surface_get_stride(surface) / sizeof(std::uint32_t);
    const auto *pixels = reinterpret_cast<const std::uint32_t*>(
        cairo_image_surface_get_data(surface));
    std::size_t count = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            count += (pixels[y * stride + x] & 0xff000000U) != 0;
        }
    }

    return count;
}
}
