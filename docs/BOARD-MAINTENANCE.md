# Board-specific images and board-side USB mass storage

These are **explicit local maintenance tools**, not part of the normal build or
release pipeline. The tools do not personalize STSAFE, change its keys, format
storage or change Windows automount/device settings.

| Tool | Purpose | Writes hardware? |
| --- | --- | --- |
| [build-board-image.ps1](../tools/build-board-image.ps1) | Read the current board prefix twice and combine it with a previously built application. | No flash writes. Briefly halts/resumes the target and restores its watchdog debug setting. |
| [stlink-mass-storage.ps1](../tools/stlink-mass-storage.ps1) `Status` | Inspect the selected probe's current USB interface set. | No. |
| Same tool, `Setup` | Validate and prepare ST's updater in a private local directory. | No. |
| Same tool, `Disabled` or `Enabled` | Program the ST-Link coprocessor's reversible mass-storage personality. | **Yes: ST-Link firmware.** Not an application upload. USB may disconnect and the application may reset. |

Both entry points require **64-bit PowerShell 7.2 or newer on Windows**. Run
`pwsh` rather than the older Windows PowerShell when following these examples.
This is the host platform of the tools, **not** the scope of the USB setting:
the mass-storage change is in the probe and travels with it to other computers.

`-WhatIf` never reads target flash, creates a board image, downloads tools, or
loads the ST updater. For the image tool it can query the local OpenOCD version
and current USB enumeration. The default confirmation prompt is intentional;
do not suppress it in unattended workflows without a separately reviewed plan.

## 1. Create a same-board full image

### What it contains

```text
Same connected STM32, two identical reads:
    0x08000000 .. 0x0800BFFF (49,152 bytes)
        bootloader
        reserved host-key/provisioning region, including 0x08008000 .. 0x0800BFFF

Previously compiled application, byte-for-byte:
    0x0800C000 .. application end

Result:
    MXCHIPTest1.<STM32-UID>.board.full.bin, intended flash base 0x08000000
```

This deliberately does **not** use the repository's factory firmware prefix.
It preserves the exact bytes currently readable on the connected board.

The result is **not a complete device backup**:

- STSAFE is a separate chip. Its data, counters and non-exportable keys are
  not captured. On the same board they remain in that chip; the image preserves
  only the STM32-side material needed to access them.
- External QSPI contents are not included.
- STM32 option bytes, OTP and flash beyond the new application's end are not
  included. Do not use this for a layout that keeps application data there.
- A saved image can become stale if provisioning state changes later. Pair
  it with the correct state/epoch and backup procedure; do not mix boards.
- Restoring the image on another STM32 or replacing the STSAFE is not a
  supported cloning/recovery procedure.

### Preconditions

1. Keep the board on reliable USB power; close debuggers/OpenOCD sessions.
2. The main flash must be readable. The tool checks STM32F412 device ID
   `0x441`, 1 MiB flash size, RDP Level 0, and absence of PCROP mode.
   It **refuses** protected targets, never tries to unlock them, and never
   performs an erase. RDP Level 1 -> Level 0 erases STM32 flash; that is not
   a way to obtain a backup.
3. Ensure the application can tolerate a brief debugger halt. The tool does
   not reset it to obtain a snapshot. An already halted target remains halted.
4. Prepare a nonsynchronized, fixed local NTFS/ReFS destination outside all
   Git worktrees. The default is below
   `%LOCALAPPDATA%\MXCHIPTest1\BoardImages`.
5. Treat both the output image and any encrypted backup made from it as
   confidential. The tool restricts its new directory to the current user's
   SID **before reading flash**, but this is an ACL, not encryption. Use
   BitLocker/appropriate encrypted storage. Administrator access, malware
   running as you and disk recovery are not prevented by the ACL.

### Step-by-step

Build the desired application normally; this still does **not** generate a full
image or flash the device:

```powershell
pwsh
Set-Location C:\Projects\Arduino\MXCHIPTest1
.\tools\build.ps1
.\tools\stlink-mass-storage.ps1 -Action Status
```

