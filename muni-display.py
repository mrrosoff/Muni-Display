#!/usr/bin/env python

import json
import os
import sys
import threading
import time
from datetime import datetime, timezone

from rgbmatrix import RGBMatrix, RGBMatrixOptions, graphics
import requests
from requests import RequestException

API_KEY = os.environ["MUNI_API_KEY"]
AGENCY = "SF"
API_URL = "https://api.511.org/transit/StopMonitoring"
CA_BUNDLE = "/etc/ssl/certs/ca-certificates.crt"

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


def log(msg):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}", flush=True)


def fetch_stop(stop_code, lines):
    params = {"api_key": API_KEY, "agency": AGENCY,
              "stopCode": stop_code, "format": "json"}
    headers = {"Connection": "close"}
    t0 = time.monotonic()
    log(f"fetch {stop_code}: start")
    with requests.get(API_URL, params=params, headers=headers,
                      timeout=(CONNECT_TIMEOUT, READ_TIMEOUT),
                      verify=CA_BUNDLE, stream=True) as r:
        r.raise_for_status()
        raw = r.raw.read(decode_content=True)
    data = json.loads(raw.decode("utf-8-sig"))
    log(f"fetch {stop_code}: ok in {time.monotonic() - t0:.2f}s")

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


def fetcher_loop():
    while True:
        now = time.time()
        with cache_lock:
            stale = [(code, now - cache[code]["last_fetch"] - STOPS[code]["ttl"])
                     for code in STOPS]
        stale.sort(key=lambda x: -x[1])
        code, overdue = stale[0]
        if overdue < 0:
            time.sleep(min(-overdue, 5))
            continue
        refresh_stop(code)
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


def fill_circle(canvas, cx, cy, r, color):
    rr = r * r
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy < rr:
                canvas.SetPixel(cx + dx, cy + dy, color.red, color.green, color.blue)


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


def pick_badge_font(fonts, label):
    if len(label) <= 1:
        return fonts["badge_1"]
    if len(label) == 2:
        return fonts["badge_2"]
    return fonts["badge_3"]


def render(canvas, page, fonts):
    canvas.Clear()
    title_font = fonts["title"]
    dir_font = fonts["dir"]
    row_font = fonts["row"]

    draw_text_top(canvas, title_font, 1, 1, GREY, page["title"])
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
        cx, cy, r = 8, y_top + 8, 8
        fill_circle(canvas, cx, cy, r, rgb(*row["color"]))
        badge_font = pick_badge_font(fonts, row["label"])
        draw_text_centered(canvas, badge_font, cx, cy, WHITE, row["label"])

        if times:
            if len(times) == 1:
                nums = str(times[0])
            else:
                nums = f"{times[0]},{times[1]}"
            draw_text_top(canvas, row_font, 18, y_top + 5, YELLOW, nums)
            draw_text_top(canvas, row_font,
                          18 + text_width(row_font, nums) + 2,
                          y_top + 5, AMBER, "min")
        else:
            draw_text_top(canvas, row_font, 18, y_top + 5, DIM, "--")


def main():
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
    options.limit_refresh_rate_hz = 120
    options.hardware_mapping = "regular"
    matrix = RGBMatrix(options=options)

    threading.Thread(target=fetcher_loop, daemon=True).start()

    canvas = matrix.CreateFrameCanvas()
    page_index = 0
    log("matrix ready, entering loop")
    while True:
        page = PAGES[page_index]
        render(canvas, page, fonts)
        canvas = matrix.SwapOnVSync(canvas)
        log(f"render {page['title']} {page['dir']}")

        time.sleep(page["duration"])
        page_index = (page_index + 1) % len(PAGES)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
