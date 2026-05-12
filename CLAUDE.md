# Muni Display

## Deploy to Pi

SSH config alias: `muni-display` (host: `muni-display.local`, user: `user`, password: `pipi`)

Use `sshpass` to avoid interactive password prompts:

```bash
# Stop (takes a while — be patient, it blocks until the display loop exits)
sshpass -p pipi ssh muni-display "sudo systemctl stop munidisplay"

# Pull & build
sshpass -p pipi ssh muni-display "cd ~/Muni-Display && git pull && make"

# Start
sshpass -p pipi ssh muni-display "sudo systemctl start munidisplay"
```

If there are build errors, fix them locally, commit, push, then re-pull and build on the Pi.

The build system is a plain `Makefile` (no cmake). The binary is `~/Muni-Display/muni-display`.
GCC notes about nlohmann/json ABI in the build output are harmless — only actual `error:` lines matter.
