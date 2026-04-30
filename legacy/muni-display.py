#!/usr/bin/env python

import json
import os
import sys
import threading
import time
from datetime import datetime, timedelta, timezone

from rgbmatrix import RGBMatrix, RGBMatrixOptions, graphics
import requests
from requests import RequestException

import math

import home_assistant
import weather

NIGHT_START_MIN = 20 * 60 + 30  # 8:30 PM
NIGHT_END_MIN = 7 * 60          # 7:00 AM
DIM_START_MIN = 23 * 60         # 11:00 PM
DIM_END_MIN = 5 * 60            # 5:00 AM
DIM_BRIGHTNESS = 60
FULL_BRIGHTNESS = 100
WEATHER_TTL = 3600     # 1 hour
WEATHER_RETRY_TTL = 120
LAUNDRY_TTL = 60       # while idle
LAUNDRY_ACTIVE_TTL = 20  # while a load is on

API_KEY = os.environ["MUNI_API_KEY"]
AGENCY = "SF"
API_URL = "https://api.511.org/transit/StopMonitoring"
CA_BUNDLE = "/etc/ssl/certs/ca-certificates.crt"

HA_URL = os.environ.get("HA_URL")
HA_TOKEN = os.environ.get("HA_TOKEN")
HA_ENABLED = bool(HA_URL and HA_TOKEN)

CONNECT_TIMEOUT = 5
READ_TIMEOUT = 10

FONTS_DIR = os.path.join(os.path.dirname(__file__), "fonts")

# Rate budget: 60 req/hr across all stops. TTLs tuned to stay under.
STOPS = {
    "15728": {"lines": {"K", "L", "M"}, "ttl": 60},    # Castro East
}

RAIL_BLUE = (70, 145, 205)
RAIL_PURPLE = (155, 90, 175)
RAIL_GREEN = (50, 165, 60)
BUS_RED = (200, 40, 40)

PAGES = [
    {
        "title": "CASTRO", "dir": "East", "duration": 10,
        "empty": "No Trains",
        "rows": [
            {"label": "K", "color": RAIL_BLUE,   "stop": "15728", "lines": {"K"}},
            {"label": "L", "color": RAIL_PURPLE, "stop": "15728", "lines": {"L"}},
            {"label": "M", "color": RAIL_GREEN,  "stop": "15728", "lines": {"M"}},
        ],
    },
]

cache = {code: {"departures": [], "last_fetch": 0.0, "cold": True} for code in STOPS}
cache_lock = threading.Lock()

weather_cache = {"data": None, "last_fetch": 0.0, "day_index": None}
weather_lock = threading.Lock()

laundry_cache = {"data": None, "last_fetch": 0.0}
laundry_lock = threading.Lock()


def log(msg):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}", flush=True)


def fetch_stop(stop_code, lines):
    params = {"api_key": API_KEY, "agency": AGENCY,
              "stopCode": stop_code, "format": "json"}
    headers = {"Connection": "close"}
    t0 = time.monotonic()
    with requests.get(API_URL, params=params, headers=headers,
                      timeout=(CONNECT_TIMEOUT, READ_TIMEOUT),
                      verify=CA_BUNDLE, stream=True) as r:
        r.raise_for_status()
        raw = r.raw.read(decode_content=True)
    data = json.loads(raw.decode("utf-8-sig"))
    log(f"stop {stop_code}: {time.monotonic() - t0:.2f}s")

    delivery = data.get("ServiceDelivery", {})
    visits = (delivery.get("StopMonitoringDelivery", {})
                      .get("MonitoredStopVisit", []))
    ts = delivery.get("ResponseTimestamp")
    now = datetime.fromisoformat(ts) if ts else datetime.now(timezone.utc)
    departures = []
    for visit in visits:
        journey = visit.get("MonitoredVehicleJourney", {})
        line = journey.get("LineRef")
        if line not in lines:
            continue
        call = journey.get("MonitoredCall", {})
        arrival_str = call.get("ExpectedArrivalTime") or call.get("AimedArrivalTime")
        if not arrival_str:
            continue
        arrival = datetime.fromisoformat(arrival_str)
        minutes = int((arrival - now).total_seconds() / 60)
        if minutes < 1:
            continue
        departures.append({"line": line, "minutes": minutes})
    departures.sort(key=lambda d: d["minutes"])
    return departures


