# Muni Display

64x64 RGB LED matrix on a Raspberry Pi Zero W. Native C++.

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

That produces `./muni-display`, which the systemd unit runs.

## Install service

```
sudo cp startup/munidisplay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now munidisplay
```

## Env

Required: `MUNI_API_KEY` (511.org). Optional: `HA_URL` + `HA_TOKEN` to enable
the laundry screen (washer/dryer state from Home Assistant).

`/etc/munidisplay.env`:

```
MUNI_API_KEY=...
HA_URL=http://homeassistant.local:8123
HA_TOKEN=...
```

## Laundry mode (optional)

If `HA_URL` and `HA_TOKEN` are set, the display shows a dedicated washer/dryer
screen whenever either appliance is running. Required HA entities:

- `binary_sensor.washer_running`, `binary_sensor.dryer_running`
- `sensor.washer_avg_run_minutes`, `sensor.dryer_avg_run_minutes` (optional;
  used for the progress bar and time-remaining)
