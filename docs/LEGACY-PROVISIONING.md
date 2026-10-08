# Supplied STSAFE host keys with the original EEPROM format

## Scope and validation status

Mode **15** installs **your supplied host keys** for the AZ3166 core 2.0.0
legacy encryption path. It is a level-2 equivalent in its **key source**, not
an implementation of every operation in the old SDK's dormant level-2 code.
It does not convert stored data or enable flash protection.

**Key personalization and subsequent encrypted EEPROM access remain
unqualified on hardware.** On 2026-10-08, the mode-15 application was flashed,
readback-verified and booted to validate the native ST-Link upload recipe;
its startup reported no host/envelope keys, an empty host-key sector, RDP0
and PCROP off. No keys were installed and no protection settings were changed.
The setup implementation is checked with host models and embedded builds.
First personalization still requires qualification on a board whose contents you do not
need to retain. The host tool requires `-AcknowledgeUnvalidatedHardware` before
writing keys.

The project no longer implements:

- Random host-key generation / the level-3 workflow.
- Application-data export, import, conversion, copying or migration.
- Provisioning journals, completion records or interruption protection.
- Recovery archives, backup requirements, resume, rollback, host-key
  restoration or recovery-image generation.
- RDP1 arming, cancellation or boot-time protection changes.

The separately requested [board-maintenance tools](BOARD-MAINTENANCE.md)
remain available. Their same-board STM32 flash image and ST-Link mass-storage
switch are independent of key setup.

## 1. What "setup" writes

Here, provisioning means only these operations:

1. If necessary, ask the STSAFE to create an **AES-128 local-envelope key in
   slot 0**. A compatible existing key is retained.
2. Install the supplied **16-byte MAC key and 16-byte cipher key** in the
   STSAFE host-key slot.
3. Verify the host keys through an authenticated, encrypted wrap/unwrap
   exchange on a fixed test block held only in RAM.
4. Erase STM32 **sector 2 only** and write/readback-check the **88-byte legacy
   executable key loaders**.

The new setup path does **not** read or write STSAFE application-data zones,
initialize their contents, alter their access policies, overwrite existing
host keys, replace the envelope key, or program option bytes. Booting mode 15
or requesting status does not perform any of these key/flash writes.

### Two different kinds of key

| Key | Source and purpose |
| --- | --- |
| Host MAC key | Supplied by you; authenticates host/STSAFE communications. |
| Host cipher key | Supplied by you; encrypts protected host/STSAFE payloads. |
| Local-envelope key | Generated inside this STSAFE, non-exportable; wraps the application data used by the SDK's legacy EEPROM path. |

Generating the missing local-envelope key is **not** random host-key / level-3
support. It is required for legacy, chip-bound data encryption even when you
supply both host keys.

**Using the same supplied host keys on two boards does not make their existing
envelopes portable.** Each STSAFE has its own local-envelope key. STM32 flash
images do not contain that key or the STSAFE's EEPROM contents. A flash image
from board A plus the same host keys on board B cannot decrypt A's envelopes.
There is no cross-board data-transfer workflow in this project.

## 2. Architecture

```text
PowerShell 7.2+ host tool
  supplied keys -> local validation -> typed board-UID confirmation
                        |
                        | trusted USB serial, 115200 baud
                        v
Mode 15 / SecureProvisioningMode
  bounded input + one-use physical Button A authorization
                        |
                        v
SuppliedKeySetup
  key-presence queries and basic preconditions
  create missing envelope key -> install supplied host keys
  protected RAM-only proof -> write legacy STM32 key loaders

Ordinary application access (unchanged):
  SDK EEPROMInterface -> SDK STSAFE HAL -> original legacy envelopes
```

Source responsibilities:

- [SuppliedKeySetup.cpp](../src/SuppliedKeySetup.cpp): STSELib key operations,
  RAM-only verification and sector-2 flash programming.
- [HostKeyBlock.h](../src/HostKeyBlock.h): supplied-key validation, legacy
  Thumb key-loader encoding.
- [SensitiveMemory.h](../src/SensitiveMemory.h): clearing of project-owned
  key and credential buffers, shared with the serial input code.
- [SecureProvisioningMode.cpp](../src/SecureProvisioningMode.cpp): USB input
  parsing, status output and physical authorization.
- [provision-stsafe.ps1](../tools/provision-stsafe.ps1) and
  [SuppliedKeySetup.psm1](../tools/SuppliedKeySetup.psm1): local prompts,
  serial transport and the supplied-key-only workflow.
- [verify-legacy-link.ps1](../tools/verify-legacy-link.ps1): checks that the
  original core EEPROM implementation is linked, without a project
  replacement or interception.

