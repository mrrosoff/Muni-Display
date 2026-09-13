# Muni Display

A 64x64 RGB LED matrix that tells you when the next Muni train is coming, so you
can look up from the kitchen and know whether it is worth leaving now. It runs
on a Raspberry Pi Zero W in native C++, with no framework underneath it.

If you point it at Home Assistant it also doubles as a laundry timer, which is
the other thing worth knowing before you walk out of the room.

## Build (on the Pi)

The matrix library is expected at `/home/user/rpi-rgb-led-matrix`:

```
git clone https://github.com/hzeller/rpi-rgb-led-matrix /home/user/rpi-rgb-led-matrix
make -C /home/user/rpi-rgb-led-matrix/lib
```

Then in this repo:

```
make
```

That produces `./muni-display`, which is what the systemd unit runs.

## Install service

```
sudo cp startup/munidisplay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now munidisplay
```

## Env

You need a 511.org API key in `MUNI_API_KEY`. Setting `HA_URL` and `HA_TOKEN`
is optional and turns on the laundry screen, which reads washer and dryer state
from Home Assistant.

`/etc/munidisplay.env`:

```
MUNI_API_KEY=...
HA_URL=http://homeassistant.local:8123
HA_TOKEN=...
```

## Laundry mode (optional)

If `HA_URL` and `HA_TOKEN` are set, the display shows a dedicated washer/dryer
screen whenever either appliance is running. It expects these HA entities.

- `binary_sensor.washer_running`, `binary_sensor.dryer_running`
- `sensor.washer_avg_run_minutes`, `sensor.dryer_avg_run_minutes`, which are
  optional and feed the progress bar and the time remaining

## The CA bundle gotcha (read this if HTTPS is slow)

Debian's libcurl default sets `CURLOPT_CAINFO=/etc/ssl/certs/ca-certificates.crt`,
a single ~200 KB file with 130+ CA certs. Every fresh `curl_easy_init()`
re-parses the entire bundle to build a new `SSL_CTX`. On a Pi Zero ARMv6 with
no crypto acceleration, that's **~1.4 s of user CPU per HTTPS call**, which
made every stop fetch take 15-30 s instead of the ~1 s it should.

The fix is one line in `src/net/http.cpp`:

```cpp
curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/ssl/certs");
```

OpenSSL hash-lookups only the specific issuer it needs to verify a chain
from the hashed cert directory, instead of slurping the whole bundle. Drops
per-call CPU ~10×. Verification is still on. Confusingly, Debian's
`/usr/bin/curl` is fast out of the box, and it is only the libcurl C API that
defaults to the slow bundle path.

## Credits

Holiday header icons (under `icons/special/`) are derived from
[Twemoji](https://github.com/jdecked/twemoji) (Twitter/jdecked fork), licensed
under [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/). They are
rendered to 12×12 pixels by `tools/gen_icons.py`.
