#include "core/colors.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "data/caches.hpp"
#include "data/fetchers.hpp"
#include "data/weather_codes.hpp"
#include "net/http.hpp"
#include "render/fonts.hpp"
#include "render/render.hpp"
#include "util/time_utils.hpp"
#include "util/xbm.hpp"

#include "led-matrix.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace std;
using logx::log;

namespace {

atomic<bool> g_interrupted{false};

void wait_for(chrono::milliseconds d, bool on_change = false) {
    unique_lock lk(caches::mtx);
    const auto seen = caches::version;
    caches::changed.wait_for(lk, d, [&] {
        return g_interrupted.load() || (on_change && caches::version != seen);
    });
}

thread start_signal_thread(const sigset_t &sigs) {
    return thread([sigs] {
        int sig = 0;
        sigwait(&sigs, &sig);
        g_interrupted.store(true);
        http::abort_all();
        caches::notify_changed();
    });
}

thread start_fetcher(bool (*tick)()) {
    return thread([tick] {
        while (!g_interrupted.load()) {
            wait_for(tick() ? chrono::milliseconds{500} : chrono::seconds{2});
        }
    });
}

void load_env() {
    if (const char *u = getenv("HA_URL")) caches::ha_url = u;
    if (const char *t = getenv("HA_TOKEN")) caches::ha_token = t;
    caches::ha_enabled = !caches::ha_url.empty() && !caches::ha_token.empty();
    log("home assistant: ", caches::ha_enabled ? "enabled" : "disabled");
}

map<string, XbmIcon> load_all_icons() {
    map<string, XbmIcon> icons;
    for (const auto &[code, name] : CODE_ICON) {
        if (icons.count(name)) continue;
        XbmIcon ic;
        const string path = "icons/" + name + ".xbm";
        if (load_xbm(path, &ic))
            icons[name] = move(ic);
        else
            log("icon FAILED: ", path);
    }
    log("loaded ", icons.size(), " weather icons");

    if (caches::ha_enabled) {
        for (const auto *name : {"washer", "dryer"}) {
            XbmIcon ic;
            const string path = string("icons/") + name + ".xbm";
            if (load_xbm(path, &ic))
                icons[name] = move(ic);
            else
                log("icon FAILED: ", path);
        }
    }
    return icons;
}

unique_ptr<rgb_matrix::RGBMatrix> create_matrix() {
    rgb_matrix::RGBMatrix::Options options;
    options.rows = 64;
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;
    options.hardware_mapping = "regular";
    options.brightness = cfg::FULL_BRIGHTNESS;
    // Tuned on the Pi Zero for a steady, flicker-free panel (measured):
    options.pwm_lsb_nanoseconds = 70;     // base bit-time; low value keeps raw refresh well above the 120 cap
    options.limit_refresh_rate_hz = 120;  // cap below the raw ceiling -> uniform frames, no swap-induced dips
    options.disable_busy_waiting = true;  // nanosleep: keep the single core free (busy-wait spiked SSH/fetch latency to ~70s)

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = 1;            // library default; slowdown=0 caused panel ghosting on the Pi Zero

    return unique_ptr<rgb_matrix::RGBMatrix>(
        rgb_matrix::RGBMatrix::CreateFromOptions(options, runtime)
    );
}

}  // namespace

int main() {
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGINT);
    pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

    http::global_init();
    load_env();

    Fonts fonts;
    if (!fonts.load()) {
        cerr << "font load failed\n";
        return 1;
    }

    auto icons = load_all_icons();

    auto matrix = create_matrix();
    if (!matrix) {
        cerr << "matrix init failed\n";
        return 1;
    }
    auto signal_thread = start_signal_thread(sigs);

    auto *canvas = matrix->CreateFrameCanvas();
    log("matrix ready, entering loop");

    auto stop_fetcher = start_fetcher(fetch::tick_stops);
    auto fetcher = start_fetcher(fetch::tick);

    auto last_heartbeat = chrono::steady_clock::now();
    unsigned long frames = 0;

    while (!g_interrupted.load()) {
        matrix->SetBrightness(tu::is_dim() ? cfg::DIM_BRIGHTNESS : cfg::FULL_BRIGHTNESS);
        const bool laundry = fetch::laundry_active();
        const bool night = tu::is_night();
        if (laundry)
            render::laundry(canvas, fonts, icons);
        else if (night)
            render::weather(canvas, fonts, icons);
        else
            render::muni(canvas, fonts);

        canvas = matrix->SwapOnVSync(canvas);
        ++frames;

        const auto now = chrono::steady_clock::now();
        if (now - last_heartbeat >= chrono::minutes{5}) {
            log("render alive: ", frames, " frames in last 5min");
            frames = 0;
            last_heartbeat = now;
        }

        if (laundry)
            this_thread::sleep_for(chrono::milliseconds{33});
        else
            wait_for(chrono::seconds{10}, true);
    }

    stop_fetcher.join();
    fetcher.join();
    signal_thread.join();
    matrix->Clear();
    matrix.reset();
    http::global_cleanup();
    return 0;
}
