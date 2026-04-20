# Pico Switch Controller Bring-Up

This folder contains a standalone Raspberry Pi Pico SDK project for a Raspberry Pi Pico 2 W (`PICO_BOARD=pico2_w`).

The current firmware goal is simple bring-up:

- enumerate over USB as a HID game controller
- present itself using a HORIPAD S-style wired-controller profile
- run a SOF-disciplined left-stick timing test that alternates `UP` and `RIGHT`

This is intended as the first milestone before we add API-driven control later.

## Project Layout

- `CMakeLists.txt` - Pico SDK build definition
- `pico_sdk_import.cmake` - official Pico SDK import helper
- `src/main.c` - TinyUSB main loop and scripted input generator
- `src/usb_descriptors.c` - USB device, HID report, config, and string descriptors
- `src/usb_descriptors.h` - controller report definitions
- `src/tusb_config.h` - TinyUSB device configuration

## What The Firmware Does

After the board enumerates on USB, it waits about 2 seconds and then repeatedly sends:

1. A short `A` button press to activate the controller on the Switch
2. A repeating left-stick sweep inside an assumed `60 Hz` game frame
3. `UP` for the first half of the frame
4. `RIGHT` for the second half of the frame
5. The next frame restarts at `UP`

The timing is disciplined by USB start-of-frame ticks instead of the Pico's free-running clock alone. The firmware treats `50 ms` of USB time as exactly `3` game frames, and uses the Pico microsecond timer only to interpolate between adjacent SOFs so the logical `UP/RIGHT` midpoint stays stable instead of dithering on whole-millisecond boundaries.

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

Once the board is recognized, it will first send a short `A` press to help the Switch activate the wired controller, then it will begin the SOF-disciplined `60 Hz` frame timing pattern on the left analogue stick. A game that samples only once per frame should consistently prefer `UP`, while a faster sampler may observe both `UP` and `RIGHT` from the same frame.

## Notes

- This firmware is USB-only. The Pico 2 W radio is not used yet.
- The current USB identity is a HORIPAD S-style compatibility profile used only for Switch bring-up testing. Replace it with your own VID/PID before any real distribution.
- When we move to API control, the main change will be replacing the scripted pattern generator in `src/main.c` with a command-driven state machine.
- The timing test uses TinyUSB's `tud_sof_cb()` callback as the long-term clock reference and a `1 ms` HID endpoint interval so the host can observe per-millisecond state changes.
- `docker-build.sh` is the intended repeatable build entrypoint for this folder.
