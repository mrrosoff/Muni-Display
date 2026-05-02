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
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace std;
using logx::log;

namespace {

atomic<bool> g_interrupted{false};

void on_signal(int) {
    g_interrupted.store(true);
}

void boot_grace() {
    ifstream f("/proc/uptime");
    if (!f) return;
    double up = 0;
    f >> up;
    const double delay = cfg::BOOT_GRACE.count() - up;
    if (delay > 0) {
        log("boot grace: sleeping ", static_cast<int>(delay), "s before matrix init");
        this_thread::sleep_for(chrono::duration<double>(delay));
    } else {
        log("boot grace: uptime ", static_cast<int>(up), "s already past, no sleep");
    }
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
    options.pwm_dither_bits = 1;

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = 1;

    return unique_ptr<rgb_matrix::RGBMatrix>(
        rgb_matrix::RGBMatrix::CreateFromOptions(options, runtime)
    );
}

}  // namespace

int main() {
    boot_grace();
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
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    auto *canvas = matrix->CreateFrameCanvas();
    log("matrix ready, entering loop");

    // Fetcher runs on its own thread so HTTP latency doesn't freeze rendering.
    thread fetcher([] {
        while (!g_interrupted.load()) {
            const bool ran = fetch::tick();
            this_thread::sleep_for(ran ? chrono::milliseconds{500} : chrono::seconds{2});
        }
    });

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

        const auto wait = (laundry || night) ? chrono::duration<double>{0.2}
                                             : chrono::duration<double>{10.0};
        const auto deadline = chrono::steady_clock::now() + wait;
        while (!g_interrupted.load() && chrono::steady_clock::now() < deadline) {
            this_thread::sleep_for(chrono::milliseconds{50});
        }
    }

    fetcher.join();
    matrix->Clear();
    matrix.reset();
    http::global_cleanup();
    return 0;
}