def refresh_stop(code):
    try:
        deps = fetch_stop(code, STOPS[code]["lines"])
        with cache_lock:
            cache[code] = {"departures": deps, "last_fetch": time.time(), "cold": False}
        log(f"fetched {code}: {len(deps)} departures")
    except (RequestException, ValueError, KeyError) as e:
        log(f"fetch FAILED {code}: {type(e).__name__}: {e}")
        time.sleep(20)


def desired_day_index():
    # Midnight to 7 AM: today's forecast (index 0).
    # 7 AM to midnight: tomorrow's forecast (index 1).
    return 0 if datetime.now().hour < 7 else 1


def refresh_weather(day_index):
    t0 = time.monotonic()
    try:
        data = weather.fetch_forecast(CA_BUNDLE, day_index=day_index)
        with weather_lock:
            weather_cache["data"] = data
            weather_cache["last_fetch"] = time.time()
            weather_cache["day_index"] = day_index
        log(f"weather day={day_index}: {time.monotonic() - t0:.2f}s")
    except (RequestException, ValueError, KeyError) as e:
        with weather_lock:
            weather_cache["last_fetch"] = time.time() - (WEATHER_TTL - WEATHER_RETRY_TTL)
        log(f"weather FAILED in {time.monotonic() - t0:.2f}s: {type(e).__name__}: {e}")
        time.sleep(20)


def refresh_laundry():
    t0 = time.monotonic()
    try:
        data = home_assistant.fetch_laundry(HA_URL, HA_TOKEN)
        with laundry_lock:
            laundry_cache["data"] = data
            laundry_cache["last_fetch"] = time.time()
        log(f"laundry: {time.monotonic() - t0:.2f}s")
    except (RequestException, ValueError, KeyError) as e:
        with laundry_lock:
            laundry_cache["last_fetch"] = time.time() - (LAUNDRY_TTL - 10)
        log(f"laundry FAILED in {time.monotonic() - t0:.2f}s: {type(e).__name__}: {e}")
        time.sleep(5)


def laundry_ttl_now():
    with laundry_lock:
        data = laundry_cache["data"]
    if data and (data["washer"]["on"] or data["dryer"]["on"]):
        return LAUNDRY_ACTIVE_TTL
    return LAUNDRY_TTL


def laundry_active():
    if not HA_ENABLED:
        return False
    with laundry_lock:
        data = laundry_cache["data"]
    if not data:
        return False
    return data["washer"]["on"] or data["dryer"]["on"]


def laundry_done():
    with laundry_lock:
        data = laundry_cache["data"]
    if not data:
        return False
    now = time.time()
    for key in ("washer", "dryer"):
        s = data[key]
        if s["on"] and s["started_at"] and s["avg_min"]:
            if (now - s["started_at"]) >= s["avg_min"] * 60:
                return True
    return False


def fetcher_loop():
    while True:
        now = time.time()
        night_now = is_night()
        desired_day = desired_day_index()

        with cache_lock:
            stops_warm = all(not cache[code]["cold"] for code in STOPS)
            stale = sorted(
                ((code, now - cache[code]["last_fetch"] - STOPS[code]["ttl"])
                 for code in STOPS),
                key=lambda x: -x[1])
        stop_code, stop_overdue = stale[0]

        with weather_lock:
            weather_warm = weather_cache["data"] is not None
            w_overdue = now - weather_cache["last_fetch"] - WEATHER_TTL
            day_changed = weather_warm and weather_cache["day_index"] != desired_day

        if HA_ENABLED:
            with laundry_lock:
                l_overdue = now - laundry_cache["last_fetch"] - laundry_ttl_now()
        else:
            l_overdue = float("-inf")

        # Gate non-essential fetches so boot only loads what the current display needs.
        # Weather only when we'd actually render it (night) or for a forced day flip.
        weather_allowed = night_now or day_changed
        # HA only after the primary display has data.
        primary_warm = weather_warm if night_now else stops_warm
        ha_allowed = HA_ENABLED and primary_warm

        candidates = []
        if stop_overdue >= 0:
            candidates.append(("stop", stop_overdue))
        if weather_allowed and w_overdue >= 0:
            candidates.append(("weather", w_overdue))
        if ha_allowed and l_overdue >= 0:
            candidates.append(("laundry", l_overdue))

        if not candidates:
            time.sleep(2)
            continue

        candidates.sort(key=lambda x: -x[1])
        kind = candidates[0][0]
        if kind == "weather":
            refresh_weather(desired_day)
        elif kind == "laundry":
            refresh_laundry()
        else:
            refresh_stop(stop_code)
        time.sleep(1)


