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

The Windows port compiles and runs natively with MSVC, and its two software gates
pass, but it has **not been tested against a real Zune device on Windows
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

Scanning is covered the same way, with a fixture built from the cases Windows
historically gets wrong: a plain MP3, a WMA file named `.mp3`, a folder named in
Chinese characters, and a track nested deep enough that its absolute path is 277
characters.

```powershell
python tests\win-usability\make_scan_fixture.py build\wtest\fx\music `
  --ffmpeg D:\dev\zuuned-deps\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe
build\librarytool.exe scan build\wtest\fx\music build\wtest\fx-library.db
python tests\win-usability\inspect_scan_db.py build\wtest\fx-library.db `
  --expect-tracks 4 --expect-codepoints 0x6F22,0x8A5E --min-longest-path 200
```

Expected: the fixture prints `created=4` with a longest path of about 277
characters, the scan exits 0 reporting `4 tracks added`, and the inspector reports
`fingerprints=4 probe_failures=0 codepoint_path_rows=1 OVERALL=ok`. The stored path
is a little shorter than the one on disk because the folder was given relative.
Each of those numbers has been wrong at some point during the port.

### Windows usability gate

Whether the built app is actually usable, as opposed to merely compiling: one
instrumented launch per page (plus the device page with no device attached), the five
settings tabs, both pinned chrome states, the compact Now Playing island reached by
resizing a live window, the four command-line probes, the transcode layer making real
ASF, MP4 and MP3 files instead of only probing them, the same clip decoded once forced
through software and once through the accelerator Settings asks for with the decoder's first
pixel format read back off it, two launches into a profile folder
named in Chinese, a read-only account of what the machine can see about a Zune, a seek
on the null audio driver, a muted audible run that has to bind WASAPI,
single-instance refusal, and before-and-after proof that the
real profile stayed untouched.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\win-usability\run.ps1
```

Expected: `launches=17 checks=83 failures=0 skips=0 killed=0` and `OVERALL=ok`. It
needs the same MSVC environment as the build and compiles its own capture tool into
`build\win-usability\`. `-Only fixture,pages` and similar run a subset - the phase
list is comma-separated, and because the scratch tree is rebuilt on every run a page
phase has to be paired with `fixture`. Each launch gets its own
`ZUUNED_PROFILE_DIR`, its own `ZUUNED_LOCK_SUFFIX` and `ZUUNED_MPV_AO=null`, and every
one ends with a capture of the live window plus the app's own `--qa-probe` account of
what it decided to show, because a Qt Quick window can come up with a clean stderr
and paint nothing.

The gate opens no USB device handle and calls no libzune function, so it cannot
certify the part that matters most on this platform: **no Zune has ever been
connected to a Windows build**. The `what is plugged in` phase reads presence only —
what libusb enumerated, what the PnP manager reports, and whether the two agree. `tests/win-usability/README.md` lists the phases and what they do not cover.

### Path encodings on a non-English Windows

`compat/win/` decodes the narrow path it is handed with the **system ANSI code
page**, matching `QFile::encodeName()`, so the shim and Qt agree with each other.
A library with its own encoding policy has to be fed explicitly rather than
inherit that: FFmpeg documents UTF-8 file names on Windows, which is why
`src/library/AudioCopyFingerprint.h` passes `path.toUtf8()` and not
`QFile::encodeName(path)`. On a GBK machine the difference is visible - a CJK song
name becomes bytes FFmpeg cannot resolve, and the track is then indexed with no
copy fingerprint and no error to show for it.

That same ACP convention is why no shared source may hand a user path to CRT
`access()`: it is ANSI-only, so a folder with a non-ASCII name reads as
unreadable, and mode bit 1 is not even a valid argument - `_access()` runs the
invalid parameter handler, which `__fastfail()`s the process rather than setting
`errno`. `X_OK` is therefore `0` in `compat/win/posix_shim.h`, and
`LibraryScanner::accessibleDirectory()` lets the shim's UTF-16 `opendir()` prove
read access. Names outside the local code page are still lossy on their way
through the shim, exactly as they are through `QFile::encodeName()`.

Anything with its own encoding policy has to be named explicitly rather than inherit
the ACP convention, and SQLite is the second one found after FFmpeg. Its Windows VFS
decodes a path name with `CP_UTF8` and never consults the ANSI code page, so handing it
`QFile::encodeName(path)` asked for a different file than the folder Qt had just made:
a two-byte GBK sequence for a common Han character is very often valid UTF-8 as well
(`U+4E00` is `D2 BB`, which reads back as `U+04BB`). Under a profile folder named in
Chinese - a Chinese Windows user name is enough - `spoken/` appeared, `spoken.db` did
not, and every podcast and audiobook operation answered "Cannot open spoken-media
database", while `library.db` in the same folder opened fine because `LibraryDb` passes
`toUtf8()`. `src/spoken/SpokenStore.cpp` passes `toUtf8()` now, which is the same
bytes on Linux, and the `cjkprofile` phase of the usability gate launches twice into a
Han-named profile folder and reads the store's schema back with
`tests/win-usability/inspect_spoken_db.py`.

Not every `QFile::encodeName()` caller is a bug waiting to happen, and each remaining
one has been checked rather than guessed at: `compat/win/` decodes the bytes it is
handed as UTF-8 first and as the ANSI code page second, and CRT `access()` is
ANSI-only, so local 8-bit is the correct thing to give both. The `fixture` phase proves
the scanner side of that by storing a Chinese album folder name and hashing the audio
behind it. The one known loss is inside Qt itself: with a profile folder the ANSI code
page cannot spell, Qt's own shader/pipeline cache cannot open its file even though the
directory exists, so such a profile pays for a cold shader cache on every start. It is
recorded by the gate, not asserted, and no data is involved.

### Incremental builds and the compiler's include note

Where the Windows UI language is not English, `cl.exe /showIncludes` writes its
"Note: including file:" line in that language, and Ninja's `deps = msvc` parser
recognises only the English wording. The dependency line is dropped. The Ninja
dependency query then answers `#deps 0` for a translation unit that did include
headers, an edit
to any header rebuilds nothing, and the link quietly reuses the old object.
`VSLANG=1033` does not change the note, and Ninja 1.12's `MSVCPREFIX` alternative
cannot match it either while the ANSI environment block and a UTF-8 note disagree.
Until the build moves to `cmcldeps`, or the machine enables "Use Unicode UTF-8 for
worldwide language support", or English compiler resources are installed, force a
full rebuild after a header-only change:

```powershell
cmake --build build --clean-first
```

A stale object looks exactly like a fix that did not work. When a change appears
to have no effect, prove the object is new before believing the diagnosis.

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

The Linux udev rules do nothing here, and a Zune plugged into a clean Windows
machine has no driver at all: Windows enumerates it, marks it
`CM_PROB_FAILED_INSTALL` (problem 28) and leaves it out of
`libusb_get_device_list()` entirely, so libusb reports nothing rather than
something useful.

zuuned therefore asks the PnP manager as well as libusb (`src/DevicePresenceWin.cpp`,
through `cfgmgr32`). A player that is on the bus but unopenable is named as such —
status "Zune plugged in, but no driver zuuned can use" instead of the silent
"waiting for a zune…" — and the device id to select in Zadig is printed where the
Linux udev command would have been. Nothing is installed for you: bind WinUSB with
the Zadig utility and replug. That binding step and everything behind it remain
untested on Windows.

`build\devicetool.exe` asks the same question in one command, with no app, no UI and
no device handle:

    build\devicetool.exe

It prints both accounts and a verdict. On a machine holding a driver-less player the
expected answer is `blocked`:

    libusb_enumerated=1 libusb_devices=7 libusb_hotplug=0 libusb_sighted=0
    os_supported=1 os_present=1 os_usable=0 os_problem=28
    VERDICT=blocked

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
