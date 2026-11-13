# SUPERPRO 9000U for Windows 11 x64

## Required files before first launch

The GitHub download includes only the custom compatibility tools. Download the
original Xeltek package to `../downloads/nsp9kw.exe`, then run
`../Prepare-Software.cmd` from the project root. Install Python 3.10 or newer
first if necessary. The project-root `README.md` contains direct download links
and the complete file-by-file placement table.

Preparation places the original application and firmware in `SP9000U/bin/`,
all device libraries in `SP9000U/lib/`, and all algorithms in `SP9000U/algo9/`.
It creates `SP9000U/bin/sp9000-win11.exe` beside the supplied `xusb32.dll`.
Keep `Setup-USB.exe`, `XeltekUsbSetup.exe` and `Start-SUPERPRO.exe` in this folder.

This package connects the original SUPERPRO 9000U application to Microsoft's
WinUSB driver directly on Windows 11. No virtual machine is required.

## Start

1. Insert a CF card in module 1 before switching on the programmer.
2. Connect power and USB. Disconnect other Xeltek programmers for setup.
3. Run `Setup-USB.exe` and accept the Windows administrator prompt.
4. Run `Start-SUPERPRO.exe` normally.
5. Check for `Ready` without `Demo mode`.

Setup registers the WinUSB interface and restarts only the matching device.
Reconnect its USB cable if setup reports that the device restart failed.
If firmware startup changes the USB ID, close the application and run setup
again for that device state.

## Shared log

Setup, launchers, USB communication, warnings, errors, builds and tests append to:

```text
SP9000U/bin/superpro9000u.log
```

This is the only log file created by the custom tools. It is UTF-8 and uses UTC
timestamps, severity and component names. The folder must be writable. No
firmware or chip contents are recorded. Windows maintains its own system logs;
the original application's on-screen messages are not captured as text.

## Diagnostics

```powershell
.\XeltekUsbSetup.exe --list
.\XeltekUsbSetup.exe --check-driver
.\XeltekUsbSetup.exe --check-device-driver
```

These checks do not install a driver. Installation requires administrator
privileges; `Setup-USB.exe` requests them automatically. The helper uses the
Microsoft WinUSB driver already included with Windows.

## Verified behavior

On November 13, 2026, a physical 9000U on Windows 11 Pro x64 build 26200 passed
driver installation, USB descriptor access, firmware upload, bulk communication
and application initialization. The application reported `Ready` without Demo
mode and module 1 was green. Modules 2 through 4 and chip read, erase, program
and verify operations remain untested.

`build-report.json` describes software tests. `hardware-verification.json`
preserves the earlier physical-device results separately from rebuilds.

This is an experimental compatibility layer. Validate operations using a
suitable test chip before relying on it for programming work.