def row_state(row):
    stop = row["stop"]
    with cache_lock:
        deps = [d for d in cache[stop]["departures"] if d["line"] in row["lines"]]
        age = time.time() - cache[stop]["last_fetch"]
        cold = cache[stop]["cold"]
    adjusted = [d["minutes"] - int(age / 60) for d in deps]
    times = [m for m in adjusted if m >= 1][:2]
    return times, cold


def load_font(name):
    f = graphics.Font()
    f.LoadFont(os.path.join(FONTS_DIR, name))
    return f


def fill_rounded_square(canvas, x0, y0, size, radius, color):
    rr = radius * radius
    left = radius - 0.5
    right = size - radius - 0.5
    top = radius - 0.5
    bot = size - radius - 0.5
    for dy in range(size):
        cy = top if dy < radius else (bot if dy >= size - radius else dy)
        for dx in range(size):
            cx = left if dx < radius else (right if dx >= size - radius else dx)
            ex = dx - cx
            ey = dy - cy
            if ex * ex + ey * ey < rr:
                canvas.SetPixel(x0 + dx, y0 + dy,
                                color.red, color.green, color.blue)


def rgb(r, g, b):
    return graphics.Color(r, g, b)


WHITE = rgb(255, 255, 255)
GREY = rgb(100, 100, 100)
DIM = rgb(40, 40, 40)
LABEL = rgb(200, 200, 200)
YELLOW = rgb(255, 200, 0)
AMBER = rgb(140, 120, 0)
RED = rgb(255, 80, 80)


def text_width(font, s):
    return sum(font.CharacterWidth(ord(c)) for c in s)


def draw_text_top(canvas, font, x, top_y, color, text):
    graphics.DrawText(canvas, font, x, top_y + font.baseline, color, text)


