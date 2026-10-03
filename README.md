# MXCHIPTest1

Arduino test sketch for the Microsoft Azure IoT DevKit / MXCHIP AZ3166.

## Installed support

- Arduino CLI 1.5.1
- MXChip - Microsoft Azure IoT Developer Kit core 2.0.0
- Board: `AZ3166:stm32f4:MXCHIP_AZ3166`
- Serial/upload port: `COM3`

The board core includes the device libraries required for the AZ3166:
Audio, Azure IoT, filesystem, MQTT, sensors, SPI, WebSocket, Wi-Fi, and Wire.
The initial sketch uses the core-provided OLED display and built-in LED, so no
additional Library Manager packages are required.

## Arduino CLI

The board and port defaults are stored in `sketch.yaml`.

```powershell
arduino-cli compile .
arduino-cli upload .
arduino-cli monitor -p COM3 -c baudrate=115200
```

## Arduino IDE

Open `MXCHIPTest1.ino` in Arduino IDE. Select **MXCHIP AZ3166** as the board and
**COM3** as the port if the IDE does not load the defaults automatically.

After upload, the onboard LED blinks, the OLED shows the uptime, and the serial
monitor reports the uptime at 115200 baud.

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
firmware files in a GitHub release.
