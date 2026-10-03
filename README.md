# MXCHIPTest1

Arduino test sketch for the Microsoft Azure IoT DevKit / MXCHIP AZ3166.

## Installed support

- Arduino CLI 1.5.1
- MXChip - Microsoft Azure IoT Developer Kit core 2.0.0
- Windows ST-Link debug driver 2.2.0.0 from STSW-LINK009 2.0.2
- Board: `AZ3166:stm32f4:MXCHIP_AZ3166`
- Serial/upload port: `COM3`

The board core includes the device libraries required for the AZ3166:
Audio, Azure IoT, filesystem, MQTT, sensors, SPI, WebSocket, Wi-Fi, and Wire.
The initial sketch uses the core-provided OLED display and built-in LED, so no
additional Library Manager packages are required.

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
staged copy of the sketch under `_build/MXCHIPTest1`, without the local upload-port
metadata. A board does not need to be connected to compile.

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
It adds an entry for the original `.ino` file because Arduino CLI normally emits
one only for its generated `.ino.cpp` translation unit. That entry injects
`Arduino.h`, matching the preprocessing Arduino applies to sketches.

## Arduino IDE

Open `MXCHIPTest1.ino` in Arduino IDE. Select **MXCHIP AZ3166** as the board and
**COM3** as the port if the IDE does not load the defaults automatically.

After upload, the onboard LED blinks, the OLED shows the uptime, and the serial
monitor reports the uptime at 115200 baud.

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
and map files, in a GitHub release.
