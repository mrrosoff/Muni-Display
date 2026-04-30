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

## Legacy

The previous Python implementation lives under `legacy/` for reference while
features are ported to C++.

## Env

Optional `/etc/munidisplay.env`:

```
MUNI_API_KEY=...
HA_URL=http://homeassistant.local:8123
HA_TOKEN=...
```