The full pinned STSELib middleware and mode-14 diagnostics remain. In
particular, its generic RNG APIs and diagnostic RNG test are not a random
host-key provisioning workflow. No upstream middleware or globally installed
Arduino core files are changed.

### Original key-loader ABI

The supplied bundle is **MAC first, cipher second**. The executable getters
have the opposite order:

| Address | Contents |
| --- | --- |
| `0x08008000` | 44-byte Thumb cipher-key getter; callable pointer `0x08008001`. |
| `0x0800802C` | 44-byte Thumb MAC-key getter; callable pointer `0x0800802D`. |
| `0x08008058` through `0x0800BFFF` | Left erased; no project metadata or journal. |

The bootloader below sector 2 and the application at `0x0800C000` are not
written by key setup. Sector 2 must initially be uniformly `FF` or uniformly
`00` (an empty factory/reference-image representation); other contents are
rejected. The loader layout is the original SDK layout, not a new key-storage
or EEPROM format. The legacy envelope slot, zone layout, 480-byte maximum
plaintext block and 8-byte envelope overhead remain unchanged.

### Original EEPROM behavior, including its drawbacks

There is **no local EEPROM compatibility layer**. The original core detects
the key-loader marker and selects its existing encrypted read/write path.
It does not know about the host tool or Button A authorization.

**Existing plaintext is not converted.** Setup leaves the data-zone bytes
alone, but once the loaders are installed the core interprets those bytes as
encrypted envelopes. Old plaintext may therefore become unreadable; it is
not promised to survive subsequent normal access. The core can write
replacement zero-filled envelopes after a failed decrypt. Even a legacy
read is not guaranteed to be nonmutating in that situation.

The host tool's "application data not modified" message describes the setup
code's operations, **not** preservation or readability of pre-existing data
under the SDK's later behavior. Save new application values through the
ordinary SDK interfaces when needed; this workflow does not initialize or
reencrypt all zones on your behalf.

The built-in console remains unchanged: `enable_secure 1` is still its
historical operation, and levels 2/3 are still rejected. Do not use that
separate level-1 operation as a step in supplied-key setup; it uses its own
keys and conversion behavior. `ProvisionSupplied` does not call it.

## 3. Preconditions

Use this workflow only when:

- The STSAFE host-key slot is **not provisioned**.
- The reserved STM32 host-key sector is empty as described above.
- STM32 protection is **RDP Level 0**, PCROP is not selected, and sector 2
  is not write-protected. The firmware checks protection before key writes;
  the tool does not disable or enable it.
- Local-envelope slot 0 is empty or already has a compatible AES-128 key.
- You can accept the lack of data conversion and the possibility of
  incomplete, unusable state if setup fails.
- You have two different, high-entropy 16-byte keys from your own key
  management process. All-zero, all-`FF` and identical MAC/cipher keys are
  rejected.

An already-provisioned chip is rejected even if you know its keys. There is
no rekeying or host-loader restoration action. A status query reports key
presence, not proof that a previously provisioned host/chip pair still matches.

## 4. Detailed how-to

These instructions require **explicit authorization for hardware writes**.
Application-only upload and mode-15 startup have been tested as described
above; the key-installation procedure has not been performed.

### A. Compile and preview without touching the board

From the repository root, use PowerShell 7.2 or later:

```powershell
.\tools\build.ps1 -Mode 15 -BuildDirectory _build\supplied-keys
.\tools\provision-stsafe.ps1 -Action ProvisionSupplied -Port COM8 -WhatIf
```

The build creates an application-only binary. `-WhatIf` opens no serial port,
prompts for no keys and sends no commands. The source default stays at mode 3.
Run builds sequentially because the staging directory is shared.

### B. When authorized, upload the mode-15 application

Use the connected board's actual port instead of assuming the example:

```powershell
arduino-cli board list
arduino-cli upload --port COM8 --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 --input-dir _build\supplied-keys .
```

This is an actual firmware write. Use Arduino CLI/OpenOCD at the application's
`0x0800C000` address; **do not drop the application-only binary onto the
AZ3166 virtual disk**. Do not upload the full factory image. For newer
OpenOCD configuration, see [board maintenance](BOARD-MAINTENANCE.md).

### C. Inspect the initial state

Close other serial monitors and run:

```powershell
.\tools\provision-stsafe.ps1 -Action Status -Port COM8
```

After the host tool opens the port, press Reset **without holding A or B**.
The tool waits for the exact `MXCHIP_SUPPLIED_KEYS_MODE15_V1` banner before
sending any commands. A different application or older provisioning build
will time out without receiving a setup command.

