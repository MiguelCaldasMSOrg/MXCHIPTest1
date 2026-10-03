# MXCHIPTest1

Arduino test sketch for the Microsoft Azure IoT DevKit / MXCHIP AZ3166.

## Project layout

- [MXCHIPTest1.ino](MXCHIPTest1.ino): board startup, button dispatch, RGB test,
  LED blink, and uptime.
- [src/AppConfig.h](src/AppConfig.h): the single audio/RF-transmit mode setting.
- [src/AudioTests.cpp](src/AudioTests.cpp): bounded waveform, melody, and speech
  playback, including codec/DMA error handling.
- [src/RadioBridge.cpp](src/RadioBridge.cpp): Grove pin ownership, USB serial
  input, RF transmission/reception, and the scrolling display.
- [src/VirtualWireProtocol.h](src/VirtualWireProtocol.h),
  [src/rf_receiver.h](src/rf_receiver.h), and
  [src/rf_transmitter.h](src/rf_transmitter.h): hardware-independent packet
  coding, decoding, and bounded serial-line buffering.
- [src/generated/audio_test_sample.h](src/generated/audio_test_sample.h):
  pre-generated waveform tables and speech PCM.
- [tests/](tests/): protocol and application regression tests.

## Operating modes

Set `MXCHIP_ENABLE_AUDIO_TESTS` in [src/AppConfig.h](src/AppConfig.h):

| Value | Audio | RF transmission | RF reception |
| --- | --- | --- | --- |
| `0` (default) | Suspended, code retained | Enabled on P2 | Enabled on P0 |
| `1` | Enabled | Disabled; the RF driver does not configure P2 | Enabled on P0 |

The RF-transmit setting is derived from the audio setting, so they cannot both
be enabled. Other values are rejected at compile time. Disconnect the RF
transmitter before using audio mode because the physical P2 signal is then
used by the audio codec's I2C bus.

## Installed support

- Arduino CLI 1.5.1
- MXChip - Microsoft Azure IoT Developer Kit core 2.0.0
- Windows ST-Link debug driver 2.2.0.0 from STSW-LINK009 2.0.2
- Board: `AZ3166:stm32f4:MXCHIP_AZ3166`
- Serial/upload port: `COM3`

The board core includes the device libraries required for the AZ3166:
Audio, Azure IoT, filesystem, MQTT, sensors, SPI, WebSocket, Wi-Fi, and Wire.
The sketch uses the core-provided OLED display, built-in LED, button input, and
RGB LED driver from the bundled Sensors library. Audio output uses the bundled
AudioV2 library. No additional Library Manager packages are required.

## Windows ST-Link driver

Arduino CLI uploads use the board's ST-Link debug interface. The serial port
and `AZ3166` USB drive can work even when this interface has no driver, resulting
in Device Manager error Code 28 and OpenOCD `LIBUSB_ERROR_NOT_FOUND` errors.

