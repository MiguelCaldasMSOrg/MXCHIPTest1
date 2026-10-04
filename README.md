# MXCHIPTest1

Arduino test sketch for the Microsoft Azure IoT DevKit / MXCHIP AZ3166.

## Project layout

- [MXCHIPTest1.ino](MXCHIPTest1.ino): board startup, button dispatch, RGB test,
  LED blink, and uptime.
- [src/AppConfig.h](src/AppConfig.h): the single ASK/audio/LoRa mode setting.
- [src/AudioTests.cpp](src/AudioTests.cpp): bounded waveform, melody, and speech
  playback, including codec/DMA error handling.
- [src/RadioBridge.cpp](src/RadioBridge.cpp): Grove pin ownership, USB serial
  input, and ASK RF transmission/reception.
- [src/LoRaBridge.cpp](src/LoRaBridge.cpp), [src/LoRaE5.h](src/LoRaE5.h), and
  [src/SoftwareUart.h](src/SoftwareUart.h): the two Grove LoRa-E5 UART links,
  bounded AT command handling, and point-to-point LoRa loopback.
- [src/SerialRadioInput.h](src/SerialRadioInput.h) and
  [src/RadioDisplay.cpp](src/RadioDisplay.cpp): shared serial-line buffering and
  received-message display/scrolling.
- [src/VirtualWireProtocol.h](src/VirtualWireProtocol.h),
  [src/rf_receiver.h](src/rf_receiver.h), and
  [src/rf_transmitter.h](src/rf_transmitter.h): hardware-independent packet
  coding, decoding, and bounded serial-line buffering.
- [src/generated/audio_test_sample.h](src/generated/audio_test_sample.h):
  pre-generated waveform tables and speech PCM.
- [tests/](tests/): protocol and application regression tests.
- [docs/](docs/README.md): complete offline documentation mirrors, source links,
  and upstream licensing notes.

## Code formatting

[.clang-format](.clang-format) defines the project C++ style: two-space
indentation, same-line opening braces, compact empty bodies, and colon
separators with a space only after the colon (excluding `?:` and `::`).
Function signatures and calls stay on one line up to 300 characters; longer
parameter/argument lists put each item on its own line. These rules apply only
to C++ (including Arduino sketches), not to YAML, JSON, PowerShell, or other
languages. In particular, the 300-character rule does not apply to YAML.

Use clang-format 23 or newer, such as the version bundled with the current
VS Code C/C++ extension, via **Format Document**. C++ files in the workspace use
spaces with a tab width of two; other languages keep their existing settings.
Generated numeric tables have a
[data-specific formatting configuration](src/generated/.clang-format) to keep
their initializer rows compact. Do not reformat the vendored documentation
mirrors or third-party packages.

## Operating modes

Set `MXCHIP_TEST_MODE` in [src/AppConfig.h](src/AppConfig.h):

| Value | Audio | 433 MHz ASK RF | Grove LoRa-E5 |
| --- | --- | --- | --- |
| `0` | Suspended, code retained | TX on P2, RX on P0 | Inactive |
| `1` | Enabled | RX on P0 only; RF TX does not configure P2 | Inactive |
| `2` (default) | Suspended, code retained | Inactive; no ASK GPIO/timer initialization | Two UART-controlled modules on P0/P14 and P2/P16 |

The active paths are derived from this one setting; conflicting or invalid
settings fail at compile time. All audio and ASK code and samples remain in the
project. Existing `MXCHIP_ENABLE_AUDIO_TESTS=0/1` compiler overrides still select
the old ASK/audio modes when no new mode override is supplied.

**Power off before changing Grove modules.** LoRa-E5 drives Grove pin 1 as its
UART output, whereas the ASK transmitter expects that same wire to be driven
by the DevKit. Disconnect the E5 modules before uploading ASK or audio firmware,
and disconnect the ASK transmitter before using audio mode. To switch from ASK
to LoRa, flash LoRa mode with both module cables unplugged, then power off,
connect the E5 modules, and power back on.

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

Audio is **suspended in either radio-transmit mode** because the Grove signals
overlap the codec's I2C/I2S pins. All audio code and samples are retained. To
restore audio, disconnect the radio modules and set `MXCHIP_TEST_MODE` to `1`
in [src/AppConfig.h](src/AppConfig.h) before rebuilding. ASK reception on P0
remains available with the appropriate receiver; LoRa is inactive.

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

Select `MXCHIP_TEST_MODE=0` for the original ASK RF kit.

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

## Two Grove LoRa-E5 modules

Select `MXCHIP_TEST_MODE=2` (the current default). This uses the modules'
**factory AT firmware** in direct LoRa TEST mode, not LoRaWAN. No gateway,
join credentials, module reflashing, or LoRaWAN service is required.

Use standard Grove cables, the adapter's **3.3 V** setting, and two appropriate
**868 MHz antennas**, fitted before sending. Keep the same bottom connectors:

| Transceiver | Grove connector | Module TX to DevKit input | DevKit output to module RX |
| --- | --- | --- | --- |
| **A** | **P0/P14** | Pin 1 (yellow): P0 / `PB_0` | Pin 2 (white): P14 / `PB_14` |
| **B** | **P2/P16** | Pin 1 (yellow): P2 / `PB_7` | Pin 2 (white): P16 / `PC_6` |

