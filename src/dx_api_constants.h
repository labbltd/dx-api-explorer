#ifndef DX_API_CONSTANTS_H
#define DX_API_CONSTANTS_H

#include <array>
#include <chrono>
#include <utility>
#include "imgui.h"

namespace dx_api_explorer
{
constexpr auto config_file_name = "dx_api_explorer_config.json";
constexpr auto json_indent = 2;
constexpr auto network_thread_period_ticks = std::chrono::milliseconds{ 25 };
constexpr auto spinner_period_ticks = std::chrono::milliseconds{ 15 };
constexpr std::array<std::pair<float, const char*>, 6> font_sizes =
{
    std::make_pair(10.0f, "10"),
    std::make_pair(20.0f, "20"),
    std::make_pair(30.0f, "30"),
    std::make_pair(40.0f, "40"),
    std::make_pair(50.0f, "50"),
    std::make_pair(60.0f, "60")
};
constexpr auto font_file_name = "Cousine-Regular.ttf";
constexpr auto hidpi_pixel_width_threshold = 900; // Screen width divided by this gives us our default index into our font sizes array.
constexpr auto selected_text_color = ImVec4(0, 0, 1, 1);
}

#endif // DX_API_CONSTANTS_H