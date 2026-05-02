#pragma once

#include <chrono>
#include <string_view>

namespace cfg {

// Muni stop
inline constexpr std::string_view AGENCY = "SF";
inline constexpr std::string_view STOP_CODE = "15728";
inline constexpr auto STOP_TTL = std::chrono::seconds{60};
inline constexpr auto STOP_RETRY_TTL = std::chrono::seconds{30};

// Weather
inline constexpr auto WEATHER_TTL = std::chrono::seconds{3600};
inline constexpr auto WEATHER_RETRY_TTL = std::chrono::seconds{120};

// Home Assistant laundry
inline constexpr auto LAUNDRY_TTL = std::chrono::seconds{60};
inline constexpr auto LAUNDRY_ACTIVE_TTL = std::chrono::seconds{20};
inline constexpr auto LAUNDRY_RETRY_TTL = std::chrono::seconds{30};
inline constexpr auto LAUNDRY_ROTATE = std::chrono::seconds{30};

// Daypart schedule (minutes from midnight, local time).
inline constexpr int NIGHT_START_MIN = 20 * 60 + 30;  // 8:30 PM
inline constexpr int NIGHT_END_MIN = 7 * 60;          // 7:00 AM
inline constexpr int DIM_START_MIN = 23 * 60;         // 11:00 PM
inline constexpr int DIM_END_MIN = 5 * 60;            // 5:00 AM
inline constexpr int DIM_BRIGHTNESS = 60;
inline constexpr int FULL_BRIGHTNESS = 100;

// HTTP
inline constexpr long CONNECT_TIMEOUT_S = 30;
inline constexpr long READ_TIMEOUT_S = 60;

// Fail threshold before showing "Error" red.
inline constexpr int FAIL_THRESHOLD = 5;

// Boot grace before matrix init (so SSH stays responsive on fresh boot).
inline constexpr auto BOOT_GRACE = std::chrono::seconds{90};

}  // namespace cfg
