#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

// WMO weather code -> icon name (matches an XBM file in icons/).
inline const std::unordered_map<int, std::string> CODE_ICON = {
    {0, "sun"},
    {1, "cloud_sun"},
    {2, "cloud_sun"},
    {3, "clouds"},
    {45, "cloud_wind"},
    {48, "cloud_wind"},
    {51, "rain0_sun"},
    {53, "rain0"},
    {55, "rain0"},
    {56, "rain0"},
    {57, "rain0"},
    {61, "rain1_sun"},
    {63, "rain1"},
    {65, "rain2"},
    {66, "rain1"},
    {67, "rain2"},
    {71, "snow_sun"},
    {73, "snow"},
    {75, "snow"},
    {77, "snow"},
    {80, "rain1_sun"},
    {81, "rain1"},
    {82, "rain2"},
    {85, "snow"},
    {86, "snow"},
    {95, "lightning"},
    {96, "rain_lightning"},
    {99, "rain_lightning"},
};

// WMO weather code -> short condition word.
inline const std::unordered_map<int, std::string> CODE_WORD = {
    {0, "Clear"},    {1, "Clear"},    {2, "Cloudy"},   {3, "Cloudy"},   {45, "Fog"},
    {48, "Fog"},     {51, "Drizzle"}, {53, "Drizzle"}, {55, "Drizzle"}, {56, "Drizzle"},
    {57, "Drizzle"}, {61, "Rain"},    {63, "Rain"},    {65, "Rain"},    {66, "Rain"},
    {67, "Rain"},    {71, "Snow"},    {73, "Snow"},    {75, "Snow"},    {77, "Snow"},
    {80, "Showers"}, {81, "Showers"}, {82, "Showers"}, {85, "Snow"},    {86, "Snow"},
    {95, "Storm"},   {96, "Storm"},   {99, "Storm"},
};

inline std::string_view icon_for_code(int code) {
    auto it = CODE_ICON.find(code);
    return it != CODE_ICON.end() ? std::string_view{it->second}
                                 : std::string_view{"cloud"};
}

inline std::string_view word_for_code(int code) {
    auto it = CODE_WORD.find(code);
    return it != CODE_WORD.end() ? std::string_view{it->second} : std::string_view{};
}
