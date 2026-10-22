> **Public source snapshot:** read [PUBLIC_SOURCE.md](PUBLIC_SOURCE.md) for credential requirements and differences from private builds.

# Zuuned Modern

**Bring your Zune back into the rebellion - on Windows.**

Zuuned Modern is a **Windows client port** of [Zuuned Linux](https://github.com/badgeromen/zuuned-linux),
the community app for managing Microsoft Zune players. It is the same Qt 6 / QML and
C++ application and the same [libzune](https://github.com/badgeromen/libzune) PTP/MTP/MTPZ
protocol stack, built and run natively on Windows with MSVC - no WSL, no MinGW, no
Cygwin, no virtual machine.

One source tree serves both platforms. The Windows work is additive: a
`compat/win/` layer that keeps the POSIX surface libzune and the transcode C layer are
written against, a Windows device-presence path, and native path and encoding handling
throughout. The Linux line is upstream's, and this repository exists to carry the port
until upstream wants it.

![The music library with album artwork and a connected player](docs/readme-screenshots/music-library.png)

*The interface is shared with upstream Zuuned Linux; the capture above is from the
upstream app.*

[Status of the port](docs/WINDOWS_PORT.md) - **read that page before trusting any
hardware claim in this repository.** It records, per capability, what has been done on a
Windows machine against a real player and what has only ever been done on Linux.

## What has actually been done on Windows

Hand-tested against one Pavo-generation Zune HD on the development machine:

- **Driver binding** - the player bound to WinUSB, read back as `CM_PROB_NONE` with an
  interface GUID ([`packaging/windows/DRIVER_INSTALL.md`](packaging/windows/DRIVER_INSTALL.md)).
- **Enumeration and hotplug** - unplug and replug produced the same read twice.
- **Reading the device** - `GetDeviceInfo`, the ZMDB scan, the device page browsed with
  album thumbnails fetched off the player.
- **Authenticated transfers, both ways, through the ordinary controls** - music sent to
  the player and a track pulled back into the local library, with byte readback.
- **Local library on a Windows disk** - watch folders, native `E:\...` path display,
  non-ASCII file names, album artwork.

**Not yet proven on Windows**, and not inherited from Linux either: deletion and purge,
the podcast, audiobook, photo and video transfer categories, every Zune model but that
one Pavo, audible playback (the automated gate opens WASAPI muted, so nothing has been
listened to by a test), and packaging - there is no Windows installer yet, so today the
port is a build-from-source project.

## Getting a Zune seen by Windows

libusb cannot see a Zune that has no driver bound, and Windows ships none for one. An
unbound player sits at PnP *problem 28* and appears nowhere. Binding is a one-time manual
step per machine and **nothing in Zuuned installs a driver**:

1. Plug the player in and run [`packaging/windows/check-driver.ps1`](packaging/windows/check-driver.ps1)
   to see what Windows thinks it has.
2. If it is unbound, follow
   [`packaging/windows/DRIVER_INSTALL.md`](packaging/windows/DRIVER_INSTALL.md) - the short
   version is *use Zadig and pick the device id that script prints*. Do not hand-write an
   INF, and do not install the Zune software.
3. Replug. The app then says plainly whether it can use what is plugged in.

## Build on Windows

The full, current recipe is
[the Windows section of BUILDING.md](BUILDING.md#build-on-windows). Short version:

- Visual Studio 2022 (`msvc2022_64`), CMake, Ninja
- Qt 6.11 for MSVC 2022 64-bit
- a vcpkg tree, or a `ZUUNED_WIN_DEPS` tree, for libusb, libgcrypt, SQLite, mpv, FFmpeg
  and LAME
- load the MSVC environment (`vcvars64.bat`) before configuring - loading none, or the
  one belonging to a different Visual Studio, fails as a flood of bogus `std::thread`
  diagnostics or an `STL1001` static assert rather than as a linker error

```powershell
git clone https://github.com/miemiekurisu/zuuned-modern.git
cd zuuned-modern
cmake -B build -G Ninja
cmake --build build --clean-first
build\zuuned.exe
```

`--clean-first` is not a stylistic preference on this platform: on a localized Windows
install Ninja records zero header dependencies, so an incremental build after a header
edit quietly relinks stale objects. `BUILDING.md` explains, with the upstream issue
numbers.

Two software gates ship with the tree and neither one touches a player:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\win-compat\run.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\win-usability\run.ps1
```

They cover the POSIX-compatibility layer and launch every page, every settings tab and
the command-line probes against isolated profiles. A green run of both says nothing at all
about a Zune - that is deliberate, and is why the hardware evidence lives on
[docs/WINDOWS_PORT.md](docs/WINDOWS_PORT.md) instead.

## Building on Linux

The upstream instructions still apply and are unchanged by the port: see
[BUILDING.md](BUILDING.md), [docs/BUILD_WITH_MTPZ.md](docs/BUILD_WITH_MTPZ.md) for Zune
authentication data, and the upstream
[badgeromen/zuuned-linux](https://github.com/badgeromen/zuuned-linux) repository for
released Linux packages, the AppImage workflow and the udev rule. This repository builds
Linux too, but it publishes no Linux binaries.

## Reporting a problem

Open **Settings -> Health -> export debug report**, read the saved text, and attach it to
an issue here. Say which platform and build, the Zune model, and the steps. Logs are under
`%LOCALAPPDATA%\Zuuned\Zuuned\State\logs\` on Windows and
`~/.local/state/Zuuned/Zuuned/logs/` on Linux; settings are an ini file under
`%APPDATA%\Zuuned` on Windows. Reports never upload themselves. See
[docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md).

## Layout

```
src/            C++; DeviceService (libzune bridge), app entry
qml/            UI; Theme.qml design tokens
compat/win/     the Windows POSIX surface, built once as zuuned_wincompat
libzune/        included source; shared C protocol library
packaging/      Linux USB rule, PKGBUILD and AppImage builder
packaging/windows/  WinUSB driver guide and check-driver.ps1
tests/win-compat/    Windows compatibility gate (no device)
tests/win-usability/ Windows launch and UI gate (no device)
docs/WINDOWS_PORT.md what is and is not proven on Windows
```

## What you can do with Zuuned

Browse a Zune's library and transfer media to it over USB; enjoy a local music library
with artists, albums, playlists and playback; organize videos, podcasts and audiobooks;
edit metadata and artwork; build photo albums. Local playback and organization work with
no player connected. The Windows port currently carries the music path end to end and
the rest of the categories are unproven there - see the status page.

## License

Original Zuuned application code is licensed under **GPL-3.0-or-later**. See
[LICENSE](LICENSE) and [licensing scope](COPYING.md). Third-party components retain their
own licenses and attribution. libzune has its own repository.

Zuuned is an independent community project, not affiliated with or endorsed by Microsoft.
Zune is a trademark of Microsoft Corporation.
