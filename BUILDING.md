# Building ZuunedLinux

This guide covers the supported native source build and documents the separate
maintainer AppImage workflow with its current public-layout limitations. Run
commands from the repository root unless a step says otherwise.

## Get the source

This public repository includes `libzune/` as ordinary source files. No submodule
initialization is required. Use the bundled revision for this application; do not
replace it with a newer standalone library without testing the integration.

```bash
git clone https://github.com/badgeromen/zuuned-linux.git
cd zuuned-linux
```

For Zune authentication, follow [BUILD_WITH_MTPZ.md](docs/BUILD_WITH_MTPZ.md).
You may build without embedded data and use the external file at runtime.
Local library and playback do not require authentication data.

## Install native build dependencies

Qt 6.8 or newer is required. The application also links libusb, libgcrypt,
SQLite, mpv, FFmpeg and LAME. The complete Arch and Debian package commands are
kept in the [main README](README.md#build).

## Build the native application

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/zuuned
```

CMake builds the included `libzune.a` automatically. If the linker reports an
unrecognized file format after moving the checkout between macOS and Linux,
remove the stale library objects and rebuild:

```bash
make -C libzune clean
cmake --build build --parallel 4
```

The native outputs are:

- `build/zuuned` — the Qt application
- `build/librarytool` — local-library/scanner diagnostics
- `build/playertool` — playback diagnostics
- `build/transcodetool` — media conversion diagnostics

This build uses the current machine's libraries. It is suitable for development
and hardware checks on that machine, but it is not the distributable AppImage.

## Build on Windows

The Windows port compiles and runs natively with MSVC, and its software gate
passes, but it has **not been tested against a real Zune device on Windows
yet**. Every libusb path, the WinUSB driver binding and all transfers are still
unverified on this platform. Treat the outputs as development builds.

Qt 6.8 or newer is required. The reference machine uses the Qt 6.11.0
`msvc2022_64` kit together with the CMake and Ninja that ship in `Qt\Tools`.

### Toolchain and dependencies

- Visual Studio 2022 with the "Desktop development with C++" workload plus a
  Windows SDK.
- Qt including Qt Quick Controls, Qt Quick Dialogs and Qt 5Compat
  GraphicalEffects.
- vcpkg, for the four ports the build consumes:

```powershell
vcpkg install libusb sqlite3 libmp3lame libgcrypt:x64-windows
```

- One extra dependency root for what vcpkg does not carry. `ZUUNED_WIN_DEPS`
  points at it and this layout is expected:

```
zuuned-deps/
      ffmpeg-master-latest-win64-gpl-shared/   win64 gpl *shared* FFmpeg build
      mpv-dev/include/mpv/client.h             mpv development headers
      pcre2/include, pcre2/lib                 PCRE2, static, with POSIX wrapper
      probe/libmpv.lib                         MSVC import library for libmpv
```

libmpv ships as a DLL plus a MinGW import library that MSVC cannot read, so the
MSVC import library is generated once from the DLL export table
(`dumpbin /exports libmpv-2.dll`, rewritten as a `LIBRARY`/`EXPORTS` definition
file) and then:

```powershell
lib /def:libmpv.def /out:libmpv.lib /machine:x64
```

### Configure and build

Load the MSVC x64 environment into the shell first (call `vcvars64.bat` from
`VC\Auxiliary\Build`, or dot-source a helper that imports the variables it
sets), then:

```powershell
cmake -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=D:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=D:/dev/Qt/6.11.0/msvc2022_64 `
  -DZUUNED_VCPKG_PREFIX=D:/dev/vcpkg/installed/x64-windows `
  -DZUUNED_WIN_DEPS=D:/dev/zuuned-deps
cmake --build build
build\zuuned.exe
```

`CMAKE_TOOLCHAIN_FILE` also wires up vcpkg's `applocal.ps1`, which copies the
vcpkg DLLs next to every executable. Run `windeployqt` in `build` for the Qt
runtime and copy the FFmpeg `bin` DLLs plus `libmpv-2.dll` beside the
executable.

`CMakeLists.txt` keeps one source tree for both platforms: the D-Bus/Mpris
service and the POSIX-only install rules are skipped on Windows, and
`compat/win/` is built once as `zuuned_wincompat` and force-included into every
shared translation unit through `posix_shim.h`, so libzune and the transcode
layer keep the POSIX surface they expect without `#ifdef` sprawl.

### Windows gate

```powershell
pwsh -NoProfile -File tests/win-compat/run.ps1
```

It rebuilds the compat layer and runs the software probes: temporary
directories, `mkstemps`, UTF-8 and long paths, large files, directory
descriptors, pipes, `mmap`-backed publishing and wide `stdio`. Expected result:
`166 checks, 0 failures, 0 skips`.

The bash suites in `ci/check.sh` need pkg-config and a POSIX shell, so on
Windows the library core is covered by the native tools instead:
`build\librarytool.exe names` and `build\librarytool.exe customize` (the latter
rebuilds a pre-Customize database and runs the real v0/v2 to v9 migration chain,
including the pre-migration backup). Both pass; neither touches a device.

### Settings and profile location

A Windows Zuuned profile lives in files, not the registry: settings are the ini
at `%APPDATA%\Zuuned\Zuuned.ini`, while the library, cache, logs and the
single-instance lock stay under `%LOCALAPPDATA%\Zuuned\Zuuned`. On first start the
app copies any values an earlier build left in `HKCU\Software\Zuuned\Zuuned` into
the ini and logs `[settings] imported N value(s)`.

Set `ZUUNED_PROFILE_DIR` to an absolute directory to keep the whole profile in one
folder instead - a portable folder you carry with you, or a throwaway one for a
test run. That is the Windows equivalent of relocating `$HOME` on Linux, and it is
needed because `QStandardPaths` here asks the shell known folders and ignores
`LOCALAPPDATA`/`APPDATA` overrides. The settings ini moves with it, to
`<profile>\Zuuned\Zuuned.ini`, and the app logs
`[profile] every profile file goes to "..."` so the effective location is never a
guess. Your music, video, picture and document folders are deliberately left where
the platform puts them. Unset, every path is as described above, on either
platform.

### Device access

The Linux udev rules do nothing here. libusb on Windows needs the Zune
interface bound to WinUSB before it can open the device (install the driver
with Zadig); that binding step and everything behind it are untested on
Windows.

## Build the tester AppImage

The exporter supports both the private submodule checkout and the public
repository's included libzune source. Public release commits contain
`PUBLIC_LIBZUNE.json`, recording the exact library commit and Git tree. The
exporter requires a clean public checkout and verifies its included library
against that tree before archiving the captured application commit. If you alter
libzune, commit it in your library repository and update the manifest to match.

Do not invent a library revision or mark modified input clean to bypass that
check. Official downloads also require the private authentication input, as
described in [the release architecture](docs/RELEASE_ARCHITECTURE.md).

The release workflow builds inside pinned Debian 13 images. This gives the
package a known Qt 6.8.2 and glibc baseline while keeping build dependencies off
the development desktop. Docker is used only to compile and inspect the bundle:
the containers receive no USB device, personal library, audio device or desktop
session. Zune hardware testing still runs on a physical host.

Install Docker first. If the current account can access Docker directly, omit
`sudo` from the following commands. The checked-in images use tester UID/GID
`1000:1000`; if the checkout owner differs, follow the UID instructions in the
detailed container README rather than changing only `--user` below.

Build the two images whenever either Dockerfile or the runtime smoke script
changes:

```bash
sudo docker build --platform linux/amd64 \
  -f packaging/containers/Dockerfile.debian13 \
  -t zuuned-tester-debian13 packaging/containers

sudo docker build --platform linux/amd64 \
  -f packaging/containers/Dockerfile.runtime-debian13 \
  -t zuuned-runtime-debian13 packaging/containers
```

The package must come from committed, clean application and library states.
This prevents an AppImage whose filename identifies one revision while its
contents came from another.

```bash
test -z "$(git status --porcelain --untracked-files=normal --ignore-submodules=none)"
test -z "$(git -C libzune status --porcelain --untracked-files=normal)"

repo=$(pwd)
release_root=$(mktemp -d /tmp/zuuned-release-XXXXXXXX)
snapshot="$release_root/source"

node packaging/containers/snapshot.mjs "$snapshot"
grep -qx 'export ZUUNED_SOURCE_DIRTY=false' "$snapshot/snapshot.env"
grep -qx 'export ZUUNED_LIBZUNE_DIRTY=false' "$snapshot/snapshot.env"

mkdir -p packaging/appimage-tools
tools=$(realpath packaging/appimage-tools)

sudo docker run --rm --platform linux/amd64 --user 1000:1000 \
  --env LANG=C.UTF-8 --env ZUUNED_APPIMAGE_TOOLS=/tools \
  --mount "type=bind,src=$snapshot,dst=/work" \
  --mount "type=bind,src=$tools,dst=/tools" \
  --workdir /work zuuned-tester-debian13 \
  bash -lc 'source /work/snapshot.env; bash packaging/build-appimage.sh'

mkdir -p "$repo/build-appimage/dist"
cp -a "$snapshot/build-appimage/dist/." "$repo/build-appimage/dist/"
find "$repo/build-appimage/dist" -maxdepth 1 -type f -printf '%f\n' | sort
```

The first package run may download the AppImage deployment tools. Their hashes
are pinned in `packaging/appimage-tools.lock`; the builder verifies cached and
downloaded copies before executing them.

The output set under `build-appimage/dist/` contains:

- `Zuuned-<version>-g<revision>-<UTC>-x86_64.AppImage`
- the matching `.AppImage.sha256`
- `.AppImage.build.json` with the exact app and libzune revisions
- `.AppImage.environment.txt` with the build environment and glibc requirements
- `.AppImage.tools.lock` with the deployment-tool hashes

Verify the exact candidate from inside its output directory, because the
checksum file contains the AppImage's basename:

```bash
cd build-appimage/dist
candidate=$(find . -maxdepth 1 -type f -name 'Zuuned-*.AppImage' \
  -printf '%T@ %f\n' | sort -nr | head -1 | cut -d' ' -f2-)
sha256sum --check "$candidate.sha256"
chmod +x "$candidate"
"./$candidate" --version
cd ../..
```

## Run the isolated package gate

This launches the actual AppImage in Xvfb with software OpenGL, disposable XDG
profiles, no network and an empty USB view. It checks packaged libraries,
onboarding, local music import, artwork rendering, desktop file pickers,
playlist import, diagnostics export and normal shutdown. It cannot certify real
GPU/audio/USB behavior.

```bash
candidate=$(realpath "build-appimage/dist/$candidate")
runtime_evidence="$release_root/runtime"
mkdir -p "$runtime_evidence"

sudo docker run --rm --platform linux/amd64 --user 1000:1000 \
  --network none --shm-size 512m \
  --mount type=tmpfs,dst=/sys/bus/usb/devices \
  --mount "type=bind,src=$candidate,dst=/candidate.AppImage,readonly" \
  --mount "type=bind,src=$runtime_evidence,dst=/evidence" \
  zuuned-runtime-debian13 /candidate.AppImage /evidence

cat "$runtime_evidence/latest-runtime.txt"
```

The detailed evidence contract and failure interpretation live in
[`packaging/containers/README.md`](packaging/containers/README.md). A passing
container gate does not replace playback, transfer and USB recovery tests on a
physical machine.

## Run the AppImage

```bash
chmod +x Zuuned-*.AppImage
./Zuuned-*.AppImage
```

If FUSE is unavailable:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./Zuuned-*.AppImage
```

If a file manager says “archive not supported,” it has handed the executable to
an archive application instead of running it. Use the commands above or enable
the file's executable permission in its Properties dialog. Code inside an
AppImage cannot change a desktop's file association before the package starts.

Install the two USB rules only on the physical test system; the application can
also install these through its USB permission card:

```bash
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/68-zuuned.rules
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/72-zuuned.rules
sudo udevadm control --reload-rules
```

Replug the Zune after installing the rules. See
[`packaging/TESTER_INSTALL.md`](packaging/TESTER_INSTALL.md) for tester-facing
launch, diagnostics and reporting instructions.