def draw_text_centered(canvas, font, cx, cy, color, text):
    w = text_width(font, text)
    top = cy - font.height // 2
    graphics.DrawText(canvas, font, cx - w // 2, top + font.baseline, color, text)


# Per-label nudges to compensate for BDF glyph ink asymmetry.
BADGE_LABEL_OFFSET = {"L": (-1, 0)}


def pick_badge_font(fonts, label):
    offset = BADGE_LABEL_OFFSET.get(label, (0, 0))
    if len(label) <= 1:
        return fonts["badge_1"], offset
    if len(label) == 2:
        return fonts["badge_2"], offset
    return fonts["badge_3"], offset


def render(canvas, page, fonts):
    canvas.Clear()
    title_font = fonts["title"]
    dir_font = fonts["dir"]
    row_font = fonts["row"]

    draw_text_top(canvas, title_font, 2, 1, GREY, page["title"])
    label = page["dir"]
    if label:
        draw_text_top(canvas, dir_font, 63 - text_width(dir_font, label), 2,
                      LABEL, label)
    graphics.DrawLine(canvas, 0, 10, 63, 10, DIM)

    row_data = [(row, *row_state(row)) for row in page["rows"]]
    row_data.sort(key=lambda rd: (not rd[1], rd[1][0] if rd[1] else 0))
    row_data = row_data[:3]
    any_times = any(times for _, times, _ in row_data)
    any_cold = any(cold for _, _, cold in row_data)

    if not any_times:
        msg = "Loading..." if any_cold else page.get("empty", "No Arrivals")
        color = LABEL if any_cold else RED
        draw_text_centered(canvas, row_font, 32, 38, color, msg)
        return

    for i, (row, times, _) in enumerate(row_data):
        y_top = 13 + i * 17
        badge_size = 16
        x0 = 2
        y0 = y_top
        cx = x0 + badge_size // 2
        cy = y0 + badge_size // 2
        fill_rounded_square(canvas, x0, y0, badge_size, 8,
                            rgb(*row["color"]))
        badge_font, (dx, dy) = pick_badge_font(fonts, row["label"])
        draw_text_centered(canvas, badge_font, cx + dx, cy + dy, WHITE, row["label"])

        if times:
            x = 20
            if len(times) == 1:
                draw_text_top(canvas, row_font, x, y_top + 5, YELLOW, str(times[0]))
                x += text_width(row_font, str(times[0]))
            else:
                first = f"{times[0]},"
                second = str(times[1])
                draw_text_top(canvas, row_font, x, y_top + 5, YELLOW, first)
                x += text_width(row_font, first) + 1
                draw_text_top(canvas, row_font, x, y_top + 5, YELLOW, second)
                x += text_width(row_font, second)
            draw_text_top(canvas, row_font, x + 2, y_top + 5, AMBER, "min")
        else:
            draw_text_top(canvas, row_font, 20, y_top + 5, DIM, "--")


def draw_icon(canvas, pixels, x0, y0, color):
    for y, row in enumerate(pixels):
        for x, on in enumerate(row):
            if on:
                canvas.SetPixel(x0 + x, y0 + y, color.red, color.green, color.blue)


ICON_COLOR = rgb(180, 200, 230)


def render_weather(canvas, fonts, icons):
    canvas.Clear()
    title_font = fonts["title"]
    row_font = fonts["row"]
    small_font = fonts["dir"]

    with weather_lock:
        data = weather_cache["data"]
        day_index = weather_cache["day_index"]

    when = datetime.now() + timedelta(days=day_index or 0)
    header = when.strftime("%b %d").upper()
    draw_text_centered(canvas, title_font, 32, 6, GREY, header)
    graphics.DrawLine(canvas, 0, 11, 63, 11, DIM)

    if data is None:
        draw_text_centered(canvas, row_font, 32, 38, LABEL, "Loading...")
        return

    icon_name = weather.CODE_ICON.get(data["code"], "cloud")
    word = weather.CODE_WORD.get(data["code"], "")
    _, _, pixels = icons.get(icon_name, icons["cloud"])
    draw_icon(canvas, pixels, 6, 17, ICON_COLOR)

    # Right column: hi / lo / precip
    rx = 41
    draw_text_top(canvas, title_font, rx, 17, YELLOW, f"{data['hi']}\u00b0")
    draw_text_top(canvas, row_font, rx, 29, LABEL, f"{data['lo']}\u00b0")
    draw_text_top(canvas, row_font, rx, 39, rgb(120, 170, 220),
                  f"{data['precip']}%")

    # Condition word centered below icon area
    draw_text_centered(canvas, row_font, 32, 55, LABEL, word.upper())


WASHER_COLOR = rgb(80, 160, 220)
DRYER_COLOR = rgb(220, 130, 60)


def fill_rect(canvas, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            canvas.SetPixel(x, y, color.red, color.green, color.blue)


def draw_progress_bar(canvas, x0, y0, x1, y1, frac, color):
    fill_rect(canvas, x0, y0, x1, y1, DIM)
    if frac <= 0:
        return
    frac = min(frac, 1.0)
    fill_x = x0 + int(round((x1 - x0) * frac))
    if fill_x >= x0:
        fill_rect(canvas, x0, y0, fill_x, y1, color)


def _laundry_metrics(state):
    now = time.time()
    started = state.get("started_at")
    avg = state.get("avg_min")
    elapsed_s = max(now - started, 0) if started else 0
    if avg:
        remaining_s = max(avg * 60 - elapsed_s, 0)
        remaining_m = int(remaining_s // 60)
        frac = min(elapsed_s / (avg * 60), 1.0)
    else:
        remaining_m = None
        frac = 0.0
    return remaining_m, frac


DONE_GREEN = rgb(60, 220, 90)


def _tint(color, factor):
    factor = max(0.0, min(1.0, factor))
    return rgb(int(color.red * factor),
               int(color.green * factor),
               int(color.blue * factor))


def _draw_check(canvas, x, y, color):
    # Compact 7x5 checkmark
    pts = [(0,2),(1,3),(2,4),(3,3),(4,2),(5,1),(6,0)]
    for dx, dy in pts:
        canvas.SetPixel(x + dx, y + dy, color.red, color.green, color.blue)
        canvas.SetPixel(x + dx, y + dy + 1, color.red, color.green, color.blue)


def _draw_drum_spin(canvas, icon_x, icon_y, color, phase):
    # Brightening-arc spinner inside the drum. The arc lags behind the
    # leading edge with a falloff, so it reads as motion clearly.
    cx, cy = icon_x + 16, icon_y + 19
    arc_len = 2.4   # radians of trailing arc
    r_min, r_max = 1.5, 4.0
    rmax_int = int(math.ceil(r_max))
    for dy in range(-rmax_int, rmax_int + 1):
        for dx in range(-rmax_int, rmax_int + 1):
            d = math.hypot(dx, dy)
            if d < r_min or d > r_max:
                continue
            ang = math.atan2(dy, dx)
            # Trailing arc: angular distance from phase, only behind it
            back = (phase - ang) % (2 * math.pi)
            if back > arc_len:
                continue
            falloff = 1.0 - (back / arc_len)
            factor = 0.15 + 0.85 * falloff
            c = _tint(color, factor)
            canvas.SetPixel(cx + dx, cy + dy, c.red, c.green, c.blue)


def _draw_done_state(canvas, fonts, icons, title, icon_name, accent):
    title_font = fonts["title"]
    big_font = fonts["badge_1"]   # 9x15B

    # Smooth breathing factor 0.55..1.0, period ~3s
    breath = 0.78 + 0.22 * math.sin(time.time() * 2.0)

    draw_text_centered(canvas, title_font, 32, 6, GREY, title)
    graphics.DrawLine(canvas, 0, 11, 63, 11, DIM)

    # Icon at full color (motion stopped — reads as finished).
    pixels = icons[icon_name][2]
    draw_icon(canvas, pixels, 16, 14, accent)

    # "DONE" + green check, breathing white text.
    text = "DONE"
    tw = text_width(big_font, text)
    cw_w = 7
    gap = 3
    total = tw + gap + cw_w
    x0 = 32 - total // 2
    draw_text_top(canvas, big_font, x0, 49, _tint(WHITE, breath), text)
    _draw_check(canvas, x0 + tw + gap, 52, _tint(DONE_GREEN, breath))


def _draw_single_appliance(canvas, fonts, icons, title, icon_name, accent, state):
    title_font = fonts["title"]
    big_font = fonts["badge_1"]    # 9x15B
    row_font = fonts["row"]         # 5x7
    small_font = fonts["dir"]       # 4x6

    remaining_m, frac = _laundry_metrics(state)

    if remaining_m is not None and frac >= 1.0:
        _draw_done_state(canvas, fonts, icons, title, icon_name, accent)
        return

    draw_text_centered(canvas, title_font, 32, 6, GREY, title)
    graphics.DrawLine(canvas, 0, 11, 63, 11, DIM)

    pixels = icons[icon_name][2]
    draw_icon(canvas, pixels, 2, 14, accent)
    _draw_drum_spin(canvas, 2, 14, accent, time.time() * 4.0)

    rx = 38
    if remaining_m is not None:
        draw_text_top(canvas, big_font, rx, 14, YELLOW, str(remaining_m))
        draw_text_top(canvas, small_font, rx, 30, GREY, "MIN")
        draw_text_top(canvas, small_font, rx, 37, GREY, "LEFT")
        pct = int(round(frac * 100))
        draw_text_centered(canvas, row_font, 32, 51, AMBER, f"{pct}%")
        draw_progress_bar(canvas, 2, 58, 61, 60, frac, accent)
    else:
        draw_text_top(canvas, big_font, rx, 18, YELLOW, "ON")
        draw_text_centered(canvas, row_font, 32, 55, LABEL, "RUNNING")


# Both-running mode: rotate full-screen between washer and dryer.
LAUNDRY_ROTATE_SECONDS = 30


def render_laundry(canvas, fonts, icons):
    canvas.Clear()
    with laundry_lock:
        data = laundry_cache["data"]
    if not data:
        draw_text_centered(canvas, fonts["row"], 32, 32, LABEL, "Loading...")
        return

    washer_on = data["washer"]["on"]
    dryer_on = data["dryer"]["on"]

    if washer_on and dryer_on:
        which = int(time.time() // LAUNDRY_ROTATE_SECONDS) % 2
        if which == 0:
            _draw_single_appliance(canvas, fonts, icons, "WASHER",
                                    "washer", WASHER_COLOR, data["washer"])
        else:
            _draw_single_appliance(canvas, fonts, icons, "DRYER",
                                    "dryer", DRYER_COLOR, data["dryer"])
    elif washer_on:
        _draw_single_appliance(canvas, fonts, icons, "WASHER",
                                "washer", WASHER_COLOR, data["washer"])
    elif dryer_on:
        _draw_single_appliance(canvas, fonts, icons, "DRYER",
                                "dryer", DRYER_COLOR, data["dryer"])


def is_night():
    if os.environ.get("FORCE_WEATHER") == "1":
        return True
    now = datetime.now()
    m = now.hour * 60 + now.minute
    return m >= NIGHT_START_MIN or m < NIGHT_END_MIN


def is_dim():
    now = datetime.now()
    m = now.hour * 60 + now.minute
    return m >= DIM_START_MIN or m < DIM_END_MIN


BOOT_GRACE_SECONDS = 90


def boot_grace():
    # Single-core Pi pegs out under matrix refresh, blocking SSH. On a
    # fresh boot, hold the matrix off for BOOT_GRACE_SECONDS so SSH stays
    # responsive long enough to log in if needed.
    try:
        with open("/proc/uptime") as f:
            uptime_s = float(f.read().split()[0])
    except OSError as e:
        log(f"boot grace: could not read uptime ({e}); skipping")
        return
    delay = BOOT_GRACE_SECONDS - uptime_s
    if delay > 0:
        log(f"boot grace: sleeping {delay:.0f}s before matrix init")
        time.sleep(delay)
    else:
        log(f"boot grace: uptime {uptime_s:.0f}s already past grace, no sleep")


def main():
    boot_grace()
    font_9x15b = load_font("9x15B.bdf")
    font_6x10 = load_font("6x10.bdf")
    font_5x7 = load_font("5x7.bdf")
    font_4x6 = load_font("4x6.bdf")
    fonts = {
        "title":   font_6x10,
        "dir":     font_4x6,
        "row":     font_5x7,
        "badge_1": font_9x15b,
        "badge_2": font_6x10,
        "badge_3": font_4x6,
    }

    options = RGBMatrixOptions()
    options.rows = 64
    options.cols = 64
    options.chain_length = 1
    options.parallel = 1
    options.pwm_dither_bits = 1
    options.limit_refresh_rate_hz = 30
    options.hardware_mapping = "regular"
    matrix = RGBMatrix(options=options)

    icons = weather.load_icons()
    log(f"loaded {len(icons)} weather icons")
    icons["washer"] = weather.load_xbm(os.path.join(
        os.path.dirname(__file__), "icons", "washer.xbm"))
    icons["dryer"] = weather.load_xbm(os.path.join(
        os.path.dirname(__file__), "icons", "dryer.xbm"))

    threading.Thread(target=fetcher_loop, daemon=True).start()

    canvas = matrix.CreateFrameCanvas()
    log("matrix ready, entering loop")
    while True:
        matrix.brightness = DIM_BRIGHTNESS if is_dim() else FULL_BRIGHTNESS
        if laundry_active():
            render_laundry(canvas, fonts, icons)
            canvas = matrix.SwapOnVSync(canvas)
            time.sleep(0.2)  # 5fps — retro feel + lighter on the Pi Zero
        elif is_night():
            render_weather(canvas, fonts, icons)
            canvas = matrix.SwapOnVSync(canvas)
            time.sleep(30)
        else:
            page = PAGES[0]
            render(canvas, page, fonts)
            canvas = matrix.SwapOnVSync(canvas)
            time.sleep(page["duration"])


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
