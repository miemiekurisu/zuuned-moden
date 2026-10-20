# Windows port status

Dated 2026-10-20.  This is where the native Windows build of Zuuned actually
stands: what compiles, what has been proven on a Windows machine, and what has
not.  It exists so that nobody reads "the Windows gates pass" as "the Windows
build works with a Zune".  Build steps and dependency locations live in
`BUILDING.md`; the two gates are documented in `tests/win-compat/README.md` and
`tests/win-usability/README.md`.

## No Windows build has ever talked to a Zune

**No Zune has ever been connected to a Windows build, and no hardware claim in
this repository carries across platforms.**  Read that as a release rule, not a
formality:

- Every hardware fact this project states - MTPZ authentication, library reads,
  track, album, video, podcast and audiobook sends, on-device deletion, byte
  readback, firmware category presentation, resume - is evidence gathered on
  Linux over libusb.  None of it has been repeated on Windows.
- Nothing in the Windows tree has ever called a libzune function against a
  device handle.  The `what is plugged in` phase of the usability gate reads
  presence only, and `devicetool` opens no handle by design.
- On the development machine a Zune HD is attached and the PnP manager reports
  it at `CM_PROB_FAILED_INSTALL` (problem 28): Windows sees the devnode, no
  driver is bound, so `libusb_get_device_list()` does not contain it and
  `devicetool` answers `blocked`.  `src/DevicePresenceWin.cpp` exists so the
  device page says that in the user's terms instead of claiming nothing was
  plugged in.
- The driver route is now decided but still unproven: upstream Zadig installing
  WinUSB, as `packaging/windows/DRIVER_INSTALL.md` documents - including the device
  interface GUID rule that makes the in-box class picker on its own useless to
  libusb.  No machine has run that recipe yet, so nothing here may be described as
  a working Windows device path.  And `zunetool` - the tool every Linux hardware
  gate was run with - is built by libzune's POSIX Makefile and is not part of the
  Windows build, which compiles libzune directly as the `libzune_build` target.

So a Windows build may be described as building, launching, painting, scanning,
encoding and persisting.  It may not be described as syncing.

## Toolchain that works here

- Qt 6.11.0 `msvc2022_64`.  `CMakeLists.txt` requires Qt 6.8, so this kit is
  comfortably new enough; it has to be the MSVC 64-bit kit because the app is
  built with MSVC, and the other kits in the same Qt installation do not apply.
- Visual Studio 2022 (MSVC x64) with the Windows SDK, plus the CMake and Ninja
  that ship in `Qt\Tools`.
- vcpkg `x64-windows` for libusb, sqlite3, libmp3lame and libgcrypt; PCRE2
  headers back libzune's pattern helpers and the shim gate.
- A shared FFmpeg build for the transcode layer and libmpv for playback.  libmpv
  ships a MinGW import library MSVC cannot read, so an MSVC import library is
  generated once from the DLL export table.
- Native throughout, by decision: no WSL, no MinGW, no Cygwin.  The consequence
  is stated plainly below under what is not proven - the Linux bash gates are
  not run from this checkout.

## What the port consists of

- `CMakeLists.txt`: `if(WIN32)` guards for dependency discovery in place of
  pkg-config, the `compat/win` POSIX surface force-included for the C layer,
  MPRIS and `Qt6::DBus` kept Linux-only, and the POSIX install rules skipped -
  shipping on Windows is a self-contained directory assembled with `windeployqt`
  beside the exe, and `cmake --install` deliberately does nothing.
- `src/DevicePresence.h`, `src/DevicePresence.cpp` and the two backends: Windows
  asks libusb first and then the PnP manager through `cfgmgr32`;
  `src/DevicePresenceUnix.cpp` answers from libusb alone with its existing
  wording byte-identical, so the line that has hardware evidence behind it is
  untouched.  `src/UsbWatcher.cpp` polls that presence every two seconds and
  emits `zuneBlocked`/`zuneUnblocked`.
- `src/ProfilePaths.h` plus `ZUUNED_PROFILE_DIR` and `ZUUNED_LOCK_SUFFIX`: one
  folder owns the settings ini, both databases, every cache, the session logs
  and the single-instance lock.  That is the Windows equivalent of relocating
  `$HOME`, and it is what makes an instrumented launch honest instead of
  destructive to the real profile.
- Path encodings, the subtlest part of the port: SQLite wants UTF-8, FFmpeg is
  documented UTF-8 on Windows, the CRT is ANSI-only, and `compat/win` decodes a
  narrow path as UTF-8 with an ANSI fallback.  `src/spoken/SpokenStore.cpp`
  handed SQLite the local code page until 2026-10-20, which silently lost the
  whole spoken library under a Chinese profile folder.  `BUILDING.md` has the
  full account.
- Twelve translation units carry platform guards.  The rest of the application
  is the same shared code as on Linux.

## What is proven on a Windows machine, by software alone

Numbers below come from the development machine at this checkpoint.  Re-run
`tests\win-usability\run.ps1` and `tests\win-compat\run.ps1` from an MSVC
environment to reproduce them.

