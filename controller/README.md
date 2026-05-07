# Pico Switch Controller Bring-Up

This folder contains a standalone Raspberry Pi Pico SDK project for a Raspberry Pi Pico 2 W (`PICO_BOARD=pico2_w`).

The current firmware goal is simple bring-up:

- enumerate over USB as a HID game controller
- present itself using a HORIPAD S-style wired-controller profile
- run a SOF-disciplined drawing automation and cursor-speed calibration sequence

This is intended as the first milestone before we add API-driven control later.

## Project Layout

- `CMakeLists.txt` - Pico SDK build definition
- `pico_sdk_import.cmake` - official Pico SDK import helper
- `src/main.c` - TinyUSB main loop and scripted input generator
- `src/usb_descriptors.c` - USB device, HID report, config, and string descriptors
- `src/usb_descriptors.h` - controller report definitions
- `src/tusb_config.h` - TinyUSB device configuration

## What The Firmware Does

After the board enumerates on USB, it waits about 2 seconds and then runs this scripted sequence:

1. Sends 5 long `A` presses to force controller selection
2. Sends 3 long `B` presses
3. Waits 30 frames
4. Runs `SET BRUSH`
5. Runs `CLEAR`
6. Runs `ZOOM OUT`
7. Runs `HOME`
8. Runs the top-left outline dot probe

`HOME` now includes an 11-frame neutral canvas-settle window after returning to `PEN`. The flip test showed the first measured analog movement after HOME was the one losing the calibrated cleanup distance, independent of axis, so this pause lets the game finish committing the stamp/PEN canvas transition before measured movement begins.

For this reduced test build, `SET BRUSH` is a single explicit sequence:

1. Press `X`, then wait
2. Wait again for the tool menu to settle
3. Press `RIGHT` enough times to guarantee the cursor reaches the far-right `QUIT` slot
4. Press `LEFT` enough times to land on `PEN`
5. Wait, then press `X` to open the pen submenu
6. Press `LEFT`, wait, press `LEFT`, wait
7. Press `A`, wait, press `A`, then wait so the submenu closes cleanly

For this reduced test build, `CLEAR` is also a single explicit sequence:

1. Press `X`, then wait
2. Wait again for the tool menu to settle
3. Press `RIGHT` enough times to guarantee the cursor reaches the far-right `QUIT` slot
4. Press `LEFT` enough times to land on `ERASER`
5. Wait, then press `X` to open the eraser submenu
6. Press `UP`, wait, press `UP`, wait
7. Press `DOWN`, wait, press `DOWN`, wait, press `DOWN`, wait
8. Press `A`, then wait

The timing is disciplined by USB start-of-frame ticks instead of the Pico's free-running clock alone. The firmware uses the Pico microsecond timer to interpolate between adjacent SOFs, and the frame index is now derived directly from that disciplined timeline at `60 Hz`, so every scripted button/direction hold uses the same exact frame basis as the integrity test.

After the first tool-menu interaction, the firmware now remembers which tool is currently selected. The first tool-focused action still homes to the far-right end of the menu so the starting position is known, but later tool-focused actions move relative to the remembered tool instead of re-running the full reset each time.

The calibration pass is now a top-left outline dot probe. After the setup sequence finishes, the firmware homes before every target, moves to exactly one pixel coordinate, taps one dot, then repeats for the next target.

The drawing game has two coordinate systems, and the firmware treats them separately:

- Pixel coordinates are integer cells. The four far corner pixels are `(-128,-128)`, `(128,-128)`, `(128,128)`, and `(-128,128)`.
- Cursor coordinates are decimal boundaries/positions. Cursor `(0,0)` is the exact center boundary, and boundary hits favor the positive pixel direction.
- A drawn mark at cursor `(0,0)` therefore lands in pixel `(1,1)`. If the cursor then moves 5 units right and marks again, the endpoint is pixel `(6,1)`; that 6-pixel span is a cell-boundary artifact, not 6 units of cursor travel.
- To avoid ambiguous boundary placement, pixel targets are converted to cursor-cell centers before moving. For example, pixel `128` maps to cursor `127.5`, and pixel `-128` maps to cursor `-127.5`.
- The script tracks cursor position in fixed-point units, so fractional cursor position is preserved and used by later moves instead of being trimmed away.

Menu navigation still uses the D-pad. During the top-left outline probe, canvas travel from home to the target pixel now uses analog-only cardinal movement:

- first move the left stick horizontally with calibrated analog-only free movement
- wait the same neutral settle window that the final axis receives before marking
- then move the left stick vertically with calibrated analog-only free movement
- each axis uses max-speed frames for the whole-frame travel, then one calibrated analog remainder pulse for the final decimal cursor distance

The top-left outline probe now draws 257 dots:

1. Left edge: `(-128,1)`, then `(-128,-1)` through `(-128,-128)`.
2. Top edge: `(-127,-128)` through `(-1,-128)`, then `(1,-128)`.

Pixel coordinate `0` is intentionally skipped because this game has no pixel `(0,0)`. Pixel targets are converted to cursor-cell centers before tapping the dot, so the firmware avoids relying on ambiguous pixel boundaries.

Canvas travel from home to each dot is analog-only. This sanity-check probe intentionally separates horizontal and vertical components so we can compare against the diagonal planner.

The diagonal point-to-point movement planner is still available but inactive for this probe. It tracks the cursor in fixed-point decimal coordinates, emits one full-speed analog frame at a time, models the rounded HID stick vector that was actually sent, updates the internal cursor estimate, and then re-aims the next frame from the remaining error. The diagonal planner treats stick power as non-linear: it chooses raw stick axis values by inverting the measured speed curve, then models the game as clamping the resulting movement vector to the max cursor speed.

