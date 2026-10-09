# MXCHIPTest1

Arduino multi-peripheral test sketch for the Microsoft Azure IoT DevKit /
MXCHIP AZ3166. The current default mode tests a Grove High Precision RTC v1.0
(PCF85063TP) on the I2C Grove socket. Sixteen independent modes cover Grove
peripherals, onboard sensors/audio, Wi-Fi, storage, infrared and read-only
security-chip diagnostics plus explicitly confirmed supplied-host-key setup.

**Updates are application-only.** Use Arduino CLI/OpenOCD at `0x0800C000`;
do not copy the release binary onto the `AZ3166` disk.
See [build/upload instructions](#arduino-cli), [operating modes](#operating-modes),
[test commands](#tests) and the [documentation index](docs/README.md).

## Project layout

- [MXCHIPTest1.ino](MXCHIPTest1.ino): board startup, mode/button dispatch, RGB
  tests, the RTC result LED, heartbeat, and non-RTC uptime.
- [src/RtcClock.cpp](src/RtcClock.cpp): validated date/time conversion,
  chip-specific register access through native I2C, and verified snapshot
  capture/restoration for both RTCs.
- [src/RtcTests.cpp](src/RtcTests.cpp): PCF85063TP/DS1307 feature suites, serial
  commands, test status, and the live OLED clock.
- [src/AppConfig.h](src/AppConfig.h): the single compile-time mode setting.
- [src/OnboardTests.cpp](src/OnboardTests.cpp): controls for the independent
  sensor, microphone, filesystem, network, IrDA and STSAFE diagnostics.
- [src/SensorTests.cpp](src/SensorTests.cpp): checked native-I2C sensor
  identities, configuration readback, calibrated readings and live changes.
- [src/MicrophoneTests.cpp](src/MicrophoneTests.cpp): bounded RAM capture,
  input statistics and explicitly requested, level-limited playback.
- [src/FileSystemTests.cpp](src/FileSystemTests.cpp): exclusive test-file
  creation, remounted verification, reboot retention and guarded cleanup.
- [src/NetworkTests.cpp](src/NetworkTests.cpp) and
  [src/NetworkTestConfig.h](src/NetworkTestConfig.h): public-endpoint
  DNS/TCP/HTTP/NTP/TLS checks with an explicit public CA trust anchor.
- [src/IrdaTests.cpp](src/IrdaTests.cpp) and
  [src/SecurityChipTests.cpp](src/SecurityChipTests.cpp): bounded IrDA
  transmission and non-destructive legacy HAL/full-middleware STSAFE tests.
- [libraries/STSELib/](libraries/STSELib/): all upstream STSELib runtime
  layers, pinned to v1.1.11, plus an AZ3166 native-I2C/mbedTLS platform port.
- [src/HostKeyBlock.h](src/HostKeyBlock.h),
  [src/SuppliedKeySetup.cpp](src/SuppliedKeySetup.cpp): supplied STSAFE host
  keys and SDK-format STM32 key loaders. Ordinary application access uses
  the core EEPROM implementation directly.
- [docs/LEGACY-PROVISIONING.md](docs/LEGACY-PROVISIONING.md): architecture,
  detailed supplied-key setup procedures and legacy limitations.
  Hardware personalization remains unvalidated.
- [src/AudioTests.cpp](src/AudioTests.cpp): bounded waveform, melody, and speech
  playback, including codec/DMA error handling.
- [src/RadioBridge.cpp](src/RadioBridge.cpp): Grove pin ownership, USB serial
  input, and ASK RF transmission/reception.
- [src/LoRaBridge.cpp](src/LoRaBridge.cpp), [src/LoRaE5.h](src/LoRaE5.h), and
  [src/SoftwareUart.h](src/SoftwareUart.h): the two Grove LoRa-E5 UART links,
  bounded AT command handling, and point-to-point LoRa loopback.
- [src/SerialLineInput.h](src/SerialLineInput.h): bounded, fixed-capacity
  serial-line buffering shared by radios and RTC commands.
  [src/SerialRadioInput.h](src/SerialRadioInput.h) retains the radios'
  77-byte/four-message configuration; RTC input uses one 20-byte command.
- [src/RadioDisplay.cpp](src/RadioDisplay.cpp): shared received-message
  display/scrolling.
- [src/VirtualWireProtocol.h](src/VirtualWireProtocol.h),
  [src/rf_receiver.h](src/rf_receiver.h), and
  [src/rf_transmitter.h](src/rf_transmitter.h): hardware-independent packet
  coding, decoding, and bounded serial-line buffering.
- [src/generated/audio_test_sample.h](src/generated/audio_test_sample.h):
  pre-generated waveform tables and speech PCM.
- [tests/](tests/): protocol and application regression tests.
- [docs/BOARD-MAINTENANCE.md](docs/BOARD-MAINTENANCE.md): confidential
  same-board image creation and reversible board-side ST-Link mass-storage controls.
- [docs/](docs/README.md): maintained project guides, historical offline
  documentation mirrors, source links and upstream licensing notes.

The hardware-independent radio protocols and existing audio/radio modules stay
separate. RTC register/calendar handling is isolated from the interactive test
harness, without dynamic allocation or a runtime driver framework. Shared
[serial input](src/SerialLineInput.h) clears consumed/rejected buffers using
[SensitiveMemory.h](src/SensitiveMemory.h); mode-specific code owns the
validation, hardware actions and error messages. One
compile-time mode selects the hardware path; changing a mode does not delete
the other implementations.

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

| Value | Mode | Active peripherals |
| --- | --- | --- |
| `0` | ASK radio | RF TX on P2, RX on P0; audio off. |
| `1` | Audio | Audio output and ASK RX on P0; RF TX does not claim P2. |
| `2` | LoRa | Two UART-controlled E5 modules on P0/P14 and P2/P16; ASK/audio off. |
| `3` (default) | High Precision RTC v1.0 | PCF85063TP at I2C `0x51`; radios/audio off. |
| `4` | RTC v1.2 | DS1307 at I2C `0x68`; radios/audio off. |
| `5` | Wi-Fi | Connect with credentials saved on the board; report SSID, IP, and RSSI. |
| `6` | Grove OLED | Exercise the Grove 1.12-inch OLED v2.0 (SH1107) at I2C `0x3C`. |
| `7` | Grove triple-color e-ink | Send a black/white/red test image over P0/P14 UART. |
| `8` | Wi-Fi provisioning | Save credentials received over USB serial to STSAFE EEPROM. |
| `9` | Onboard sensors | HTS221, LPS22HB, LSM6DSL and LIS2MDL on shared I2C; no Wi-Fi/audio/radios. |
| `10` | Microphone | One-second codec/I2S DMA capture into RAM; optional limited headphone playback. |
| `11` | Filesystem | Dedicated 4 KiB test file on the QSPI filesystem partition; no automatic formatting. |
| `12` | Network services | Saved Wi-Fi, DNS, TCP, HTTP, NTP and certificate-validated HTTPS; no Azure account. |
| `13` | IrDA transmitter | Explicitly requested 38400-baud SIR test bursts on the onboard emitter. |
| `14` | Security chip | STSELib metadata, binary echo, hardware RNG and public-vector ECDSA verification; no personalization. |
| `15` | Supplied STSAFE host keys | Install operator-supplied MAC/cipher keys and SDK-compatible host loaders after host-tool and Button A confirmation. |

The active paths are derived from this one setting; conflicting or invalid
settings fail at compile time. `MXCHIP_ENABLE_AUDIO_TESTS=0/1` selects ASK/audio
mode when `MXCHIP_TEST_MODE` is not defined.
The release workflow validates all sixteen configurations and builds its default
firmware from the same `MXCHIP_TEST_MODE` setting, without a separate mode override.

Wi-Fi mode uses the SSID and password already stored in the board's secure
EEPROM; credentials are not compiled into this project. It reports connection
failure explicitly on the onboard OLED and USB serial console. It checks live
connection status every five seconds, clearing stale IP/RSSI values on
disconnect and reporting a restored connection without forcing reassociation.
Press either button to report status immediately.

Wi-Fi credentials are stored in the board's STSAFE secure EEPROM, not STM32
application flash, and survive normal Arduino compilation and reflashing. They
can be replaced through either of these local-only paths:

- Hold Button A during reset to enter the board's built-in configuration
  console, then use `set_wifissid` and `set_wifipwd`.
- Run `.\tools\configure-wifi.ps1`; it prompts for the password securely and
  drives that same console without writing credentials to files or command-line
  arguments.
- Compile mode `8`, open USB serial at 115200 baud, type `PROVISION`, then send
  the SSID and password as separate lines. Type `-` instead of a password for
  an open network. Reflash another mode after provisioning.

Mode `8` and the script validate lengths, surface storage errors, and never
print the password. Erasing or replacing the STSAFE device is outside normal
application reflashing and can remove the stored settings.

Mode `8` accepts up to **32 SSID characters and 64 password characters**.
The unmodified SDK configuration console counts the terminating NUL in its
limits, so its host helper accepts **31/63 characters**; use mode `8` for the
full-length values. Both paths accept printable ASCII, with `-` selecting an
open network in mode `8` and an empty password in the console helper.
The SSID and password are separate SDK writes: a failure can leave partially
updated settings. Errors do not echo console responses or claim atomicity;
re-enter both values after a reported save failure.
The Wi-Fi console helper supports confirmation and `-WhatIf`, and reports
the failed stage without retrying an uncertain write. Credential and key
tools keep raw serial traffic out of logs, including with `-Verbose`.
Their green final **Verified** message is shown after serial cleanup.

Legacy host-key provisioning is a different operation from saving Wi-Fi
settings. Mode `15` plus [provision-stsafe.ps1](tools/provision-stsafe.ps1)
installs **only your supplied MAC/cipher keys** and legacy STM32 key loaders,
with typed confirmation and physical Button A authorization. A missing
chip-internal local-envelope key is generated. Encryption is bound to that STSAFE, even if
another board uses the same host keys.

Setup writes key material, not the STSAFE application-data zones or QSPI
filesystem.
**Existing plaintext is not preserved as usable encrypted data.** Once the
loaders activate the original encrypted path, legacy reads can fail and can
overwrite invalid envelopes with zero-filled replacements. STM32 RDP/PCROP
settings are unchanged. Use `ProvisionSupplied` for supplied keys; the
built-in SDK console supports only `enable_secure 1`, a different operation.
Read the [full provisioning guide](docs/LEGACY-PROVISIONING.md) first.

Grove OLED mode uses the shared I2C connector at 3.3 V and requires no additional
Arduino library. It detects the display before sending the SH1107 initialization
sequence, then cycles checkerboard, vertical-bar, horizontal-bar, and border
patterns every three seconds. These patterns exercise every page and pixel
column. Press either button to advance immediately.

The onboard OLED and the Grove OLED both use I2C address `0x3C`. In mode `6`,
the SH1107 initialization therefore also reaches the onboard display, leaving
its output upside down and horizontally mirrored; this is expected for this
test. All other modes never invoke the Grove driver and continue to
initialize and use the onboard display through the board's original `Screen`
API, preserving their existing orientation and behavior.

Grove e-ink mode uses the P0/P14 connector as a 230400-baud UART: the module's
TX signal connects to P0 (`PB_0`) and its RX signal connects to P14 (`PB_14`).
At startup it performs the module's `'a'`/`'b'` handshake and sends a 152x152
test image with black, white, and red horizontal bands. The transfer follows
Seeed's required 76-byte pacing. Because frequent refreshes can permanently
damage or ghost the panel, both buttons reject another refresh until 180
seconds have elapsed. Leave the Grove adapter at 3.3 V.

**Power off before changing Grove modules.** LoRa-E5 drives Grove pin 1 as its
UART output, whereas the ASK transmitter expects that same wire to be driven
by the DevKit. Disconnect the E5 modules before uploading ASK or audio firmware,
and disconnect the ASK transmitter before using audio mode. To switch from ASK
to LoRa, flash LoRa mode with both module cables unplugged, then power off,
connect the E5 modules, and power back on.

**For microphone mode `10`, disconnect Grove peripherals before powering on.**
The audio codec shares expansion pins, including P14 (`PB_14`, I2S input),
P16 (`PC_6`, master clock), and the P1/P2 codec-control bus. Onboard diagnostic
modes do not initialize the ASK/LoRa or Grove display drivers. The normal onboard
OLED initialization and orientation are retained.

### Grove High Precision RTC v1.0 power

The [Grove High Precision RTC](https://wiki.seeedstudio.com/Grove_High_Precision_RTC/)
supports 3.3 V operation; leave the adapter at **3.3 V** for the AZ3166.
Its CR1225 battery holder provides backup power for the clock and RAM when the
Grove supply is disconnected. The DS1307-specific voltage limitation below
does not apply to this PCF85063TP module.

### Grove RTC v1.2 power requirements

The [Grove RTC](https://wiki.seeedstudio.com/Grove-RTC/) uses the DS1307 and a
CR1225 backup cell. Without a battery, time and RAM retention cannot be expected
after power loss. Although Seeed's module page lists 3.3-5.5 V, the
[manufacturer's DS1307 datasheet](https://raw.githubusercontent.com/SeeedDocument/Grove-RTC/master/res/DS1307.pdf)
specifies **4.5-5.5 V VCC** and inhibits I2C below approximately 1.25 times the
battery voltage (about 3.75 V for a 3 V cell). The adapter's existing 3.3 V
setting is therefore not a guaranteed operating condition; a missing `0x68`
response can indicate insufficient supply voltage rather than a faulty RTC.

**Do not simply switch the adapter to 5 V.** The
[published Grove RTC schematic](https://raw.githubusercontent.com/SeeedDocument/Grove-RTC/master/res/Grove%20-%20RTC%20v1.1%20Sch.pdf)
has SDA/SCL pull-ups to VCC. A compliant 5 V supply requires suitable
bidirectional I2C level shifting so the AZ3166's shared OLED/sensor bus remains
at 3.3 V. The firmware does not change the adapter's supply voltage.
Both RTC modes use the MXCHIP-native shared I2C driver at 100 kHz, without
reinitializing the bus through `Wire`.

## Installed support

- Arduino CLI 1.5.1
- MXChip - Microsoft Azure IoT Developer Kit core 2.0.0
- Windows ST-Link debug driver 2.2.0.0 from STSW-LINK009 2.0.2
- Board: `AZ3166:stm32f4:MXCHIP_AZ3166`
- Saved local serial/upload default: `COM3` in [sketch.yaml](sketch.yaml);
  use `arduino-cli board list` to find the actual port.

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

## ST-Link firmware updater

An unchanged [STSW-LINK007 3.17.11 ZIP](drivers/stsw-link007.zip) is stored
alongside the USB driver as an offline vendor package. Its official source is
[STMicroelectronics](https://www.st.com/en/development-tools/stsw-link007.html).
It is **4,973,107 bytes**, with SHA-256:

```text
51b76fcbf6b417d03c7cbfc9f029a2d1f463bd0200ee8f3d80764d45d735ee1c
```

The archive retains all ST and third-party notices. The accompanying
[ST software license](drivers/STSW-LINK007-LICENSE.txt) applies to the ST
components; **the project's Unlicense does not apply to this package**.
ST's terms permit redistribution with the notices retained and restrict use
to ST hardware. The JAR's pinned ST signature, SHA-384 manifest/member
digests, and the Windows x64 driver's Authenticode signature are verified
before the updater is used.

The archive also includes LGPL-2.1 libusb libraries for macOS. Their matching,
unchanged source releases are provided alongside it, with their original
`COPYING` files and copyright notices inside:

| Library | Corresponding source | Upstream release | SHA-256 |
| --- | --- | --- | --- |
| libusb 1.0.23 (nano 11397) | [libusb-1.0.23.tar.bz2](drivers/libusb-1.0.23.tar.bz2) | [v1.0.23](https://github.com/libusb/libusb/releases/tag/v1.0.23) | `db11c06e958a82dac52cf3c65cb4dd2c3f339c8a988665110e0d24d19312ad8d` |
| libusb 1.0.27 (nano 11882) | [libusb-1.0.27.tar.bz2](drivers/libusb-1.0.27.tar.bz2) | [v1.0.27](https://github.com/libusb/libusb/releases/tag/v1.0.27) | `ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575` |

The Windows setup tool installs only the JAR and its x64 Windows driver:

```powershell
.\tools\stlink-mass-storage.ps1 -Action Setup `
  -VendorZip .\drivers\stsw-link007.zip -AcceptVendorLicense
```

Review the license before passing `-AcceptVendorLicense`. Setup validates
and prepares local files; it does not run the updater or modify the board.
See [board-side mass-storage control](docs/BOARD-MAINTENANCE.md#2-disablere-enable-mass-storage-on-the-board)
before requesting an actual firmware switch.

## Arduino CLI

The local upload defaults are stored in [sketch.yaml](sketch.yaml). The build script mirrors
the release workflow: it explicitly targets the MXCHIP AZ3166 and compiles a
fresh staged copy of the sketch, complete `src/` tree and project-local
`libraries/` under
`_build/MXCHIPTest1`, without the local upload-port metadata. A board does not
need to be connected to compile. The build rejects source changes made during
compilation, so rebuild if it reports that the source snapshot changed.
It also verifies the pinned STSELib source hashes before compiling.

```powershell
.\tools\build.ps1
arduino-cli board list
arduino-cli upload --port COM8 --input-dir build .
arduino-cli monitor -p COM8 -c baudrate=115200
```

`COM8` is an example; use the connected board's reported port. The explicit
`--port` overrides the saved local default.

To keep provisioning and normal application binaries separate without editing
the source default:

```powershell
.\tools\build.ps1 -Mode 15 -BuildDirectory _build\provisioning
.\tools\build.ps1 -Mode 9 -BuildDirectory _build\production
```

These commands compile only. Run them sequentially because the staged source
directory is shared. The build also verifies that the original core EEPROM
implementation is linked, with no local replacement/interception.
Tool discovery uses the effective Arduino CLI data directory, including its
platform default when no explicit directory setting exists.
For xPack 0.12.0-7, also apply the
[matching native ST-Link/SWD upload recipe](docs/BOARD-MAINTENANCE.md#arduino-uploads-with-xpack-0120-7).
Changing only the executable path leaves the core's `hla_swd` transport paired
with a now-native ST-Link script and prevents uploads.

For direct compilation, include the **project-local libraries** explicitly:

```powershell
arduino-cli compile --port COM8 --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --libraries .\libraries --build-property "compiler.cpp.extra_flags=-DMXCHIP_TEST_MODE=14" --build-path _build\stselib-mode-14 .
arduino-cli upload --port COM8 --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --input-dir _build\stselib-mode-14 .
```

The build script also refreshes `build/compile_commands.json`, which the
workspace uses to configure C/C++ IntelliSense for the MXCHIP core and libraries.
It adds entries for the original sketch and every C/C++ source file, rather
than only their generated build copies. The `.ino` entry injects `Arduino.h`
and selects C++, matching the sketch's compilation environment.

### Keeping local Arduino overrides across core updates

Arduino loads the board package's `platform.txt` as its base configuration,
then applies matching properties from `platform.local.txt` in the **same
installed core directory**. Keep the vendor base file unchanged and put all
three custom OpenOCD settings in the local override: the installation path,
executable and native SWD upload recipe. There is no need to split these settings
between the two files or comment out the vendor defaults.

A copy of the working override is kept at
[tools/platform.local.txt](tools/platform.local.txt), **outside the versioned
Arduino board-package directory**. Board Manager updates/reinstalls do not
replace this project copy. The installed override can be removed or left in
the old core version's directory, so it may need restoring after an update.
Arduino does not automatically load the copy under `tools/`.

The saved configuration targets **AZ3166 core 2.0.0 and xPack OpenOCD
0.12.0-7 on Windows**. On 2026-10-08 it successfully programmed, readback-verified
and booted mode 15 on the connected ST-Link/V2-1 running V2J28M17, with no
HLA or transport deprecation warnings. This did not provision keys or change
protection settings. Review compatibility before applying it to newer
versions, and adjust its xPack installation path if necessary. From the
repository root, restore it to the appropriate installed core directory:

```powershell
arduino-cli core list
$coreVersion = "2.0.0"  # Set to the installed version after reviewing compatibility.
$dataDirectory = arduino-cli config get directories.data --json | ConvertFrom-Json
$coreDirectory = Join-Path $dataDirectory "packages\AZ3166\hardware\stm32f4\$coreVersion"
if (-not (Test-Path -LiteralPath (Join-Path $coreDirectory "platform.txt"))) {
  throw "The selected AZ3166 core is not installed at $coreDirectory."
}
Copy-Item -LiteralPath .\tools\platform.local.txt `
  -Destination (Join-Path $coreDirectory "platform.local.txt") -Confirm
```

If the destination already contains other customizations, merge them rather
than replacing the file. Keep the project copy in sync when changing your
overrides. Restart an open Arduino IDE after restoring; Arduino CLI reads
the override on its next invocation. These are PC-side tooling settings,
**not a backup of firmware, board data or STSAFE keys**.

## Arduino IDE

The CLI build script is the reproducible path and does not modify your global
Arduino installation. To compile with Arduino IDE, install the bundled
[STSELib library folder](libraries/STSELib/) into your sketchbook's `libraries`
directory first. Use this pinned local port, not an unrelated Library Manager
package with the same name.

Open [MXCHIPTest1.ino](MXCHIPTest1.ino), select **MXCHIP AZ3166**, and select the
board's actual serial port. Change the mode in
[src/AppConfig.h](src/AppConfig.h) before compiling/uploading.

## Startup and controls

The USB console runs at **115200 baud** in every mode. The built-in LED is a
heartbeat, not a test result; it pauses while the RTC's blocking self-test runs.

| Modes | Startup / display | Button A | Button B | Serial input |
| --- | --- | --- | --- | --- |
| `0`, `1` | ASK reception on the top row, audio/RGB status and uptime below. | Cycle RGB colors/intensities. | Play the next audio clip in mode `1`; report audio suspended in mode `0`. | Queue ASK text, or reject TX in audio mode. |
| `2` | Initialize both LoRa modules; display received text, test status, RGB and uptime. | Cycle RGB colors/intensities. | Report audio suspended. | Queue LoRa round-trip text. |
| `3`, `4` | Detect RTC, initialize invalid/untrusted time to the compile timestamp, run the suite, then show date/time. | Rerun the RTC suite. | Set RTC to the compile timestamp. | RTC commands described below. |
| `5` | Connect to saved Wi-Fi and show IP/RSSI. | Report status now. | Report status now. | None. |
| `6` | Detect the external SH1107 and cycle full-panel patterns. | Next pattern. | Next pattern. | None. |
| `7` | Handshake with the e-ink module and send three color bands once. | Refresh after the 180-second guard. | Refresh after the 180-second guard. | None. |
| `8` | Wait for explicit USB-serial provisioning input. | Print a serial reminder. | Print a serial reminder. | `PROVISION`, SSID, then password on separate lines. |
| `9` | Check sensor identities/configuration, then sample every two seconds. | Next sensor page. | Sample now. | `a`, `b`, `h`/`?`. |
| `10` | Initialize the codec muted; do not record automatically. | Record one second. | Play the last completed recording. | `a`, `b`, `h`/`?`. |
| `11` | Create/verify only the dedicated test file. | Rerun without overwriting existing data. | Verify only. | `a`, `b`, `c` to clean up, `h`/`?`. |
| `12` | Run one public-endpoint network test. | Rerun. | Print help. | `a`, `b`, `h`/`?`. |
| `13` | Initialize IrDA; do not transmit automatically. | Send one known burst. | Print help. | `a`, `b`, `h`/`?`. |
| `14` | Run non-destructive STSAFE HAL and middleware checks. | Rerun. | Print help. | `a`, `b`, `h`/`?`. |
| `15` | Report key state; no automatic writes. | Authorize one supplied-key setup request for 60 seconds. | Cancel pending authorization/input; no undo. | `SK2` protocol via the supplied-key host tool; serial `a` cannot arm it. |

In RTC modes, the RGB LED and OLED status show the last suite's pass/fail
result, including suites invoked with serial `f`. The date/time rows update
only when their text changes. A failed/invalid RTC read clears the stale time
and reports an error; reconnecting a previously detected RTC can recover the
display without resetting it.

## Onboard diagnostics (modes 9-14)

These modes use core-bundled drivers and the checked-in STSELib middleware;
no network dependency download is needed to build.
The sensor dashboard and microphone DMA update from the main loop. Filesystem,
network and STSAFE operations are synchronous, so the heartbeat/buttons can
pause during a test; socket I/O and the TLS handshake have finite time limits.

### Sensors (9)

The dashboard checks four `WHO_AM_I` values and readback-verifies configuration:
HTS221 `0xBC` at `0x5F`, LPS22HB `0xB1` at `0x5C`, LSM6DSL `0x6A` at `0x6A`,
and LIS2MDL `0x40` at `0x1E`. It uses the same native 100 kHz I2C transport as
the RTC modes, avoiding the bundled sensor wrappers' bus reinitialization and
unchecked initialization results.

Readings include factory-calibrated humidity/temperature, pressure in hPa,
acceleration in g, angular velocity in degrees/second and magnetic field in mG.
USB output reports all axes and the number of changed samples. Button A cycles
five OLED pages; gently rotate/tilt the board to observe changes. Missing
devices, incorrect IDs, invalid calibration, failed readback, out-of-range
readings and missing fresh data are reported, not displayed as stale success.
These are functional/plausibility checks, not a metrological calibration or
the IMU's built-in excitation/self-test suite.

### Microphone (10)

Button A captures exactly 16,000 frames at 16 kHz/16-bit, with two I2S slots,
using finite full-duplex DMA and a two-second timeout. The NAU88C10 is mono:
the slots are not two independent microphones. The output DMA contains silence
and the headphone output is muted during capture.

The console reports sample count, peak, RMS, DC offset, DC-removed RMS and
clipped-sample count per slot. Constant/silent input is flagged; clipping is
reported separately. The quietest 20 ms window's DC-removed RMS is reported as
a noise-floor estimate in ADC counts, not a calibrated acoustic measurement.
Record once in quiet and once while speaking to compare levels; no fixed
background-noise threshold is asserted without an acoustic reference.
Button B plays only a completed capture at volume 25/100, with
PCM amplitude capped at 3,000 and faded edges. Busy requests and partial/failed
captures cannot start playback. Recordings remain in RAM only: they are never
saved to flash or sent over the network.

### QSPI filesystem (11)

Only `MXCTEST.BIN` on the `diagfs` mount of the SDK filesystem partition is
used. A new file is created exclusively (no truncation), contains a versioned
signature plus deterministic data, and must be exactly 4,096 bytes. Every
byte is verified after sync/close/unmount/remount; an FNV-1a checksum is also
reported. Existing files are checked before any write. Corrupt, partial or
unrecognized files are left untouched for inspection.

Reset or power-cycle to check retained data on the next startup; a valid
pre-existing test file is verified without rewriting it. Send `c` only when
finished: cleanup requires complete verification, removes only this file, then
remounts and checks its absence. Other files, firmware partitions and STSAFE
credentials are not touched. An unformatted filesystem reports an error;
**the mode never formats automatically**. If initialization is necessary and
the filesystem partition contains nothing you need, the exact uppercase serial
line `FORMAT FILESYS` explicitly authorizes formatting that partition. This
command erases all files there, not just the test file. No partial, misspelled
or overlong confirmation is accepted, and other modes cannot invoke it.

### Network services and Azure dependencies (12)

The default endpoint is Let's Encrypt's public
[valid ISRG Root X1 test site](https://valid-isrgrootx1.letsencrypt.org/).
The host, path, expected HTTP statuses and public trust anchor are in
[NetworkTestConfig.h](src/NetworkTestConfig.h). If changing the endpoint, update
its trust anchor and expected statuses together.

The mode uses saved credentials without invoking the SDK's automatic cloud
telemetry, resolves IPv4 DNS, connects to TCP port 80, checks a complete HTTP
301 response, obtains UTC through the core NTP client, then connects to port
443. HTTPS must return complete headers with status 200. HTTP response headers
are size-bounded, partial sends/receives are handled, redirects are not followed,
and failures are reported per stage. Socket calls use a one-second timeout,
request I/O has a ten-second limit, and TLS handshakes have a thirty-second
limit. Wi-Fi association, DNS and NTP retain their core driver timeouts.

TLS requires at least TLS 1.2, the ISRG Root X1 trust anchor and the expected
hostname. Core 2.0.0 disables `MBEDTLS_HAVE_TIME_DATE`, so a verification callback
additionally enforces validity dates on **each certificate in the chain**.
An extra verification with an intentionally wrong hostname must fail. Ordinary
NTP is not authenticated; the test is not a secure-time bootstrap. Expired or
replaced site certificates/root anchors cause a failure, never an insecure
fallback. The checked-in certificate is public, not a private key.

"No mandatory Azure dependencies" means no Azure subscription, IoT Hub, DPS
enrollment, device connection string, cloud account or cloud service keys are
required. This mode sends only generic public HTTP requests, not sensor data,
audio or credentials. The installed board SDK still contains Azure libraries;
their presence is distinct from requiring provisioned Azure services.

### Infrared transmitter (13)

The bundled IrDA driver uses USART3/PB_10 at 38,400 baud in SIR mode. Button A
sends `55 AA 00 FF 4D 58 43 48` once with a 500 ms transmit timeout and a
one-second minimum interval. This is **not a 38 kHz NEC/TV-remote protocol**.
Successful UART/HAL completion is reported only as a transmit-API result:
optical output and received bytes need an external IrDA receiver or probe.
There is no automatic/repeated emission and no receive-test claim.

### Explicit STSAFE interactions (14)

The project includes the complete upstream runtime of
[STMicroelectronics STSELib v1.1.11](https://github.com/STMicroelectronics/STSELib/tree/01f494046ee1df593601e40371224969e20402d4):
API, service, core and certificate-parser layers (109 source/license files).
The [source pin](libraries/STSELib/upstream.json) and
[SHA-256 manifest](libraries/STSELib/upstream-files.sha256) identify the exact
unmodified release. The ST development-board example applications and their
platform-specific CMOX binaries are not needed; this board uses its existing
mbedTLS instead.

Mode `14` first performs an `Init_HAL`/`HAL_Get_Data_Zone` read check,
then uses STSELib to:

- Verify the STSAFE-A100 silicon identity.
- Query lifecycle, host-key presence, private-key slot count and all storage
  partition metadata (sizes/types/access conditions, not stored contents).
- Send and verify a binary echo through CRC-checked I2C frames.
- Request two hardware random blocks and reject constant/repeated output.
- Verify the RFC 6979 P-256/SHA-256 public test signature using both host
  mbedTLS and the STSAFE itself; both must reject an altered signature.
- Check the host platform's AES-128/256 ECB/CBC, fragmented/truncated CMAC and
  HKDF bindings against NIST/RFC known-answer vectors.

The diagnostic never reads Wi-Fi/password zones, dumps certificate contents,
uses a private chip key, writes persistent data, personalizes the chip,
creates/replaces keys, or changes STM32 option bytes/PCROP. Its known-answer
keys are public test data, not production credentials. Random test output is
cleared rather than logged; the RNG test is functional, not entropy certification.

The core 2.0.0 STSAFE HAL returns status `10` when host/local-envelope key slots
are not personalized. This is reported as a warning, not silently treated as a
provisioned chip. The existing read handle can still be used for accessible
certificate-zone reads. Other initialization failures or denied reads stop
the test; neither case triggers personalization or a security-policy change.

#### A100 compatibility and platform scope

The AZ3166's A100 rejects STSELib's newer command-authorization-table query
(`QUERY 0x24`). [Az3166StSafe.h](libraries/STSELib/src/Az3166StSafe.h) exposes
`Az3166StSafe::begin(handler)` to use upstream's static-configuration option,
still verify the A100 identity on the wire, and install a conservative
**host-side** profile. Echo, RNG and public-key signature verification are
sent without a host session; other frame-policy-controlled commands default
to requiring a host session. Device-side access controls still apply. This
profile does not write or weaken any chip policy.

The adapter supports the onboard device at `0x20`, bus `0`, 100 kHz, with a
512-byte checked staging buffer. It uses MiCO's shared I2C transactions, not a
second `Wire` bus, and waits ST's A100 command timings before reading. This
avoids the older MiCO driver's noisy assertions for expected busy-address NACKs.
Retries remain bounded. Call the middleware serially from main/thread context,
not from interrupts or concurrent callers; the CRC, transport and incremental
CMAC contexts are shared.

The active configuration enables STSAFE-A, NIST P-256/P-384, SHA-224/256/384/512,
and AES host-session support. A110/A120-only features, Brainpool/Edwards curves,
STSAFE-L and 1-Wire are not enabled. Host NIST key-wrap is not present in this
board's mbedTLS build, so that platform entry point explicitly returns an error;
wrapped key-provisioning helpers are not enabled. The A100's own local-envelope
services remain in the middleware. Independent power-off is unsupported because
the onboard chip has no switchable supply.

This is **not** device-identity authentication, private-key signing verification,
or a claim that host secure-channel personalization has been performed. Those
operations need the correct existing key/slot policy and separately approved
provisioning. Full mutable service APIs are present in the source; the diagnostic
does not call them. Never run `enable_secure` or a middleware provisioning API
as a generic test or retry.

Mode `15` installs supplied host keys and SDK-compatible STM32 loaders, with
explicit operator authorization. Application reads/writes use the core's
legacy encrypted representation and failure behavior. Existing plaintext is
not converted into usable envelopes; read the
[supplied-key guide](docs/LEGACY-PROVISIONING.md) before use.
Do not interchange the SDK HAL and STSELib in the middle of an authenticated
session. Preserve the host-key flash region on a personalized board.

#### Licensing

STSELib retains its [BSD-3-Clause license](libraries/STSELib/LICENSE.txt), not
the project's Unlicense. The license is also published with firmware release
assets. When redistributing a binary containing this middleware, retain the
copyright notice, license conditions and disclaimer with the binary's
documentation/materials.

## RTC tests and commands (modes 3 and 4)

In mode `3`, the 15-check suite covers I2C communication, validated BCD time/calendar data, leap-day and
year rollover, stop/start, 12-hour mode, minute and half-minute interrupt flags,
the free RAM byte, both offset-calibration modes, oscillator load selection,
correction interrupt enable, external test control, every CLKOUT selection,
oscillator-stop handling, and software reset. Minute/half-minute flags are
tested with normal, zero-offset calibration as required by the PCF85063TP.
It readback-verifies restoration of the saved time, RAM byte, calibration,
control settings, and running/stopped state afterward. The INT and CLKOUT
electrical waveforms require probes on the module's dedicated pads because
those signals are not present on the four-wire Grove I2C connector.

In mode `4`, the DS1307 suite performs 23 I2C feature checks:

- Communication and valid calendar fields at `0x68`.
- CH oscillator halt and restart; 24-hour minute/hour rollover.
- Leap-day entry/exit, non-leap February, 30-day months, weekday wrap, and the
  two-digit year register's `99` to `00` rollover.
- 12-hour AM-to-PM noon and PM-to-AM midnight/year rollover.
- All **56 battery-backed RAM bytes** with zero, all-one, address, and
  complemented-address patterns, plus random access at both ends of RAM.
- SQW/OUT static low/high control and all four square-wave selections:
  **1, 4096, 8192, and 32768 Hz**.
- Readback-verified restoration of the saved time, 12/24-hour mode, CH state,
  output control, and all RAM bytes. A running saved clock is advanced by
  elapsed test time; a deliberately halted clock stays halted.

Both suites take a single register-map snapshot before modifying the device.
Invalid BCD/calendar data prevents destructive testing; use `t` to set a valid
time first. Both suites advance a saved running clock by the elapsed test time
and preserve a deliberately stopped clock. Restoration attempts the original
run/stop state even when another register write fails. Full tests intentionally
exercise/reset latched flags; they do not preserve pending interrupt events.

These tests temporarily change the clock and RAM. **Do not reset or disconnect
the board while the suite is running.** Normal completion restores the saved
state; I2C or restoration failures are reported explicitly. Allow about 15
seconds for PCF85063TP and 20-30 seconds for DS1307, or longer if oscillator
tests time out. Buttons, serial command processing, and the heartbeat wait
until a suite finishes.

SQW/OUT register checks do **not** measure the physical waveform. A scope or
frequency counter on the separate SQW/OUT pad is required for frequency/duty
cycle measurements. Battery retention, backup current, and long-term crystal
accuracy also require physical tests; they are not counted as software passes.
The DS1307 has no programmable alarm, countdown timer, software reset, or
calibration register; the PCF85063TP-only tests are never sent to it.

At 115200 baud, use `f` to rerun the full test, `s` to show the time, `b` to set
the RTC to the firmware build time, `d` for a read-only register dump (including
RAM), and `h` (or `?`) for help. Button A reruns the test; button B sets the
firmware compile timestamp. These controls work in both RTC modes, but are
ordinary payload text in radio modes.

`s` reports `OS!` when PCF85063TP clock integrity is lost, `STOP` when its
counters are deliberately stopped, or `CH!` when the DS1307 oscillator is
halted. `b` uses the RTC test code's compile timestamp, not the upload time or
the computer's current time; use the computer synchronization below for that.

To set an explicit date and time, send `t YYYY-MM-DD HH:MM:SS` followed by Enter,
for example `t 2026-10-04 16:25:18`. Use 24-hour input and a valid date in
2000-2099; the weekday is calculated automatically. LF, CR, and CRLF line
endings are accepted. Invalid or overlong input is rejected without changing
the RTC. Successful writes are read back before the clock restarts, and failures
are reported on serial. The RTC's 12/24-hour setting, RAM, output configuration,
and calibration (PCF85063TP only) are preserved. Displayed weekdays use Sunday
`W0` through Saturday `W6`; DS1307 registers use Seeed's Monday=1 through
Sunday=7 convention. Single-character commands work without Enter.

To use the computer's current local time from PowerShell, close any other
serial monitor, select the connected board's port, and run:

```powershell
$port = [System.IO.Ports.SerialPort]::new('COM8', 115200, 'None', 8, 'One')
$port.ReadTimeout = 75000
$port.WriteTimeout = 2000
try {
  $port.Open()
  $port.DiscardInBuffer()
  $port.Write('s') # Wait for the queued read to finish after any startup self-test.
  do {
    $line = $port.ReadLine().Trim()
    if ($line -match 'not found|RTC read failed') { throw $line }
  } while ($line -notmatch '^\d{4}-\d{2}-\d{2} W[0-6] \d{2}:\d{2}:\d{2}(?: (?:OS!|CH!|STOP))?$')
  $port.WriteLine('t ' + (Get-Date).AddSeconds(1).ToString('yyyy-MM-dd HH:mm:ss', [System.Globalization.CultureInfo]::InvariantCulture))
  $reply = $port.ReadLine().Trim()
  if (-not $reply.StartsWith('RTC set from serial: ')) { throw $reply }
  $reply
} finally {
  $port.Dispose()
}
```

Expect `RTC set from serial: ...` as confirmation. The RTC stores the supplied
wall-clock time, not a time zone or automatic daylight-saving rules. This
computer-time sync adds one second before sending to compensate for the
observed setting delay; small UART and whole-second rounding errors may remain.
The manual `t` command itself does not add an offset.

For a battery-retention check in either RTC mode, finish the suite and synchronize first.
Save the `s` and `d` responses, keep the AZ3166 powered over USB, disconnect only
the RTC's Grove cable for at least 30 seconds with a good CR1225 installed,
then reconnect the same RTC and read `s` and `d` again. Check that time advanced
by the elapsed interval:

- **PCF85063TP (mode 3):** RAM register `03` must retain its byte and the
  oscillator-stop flag (bit 7 of seconds register `04`) must remain clear.
- **DS1307 (mode 4):** registers `08` through `3F` must retain all 56 RAM bytes,
  and the CH bit (bit 7 of seconds register `00`) must remain clear.

Do not reset the AZ3166 or run `f`, `b`, or `t` between these readings:
reinitialization could mask loss of battery-backed data. The I2C/display read
errors while disconnected are expected, not a passing backup test.

## Button A: RGB LED test (modes 0-2)

Press and release **Button A** to advance the RGB LED through red, green, blue,
yellow, cyan, magenta, and white. Each color is shown at intensities **32, 128,
and 255** out of 255 before advancing to the next color. The sequence repeats
after 21 presses, and the RGB LED starts off at boot.

The OLED shows the current RGB values, and the serial monitor reports each
change and its intensity. Button input is debounced; holding A advances only
once. The existing LED blink and uptime updates continue during the test.

## Button B: audio output test (mode 1)

Audio is **enabled only in mode 1**. Radio-transmit modes conflict with the
codec's I2C/I2S pins; RTC modes also leave audio inactive and use Button B for
clock setting. All audio code and samples are retained. To restore audio,
disconnect the radio transmitter modules and set `MXCHIP_TEST_MODE` to `1`
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

Select `MXCHIP_TEST_MODE=2`. This uses the modules'
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

The host tests require PowerShell **7.2+** and `gcc`/`g++` on `PATH`.
Run all suites on Windows with:

```powershell
.\tools\test-rf.ps1
.\tools\test-diagnostics.ps1
.\tools\test-stsafe.ps1
.\tools\test-supplied-keys.ps1
.\tools\test-validate-firmware.ps1
.\tools\test-board-maintenance.ps1
```

Mode-exclusion tests run in all sixteen modes and cover framing, CRC, payload bounds,
sampling phase, timing variation, noise, exact transmitted bits, serial line
endings, queue overflow, display scrolling, and the absence of local echo.
Application tests also verify that audio mode never constructs or drives the
RF output, and exercise all 15 audio/volume combinations, PCM amplitude limits,
busy handling, automatic mute, the two-second cutoff, and failure paths.
LoRa tests cover 8N1 sampling/timing, both module UARTs, verified AT settings,
request/reply framing, half-duplex handover, both-hop loss, stale replies,
payload mismatch, maximum-length round trips, independent per-radio pacing,
and inactive-mode GPIO exclusion.
RTC tests cover the date/time serial command, strict calendar and range
validation, weekday calculation, line endings and fragmented input, 12/24-hour
encoding, invalid BCD rejection, setting preservation, ignored writes,
readback verification, and I2C failure paths for both chips. Register/clock
models exercise both complete suites, RAM/output patterns, normal-mode
periodic interrupts, running/halted state restoration, millisecond-counter
rollover, register dumps, and injected failures. Tests also verify compact RTC
input storage and change-only OLED updates with error recovery.

Diagnostic tests additionally cover live Wi-Fi disconnect/reconnect reporting,
exact credential limits, credential-buffer clearing, safe console errors,
all four complete Grove OLED patterns, display transport failures,
sensor calibration/units, every sensor
page, failed configuration readback, stale/error data, exact capture sizes,
DMA busy/timeout/error paths, PCM statistics and playback limits, partial
filesystem I/O, file collisions/corruption, guarded cleanup, strict HTTP
parsing, partial socket I/O, TLS hostname/date enforcement, missing time,
bounded timeouts, IR rate limits, direct read-only STSAFE calls and disabled
mode isolation. The public CA is parsed and its SHA-256 fingerprint checked.
These host tests model hardware failures; they do not replace physical sensor,
microphone, flash-retention or optical verification.

STSAFE transport tests compile the real upstream C API/frame/service code
against the actual project I2C adapter and a fault-injected device model. They
check command/response CRC, two-stage response reads, A100 identification,
the static profile, access-denied statuses, NACK/retry limits, malformed frame
lengths and fragment bounds. Upstream C/header extensions are isolated from
the strict C++ checks on project code. `.\tools\verify-stselib.ps1` verifies
the complete upstream hash manifest independently.

Supplied-key tests exercise the real setup backend with simulated STM32/STSAFE,
the exact legacy key-loader ABI, key/protection preconditions, explicit
partial failures, physical authorization, bounded input, the host workflow
and rejection of unsupported actions. No application-data or option-byte write
APIs are provided by the backend fixture. Embedded builds verify original-core
EEPROM linkage. These checks are not a substitute for qualification on a spare
board; no real provisioning/protection change was performed during development.

Firmware validation tests check vectors, the exact application flash-size
boundary and unchanged application bytes.
Maintenance tests use simulated devices; they do not read or flash a board.
Windows-only ACL and updater-orchestration checks run locally; portable
maintenance checks also run in GitHub Actions.

Local and CI validation use the same PowerShell test/build entry points.
The release workflow builds all sixteen modes and checks original EEPROM
linkage and application-only image validity for every build, then publishes
the source-selected default from [src/AppConfig.h](src/AppConfig.h).

## Firmware images

Local builds and GitHub releases provide **application-only firmware**:

| Image | Contents | Upload method |
| --- | --- | --- |
| `MXCHIPTest1.ino.bin` | Application only, linked at `0x0800C000`. | Normal Arduino CLI/OpenOCD upload. |

The [validation script](tools/validate-firmware.ps1) checks the stack/reset
vectors and flash capacity without altering the application.

Use Arduino CLI/OpenOCD to program the application at `0x0800C000`. **Do not
drag this application-only binary onto the `AZ3166` drive.** This upload path
preserves the preceding bootloader/host-key region. A generic image starting
at `0x08000000` cannot preserve board-specific host keys unless constructed
from that board's actual readable flash contents.

### Board-specific backup images

[build-board-image.ps1](tools/build-board-image.ps1) can explicitly create a
**confidential same-board** image from two identical reads of the connected
board's first 48 KiB (bootloader plus host provisioning region) and an
already-built application. It does not flash anything. Output must be outside
Git worktrees on a fixed local NTFS/ReFS disk and is restricted to the current
Windows user. It is not part of normal builds or release assets.

See [Board maintenance: detailed procedures and limitations](docs/BOARD-MAINTENANCE.md).

```powershell
pwsh
.\tools\build-board-image.ps1 -WhatIf
.\tools\build-board-image.ps1
```

STSAFE is a separate chip: its data and non-exportable keys are **not** in a
STM32 flash image. A per-board image can retain matching STM32 host keys while
the STSAFE remains on the same board, but cannot clone or factory-reset the
secure element. RDP/PCROP can prevent reading host flash. Do not remove RDP
to obtain a backup: reverting Level 1 to Level 0 erases STM32 flash.

### Preventing accidental disk flashing

The virtual disk is provided by the ST-Link interface, not by the Arduino
application. [stlink-mass-storage.ps1](tools/stlink-mass-storage.ps1) prepares
ST's authenticated vendor updater and performs a **board-side** reversible
MSC disable/reenable operation. It can restart the selected Windows USB
device to complete a firmware transition, but does not hide devices or change
driver bindings or automount rules. Read the [how-to](docs/BOARD-MAINTENANCE.md) before using it:
this programs the ST-Link coprocessor and can change its USB ID. The switch
uses ST's vendor updater, **not OpenOCD**, and leaves Arduino's upload
configuration unchanged. The saved xPack OpenOCD 0.12.0-7 native ST-Link/SWD
configuration supports the `3752` no-MSC personality.

```powershell
.\tools\stlink-mass-storage.ps1 -Action Status
.\tools\stlink-mass-storage.ps1 -Action Setup -VendorZip .\drivers\stsw-link007.zip -AcceptVendorLicense
.\tools\stlink-mass-storage.ps1 -Action Disabled -WhatIf
.\tools\stlink-mass-storage.ps1 -Action Disabled
.\tools\stlink-mass-storage.ps1 -Action Enabled
```

**Run `Setup` once before the first switch.** STSW-LINK007 is separate from
the USB driver, Arduino core and OpenOCD; use the [bundled archive](drivers/stsw-link007.zip)
as shown above. A missing `STLinkUpgrade.jar` means setup has not completed at the
selected location, not that the board needs repair. The default installation
is `%LOCALAPPDATA%\MXCHIPTest1\STLinkUpgrade`; use the same `-ToolDirectory`
for setup and switching if you choose another location.

Run an actual `Enabled`/`Disabled` change from normal **64-bit PowerShell
7.2+**. Confirm the operation, then approve the Windows UAC prompt if the
shell is not elevated. The script handles the selected device's loader
transitions, required Windows USB restarts and final verification; no
unplugging or separate recovery commands are part of the normal workflow.
An already-elevated shell needs only the operation confirmation.
`Status`, `Setup`, `-WhatIf` and an already-correct state do not request UAC.
Status includes the current serial-port name, such as COM14.

Normal output shows progress and the final verified result. Intermediate
errors that are successfully recovered are kept in a private per-operation
log, not reported as console warnings; the initial firmware-write warning
and confirmation remain. Add `-Verbose` to also display raw diagnostics.
Each confirmed switch prints its retained log path **before progress** under
`%LOCALAPPDATA%\MXCHIPTest1\Logs\STLink`. Terminal failures include the
failed stage, last observed USB state and log path. See
[output and diagnostics](docs/BOARD-MAINTENANCE.md#output-and-diagnostic-logs).
The final **Verified** message is green and is the last success message.

ST's STLinkUpgrade tool also documents a **Change type / without mass storage**
firmware option for supported ST-Link interfaces. The local script uses the
reversible `mscOffOpt`/`mscOnOpt` variants, never `mscAlwaysOff`.
A hardware **Disabled -> Enabled -> Disabled** round trip has been verified:
PID `3752`/COM14 without MSC, PID `374B`/COM8 with MSC, then back to
PID `3752`/COM14. Debug/VCP bindings stayed healthy, and a final native-SWD
target examination passed without resetting or programming the application.
The normal script completed this test with **UAC approvals as the only
operator interaction after authorization**. It automatically completed the
vendor's loader transitions using scoped Windows device restarts. No
physical reconnect or manually entered recovery command was needed.
Port numbers depend on the PC.
Do not treat either Windows hiding
or mass-storage disabling as a substitute for secure key-backup/update design.
A recognized failure before programming permits one controlled continuation
from the same probe's verified loader. After programming succeeds, only USB
re-enumeration is attempted, never another flash. Unknown outcomes or a
changed probe/USB location stop with an explicit error; see the
[transition limits](docs/BOARD-MAINTENANCE.md#usb-loader-transition-errors).

Suppressing AutoPlay only prevents the Explorer prompt; it does **not**
prevent mounting or writing. Removing a drive letter is weaker than disabling
the interface, and its persistence can vary for virtual removable disks.
Avoid a machine-wide `automount disable` for this one board: it affects other
new volumes too. No USB firmware or Windows mount/device settings are changed
by this project's build scripts.

References: [ST-Link firmware release notes (RN0093)](https://www.st.com/resource/en/release_note/rn0093-firmware-upgrade-for-stlink-stlinkv2-stlinkv21-and-stlinkv3-boards-stmicroelectronics.pdf),
[STLinkUpgrade / STSW-LINK007](https://www.st.com/en/development-tools/stsw-link007.html).

## Default firmware

The repository includes the MXCHIP AZ3166 default firmware image at
`firmware/devkit-firmware-2.0.0.bin`. It is kept separately from generated build
outputs for deliberate bootloader/factory recovery. It is not used by normal
builds or included in release assets.

SHA-256:

```text
33a01378a40484f306777de1e11462918ca9901d1039d2e97ab55d60cc684300
```

**Do not use this full reference image as a routine update on a personalized
board.** It can overwrite matching STM32 host-key material and make encrypted
STSAFE data inaccessible. Factory recovery is a separate, explicitly authorized
operation requiring a suitable recovery plan. The USB virtual disk can flash
such a binary automatically; merely copying it there is a firmware-write action.

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
application-only binary, ELF, map and STSELib license in a GitHub release.
Only those four named assets are published.
Every release includes an application-only upload warning in its
release notes. Mode 15 is not the default release firmware and its
personalization path still requires hardware qualification.
