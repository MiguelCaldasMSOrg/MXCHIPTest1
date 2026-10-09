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

Run `Enabled`/`Disabled` from normal PowerShell. After checking prerequisites
and asking for confirmation, the script requests **Windows UAC elevation**
when needed. Its elevated worker runs the same selected action with the
same resolved tool paths, handles device-only restarts and returns its
output to the original terminal. The original process independently checks
the resulting USB state. Cancelling UAC stops without launching the updater.
`Status`, `Setup`, `-WhatIf` and an already-correct state do not request UAC.
No physical reconnect or separately entered recovery command is needed for
the tested normal switching workflow.

Normal output contains brief progress and the final verified result.
Successful automatic recovery does not display intermediate vendor or
Windows errors as warnings. The initial firmware-write risk warning and
confirmation are always retained.

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
- A saved image can become stale if the board's keys or firmware layout
  change. Keep it paired with the matching board and STSAFE state.
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

Build the application to include in the image. This compiles the
application without flashing the device:

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

By default, the tool reads the **effective AZ3166 OpenOCD path and command**
through `arduino-cli board details --show-properties=expanded`, including
`platform.local.txt` overrides. It uses that configured executable rather
than assuming the core's bundled OpenOCD version. `-Verbose` shows the
selected executable/version. An alternative distribution can be supplied:

```powershell
.\tools\build-board-image.ps1 -OpenOcdRoot C:\Tools\OpenOCD
```

That root must contain `bin\openocd.exe`, with HLA support. The tool recognizes
Tcl scripts under `scripts`, `openocd\scripts` (the xPack layout), or
`share\openocd\scripts`. The selected script directory must contain
`target\stm32f4x.cfg` and an HLA interface: `interface\stlink-hla.cfg` is
preferred, falling back to `interface\stlink-v2-1.cfg` for older distributions.
The interface/transport/target combination is parsed with an explicit
`shutdown` before any `init`; unsupported combinations fail even with
`-WhatIf`, without opening the probe or accessing the target. For the portable xPack
0.12.0-7 release, pass the extracted `xpack-openocd-0.12.0-7` directory as the
root; do not pass its `bin` or `openocd` child.

The maintenance entry points reload their helper module on each invocation,
so updates take effect in an already open PowerShell terminal.

### Arduino uploads with xPack 0.12.0-7

Choosing `-OpenOcdRoot` for the image tool does not configure Arduino uploads.
Core 2.0.0's upload recipe selects `hla_swd`. In xPack 0.12.0-7,
`stlink-v2-1.cfg` is an alias for the **native** ST-Link driver, not HLA.
Changing only the OpenOCD executable/script paths therefore produces:

```text
Debug adapter doesn't support 'hla_swd' transport
```

Use the **native ST-Link interface and `swd` transport together**. This removes
both the `hla_swd` spelling warning and the HLA-driver deprecation warning.
The installed V2J28M17 probe firmware is newer than the native driver's V2J24
minimum; no probe-firmware upgrade was needed. Create
`platform.local.txt` beside the installed AZ3166 core's `platform.txt` rather
than changing the vendor interface scripts. For the installation used here,
that directory is
`C:\Projects\Arduino\Support\data\packages\AZ3166\hardware\stm32f4\2.0.0`.
Use your actual extracted xPack path:

```properties
tools.openocd.path.windows=C:\Tools\xpack-openocd-0.12.0-7\openocd
tools.openocd.cmd.windows=..\bin\openocd.exe
tools.openocd.upload.pattern="{path}\{cmd}" -s "{path}\scripts" -f "interface\stlink.cfg" -c "transport select swd" -f "target\stm32f4x.cfg" -c "program {{build.path}\{build.project_name}.bin} verify reset 0x0800C000; shutdown"
```

Keep the recipe on one line. Arduino CLI reads the override on its next
invocation; restart the IDE if it is already open. The application upload
address remains `0x0800C000`; this does not enable RDP/PCROP, change STSAFE
keys or create a full image. Reapply/review this local override after
reinstalling the core or changing OpenOCD distributions.