The status lists the **ST-Link serial number**, distinct from the STM32 UID.
If exactly one supported probe is attached, `-SerialNumber` can be omitted.
For multiple probes, provide the 24-hex-digit serial printed by `Status`.
With multiple devices, obtain the serial from Device Manager's parent USB
device instance ID before invoking the tool with `-SerialNumber`.

Preview, then build the image:

```powershell
.\tools\build-board-image.ps1 -WhatIf
.\tools\build-board-image.ps1
```

Or select the application, device and a **new** output directory explicitly:

```powershell
.\tools\build-board-image.ps1 `
  -Application C:\Projects\Arduino\MXCHIPTest1\build\MXCHIPTest1.ino.bin `
  -SerialNumber <24-hex-ST-Link-serial> `
  -OutputDirectory C:\PrivateBoardBackups\my-az3166-2026-10-07
```

Replace the angle-bracket placeholder before running. The output directory
must not already exist. This is intentional: previous confidential backups
are never overwritten. Relative paths, network shares, removable volumes,
drive roots, and junction/symlink output paths are rejected.

By default, OpenOCD is found through `arduino-cli config dump` in the installed
AZ3166 tool package. An alternative distribution can be supplied:

```powershell
.\tools\build-board-image.ps1 -OpenOcdRoot C:\Tools\OpenOCD
```

That root must contain `bin\openocd.exe`, with HLA support. The tool recognizes
Tcl scripts under `scripts`, `openocd\scripts` (the xPack layout), or
`share\openocd\scripts`. The selected script directory must contain
`interface\stlink-v2-1.cfg` and `target\stm32f4x.cfg`. For the portable xPack
0.12.0-7 release, pass the extracted `xpack-openocd-0.12.0-7` directory as the
root; do not pass its `bin` or `openocd` child.

The maintenance entry points reload their helper module on each invocation,
so changes take effect in an already open PowerShell terminal. If an older
script copy still reports a missing `scripts\interface\stlink-v2-1.cfg` even
though xPack has it under `openocd\scripts`, refresh the scripts or run
`Remove-Module BoardMaintenance -Force` before retrying the `-WhatIf` command.

### Verification and artifacts

The tool:

1. Validates application length, RAM stack pointer, Thumb reset vector and
   application destination range.
2. Selects exactly the requested ST-Link USB PID and serial.
3. Disables OpenOCD's network listeners and connects without reset/flash
   programming commands.
4. Checks target identity/protection and records its run/halt state.
5. Pauses a running CPU, reads the prefix twice and reads the 12-byte STM32 UID.
6. Restores execution/debug state even if a read fails. After an OpenOCD error
   or timeout it attempts a narrowly scoped state-restoration connection.
   If USB is lost, restoration may be impossible; the error explicitly warns
   that the CPU may remain halted.
7. Requires identical, full-length prefix reads and valid bootloader vectors.
8. Requires that the application file did not change during the operation.
9. Writes and hashes the image, then writes `manifest.json` with the probe
   serial, STM32 UID, addresses, lengths, time, tool version and SHA-256 hashes.
10. Deletes raw intermediate dumps. On failure it deletes incomplete outputs
    and does not claim a usable backup. Deletion is **not secure erasure**.

Keep the image and manifest together. A hash detects accidental modification;
it is not an authenticated signature or proof that an old backup matches the
board's current security state.

### Flashing this special image

**The image builder never flashes its output.** Routine firmware updates should
continue using the application-only upload at `0x0800C000`.

This special image is for deliberate, separately authorized same-board
restoration. If used, its intended address is `0x08000000`, **not** the Arduino
application-offset upload recipe. Raw `.bin` files contain no address metadata.
Do not use `arduino-cli upload --input-file` with this image and the standard
AZ3166 recipe: it would place the image at the wrong offset.

The AZ3166 virtual disk's full-image flashing behavior must be understood
before using it. A host-key-preserving prefix does not guarantee that a probe
firmware's erase strategy preserves *other* STM32 regions. This workflow has
not been tested by writing a personalized board image; keep a recovery plan
and verify the intended addresses/board before a restoration.

Normal builds still remove the obsolete generic `build\MXCHIPTest1.full.bin`.
Private board images are stored elsewhere and are **never release assets**.

## 2. Disable/re-enable mass storage on the board

### Why a sketch cannot do this

The virtual disk belongs to the separate **ST-Link/V2-1 coprocessor**. The
Arduino application runs on the target STM32F412, not that coprocessor.
Changing the sketch cannot remove the ST-Link's USB mass-storage interface.

This tool uses ST's firmware updater to select the reversible MSC personality:

```text
Disable: -sn SERIAL -msvcp -dynOpt mscOffOpt -force_prog
Enable:  -sn SERIAL -msvcp -dynOpt mscOnOpt  -force_prog
```

It does **not** call Windows `Disable-PnpDevice`, edit registry/mount settings,
remove drive letters, or use the permanent `mscAlwaysOff` option. It also never
switches to a plain `-jtag` personality that might remove the virtual COM port.

The vendor updater may update the probe to the version in its package, rather
than flipping one bit in the existing version. `-force_prog` is needed to apply
the chosen personality even when the firmware revision is already current.
Review the package version beforehand: do not unknowingly downgrade a newer
probe. USB enumeration, disk naming and COM numbering may change.

### Self-contained preparation, with ST's licensed dependency

The script automates preparation, authenticity checks, preflight, programming
and verification. It does not redistribute ST's proprietary updater/firmware
or implement an unofficial flashing protocol. It therefore needs:

- 64-bit Java available as `java`, or explicitly via `-JavaPath`.
- ST's official **STSW-LINK007** ZIP containing the `AllPlatforms` updater.
  Obtain it from [ST](https://www.st.com/en/development-tools/stsw-link007.html)
  and accept its terms. No STM32CubeIDE installation or Windows-side disk
  hiding is required.

The tool copies only the signed JAR and its x64 Windows driver into a private
directory. It validates the detached JAR signature, a pinned ST signing
certificate, the complete signed manifest and every payload entry, required
CLI tokens, and the driver's ST Authenticode signature. It does **not execute
the updater during Setup**.

The inspected vendor package contains **V2J45M31**. Its signer pin is:

```text
7B35ED6E0BD638A2E4D376A4E7E2F4AF30294A1123127EB5FD7578F7B08A4B93
```

This ST signing certificate is valid through **2027-01-17**. A different or
expired signer is rejected even if the file is named `STLinkUpgrade.jar`.
Updating the pin requires reviewing the new official package/signature and
its CLI. There is deliberately no `SkipSignatureCheck` escape hatch.

Prepare from a downloaded official ZIP:

```powershell
.\tools\stlink-mass-storage.ps1 -Action Setup `
  -VendorZip C:\Downloads\en.stsw-link007.zip `
  -AcceptVendorLicense
