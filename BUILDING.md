# Building Zuuned Modern

This guide covers the supported native source build and documents the separate
maintainer AppImage workflow with its current public-layout limitations. Run
commands from the repository root unless a step says otherwise.

## Get the source

This public repository includes `libzune/` as ordinary source files. No submodule
initialization is required. Use the bundled revision for this application; do not
replace it with a newer standalone library without testing the integration.

```bash
git clone https://github.com/miemiekurisu/zuuned-modern.git
cd zuuned-modern
```

For Zune authentication, follow [BUILD_WITH_MTPZ.md](docs/BUILD_WITH_MTPZ.md).
You may build without embedded data and use the external file at runtime.
Local library and playback do not require authentication data.

## Install native build dependencies

Qt 6.8 or newer is required. The application also links libusb, libgcrypt,
SQLite, mpv, FFmpeg and LAME. The Arch and Debian package lists belong to the
upstream Linux project and are kept in its
[README](https://github.com/badgeromen/zuuned-linux); the Windows tree is a
different list again and is under [Toolchain and dependencies](#toolchain-and-dependencies)
below.

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

The Windows port compiles and runs natively with MSVC. Its two software gates
pass and it has been driven against a real Zune HD, but which specific device
capabilities have Windows evidence is recorded capability by capability in
[the port status page](docs/WINDOWS_PORT.md) - read that before quoting any
hardware claim, because the list of what is still Linux-only is long. Outputs are
development builds: there is no Windows installer.

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

`vcvars64.bat` has to come from the same Visual Studio the build directory was
configured with. CMake pins the compiler's absolute path in the cache, so
`build\CMakeFiles\<version>\CMakeCXXCompiler.cmake` is the authority (here
`Microsoft Visual Studio\2022\Community\...\14.44.35207`, `cl` 19.44); calling a
newer `vcvars64.bat` instead leaves `cl` where it was and moves only `INCLUDE`,
and the newer standard library headers then refuse the older compiler as
`error C2338 ... STL1001: expected MSVC 19.50 or newer` - a wall of
standard-header noise that reads like a source bug and is really the wrong shell.

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

Every Windows executable built here embeds `packaging/windows/zuuned.manifest`, through
`zuuned_long_paths()` (`CMakeLists.txt:166`). That file is what opts the process out of
`MAX_PATH`: `longPathAware` lifts the 260-character ceiling for the image that declares it
and for no other, and the listener that matters is the C runtime - the route FFmpeg, LAME
and SQLite take to a file, and therefore the route a transcode temporary takes during a
sync.  CMake supplies a manifest of its own for every Windows target and two cannot share
resource ID 1, so the target first passes `/MANIFEST:NO` to stop CMake generating one,
which means this file has to be complete - the `asInvoker` `trustInfo` included - and not a
fragment. Nothing else in the build describes that resource afterwards, so a link is
confirmed by reading it back:

```powershell
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe' `
  '-inputresource:build\zuuned.exe;#1' '-out:manifest.xml'
```

Expect exactly one manifest, `level="asInvoker"`, `longPathAware` true. Two of those checks
in `librarytool paths` (`deep crt:`) fail when the declaration is missing, so the pair is the
gate that says the manifest is still doing something.

Two build-hygiene facts cost time on 2026-10-21 and are worth having written down.
Building without the MSVC environment loaded in the *same* shell does not fail the way
you expect: instead of `LNK1104: ws2_32.lib` it emits around a hundred impossible
`std::thread`/`std::mutex` diagnostics pointing into
`...\Windows Kits\10\Include\...\ScopeCppSDK\vc15\VC\include`, because that SDK folder
shadows the real STL headers. The cure is the environment, not the code.

Second, and the one that can make a passing gate mean nothing: on a localized Visual
Studio an incremental build does not know that a header changed. The Ninja rules carry
`deps = msvc`, so Ninja learns what a `.cpp` includes by reading the `/showIncludes`
echo from `cl`, and it looks for the literal English prefix `Note: including file:`.
A Chinese MSVC writes `注意: 包含文件:` instead, nothing matches, and the dependency
log comes back empty:

```
build> ninja -t deps CMakeFiles\zuuned.dir\src\LibraryService.cpp.obj
CMakeFiles/zuuned.dir/src/LibraryService.cpp.obj: #deps 0, deps mtime ... (VALID)
```

`#deps 0` is the whole story: no translation unit on this machine knows about any
header. Edit `src/library/LibraryPaths.h` - header-only, compiled into the app and into
`librarytool` - and `cmake --build build` recompiles nothing, relinks anyway, and prints
`[5/7] Building CXX object ...mocs_compilation.cpp.obj` because AUTOMOC touched its own
generated file, which makes the run look busy and reassuring. A gate executed on the
result can pass against an executable that predates the change under test, which is the
failure this page has had to record more than once. Nor is there a depfile to fall back
on: the eleven `.d` files under `build/CMakeFiles/d/` belong to CMake's own configure
steps, not to the C++ translation units.

So before writing down any gate counter on Windows, `cmake --build build --clean-first`
- a full rebuild costs a few minutes, and every recorded number in these documents came
from one. When you only need one target, touch each `.cpp` that includes the changed
header instead: source-file dependencies are tracked correctly. Fixing the underlying
problem needs an English `cl` or a Ninja that parses localized notes; `VSLANG=1033` was
tried on the 2026-10-21 machine and did not change the output language.

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
descriptors, pipes, `mmap`-backed publishing and wide `stdio`. It also pins how
a descriptor is resolved: `fstat()` works on a handle the shim never created and
on a descriptor the shim never created - the second is what `QFile::handle()`
hands the audiobook probe on Windows - and the CRT invalid-parameter guard is in
force without any `main()` asking for it. It also pins how a path is spelled for the
shell `system()` runs, because `cmd.exe` decodes its command line with the ANSI code
page while the app hands UTF-8. Expected result:
`201 checks, 0 failures, 0 skips`. Since 2026-10-21 it also pins `stat()`
identity: `st_dev`/`st_ino` are the volume serial and file index, distinct files
never share one, and it survives a rename - the artwork cache's
"this image is its own source" check is built on that.

The bash suites in `ci/check.sh` need pkg-config and a POSIX shell, so on
Windows the library core is covered by the native tools instead:
`build\librarytool.exe names`, `build\librarytool.exe customize` (the latter
rebuilds a pre-Customize database and runs the real v0/v2 to v9 migration chain,
including the pre-migration backup) and `build\librarytool.exe paths` - 62 checks
over the filename, path-comparison, playlist-resolution and playlist-decoding rules
the Windows port depends on plus the once-only rule for a watch folder that contains
a junction, the reserved-stem and trailing-dot rule a playlist export needs, m3u written
in UTF-16 with a BOM, and one path past 260 characters written twice - once through Qt
and once through `_wfopen()`, which is the pair that pins the embedded manifest,
with the expectations branched per platform so the same file gates a
Linux run without changing a Linux answer. All three pass; none touches a
device, and all three run inside `tests\win-usability\run.ps1`'s tools phase.

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

Expected: `launches=18 checks=99 failures=0 skips=0 killed=0` and `OVERALL=ok`. It
needs the same MSVC environment as the build and compiles its own capture tool into
`build\win-usability\`. `-Only fixture,pages` and similar run a subset - the phase
list is comma-separated, and because the scratch tree is rebuilt on every run a page
phase has to be paired with `fixture`. Each launch gets its own
`ZUUNED_PROFILE_DIR`, its own `ZUUNED_LOCK_SUFFIX` and `ZUUNED_MPV_AO=null`, and every
one ends with a capture of the live window plus the app's own `--qa-probe` account of
what it decided to show, because a Qt Quick window can come up with a clean stderr
and paint nothing.

The gate opens no USB device handle and calls no libzune function, so it certifies
nothing about the wire. What a Windows build does on the wire is the hand test in
`docs/WINDOWS_PORT.md`: one Pavo Zune HD bound to WinUSB, opened, MTPZ-authenticated,
read, and sent audio that came back. The `what is plugged in` phase reads presence
only - what libusb enumerated, what the PnP manager reports, and whether the two
agree. `tests/win-usability/README.md` lists the phases and what they do not cover.

### Path encodings on a non-English Windows

`compat/win/` decodes the narrow path it is handed as **UTF-8 first**, and as the
**system ANSI code page** only when the bytes are not valid UTF-8. A caller that
passes UTF-8 - which is what a POSIX filename already is, on every platform - is
therefore never second-guessed, and that is the whole reason the rule is
UTF-8-first rather than ACP-only. A library with its own encoding policy still
has to be fed explicitly rather than inherit anything: FFmpeg documents UTF-8
file names on Windows, which is why
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
read access. The shim is not where a name gets lost: handed UTF-8 it decodes
strictly and keeps every character, so the loss belongs to a caller that
encoded with the local 8-bit codec before the call.

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

The section above used to close by claiming that local 8-bit was the correct
thing to hand the shim. It is not, and that claim cost a scan that reported a
healthy empty library. Every `QFile::encodeName()`
call in the tree has now been read rather than assumed, and four of them were
bugs: `LibraryScanner`, `ArtworkPipeline`, `ArtistImageService` and
`OnlineAlbumArtService` were handing `stat()`, `opendir()` and `link()` the local
8-bit form. Measured on this CP936 machine against five scan roots - ASCII,
Chinese, Hebrew, Hangul, emoji - the scanner found its one track under the ASCII
and Chinese roots and nothing under the other three: `stat()` failed, the copy
fingerprint returned false, and the scan exited 0 looking complete. CP936 covers
Han, so a Chinese host cannot see this defect at all; an English host meets it on
its first Hebrew album. All four now pass `toUtf8()`, which is the identical byte
string on Linux. The only remaining caller sits in the non-Windows branch of
`#if defined(Q_OS_WIN)` in `src/diagnostics/SupportReport.cpp`, where local 8-bit
is right because it reads ASCII sysfs paths on Linux.

The `fixture` phase is what makes that measurable: `make_scan_fixture.py
--extra-roots` lays out one scan root per script, and the gate scans each into its
own database and asserts one track each, so a root that works cannot mask a root
that does not - which is exactly how the Chinese root used to hide the other three.

The one known loss is still inside Qt itself: with a profile folder the ANSI code
page cannot spell, Qt's own shader/pipeline cache cannot open its file even though
the directory exists, so such a profile pays for a cold shader cache on every
start. It is recorded by the gate, not asserted, and no data is involved.

### Incremental builds and the compiler's include note

The note is the line Ninja cannot read, and on 2026-10-20 it cost a long chase
after a crash that did not exist. What is actually on this machine, measured:

- `cl.exe` writes its localised "including file" note in whatever code page the
  console it is attached to uses. Under `chcp 936` the note begins `d7 a2 d2 e2
  3a 20`; under `chcp 65001` the same note begins `e6 b3 a8 e6 84 8f 3a 20`.
  `VSLANG=1033` changes neither, byte for byte, which is why that variable appears
  in no recipe that works.
- CMake captures the prefix once, at configure time, by running `cl` and decoding
  the note as UTF-8 with the ANSI code page as a fallback. On this machine it was
  handed GBK bytes, decoded them as UTF-8, and stored the result - so
  `build\CMakeFiles\rules.ninja` says `msvc_deps_prefix = ` followed by
  `e5 a8 89 e3 84 a6 e5 89 b0 3a 20`, mojibake of mojibake. It matches neither
  form `cl` can emit, so `deps = msvc` never fires.
- The result is silent, not noisy. `ninja -t deps` answers zero dependencies for a
  translation unit that demonstrably includes headers, an edit to that header
  rebuilds nothing, and the link reuses the object already on disk. Deleting
  `build\CMakeFiles\<version>` and configuring again stores the same bytes: this
  is a property of the machine, not a cache to clean.

That is what killed the first Windows device session, and it is worth spelling out
because it looked exactly like a device bug. A member had been added to
`DeviceService` in the middle of its data section, so every member declared after
it moved. The translation unit that owns `DeviceService` rebuilt; another, which
includes its header and constructs a lambda over the same object, kept the object
file it already had. The link joined both layouts into one `zuuned.exe` without a
word, and at the moment a device arrived code compiled against the old layout
incremented the reference count of the `QString` one member away from the one it
meant: `0xc0000005` inside `Qt6Core.dll`, at `lock inc dword ptr [r8]` with
`r8 = 0xf`. Both objects were correct for the header each saw. Nothing in the
application was wrong, and reading the application could not have shown otherwise -
`cmake --build build --clean-first` fixed it, 428 files removed, 329 rebuilt.

Upstream has had this shape for years: Ninja issues 2205 and 1766, CMake issue
24068, whose merge request is the `cmcldeps` work that removes the whole class of
problem. Until that lands here, or the machine enables "Use Unicode UTF-8 for
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
