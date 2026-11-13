# Native compatibility analysis

## Verified input

The original `nsp9kw-202005261414.zip` package was unpacked without running its
historical installer. Its inner ZIPs contain DAT archives consisting of
null-terminated names, 32-bit lengths, timestamps and file contents. The second
16-bit header value is not a reliable file count. Extraction continues to the
validated archive end. `extract_xeltek.py` checks lengths and destination paths
and writes a SHA256 manifest.

| File | SHA256 |
| --- | --- |
| `SP9000.EXE` | `4e74dd1e2573f13f12891eeab5a1285f4f5b837af6d522a438423c7093d34997` |
| `XEUSB.SYS` | `c34392c3c466dddb7f0a29b13251939331444f4b95365d000ff96ee921cbdcf7` |

Both PE machine values are `0x014c` (x86). The official
[Xeltek download](https://www.xeltek.com/software/sp9000u/nsp9kw.exe) contained
byte-identical application and driver files. The original INF specifies
January 1, 2002 and USB IDs `0547:1002` and `6022:0001`.

64-bit Windows cannot run a 32-bit kernel driver. WOW64 supports applications,
but does not translate kernel drivers. See
[Microsoft's driver architecture documentation](https://learn.microsoft.com/en-us/windows/win32/winprog64/drivers).

## Reconstructed interface

The application has four direct DeviceIoControl call sites at virtual addresses
`0x4266db`, `0x427a4b`, `0x427ac0` and `0x427b51`, with image base `0x400000`.
The first site chooses between two IOCTL values, giving five operations in total.
The application opens `\\.\Xeltekusb-0`.

| IOCTL | Operation | XEUSB.SYS evidence, image base 0x10000 |
| --- | --- | --- |
| 0x22204e / 0x222051 | Bulk/interrupt transfer; 32-bit input pipe index | Dispatcher 0x10e68, function 0x10aae |
| 0x222014 | Vendor control request with ten parameter bytes | Dispatcher 0x10c9e, function 0x112ae |
| 0x22206d | 64-byte firmware chunks using vendor request A0 | Dispatcher 0x10da9, function 0x121d6 |
| 0x222059 | Class/vendor control transfer with ten setup bytes | Dispatcher 0x10e73, function 0x111a4 |

The original application supplies the commands and firmware. The adapter only
translates transport operations; it does not invent firmware addresses or device
responses. Other IOCTLs from the historical universal driver fail explicitly
with `ERROR_NOT_SUPPORTED`.

## Implementation

`sp9000-win11.exe` changes the KERNEL32 import-library name and the PE checksum.
Its code, resources, algorithms and firmware are preserved. `xusb32.dll` supplies
all 146 required exports: 143 forward directly to KERNEL32; CreateFileA,
CloseHandle and DeviceIoControl handle connections using the legacy device name.
Other file handles and operations are forwarded to Windows.

The x86 bridge uses WinUsb_Initialize, WinUsb_QueryPipe,
WinUsb_ControlTransfer, WinUsb_ReadPipe and WinUsb_WritePipe. Pipe order comes
from the first interface, matching the original driver's selection. USB IDs and
device descriptors are checked before a connection is accepted. Alternative
settings and multiple-interface devices need further hardware validation.

WinUSB limits control transfers to 4096 bytes. Larger individual requests fail
explicitly rather than being split without evidence that the protocol permits
it. Both supplied Intel HEX files contain at most 16 data bytes per record; the
additional loader block visible in the application is `0xD80` (3456) bytes.
See [WinUsb_ControlTransfer](https://learn.microsoft.com/en-us/windows/win32/api/winusb/nf-winusb-winusb_controltransfer).

## Setup corrections

The x64 helper uses SetupAPI and DiInstallDevice with the existing Microsoft
WinUSB INF. On November 13, 2026, installation initially failed with error 87
because a global driver-list node was passed for a specific device. Selection
now builds, enumerates and reads the target device's own driver list.

`SetupDiOpenDevRegKey(... DIREG_DEV ...)` already returns the hardware parameters
key. The interface GUID is written directly there rather than creating another
`Device Parameters` child key. A subsequent
`DIF_PROPERTYCHANGE / DICS_PROPCHANGE` restarts only that device so WinUSB
publishes the interface without a computer reboot.

The corrected path was tested on the physical device. The regression test also
checks driver-list ownership using an unregistered in-memory device element;
administrator privileges are required to create that element.

[WinUSB API](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/using-winusb-api-to-communicate-with-a-usb-device),
[driver selection](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdisetselecteddriverw),
[driver installation](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-diinstalldevice).

## Logging

Setup, launchers and the bridge share `native/src/log.c`. Tests use that logger
as well; the Python build logger follows the same file-locking convention.
All append to `Windows11-Native/SP9000U/bin/superpro9000u.log`, regardless of the
working directory. Entries use UTF-8, UTC timestamps and explicit severity.
Windows last-error values are preserved across logging calls.

Each writer locks byte zero before appending and releases the lock after the
complete record has been written. The build checks 600 records from six
simultaneous x86/x64 workers for loss, duplicates and interleaving. Log failures
do not produce a fallback file elsewhere. Setup and launchers stop before making
changes when they cannot open the shared log at startup.

## Build

Use Python with `pefile` and LLVM-MinGW 20261104:

```powershell
python native/build.py --toolchain Analysis/toolchain/llvm-mingw-20261104-ucrt-x86_64
```

Close the programmer application first. Compilation uses `-Wall -Wextra -Werror`.
Required tests and export checks stop the build on failure. Builds do not install
drivers or perform chip operations. The optional administrator-only regression
test returns 77 if skipped.

## Hardware verification and limits

The November 13, 2026 test on Windows 11 Pro x64 build 26200 verified Microsoft
WinUSB installation, x86 bridge open/close and descriptor access. Endpoint order
was `0x02`, `0x82`, `0x04`, `0x84`, `0x01`, `0x81`, all bulk endpoints.
The application uploaded firmware and exchanged data through IOCTLs `0x22204e`
and `0x222051`. Its interface showed `Ready` without Demo mode, module 1 green
and modules 2 through 4 inactive. No chip operation was triggered.

Still unverified: physical hardware using ID `0547:1002`, an ID change during
firmware startup, modules 2 through 4, long-running operation, and actual chip
read/program/erase/verify behavior.