An unchanged copy of the signed
[STSW-LINK009 2.0.2 ZIP](drivers/stsw-link009.zip) is stored in `drivers/` as a
personal offline backup for later use, not as a separately maintained driver
distribution. The original package is available from
[STMicroelectronics](https://www.st.com/en/development-tools/stsw-link009.html).
The archive is 5,329,298 bytes, with SHA-256:

```text
df015c7760f974e9da0f4c5a098d62c72157ea45cd0e80694eb47ab7ef28352b
```

The archive and its bundled components retain their ST and third-party license
terms; the project's Unlicense does not apply to them. A text copy of the
[applicable ST software license](drivers/STSW-LINK009-LICENSE.txt) accompanies
the archive; the [official license PDF](https://www.st.com/resource/en/license/SLA0048_STSW-LINK009.pdf)
is also available from ST.

From the project root, extract the archive into the ignored build directory,
then run `pnputil` in an administrator PowerShell window to install only the
debug-interface driver:

```powershell
Expand-Archive .\drivers\stsw-link009.zip -DestinationPath .\build\stlink-driver
pnputil /add-driver .\build\stlink-driver\stlink_dbg_winusb.inf /install
```

The debug interface (`USB\VID_0483&PID_374B&MI_00`) should then use `WinUSB`
without a Device Manager warning. Leave the working USB serial and mass-storage
drivers unchanged. Reconnect the board if Windows does not refresh its status.

The archive contains three driver packages, all version 2.2.0.0 dated
2021-04-01:

| INF | Purpose | Relevance to this project |
| --- | --- | --- |
| `stlink_dbg_winusb.inf` | ST-Link debug interface | Required for OpenOCD uploads. |
| `stlink_VCP.inf` | ST-Link virtual COM port | Alternative device identification/binding using Windows' existing `usbser.sys`, not a newer serial driver binary. |
| `stlink_bridge_winusb.inf` | Selected STLINK-V3 bridge interfaces | Does not match the AZ3166's ST-Link/V2-1 interfaces. |

The bundled WDF and WinUSB co-installers are legacy Windows 7-era setup helpers,
not updates for the frameworks built into current Windows versions. Do not run
the full-package installer merely to upgrade already-working interfaces.

## Arduino CLI

The local upload defaults are stored in `sketch.yaml`. The build script mirrors
the release workflow: it explicitly targets the MXCHIP AZ3166 and compiles a
fresh staged copy of the sketch and complete `src/` tree under
`_build/MXCHIPTest1`, without the local upload-port metadata. A board does not
need to be connected to compile. The build rejects source changes made during
compilation, so rebuild if it reports that the source snapshot changed.

```powershell
.\tools\build.ps1
arduino-cli upload --input-dir build .
arduino-cli monitor -p COM3 -c baudrate=115200
```

Different boards can have different COM ports. With one board connected, use
`arduino-cli board list` to identify its port and override the stored default
with `--port COM8`, for example, when uploading or monitoring.

The build script also refreshes `build/compile_commands.json`, which the
workspace uses to configure C/C++ IntelliSense for the MXCHIP core and libraries.
It adds entries for the original sketch and C/C++ source files, rather than
only their generated build copies. The `.ino` entry injects `Arduino.h` and
selects C++, matching the sketch's compilation environment.

## Arduino IDE

Open `MXCHIPTest1.ino` in Arduino IDE. Select **MXCHIP AZ3166** as the board and
**COM3** as the port if the IDE does not load the defaults automatically.

After upload, the onboard LED blinks, the OLED shows the uptime, and the serial
monitor reports the uptime at 115200 baud.

## Button A: RGB LED test

Press and release **Button A** to advance the RGB LED through red, green, blue,
yellow, cyan, magenta, and white. Each color is shown at intensities **32, 128,
and 255** out of 255 before advancing to the next color. The sequence repeats
after 21 presses, and the RGB LED starts off at boot.

The OLED shows the current RGB values, and the serial monitor reports each
change and its intensity. Button input is debounced; holding A advances only
once. The existing LED blink and uptime updates continue during the test.

## Button B: audio output test

Audio is **suspended in the default Grove RF configuration**: the audio codec
uses P1/P2 for I2C, and RF transmission uses P2. All audio code and samples are
retained. To restore audio, disconnect the RF transmitter and set
`MXCHIP_ENABLE_AUDIO_TESTS` to `1` in [src/AppConfig.h](src/AppConfig.h) before rebuilding. That setting
disables RF transmission; RF reception on P0 remains available.

Connect headphones or a powered speaker to the **3.5 mm headphone jack**. The
NAU88C10 codec is mono; this is not a stereo-channel-separation test. Start with
the external volume low, and leave headphones off your ears for the first test.

Each press and release of **Button B** advances through:

| Test | Duration |
| --- | --- |
| 440 Hz sine wave | 1 second |
| 440 Hz triangle wave | 1 second |
| 440 Hz softened square wave | 1 second |
| Original four-note melody | 1.5 seconds |
| Synthesized "Audio test." speech | About 1.35 seconds |

Each test is repeated at codec volume settings **25, 50, and 75** out of 100
before advancing. PCM peaks are limited to roughly 9% of full scale, with short
fade-ins and fade-outs. Playback is muted afterward and has a two-second timeout.
Holding B does not repeat a clip, and presses during playback are ignored rather
than queued. Button A, the LED blink, and uptime updates remain active.

The OLED and serial monitor identify the test and volume. Serial messages report
completion time or initialization/transfer errors. Listening through an attached
output device is still required to confirm the analog output actually sounds
correct.

Waveform tables and speech PCM are pre-generated in
[src/generated/audio_test_sample.h](src/generated/audio_test_sample.h), so normal builds and GitHub Actions
need no speech service or synthesizer. To regenerate them on Windows with the
Microsoft David Desktop voice installed:

```powershell
powershell.exe -NoProfile -File .\tools\generate-audio-samples.ps1
```

The generator writes speech to a temporary WAV file without playing it on the
computer, validates the PCM format/duration, and normalizes its peak amplitude.

## Grove 433 MHz serial-to-radio loopback

Use standard Grove cables with the adapter's bottom connectors:

| Module | Grove connector | DATA signal |
| --- | --- | --- |
| Receiver | **P0/P14** | P0 = `PB_0` |
| Transmitter | **P2/P16** | P2 = `PB_7` |

The kit uses the first signal wire in each connector; its second signal is not
connected. The same cables carry power and common ground. P1/P15 is not an
audio-compatible alternative: P1 and P2 are the audio codec's I2C clock/data
pins. The USB console uses separate pins and remains available.

Keep the adapter at **3.3 V** for direct GPIO connections with standard cables.
Seeed specifies a nominal **5 V receiver supply**, so reception at 3.3 V is not
guaranteed and may be less reliable. Do not switch the adapter to 5 V without
appropriate 3.3 V DATA-level interfacing. Check power, common ground, cable
orientation, antennas, and module separation if no valid packets arrive.

Both directions use **VirtualWire at 2,000 bit/s**, compatible with
[Seeed's example](https://wiki.seeedstudio.com/Grove-433MHz_Simple_RF_Link_Kit/).
Raw UART data and RadioHead's addressed message format are not the configured
text protocol.

Open the USB serial monitor at **115200 baud** and send a line such as `hello`,
terminated by LF, CR, or CRLF. Each nonempty line becomes one RF packet; newline
characters are not transmitted. Printable ASCII and tabs are accepted, up to
**77 bytes per line**. Empty, overlong, or invalid lines are reported explicitly.
There is a four-message pending queue; excess lines are rejected with a serial
error rather than silently truncated. Leave time for transmission when sending
many lines (a maximum-size packet takes about half a second).

Transmission is timer-driven with a 100 ms quiet interval between packets, and
the receiver stays active throughout. `RF transmitting:` and `RF TX complete`
report the send attempt only. **Only a real, CRC-valid packet sampled at P0**
produces `RF received:` and updates the OLED; serial input is never directly
echoed to the RF display as a substitute for reception.

The [decoder](src/rf_receiver.h) and [transmitter](src/rf_transmitter.h) share the core's Mbed sampling timer,
without an AVR VirtualWire library or Timer1 dependency. It checks the preamble,
4-to-6-bit symbols, message length, and CRC-16/X-25 before accepting a packet.
The receiver supports up to **77 payload bytes**; older transmitter library
versions may impose a smaller limit.

The latest string appears on the OLED's top row with an `RF:` prefix. Text
longer than 12 characters scrolls, and repeated copies of the same message do
not restart the scrolling. The audio-status, RGB, and uptime rows remain available.
The full display string is also printed over serial. The OLED uses its ASCII
font: whitespace controls become spaces, unsupported bytes become `?`, and a
NUL terminates the text.

Serial RF statistics report valid/rejected packets, dropped unread messages,
DATA transitions, and completed transmissions every five seconds. Noise on this type of receiver is
normal when no transmitter is active; transitions alone do not indicate a
valid message.

The host tests require a `g++` compiler and can be
run on Windows with:

```powershell
.\tools\test-rf.ps1
```

They run in both operating modes and cover framing, CRC, payload bounds,
sampling phase, timing variation, noise, exact transmitted bits, serial line
endings, queue overflow, display scrolling, and the absence of local echo.
Application tests also verify that audio mode never constructs or drives the
RF output, and exercise all 15 audio/volume combinations, PCM amplitude limits,
busy handling, automatic mute, the two-second cutoff, and failure paths.

Firmware packaging has separate regression tests:

```powershell
.\tools\test-package-firmware.ps1
```

These verify the reference checksum, vector-table checks, exact flash-size
boundary, and unchanged bootloader/application bytes. All test suites run in
GitHub Actions, which also compiles both firmware configurations before
publishing the default RF-mode release.

## Firmware images

Local builds place both images in `build/`, and GitHub releases publish both:

| Image | Contents | Upload method |
| --- | --- | --- |
| `MXCHIPTest1.ino.bin` | Application only, linked at `0x0800C000`. | Normal Arduino CLI/OpenOCD upload. |
| `MXCHIPTest1.full.bin` | Factory 2.0.0 bootloader and padding, followed by the current application. | Copy to the `AZ3166` USB drive, or program explicitly at `0x08000000`. |

The shared [packaging script](tools/package-firmware.ps1) verifies the reference
firmware's SHA-256, retains its exact first 48 KiB through offset `0xC000`, and
appends the compiled application. It also checks the application vector table
and the combined image's flash size. The application-only binary is not modified.

The complete image is useful for drag-and-drop flashing or bootloader recovery.
It runs this project's sketch, not the default DevKit demonstration application.
Normal application updates do not need to rewrite an intact bootloader.
Do not use the full image with the standard application-offset upload recipe.

## Default firmware

The repository includes the MXCHIP AZ3166 default firmware image at
`firmware/devkit-firmware-2.0.0.bin`. It is kept separately from generated build
outputs so it can be used to restore the board to its version 2.0.0 firmware.

SHA-256:

```text
33a01378a40484f306777de1e11462918ca9901d1039d2e97ab55d60cc684300
```

To restore it, connect the board over USB and copy the binary to the root of the
`AZ3166` mass-storage device. The board flashes the image and restarts
automatically. Do not disconnect the board while it is flashing.

This reference image includes the bootloader. The generated
`build/MXCHIPTest1.ino.bin` is application-only and is linked for flash address
`0x0800C000`; upload it with Arduino CLI/OpenOCD rather than copying it directly
to the USB drive.

## Releases

The GitHub Actions release workflow accepts semantic versions in `vX.Y.Z`
format, including prerelease suffixes such as `v1.0.0-rc.1`. Start a release by
pushing a version tag:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

Alternatively, run the **Release** workflow manually and provide the version.
Select **Build without creating a tag or release** to validate the release build
without publishing it. The workflow compiles the sketch with MXCHIP board core
2.0.0, creates the tag for non-dry manual runs, and publishes the generated
application-only and bootloader-inclusive firmware images, along with the ELF
and map files, in a GitHub release. Only those four named artifacts are
published; build caches and compilation databases are not release assets.
