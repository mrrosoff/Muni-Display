"""Open-Meteo forecast + XBM icon loading."""

import os
import re
import time
from datetime import datetime, timezone

import requests

ICONS_DIR = os.path.join(os.path.dirname(__file__), "icons")
FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
LAT, LON = 37.7605, -122.4356  # Castro/Market-ish

# WMO weather code -> icon name (Dhole/weather-pixel-icons).
# Day variants; "tomorrow" forecast is daytime.
CODE_ICON = {
    0: "sun", 1: "cloud_sun", 2: "cloud_sun", 3: "clouds",
    45: "cloud", 48: "cloud",
    51: "rain0_sun", 53: "rain0", 55: "rain0",
    56: "rain0", 57: "rain0",
    61: "rain1_sun", 63: "rain1", 65: "rain2",
    66: "rain1", 67: "rain2",
    71: "snow_sun", 73: "snow", 75: "snow", 77: "snow",
    80: "rain1_sun", 81: "rain1", 82: "rain2",
    85: "snow", 86: "snow",
    95: "lightning", 96: "rain_lightning", 99: "rain_lightning",
}

CODE_WORD = {
    0: "Clear", 1: "Clear", 2: "Cloudy", 3: "Cloudy",
    45: "Fog", 48: "Fog",
    51: "Drizzle", 53: "Drizzle", 55: "Drizzle",
    56: "Drizzle", 57: "Drizzle",
    61: "Rain", 63: "Rain", 65: "Rain",
    66: "Rain", 67: "Rain",
    71: "Snow", 73: "Snow", 75: "Snow", 77: "Snow",
    80: "Showers", 81: "Showers", 82: "Showers",
    85: "Snow", 86: "Snow",
    95: "Storm", 96: "Storm", 99: "Storm",
}


def load_xbm(path):
    """Parse XBM file -> (width, height, pixels[y][x] bool)."""
    with open(path, "r") as f:
        text = f.read()
    w = int(re.search(r"#define \w+_width (\d+)", text).group(1))
    h = int(re.search(r"#define \w+_height (\d+)", text).group(1))
    bytes_ = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", text)]
    row_bytes = (w + 7) // 8
    pixels = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            b = bytes_[y * row_bytes + x // 8]
            if b & (1 << (x % 8)):
                pixels[y][x] = True
    return w, h, pixels


def load_icons():
    icons = {}
    for name in CODE_ICON.values():
        if name in icons:
            continue
        path = os.path.join(ICONS_DIR, f"{name}.xbm")
        icons[name] = load_xbm(path)
    return icons


def fetch_forecast(ca_bundle, day_index=1, timeout=(5, 10)):
    params = {
        "latitude": LAT, "longitude": LON,
        "daily": "weather_code,temperature_2m_max,temperature_2m_min,"
                 "precipitation_probability_max,sunrise,sunset",
        "temperature_unit": "fahrenheit",
        "timezone": "America/Los_Angeles",
        "forecast_days": 2,
    }
    r = requests.get(FORECAST_URL, params=params, timeout=timeout,
                     verify=ca_bundle)
    r.raise_for_status()
    data = r.json()
    d = data["daily"]
    i = day_index
    return {
        "code": int(d["weather_code"][i]),
        "hi": round(d["temperature_2m_max"][i]),
        "lo": round(d["temperature_2m_min"][i]),
        "precip": int(d["precipitation_probability_max"][i] or 0),
        "day_index": i,
        "fetched_at": time.time(),
    }