- The shim gate reports `171 checks, 0 failures, 0 skips`: temporary names,
  long paths past 260 characters, 64-bit sizes, directory descriptors,
  `opendir` semantics and UTF-8 narrow names under the same force-include the
  application builds with. Since 2026-10-20 it also pins how `fstat()` reads a
  number: a raw NT handle *and* a descriptor the shim never created both resolve.
  That closed a Windows-only bug which killed every audiobook import - Qt opens a
  `QFile` through `_wopen()`, so `QFile::handle()` is a CRT descriptor rather than
  the native handle its documentation promises, and the watermark in front of the
  descriptor table only counted descriptors `compat/win` handed out. The
  invalid-parameter guard that makes asking the table safe goes in lazily from
  `fd_handle()`: a `.CRT$XIB` initialiser was tried first, the linker folded this
  object's `.CRT` subsection into `.rdata` (LNK4254) and start-up never ran it,
  which the gate caught by finding the handler still null.
- The usability gate makes seventeen instrumented launches plus the tool,
  encoder and profile probes and ends `launches=17 checks=83 failures=0
  skips=0 killed=0` and `OVERALL=ok`.
- What those launches actually establish: every page paints real content rather
  than an empty window (`winshot` captures the live swapchain and the app
  reports its own state through `--qa-probe`); the five settings tabs, the
  pinned-header states and the compact Now-Playing island behave as designed; a
  second instance on the same profile refuses in its own words; a library scan
  stores a Chinese album folder name and a path of 277 characters and hashes the
  audio behind both; the transcode layer *produces* - profile 0 returns ASF at
  the source frame size, profile 2 returns `ftyp` at 480x272, and the LAME leg
  returns an mp3 as long as the tone it came from; and a profile folder named in
  Chinese gains both databases with the spoken store's own schema readable back
  out of it.
- Real audio output, silenced.  `playertool play <tone> 3 --audible --mute` with
  `ZUUNED_MPV_AO=wasapi` ends `audio_out=wasapi` with the position advancing, and the
  same run with the variable empty ends `audio_out=null`, so the Windows audio backend
  is proven to open and to clock playback.  Nothing was listened to.
- Which decoder path actually ran.  `transcodetool video` now prints the pixel format of
  the first frame the decoder returned and the gate reads it back: `ZUUNED_HW_DECODE=0`
  opens one decoder, asks for no accelerator and decodes in `yuv420p`, while the default
  Settings leaves on reported a first frame in `cuda` on this machine and muxed a valid
  480x272 file through the hardware frame download path.  What this corrects is worth
  keeping: the line the layer used to print inferred a hardware decode from an attached
  device context, which stays attached even when `get_format` answers with a software
  format, so the earlier note here that Windows had only ever decoded in software was
  read off a label that could not tell the two apart either way.

## What is not proven here

1. Device work of any kind, as stated at the top.  Driver binding first, then
   enumeration, hotplug, transfers, readback and deletion, each a real gate
   rather than a ported assumption.  WinUSB cannot reset a device either, so
   `zune_breach()`'s recovery from an OpenSession wedge does not exist here and an
   unplug/replug is the workaround - see the driver guide.
2. Audio anyone can hear.  The gate opens WASAPI for real and asserts mpv reports it
   through `current-ao`, but always muted, so samples never reach the speaker.
   Audible playback, exclusive mode, a hot-plugged or Bluetooth endpoint, and anything
   a human listened to remain unverified.
3. What hardware decode is worth.  The gate proves NVDEC engages on this machine and
   that the downloaded frames survive the scaler and the muxer, never that decoding on
   the GPU is faster than the software path here - the ~4x figure in the roadmap was
   measured on Linux.  A machine without an NVIDIA decoder legitimately takes the
   software path, and the gate asserts the software run only when it forces one.
4. `spokenmediatool` builds and both legs were driven by hand on 2026-10-20 - an
   RSS feed with a Chinese item title, and the audiobook probe the descriptor fix
   above unblocked - but the tool still has no phase in the usability gate.
5. Packaging.  No installer, no `cmake --install` layout, no verified
   `windeployqt` directory and no release artifact.
6. The Linux line is unchanged by construction rather than re-verified: this
   machine is the Windows checkout, Windows changes are platform-neutral or
   inside `Q_OS_WIN`, and the bash gates were not re-run from here.
7. Cosmetic and recorded: the white ring in the `qml/EnvironmentBackground.qml`
   spray halo under the Windows blur path.
8. Inside Qt and recorded rather than asserted: with a profile folder whose name
   the local ANSI code page cannot spell, Qt's own shader/pipeline cache cannot
   open its file even though the directory exists, so such a user pays a cold
   shader cache on every start and loses no data.
9. Also recorded, not asserted: a GUI launch reaches musicbrainz.org for artwork
   discovery exactly as it does on Linux, and a 503 there is noise; a brand new
   profile legitimately shows onboarding rather than the requested page.

## Order to close it in

1. Prove the driver route on a real machine: Zadig installing WinUSB per
   `packaging/windows/DRIVER_INSTALL.md`, then `packaging/windows/check-driver.ps1`
   showing a function driver plus a registered interface GUID, then `devicetool`
   answering `agree` with a Zune actually attached.
2. Repeat the Linux hardware gates in the same order on Windows: library read,
   one album send, one deletion, readback, then the podcast and audiobook
   categories and firmware resume - each with its own evidence, none inherited.
3. Add the remaining software gates: audible (unmuted) playback and a
   `spokenmediatool` phase.  Measure NVDEC against software on a real 1080p item before
   quoting a speed-up for Windows.
4. Only then talk about Windows packaging or a Windows release channel.