The movement planner now uses separate speed models for drawing and non-drawing cursor travel:

- Drawing movement with `A` held uses the measured `120 px/s` rate, or `2 px/frame`.
- Non-drawing cursor movement uses the measured `300 px/s` rate, or `5 px/frame`, for the fast stage.
- Non-drawing remainder movement is interpolated from `cursordata/Sheet 2-Table 1.csv`, with the marked pixel index adjusted back to cursor displacement first. For example, a mark at pixel `151` after starting on boundary `0` means `150` cursor units of movement, not `151`.
- Drawing cleanup magnitudes are interpolated from `cursordata/Sheet 1-Table 1.csv`, where full-strength `A`-held movement produced `241` painted pixels over `120` frames, i.e. `240` cursor units of displacement after removing the starting-cell mark.

Analog movement also accounts for two observed game/controller behaviors:

- Non-drawing movement no longer adds an extra held movement frame; explicit neutral waits after the move give the game time to apply the final input without adding another full-speed step.
- Drawing movement does not add that extra settle frame; it relies only on the measured drawing speed and a 1-frame `A` prehold so line length does not include a synthetic extra movement frame.
- Cleanup vectors use a minimum nonzero HID axis offset of `1`, matching the low-power calibration where values below that threshold produced no movement while the first nonzero axis step jumped from no painted movement to the minimum visible movement.

## Prerequisites

The preferred build flow uses Docker, so on the host machine you only need:

- Docker

The build container installs the Pico cross-toolchain and uses CMake to fetch the official `pico-sdk` automatically during the first configure.

## Build With Docker

From this `controller/` directory, run:

```bash
./docker-build.sh
```

That script:

1. builds the local Docker image from `Dockerfile`
2. configures the project for `PICO_BOARD=pico2_w`
3. builds the `pico_switch_controller` target

The output files will be created on the host in:

```text
build/
```

The firmware you want to flash is:

```text
build/pico_switch_controller.uf2
```

The first run takes longer because it downloads the Pico SDK and may build `picotool` as part of the SDK toolchain flow.

If you prefer the raw Docker commands instead of the helper script, run:

```bash
docker build -t pico-switch-controller-build .
docker run --rm -v "$PWD:/workspace" -w /workspace pico-switch-controller-build \
  cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
docker run --rm -v "$PWD:/workspace" -w /workspace pico-switch-controller-build \
  cmake --build build --target pico_switch_controller
```

## Flash To The Pico 2 W

BOOTSEL drag-and-drop is the assumed preferred flashing method.

### Preferred: BOOTSEL drag-and-drop

1. Hold the `BOOTSEL` button on the Pico 2 W.
2. Plug it into your computer over USB.
3. Release `BOOTSEL` after the `RPI-RP2` drive appears.
4. Copy `build/pico_switch_controller.uf2` onto that drive.
5. The board will reboot automatically.

### Optional: `picotool`

If you already have `picotool` installed:

```bash
picotool load -x build/pico_switch_controller.uf2
```

## Testing On A Nintendo Switch

- Use a data-capable USB cable.
- Connect the Pico through the Switch dock or another USB host path the console can use.
- If the console is not reacting, make sure wired controller support is enabled on the Switch.

Once the board is recognized, it will send the 5 startup `A` presses, 3 startup `B` presses, run `SET BRUSH`, `CLEAR`, `ZOOM OUT`, `HOME`, and then start the top-left outline dot probe.

## Notes

- This firmware is USB-only. The Pico 2 W radio is not used yet.
- The current USB identity is a HORIPAD S-style compatibility profile used only for Switch bring-up testing. Replace it with your own VID/PID before any real distribution.
- The full startup automation is active again: after the startup `A/B` presses it runs `SET BRUSH`, `CLEAR`, `ZOOM OUT`, `HOME`, and then the current top-left outline dot probe.
- `TOOL CHANGE` and `HOMING TOOL CHANGE` now include an extra settle wait after their first `X` press before any directional movement begins, and `SET BRUSH` uses the same pause before scrolling.
- The startup controller-selection sequence uses 5 `A` presses followed by 3 `B` presses before entering the scripted automation.
- Tool-focused steps now home only on the first menu interaction, then reuse the remembered selected-tool position for later relative navigation.
- The current calibration pass homes before every pixel, then taps dots along `(-128,1) -> (-128,-128) -> (1,-128)` while skipping pixel coordinate `0`. Pixel targets are converted to cursor-cell centers, and the current cursor coordinate is tracked in fixed-point form so decimal remainder is preserved across moves.
- `CLEAR` now forces the eraser submenu back to a known vertical state with `UP, UP, DOWN, DOWN, DOWN` before confirming.
- D-pad taps are still used for menus/tool selection; canvas travel from home to each dot uses analog movement.
- The long scripted automation is now generated on demand in small chunks instead of being fully precomputed up front, so the test no longer runs into the old fixed-step buffer overflow around the later rows.
- The timing test uses TinyUSB's `tud_sof_cb()` callback as the long-term clock reference and a `1 ms` HID endpoint interval so the host can observe per-millisecond state changes.
- The menu pacing is faster again: general menu waits are halved, and the extra stamp/shape confirmation pause is halved as well.
- The top-left outline dot probe uses horizontal-then-vertical analog left-stick movement for canvas travel; menu navigation still uses D-pad taps.
- Non-drawing analog positioning now uses the original two-stage path again: max-speed travel for whole frames, then one slower calibrated analog pulse for the remaining fixed-point distance. The current diagnostic adds 3 extra neutral frames after HOME to test whether post-zeroing settle time was the real issue.
- `docker-build.sh` is the intended repeatable build entrypoint for this folder.