Status fields:

| Field | Meaning |
| --- | --- |
| `Uid` | STM32 identity used in the typed confirmation and checked again before setup. |
| `Rdp` | Current readout-protection level, read only. |
| `Pcrop` | Current PCROP selection, read only. |
| `HostKeysPresent` | Whether the STSAFE reports installed host keys. Must be false initially. |
| `EnvelopeKeyPresent` | Whether compatible local-envelope slot 0 exists. Either initial value is allowed. |
| `HostFlashEmpty` | Whether sector 2 is an accepted empty representation. Must be true initially. |

There are no pending-transaction, completion-digest or recovery-state fields.

### D. Install your supplied keys

```powershell
.\tools\provision-stsafe.ps1 -Action ProvisionSupplied -Port COM8 -AcknowledgeUnvalidatedHardware
```

1. Confirm the PowerShell operation. Reset the board as prompted so the tool
   can identify the correct mode.
2. Enter **64 hexadecimal digits**, with no spaces or prefix:
   **32 digits of MAC key, then 32 digits of cipher key**. Input is hidden;
   keys are not command-line arguments or saved files.
3. Read the warning that existing data is not converted and failures have no
   rollback or repair.
4. Type the exact `SETKEYS <Uid>` phrase shown for this board.
5. Press and release **physical Button A**, then press Enter on the host
   within **60 seconds**. Serial text cannot emulate Button A. The
   authorization permits only one setup request.
6. Keep power and USB connected until a success or explicit error is reported.

Success means the RAM-only protected proof passed, the legacy loader bytes
were readback-verified, and the final status showed both key objects present
with host flash no longer empty and RDP/PCROP unchanged.

Button B or `SK2 ABORT` only cancels transient authorization/input. It does
**not** undo persistent writes, and the synchronous key-write sequence is not
interruptible by a queued button/serial command.

### E. Return to an ordinary application

After successful setup, compile the intended mode and, when authorized,
upload its **application-only** image. For example, mode 5 uses stored Wi-Fi
credentials; mode 8 can save new credentials using the original SDK path.
Pre-setup plaintext credentials have not been migrated and must not be
assumed readable. See the [Wi-Fi instructions](../README.md#operating-modes).

Normal application-only updates preserve the host-key sector. The firmware
does not set RDP or PCROP on the next boot. There is no lock request hidden
in the application or key sector.

## 5. Failure and security boundaries

If setup fails, the error identifies the failed stage. Stop: the STSAFE might
already have an envelope key or host keys, or the STM32 loader might be
partially written. The tool does not journal, roll back, resend an uncertain
mutation, repair flash, resume an operation or restore anything from a file.
Do not assume repeating the command is a recovery procedure.

Status is available to inspect reported state, but is not a recovery tool.
Do not infer completion solely from presence flags, reset into normal
encrypted reads after a partial failure, or erase protection/flash as a
generic fix. There is no project-supported reversal of personalization.

The USB protocol deliberately has only `SK2 STATUS`, `SK2 SET <Uid> <keys>`
and `SK2 ABORT`. Old `SP1` commands are unsupported. Provisioning errors are
returned as `SK2 ERR <code>`, not success-shaped fallback results.

Use a trusted local machine and physical connection:

- The initial keys traverse USB serial and the initial STSAFE provisioning
  command in plaintext. This is not remote or authenticated-USB provisioning.
- Key input is not echoed or intentionally persisted. Project-owned byte
  buffers and the host BSTR are cleared, but .NET/PowerShell immutable strings
  and operating-system buffers cannot be guaranteed erased. Do not use
  tracing, transcripts, shared terminals or serial capture while entering keys.
- With protection unchanged at RDP0/no PCROP, the STM32 host-key loaders are
  readable. This is not secure boot, flash secrecy or tamper resistance.
- A missing acknowledgement does not establish whether a key write happened.
  No interruption protection is provided by design.

## 6. Tests

```powershell
.\tools\test-supplied-keys.ps1
```

The C++ tests cover supplied-key validation, the original loader ABI,
key-presence/protection preconditions, retaining an existing envelope key,
explicit partial failures, exact flash-write boundaries, physical
authorization, bounded serial input and rejection of removed commands.
The backend fixture provides no application-data or option-byte write APIs.
PowerShell tests simulate the complete supplied-key workflow, removed-action
rejection, `-WhatIf`, confirmation, errors, post-setup checks and connection
cleanup without opening a serial port.

Embedded builds additionally check original-core EEPROM linkage. These
checks do not replace real STSAFE personalization and subsequent legacy
EEPROM read/write qualification.