```

Or let the script attempt the official ST download:

```powershell
.\tools\stlink-mass-storage.ps1 -Action Setup `
  -DownloadVendorTool -AcceptVendorLicense
```

If ST's server requires authentication, returns HTML, times out or ships an
unrecognized signer, setup fails explicitly. It does not try unofficial
download mirrors. Use a supported official ZIP instead. After a failed setup,
use a new `-ToolDirectory` or inspect/remove the empty failed setup directory;
existing directories are not overwritten.

The default tool directory is `%LOCALAPPDATA%\MXCHIPTest1\STLinkUpgrade`.
Specify `-ToolDirectory` consistently on subsequent commands if you chose a
different location. A genuine already-extracted `AllPlatforms` directory can
also be used directly, subject to the same verification.

### Switch the board's USB personality

1. Make a board-specific backup **before** switching if you need one.
2. Close Arduino monitors, debuggers, OpenOCD and other probe tools.
3. Keep stable USB power and do not interrupt the updater.
4. Read the current descriptor/interface state:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Status
   ```

5. Preview:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Disabled -WhatIf
   ```

6. Disable after reviewing the warning and confirming:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Disabled
   ```

7. Reenable later, even while the disk is absent:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Enabled
   ```

For multiple probes use `-SerialNumber <24-hex-ST-Link-serial>` on each
command. The tool supports only ST-Link/V2-1 application-mode USB IDs
`0483:374B` and `0483:3752`; it will not guess which ST-Link/V3, clone or
USB-loader device to program.

