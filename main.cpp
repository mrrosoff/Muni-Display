#include "led-matrix.h"
#include "graphics.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

using rgb_matrix::Color;
using rgb_matrix::DrawText;
using rgb_matrix::FrameCanvas;
using rgb_matrix::Font;
using rgb_matrix::RGBMatrix;

static volatile bool interrupted = false;
static void on_signal(int) { interrupted = true; }

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    RGBMatrix::Options options;
    options.rows = 64;
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;
    options.hardware_mapping = "regular";
    options.pwm_dither_bits = 1;
    options.limit_refresh_rate_hz = 30;
    options.brightness = 100;

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = 1;
    runtime.drop_privileges = 1;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, runtime);
    if (!matrix) {
        fprintf(stderr, "Could not create matrix.\n");
        return 1;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    FrameCanvas *canvas = matrix->CreateFrameCanvas();

    Font font;
    const char *font_path = "fonts/6x10.bdf";
    if (!font.LoadFont(font_path)) {
        fprintf(stderr, "Couldn't load font %s\n", font_path);
        delete matrix;
        return 1;
    }

    const Color title(180, 200, 230);
    const Color accent(255, 220, 80);

    int frame = 0;
    while (!interrupted) {
        canvas->Clear();

        for (int x = 0; x < 64; ++x) {
            float t = (frame + x) * 0.05f;
            uint8_t r = (uint8_t)(127 + 127 * sinf(t));
            uint8_t g = (uint8_t)(127 + 127 * sinf(t + 2.094f));
            uint8_t b = (uint8_t)(127 + 127 * sinf(t + 4.188f));
            for (int y = 0; y < 6; ++y) {
                canvas->SetPixel(x, y, r, g, b);
            }
        }

        DrawText(canvas, font, 6, 24, title, "MUNI");
        DrawText(canvas, font, 6, 36, accent, "C++");
        DrawText(canvas, font, 6, 48, title, "OK");

        canvas = matrix->SwapOnVSync(canvas);
        usleep(33000);
        frame++;
    }

    matrix->Clear();
    delete matrix;
    return 0;
}
