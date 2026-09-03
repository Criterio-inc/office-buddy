# Office Buddy — notes for Claude Code

Read this before touching anything. It is short on purpose.

## What this is

A face on a Waveshare ESP32-S3-Touch-AMOLED-1.8 that follows a mood over
the day and tells its owner when something needs them. Three parts:

- `delat/` — shared by board and emulator. `ansikte.c` draws and animates,
  `humor.c` decides, `vyer.c` is the clock and timer, `protokoll.c` parses
  the wire protocol. **Nothing in here may know about ESP-IDF, SDL or macOS.**
- `firmware/` — the thin ESP-IDF layer. Panel, touch, IMU, codec, buttons,
  USB link, RTC. Hardware only.
- `sim/` — the SDL emulator, plus headless still and sequence rendering.
- `server/` — the Mac side: the link, the EventKit helper, installers, hooks.

## Language

Code, comments, protocol keywords and commit messages are in **Swedish**.
The primary README is English. Keep it that way; do not translate the code.

## Principles that decide behaviour

1. Nothing repeats exactly. Randomise intervals, amplitudes, order.
2. Expressions follow the inner state, never a single event directly.
3. Silence is a feature. When in doubt, say less and say it later.
4. Every line shown comes from something real (calendar, mail, tools).
5. **Orange means "something needs you now" and nothing else may use it.**
   A knock or a tap acknowledges and clears it.

## Working rules

- Change the face in `delat/`, then look at it: build the emulator and
  render stills with `--bild`, `--serie`, `--dag`, `--rad`; the contact
  sheet is `python3 verktyg/kontaktark.py out.png`. Motion must be seen.
- New behaviour goes into the mood (`humor.c`) as state plus a protocol
  line, not as a special case in the firmware.
- Everything that touches the panel (brightness, commands) runs under the
  LVGL lock; esp_lcd's panel IO is not thread safe.
- Keep the expander code in `panelstrom.c`: the BSP does not drive the
  panel's power and reset pins, and the screen goes black without it.
- LVGL fonts are generated with `--no-compress` and must contain every
  character printed (Swedish letters, ellipsis).
- No large structs on task stacks. No blocking in ESP-IDF event handlers.
- The link service holds the USB port: `launchctl unload` it before
  `idf.py flash`, `load` it after.
- Personal settings live in `server/buddy.json`, which is gitignored. The
  repo only carries `buddy.example.json`. Never commit real accounts,
  paths or keys.

## Build and run

```bash
cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && ./sim/build/office-buddy-sim
source ~/esp/esp-idf/export.sh && cd firmware && idf.py build && idf.py -p /dev/cu.usbmodem* flash
server/installera.sh          # the Mac link as a launchd agent
python3 server/claude-krokar.py   # Claude Code hooks
```

Verify on hardware by reading the boot log over the same USB port (any
serial reader at 115200) and the service log in `~/Library/Logs/office-buddy.log`.