Before programming it runs ST's non-programming `-checkParam` preflight for
the same serial/options and rechecks the package and device selection.
Afterward it requires two consistent observations of the **same probe serial**
with the requested MSC presence/absence, working debug and serial interfaces,
and healthy drivers. An updater exit code alone is not sufficient.

If already in the requested healthy state, the tool performs no firmware write.
The vendor programming process has no automatic kill timeout: terminating it,
pressing Ctrl+C, closing the terminal or unplugging USB mid-update can leave the
probe requiring ST's recovery procedure. Verification has a finite timeout.
If verification fails, the tool does not repeatedly reflash or claim success.
Inspect the vendor output and the device state first. If the probe is stuck in
USB-loader mode, use the official updater's recovery instructions; this script
does not blindly target loader-mode devices.

### Important: the old Arduino core's OpenOCD

With MSC disabled, the probe may enumerate as **PID `3752`**. The core's bundled
OpenOCD **0.10** recognizes only the older MSC personality correctly. Merely
changing its VID/PID filter is insufficient: its driver selects different USB
endpoints for the unrecognized PID.

- The ST-Link hardware still provides SWD and VCP.
- The old automatic Arduino upload recipe may stop working until MSC is
  reenabled, or you configure a newer compatible OpenOCD/other supported
  programmer.
- `build-board-image.ps1` detects this case and requires a modern
  `-OpenOcdRoot` (0.11+ with HLA), rather than trying the wrong endpoints.
- Reenablement uses ST's updater and the exact probe serial, **not OpenOCD**,
  so it does not depend on the old upload recipe or disk visibility.
- A changed USB PID may require the appropriate ST WinUSB driver binding on
  Windows. Driver errors cause verification failure, not success.

Turning MSC off is protection against accidental drag-and-drop writes. It is
not an authentication boundary or a replacement for key backups, RDP or
secure firmware updates. It can be reenabled by someone with the necessary
physical/software access. Nothing here changes STSAFE access policy.

Vendor references: [STSW-LINK007](https://www.st.com/en/development-tools/stsw-link007.html)
and [ST-Link firmware release notes RN0093](https://www.st.com/resource/en/release_note/rn0093-firmware-upgrade-for-stlink-stlinkv2-stlinkv21-and-stlinkv3-boards-stmicroelectronics.pdf).
The exact CLI options were additionally checked in the ST-signed V2J45M31
Java updater's argument parser; common online `-no_msd` examples do not match
that verified CLI.

## Validation performed during development

- Synthetic byte-for-byte tests preserve the entire 48 KiB prefix and host-key
  sector, verify application vectors/size, and reject inconsistent reads.
- The full image-creation flow is tested with a simulated OpenOCD, including
  private output ACLs, error recovery, cleanup, and `-WhatIf`.
- Tests verify exact reversible updater arguments, preflight/write separation,
  requested USB states, failed vendor operations, and idempotence.
- The inspected ST-signed JAR/driver are checked statically; no updater firmware
  operation is run for the tests.
- Live Windows USB status was read without changing it.

**No target flash backup, target/probe flashing, or MSC switch was performed
during development.** A live round-trip disable/reenable and restoration of a
secret-bearing full image remain operator-controlled hardware validation.
Do not interpret unit tests or an updater's supported argument syntax as proof
that a firmware change has already succeeded on this board.

Run the tests without touching devices:

```powershell
.\tools\test-board-maintenance.ps1
.\tools\test-validate-firmware.ps1
```

To additionally validate a real vendor package **without executing it**:

```powershell
.\tools\test-board-maintenance.ps1 -VendorDirectory C:\Tools\STSW-LINK007\AllPlatforms
```

The shared [BoardMaintenance module](../tools/BoardMaintenance.psm1) contains
device selection, image/manifest checks and vendor-package validation. Do not
call its low-level helpers to bypass the entry points' confirmations.
