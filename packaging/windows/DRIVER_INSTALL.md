# Getting a Zune onto WinUSB on Windows

Short version: **use Zadig**.  Do not hand-write an INF and do not install the
Zune software - this is a solved problem upstream, and the in-box pieces are not
quite enough on their own.

Nothing in Zuuned installs a driver.  Until a Windows installer exists, binding
is a one-time manual step per machine, done by whoever is testing hardware.

**Status: written 2026-10-20, not yet run against hardware.**  The development
machine still reports problem 28.  Whoever runs this first should put the real
output here and say so in the commit.

## What has to be true afterwards

Both of these, not just the first.  Measured on the development machine with a
Zune HD attached (`check-driver.ps1`):

```
devnode : USB\VID_045E&PID_063E\ae55b7c9-1b54-40e4-aa77-0264d29f3080
status  : Error  problem=CM_PROB_FAILED_INSTALL
hwids   : USB\VID_045E&PID_063E&REV_0100 USB\VID_045E&PID_063E
compat  : USB\MS_COMP_MTPZ USB\Class_FF&SubClass_FF&Prot_FF USB\Class_FF&SubClass_FF USB\Class_FF
children: none - single function
```

1. `winusb.sys` is the devnode's function driver.  Windows 11 has no driver for
   `USB\MS_COMP_MTPZ`, which is why the devnode currently has no driver at all
   rather than a wrong one, and why `libusb_get_device_list()` never contains the
   player.
2. The devnode registers a device interface GUID, as `DeviceInterfaceGUIDs` or
   `DeviceInterfaceGUID` under its `Device Parameters` key.

The second is not cosmetic, and it is the reason the obvious free route is not
enough.  In libusb 1.0.29 the WinUSB path in `libusb/os/windows_winusb.c` reads
those two values and returns `LIBUSB_ERROR_ACCESS` when neither exists: a player
bound to WinUSB without a registered GUID enumerates and still cannot be opened.

## Route A - Zadig

Zadig, by Pete Batard, upstream of libwdi: <https://zadig.akeo.ie>, or
`winget install akeo.ie.Zadig` - the Microsoft package repository carries the
upstream binary under the publisher `akeo.ie`, so there is nothing to build and
no INF to write.  The development machine already has 2.8 at
`D:\dev\sdrsharp-x64\zadig-2.8.exe`.

Run it as administrator with the player attached:

1. Options, then enable **List All Devices** - the devnode has no driver, so
   without this the player is not offered.
2. Select the Zune (`VID_045E&PID_063E` on this machine, `VID_045E&PID_0710` for
   the Classic/Pad family).
3. Select **WinUSB** in the driver box - not libusbK, not libusb-win32, and not
   the generic/WCID install.
4. Install Driver, then unplug and replug.

Step 3 matters and is verified in upstream source rather than guessed: libwdi's
`libwdi/winusb.inf.in` has an `AddDeviceInterfaceGUID` section and a
`NoDeviceInterfaceGUID` one, and `libwdi.c` picks `NoDeviceInterfaceGUID` only
when `options->use_wcid_driver` is set.  A normal named WinUSB install therefore
registers the GUID that libusb needs; the generic WCID install does not.

If Windows refuses the package over signing, that is Windows' rule on third-party
packages, not something this app can code around.  libwdi self-signs, and the
community Zune installer documents the fix: reboot once with driver signature
enforcement disabled (Shift + Restart, Troubleshoot, Advanced options, Startup
Settings, Restart, then F7), install, reboot normally.  The binding survives.
Unverified on this machine - note in the commit whether it was needed.

## Route B - the community Zune installer

<https://github.com/magicisinthehole/zune-winusb-driver-setup> wraps libwdi for
exactly these two device IDs (`aft-driver-setup.exe list | install | restore |
check`, with a driver backup under `%LOCALAPPDATA%`) and is part of the
Android-File-Transfer-for-Linux Windows port.  That is the closest thing to a
ready-made Zune driver tool.  It is a third-party executable: it has not been
vetted or run here, and it is not a Zuuned release dependency.  Building it from
source is documented in that repository if you would rather not download a binary.

## Route C - Device Manager alone is not enough

Microsoft documents loading WinUSB through the class picker: Update driver,
Browse my computer, Let me pick from a list, **Universal Serial Bus devices**,
**WinUsb Device**.  That installs the in-box driver and no package at all, which
is why it is tempting - and it registers no device interface GUID, so per the
section above the app still cannot open the player afterwards.  If you end up
here, add the GUID as administrator and replug:

```
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_045E&PID_063E\<serial>\Device Parameters" /v DeviceInterfaceGUIDs /t REG_MULTI_SZ /d "{<your-guid>}" /f
```

Generate the GUID with `powershell -NoProfile -Command "[guid]::NewGuid().Guid"`.
This is also the only route whose package is signed by Microsoft, so it is the one
to prefer if Secure Boot refuses Zadig's self-signed package.

## Verifying, in this order

1. `powershell -NoProfile -File packaging\windows\check-driver.ps1` - expect
   `status: OK`, a `driver`/`service` value, and an `ifaces` line naming a GUID.
   Read-only script: it installs nothing and needs no elevation.
2. `build\devicetool.exe` - expect `libusb_sighted=1` and `VERDICT=agree`.  It
   opens no handle by design, so this proves reachability only, not MTPZ auth.
3. `build\zuuned.exe` - breach and MTPZ authentication run for the first time on
   Windows here.  `zunetool` is built by libzune's POSIX Makefile and is not part
   of the Windows build, so the application is the protocol test vehicle until a
   Windows probe target exists.  A failure at this step is a protocol or driver
   question, not an enumeration question - keep the log.

## What Windows cannot do that Linux can

Record this before a test so a Windows failure is not filed as a libzune bug.

- **No device reset.**  WinUSB cannot issue a port reset, so
  `libusb_reset_device()` always fails and `zune_breach()`'s auto-reset-and-retry
  after an OpenSession wedge cannot recover.  On Linux that wedge needs
  `usbreset 045e:0710`; here the recovery is an unplug and replug.
- **Nothing to detach.**  `libusb_kernel_driver_active()` returns
  `LIBUSB_ERROR_NOT_SUPPORTED` on Windows, so libzune's detach step is skipped.
  Correct, not a bug.
- **No udev counterpart.**  `packaging/68-zuuned.rules` keeps gvfs-mtp and libmtp
  off the device and grants the session access to the node.  Being the function
  driver answers both on Windows.  Do keep the Zune software absent.

## Backing out

Device Manager, the Zune entry, Update driver, and pick something else, or uninstall the
package's published `.inf` from the driver store (`pnputil /enum-drivers` to find
it).  After a replug the player returns to problem 28, because nothing in Windows
claims `USB\MS_COMP_MTPZ`.