Preserve the filename quoting exactly: Arduino substitutes `{build.path}`
and `{build.project_name}`, leaving **one** Tcl brace pair around the
expanded filename. Another outer pair makes modern OpenOCD treat literal
braces as part of the filename and fail with `couldn't open {filename}`,
even when the file exists. One pair also protects paths containing spaces.

A matching project copy is kept in
[tools/platform.local.txt](../tools/platform.local.txt), outside the installed
board-package directory. See the
[README restore procedure](../README.md#keeping-local-arduino-overrides-across-core-updates)
for override precedence, version/path checks and reinstalling this copy after
a core update.

This configuration-only check does **not** initialize or program the board:

```powershell
& C:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe `
  -s C:\Tools\xpack-openocd-0.12.0-7\openocd\scripts `
  -f interface\stlink.cfg -c "transport select swd" `
  -f target\stm32f4x.cfg -c "echo MXCHIP_CONFIG_ONLY_OK; shutdown"
```

The native upload recipe was hardware-tested on 2026-10-08 with a freshly
built mode-15 application: programming and readback verification succeeded,
the board restarted into mode 15, and neither deprecation warning appeared.
Startup reported no host/envelope keys, an empty host-key sector, RDP0 and
PCROP off. No key-setup commands or protection changes were performed.

The **separate board-image tool** still uses HLA for its existing
probe-selection and run/halt-state handling, with the explicit HLA script
selected on modern xPack. The saved Arduino override does not change that
tool. Very old probes/OpenOCD builds requiring HLA need the compatible
legacy recipe; do not mix HLA transport commands with the native driver.

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

The private output directory also retains `operation.log`, including the
OpenOCD snapshot and state-restoration diagnostics. Its path is printed
before progress starts; `-Verbose` displays raw details as well. Neutral
progress describes execution-state restoration, while a terminal error
reports the stage and log path. Restoring CPU state does not make an
incomplete snapshot valid: failure still publishes no image or manifest.
Intermediate dumps are removed on both success and failure.

The final **Verified** message is green and appears only after image
verification and cleanup finish. Treat the entire directory, including its
device identities and diagnostic paths, as confidential.

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

The script automates preparation, authenticity checks, programming
and verification using ST's vendor updater. It needs:

- 64-bit Java available as `java`, or explicitly via `-JavaPath`.
- ST's official **STSW-LINK007** ZIP containing the `AllPlatforms` updater.
  Use the [bundled 3.17.11 archive](../drivers/stsw-link007.zip) or obtain
  an official package from [ST](https://www.st.com/en/development-tools/stsw-link007.html),
  and accept its [license](../drivers/STSW-LINK007-LICENSE.txt).
  No STM32CubeIDE installation or Windows-side disk
  hiding is required.

The tool copies only the signed JAR and its x64 Windows driver into a private
directory. It validates the detached JAR signature, a pinned ST signing
certificate, SHA-256/SHA-384 digests for the complete signed manifest and
every payload entry, required
CLI tokens, and the driver's ST Authenticode signature. It does **not execute
the updater during Setup**.

The bundled updater is **STSW-LINK007 3.17.11**. Its Windows executable,
Java updater and Windows x64 native driver have been checked statically.
A Disabled -> Enabled -> Disabled round trip is verified through the normal
script on the connected V2-1 board. It handles loader entry/exit and scoped
USB-device restarts automatically after the user approves the operation/UAC
prompts. The vendor updater alone does not reliably complete these transitions
on the tested Windows installation.
The archive is unchanged,
with its ST/third-party notices and corresponding libusb sources preserved as
described in the [package inventory](../README.md#st-link-firmware-updater).
Its signer pin is:

```text
7B35ED6E0BD638A2E4D376A4E7E2F4AF30294A1123127EB5FD7578F7B08A4B93
```

This ST signing certificate is valid through **2027-01-17**. A different or
expired signer is rejected even if the file is named `STLinkUpgrade.jar`.
Updating the pin requires reviewing the new official package/signature and
its CLI. There is deliberately no `SkipSignatureCheck` escape hatch.

Prepare from the repository's official ZIP:

```powershell
.\tools\stlink-mass-storage.ps1 -Action Setup `
  -VendorZip .\drivers\stsw-link007.zip `
  -AcceptVendorLicense
```

You can supply another official ZIP with `-VendorZip`, or let the script
attempt the official ST download:

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

`Setup` is a **required one-time preparation step** unless you supply an
already-extracted updater directory. Installing the ST-Link USB driver,
Arduino core or xPack OpenOCD does not prepare the updater. The bundled ZIP
must be processed by `Setup` before the JAR/native driver can be used from the
selected tool directory.

If `Disabled` or `Enabled` reports a missing `STLinkUpgrade.jar` or
`STLinkUSBDriver.dll`, run `Setup` first or correct `-ToolDirectory`.
The script checks those files and the Java executable before requesting
firmware-write confirmation. A missing-component error means the updater
was not launched and no firmware write occurred. `Status`, `-WhatIf` and
an already-correct device state still need no updater installation.
If several Java installations are on `PATH`, the first is selected;
use `-JavaPath` to choose a different 64-bit runtime explicitly.
Before loading ST's JAR, the script queries the JVM's version and data model
without loading native libraries. Java 24+ receives
`--enable-native-access=ALL-UNNAMED` so its JNI native-access requirement is
explicitly satisfied; older runtimes do not receive this option.

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

6. Disable after reviewing the warning and confirming. Approve Windows UAC
   if requested; keep the cable connected while the script completes:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Disabled
   ```

7. Reenable later with the same confirmation/UAC flow, even while the disk is absent:

   ```powershell
   .\tools\stlink-mass-storage.ps1 -Action Enabled
   ```

For multiple probes use `-SerialNumber <24-hex-ST-Link-serial>` on each
command. A switch starts only from a healthy ST-Link/V2-1 application-mode
identity, `0483:374B` or `0483:3752`. Recovery is bound to that exact serial
and physical USB location. An already-enumerated `3748` device without this
in-process context is not selected for programming.
The script serializes its operations per probe and refuses to start another
vendor process or USB restart while a possible updater/debugger is active.
Do not start another probe tool during the operation.

Before launching the updater it verifies the Java runtime, signed package,
device identity and USB location. The normal path uses one vendor process.
A specifically recognized opening/loader-entry failure before programming
allows one continuation from the stable loader. This is the only additional
vendor invocation allowed.
ST's `-checkParam` option itself enters USB-loader mode; it must not be used
as a read-only preflight or in `-WhatIf`.
Afterward it requires two consistent observations of the **same probe serial**
with the requested MSC presence/absence, working debug and serial interfaces,
and healthy drivers. An updater exit code alone is not sufficient.

If already in the requested healthy state, the tool performs no firmware write.
The vendor programming process has no automatic kill timeout: terminating it,
pressing Ctrl+C, closing the terminal or unplugging USB mid-update can leave the
probe requiring ST's recovery procedure. Verification has a finite timeout.
If verification fails, the tool does not repeatedly reflash or claim success.
An unknown error or missing device stops the operation; it is not converted
into a success result or an unrestricted programming retry.

### USB-loader transition errors

`JNI error returned by system: 433` means Windows reports that a device no
longer exists. Around `GoToUsbLoader`, the ST-Link can disconnect and
re-enumerate under a different USB identity. This error does not by itself
prove that the hardware is too old, that Java failed to load its native
driver, or that an update completed.

The script handles recognized transitions after the vendor process exits:

1. It waits for the selected serial at the same physical USB location.
2. If enumeration needs help, it restarts only that device through Windows,
   with at most two restart requests per transition and a 45-second deadline.
3. A known opening/`GoToUsbLoader` failure, with no evidence of programming,
   permits one continuation from verified PID `3748`.
4. An explicit programming-success result ends all flashing. Only return
   to the requested application state is then allowed.
5. Success requires healthy debug/VCP and the requested MSC state, observed
   consistently twice. A nonzero vendor exit during loader exit can therefore
   be recovered without falsely treating the exit code itself as success.

If ST reports **"Upgrade is successful" followed by "Failure exiting upgrade
mode"**, programming may have completed even though the process exits with
an error. The script completes USB re-enumeration automatically and verifies
the actual application interfaces. It does not reprogram to leave loader mode.

Scoped Windows `pnputil /restart-device` operations complete re-enumeration
without a physical reconnect on the tested board. They restart only the
**exact selected ST-Link instance**, not a hub or an unrelated USB device.
They do not hide or disable the mass-storage interface in Windows.

The instance ID changes during loader entry/exit. If a restart races that
change, Windows may report "The device is not connected." The script
re-enumerates before choosing another instance. PnPUtil can return zero even
after an error, so only the observed USB state proves completion.

Unknown errors, evidence of partial programming, changed package hashes or
a changed serial/physical location are not automatically recovered.
If the operation stops, inspect its report rather than blindly rerunning a
write. A device already in loader mode at the start requires separate
diagnosis because PID `3748` is also used by other ST-Link variants.

The JVM's native-access deprecation warning is separate from a Windows USB
error. Suppressing that warning alone cannot restore a disconnected USB
handle.

### Output and diagnostic logs

After confirmation, every actual switch creates a private diagnostic log:

```text
%LOCALAPPDATA%\MXCHIPTest1\Logs\STLink\<operation-id>\operation.log
```

The log is retained on both success and failure. It records timestamps,
stages, USB observations, authenticated-package hashes, Java/vendor output,
and Windows restart results. Its directory is restricted to the current
Windows user. It contains device serials and local paths, not STSAFE key
material or application data; the ACL is not encryption.

For example, a successfully recovered operation normally shows:

```text
Diagnostics: C:\Users\...\MXCHIPTest1\Logs\STLink\...\operation.log
Enabling mass storage...
Waiting for Windows authorization...
Completing the USB transition...
Reconnecting ST-Link...
Verifying the requested USB state...
Verified: mass storage enabled. Serial: USB Serial Device (COM8).
```

The diagnostics location is printed at the beginning, not among the progress
messages. The green **Verified** line is the final success message. The
shared [output helper](../tools/BoardToolOutput.ps1) also supplies the same
green, final verification message for board-image creation, Wi-Fi credential
configuration and supplied-key setup. Their success messages are emitted
after cleanup, never before a serial-close or other cleanup error.

Only the stages needed for that operation appear. Raw messages such as
`JNI error 433`, failed loader-exit messages and transient PnP restart
errors are written to the log rather than displayed during successful
recovery.

To display those details as well, use PowerShell's standard `-Verbose` flag:

```powershell
.\tools\stlink-mass-storage.ps1 -Action Disabled -Verbose
```

Verbosity is carried into the elevated worker. Its temporary console/result
files are removed after the parent reads them, but the diagnostic log
remains. Success still requires verification of the actual USB interfaces.

If recovery is exhausted, unavailable or unsafe, the command fails
explicitly and reports the **stage, last observed USB state, reason and
diagnostic-log path**. An uncertain programming result is never hidden or
retried just to obtain a clean-looking result. A diagnostic-log write failure
is also reported, rather than silently proceeding without diagnostics.

`Status`, `-WhatIf`, an already-correct state and a declined confirmation
do not create switch-operation logs. Missing prerequisites are reported
before confirmation. Once an operation is complete, its retained diagnostic
directory may be removed if it is no longer needed; logs are not
automatically pruned.

### Credential and key tools

[configure-wifi.ps1](../tools/configure-wifi.ps1) requires confirmation before
requesting credentials or opening the serial connection and supports
`-WhatIf`. It checks the console identity and each SDK save acknowledgement,
reports the failed stage, and does not resend uncertain credential writes.
Saving SSID and password consists of separate writes; a failure may leave
partially updated values. Reboot is only requested, not verified.

[provision-stsafe.ps1](../tools/provision-stsafe.ps1) verifies mode identity,
device UID, physical authorization and post-install key/protection state.
It does not retry an uncertain key write or add key-setup recovery; the
one-shot workflow and original EEPROM behavior are described in the
[supplied-key guide](LEGACY-PROVISIONING.md).

Unlike ST-Link diagnostics, these serial tools **do not retain raw protocol
logs or expose them with `-Verbose`**, because the console/protocol can carry
passwords or host keys. Errors identify the operation stage without printing
secret input. PowerShell tracing/transcripts and external serial captures
should not be used for credential entry. Physical Reset/Button A prompts
remain required by the board's console/key-setup protocols.

### OpenOCD compatibility with the no-MSC personality

The mass-storage switch uses **STLinkUpgrade, not OpenOCD**. It does not
select an OpenOCD version or change the Arduino upload configuration.
The saved [xPack 0.12.0-7 native ST-Link/SWD configuration](../tools/platform.local.txt)
has been verified with the **PID `3752`** no-MSC personality.

For installations using the AZ3166 core's bundled OpenOCD **0.10**, the
no-MSC USB endpoints are not handled correctly. Merely changing its VID/PID
filter is insufficient. This compatibility limitation applies only when
that older executable is actually selected, not to the configured xPack 0.12
uploader.

- The ST-Link hardware still provides SWD and VCP.
- Uploads using OpenOCD 0.10 fail with the no-MSC personality until MSC is
  reenabled, or you configure a newer compatible OpenOCD/other supported
  programmer.
- `build-board-image.ps1` detects this case and requires a modern
  `-OpenOcdRoot` (0.11+ with HLA), rather than trying the wrong endpoints.
- Reenablement uses ST's updater and the exact probe serial, **not OpenOCD**,
  so it does not depend on the Arduino upload recipe or disk visibility.
- A changed USB PID may require the appropriate ST WinUSB driver binding on
  Windows. Driver errors cause verification failure, not success.

Turning MSC off is protection against accidental drag-and-drop writes. It is
not an authentication boundary or a replacement for key backups, RDP or
secure firmware updates. It can be reenabled by someone with the necessary
physical/software access. Nothing here changes STSAFE access policy.

Vendor references: [STSW-LINK007](https://www.st.com/en/development-tools/stsw-link007.html)
and [ST-Link firmware release notes RN0093](https://www.st.com/resource/en/release_note/rn0093-firmware-upgrade-for-stlink-stlinkv2-stlinkv21-and-stlinkv3-boards-stmicroelectronics.pdf).
The bundled Java updater is checked for the required CLI options; the script
uses `-msvcp -dynOpt mscOffOpt` / `mscOnOpt`, not `-no_msd`.

## Validation performed during development

- Synthetic byte-for-byte tests preserve the entire 48 KiB prefix and host-key
  sector, verify application vectors/size, and reject inconsistent reads.
- The full image-creation flow is tested with a simulated OpenOCD, including
  private output ACLs, error recovery, cleanup, and `-WhatIf`.
- Tests verify exact reversible updater arguments, JVM-only inspection,
  UAC handoff/cancellation, safely encoded paths, native-access flags,
  loader continuation, bounded exact-device restarts, stale-instance races,
  unknown-outcome refusal, requested USB states and idempotence.
- Output tests verify quiet successful recovery, private retained logs,
  `-Verbose` propagation through UAC, terminal stage/state summaries and
  explicit diagnostic-write failures without opening a probe.
- The inspected ST-signed JAR/driver are checked statically; no updater firmware
  operation is run for the tests.
- Live Windows USB status was read without changing it.

The **fully scripted MSC round trip** was verified using STSW-LINK007
3.17.11 and ST-Link firmware V2J48M35:

| Verified state | USB PID | MSC interface | VCP on the test PC | Debug/VCP driver health |
| --- | --- | --- | --- | --- |
| Initial Disabled | `3752` | Absent | COM14 | Healthy |
| Enabled | `374B` | Present | COM8 | Healthy |
| Final Disabled | `3752` | Absent | COM14 | Healthy |

The same probe serial was checked at each stage with repeated enumeration.
Final native OpenOCD SWD examination of the STM32F412 also succeeded,
without target reset, halt or programming. No application/STSAFE data,
keys, protection settings, or driver bindings were changed by the test.

The normal `Enabled` and `Disabled` commands were run from a non-elevated
shell. After UAC approval for each operation, the script completed recognized
opening errors and exit-upgrade error 1 using its bounded transition handling.
The AZ3166 disk appeared as D: when enabled and disappeared when disabled.
No physical reconnect, separate elevated shell or manual recovery command
was needed. UAC/operation confirmations are the only required interaction
after prerequisites are installed; rejecting a prompt cancels the operation.

Live target-flash image creation/restoration still requires
operator-controlled hardware qualification. Host tests cannot establish
that a hardware operation has succeeded.

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