Both modules transmit and receive during every successful test. The red/black
wires provide power/common ground. Unlike the ASK kit, the E5 uses both signal
wires. These connector pairs are not two usable hardware UARTs, so independent
timer-driven **9600-baud, 8N1** UARTs handle commands and replies without blocking
USB serial, Button A, or the uptime display. P1/P15 would not provide a hardware
UART pair either; the I2C connector remains free.

At startup, each module must acknowledge AT communication, TEST mode, stopping
any previous test, and the matching RF configuration. Both then enter
continuous receive mode. Before each transmission, that module stops receiving;
after `TX DONE`, it returns to receive mode. An echo is sent only after the
original sender acknowledges that it is listening again.

The configured **EU868** test profile is **868.1 MHz, SF7, 125 kHz bandwidth,
10 dBm, CRC on, normal IQ, private network**, with TX/RX preambles of 12/15.
Use it only where that frequency and power are permitted. At this profile,
the largest packet is 85 bytes (77 user bytes plus an 8-byte test header) and
has less than 160 ms airtime. **Each radio independently waits at least
16 seconds** after startup and after its own transmit completion before sending
again, conservatively pacing this bench test to a 1% duty cycle per transmitter.
The two hops of one round trip do not need a 16-second gap between them because
they use different transmitters; subsequent queued tests wait for both radios.
Do not bypass this interval; it is not the LoRaWAN stack's duty-cycle control.

Open the USB serial monitor at **115200 baud**. Send 1-77 printable ASCII bytes
(tabs are allowed) followed by CR, LF, or CRLF. The shared four-message queue
handles bursts; invalid or excess lines are reported rather than truncated.
Payload bytes are hex-encoded into `AT+TEST=TXLRPKT` so quotes and backslashes
cannot become AT commands.

Each line starts an automatically correlated round trip:

1. The initiating module transmits a request containing the text.
2. The other module receives and validates the request, then echoes the bytes
   actually received over RF.
3. The initiating module receives the echo, and its transaction ID, direction,
   length, and payload must match.

The initial direction is **B -> A -> B**. The next line uses **A -> B -> A**,
alternating on each new test. Success requires one completed transmission and
one matching reception from **each** transceiver, followed by both returning
to listening. `TX DONE` alone never counts as successful reception.

Only matched RX payloads produce `LoRa A received:` / `LoRa B received:` and
update the OLED's `RF:` row. `LoRa loopback OK` appears on the status row only
after the full round trip succeeds. Long strings continue scrolling, Button A
still cycles RGB colors, and Button B reports that audio is suspended.

[LoRaFrame.h](src/LoRaFrame.h) defines the test framing: `MX` magic, version 1,
request/reply type, a little-endian 32-bit transaction ID, then the user bytes.
IDs distinguish active tests from stale/duplicate packets within a run; this
is test correlation, not authentication. Unsolicited packets and ordinary
unframed LoRa strings are ignored rather than echoed, preventing echo loops.

A lost forward packet or return echo fails the test after a 12-second
transaction timeout. Altered payloads fail immediately. There is no automatic
RF retransmission; later queued tests may proceed when both radios are ready
and their duty-cycle guards permit.

AT timeouts, unexpected configuration, UART framing/buffer errors, and malformed
receive data are reported explicitly and stop further transmissions. No blind
retries or local display echoes are used. Correct the power/wiring/firmware
issue and reset the DevKit to retry. The modules' existing factory firmware and
LoRaWAN credentials are not erased. Statistics report **A-TX, A-RX, B-TX,
B-RX**, passed/failed round trips, ignored frames, unread-packet drops, and
both UART error counts. RX counters count matched test frames, not unrelated
traffic.

References: [Seeed's Grove v1.0 schematic](https://files.seeedstudio.com/products/113020091/Grove%20-%20LoRa%20-E5%20v1.0.pdf),
[P2P example](https://wiki.seeedstudio.com/Grove_Wio_E5_P2P/), and
[AT command specification](https://files.seeedstudio.com/products/317990687/res/LoRa-E5%20AT%20Command%20Specification_V1.0%20.pdf).

## Tests

The host tests require a `g++` compiler and can be
run on Windows with:

```powershell
.\tools\test-rf.ps1
```

They run in all three operating modes and cover framing, CRC, payload bounds,
sampling phase, timing variation, noise, exact transmitted bits, serial line
endings, queue overflow, display scrolling, and the absence of local echo.
Application tests also verify that audio mode never constructs or drives the
RF output, and exercise all 15 audio/volume combinations, PCM amplitude limits,
busy handling, automatic mute, the two-second cutoff, and failure paths.
LoRa tests cover 8N1 sampling/timing, both module UARTs, verified AT settings,
request/reply framing, half-duplex handover, both-hop loss, stale replies,
payload mismatch, maximum-length round trips, independent per-radio pacing,
and inactive-mode GPIO exclusion.

Firmware packaging has separate regression tests:

```powershell
.\tools\test-package-firmware.ps1
```

These verify the reference checksum, vector-table checks, exact flash-size
boundary, and unchanged bootloader/application bytes. All test suites run in
GitHub Actions, which also compiles all three firmware configurations before
publishing the default LoRa-mode release.

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
