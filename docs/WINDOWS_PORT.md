# Windows port status

Dated 2026-10-20.  This is where the native Windows build of Zuuned actually
stands: what compiles, what has been proven on a Windows machine, and what has
not.  It exists so that nobody reads "the Windows gates pass" as "the Windows
build works with a Zune".  Build steps and dependency locations live in
`BUILDING.md`; the two gates are documented in `tests/win-compat/README.md` and
`tests/win-usability/README.md`.

## The first Windows device contact

**A Zune HD has now been connected to a Windows build, opened, and read.**  What
follows was measured on the development machine on 2026-10-20 and it is the whole
of the Windows device evidence as of this checkpoint.  The rule this page exists
for survives it: **a hardware fact proven on Linux still does not carry across
platforms, and reading a library is not syncing one.**

The driver first.  `packaging/windows/DRIVER_INSTALL.md` closes as written - a
libwdi-generated WinUSB package is the devnode's function driver, the problem code
is `CM_PROB_NONE`, the service is `WinUSB`, and the devnode registers the device
interface GUID libusb 1.0.29 insists on.  `check-driver.ps1` reads all of that
back and `build\devicetool.exe` answers `VERDICT=agree`.  That run did surface one
documentation and tooling bug, now fixed in both: the GUID is readable only from
the devnode's `Device Parameters` registry key, while
`DEVPKEY_Device_DeviceInterfaceGuids` is empty on a player that opens perfectly
well, so a script that trusts the DEVPKEY alone reports an openable device as
unopenable.

Then the application, which is where the first Windows device traffic is:

```
[libusb] opened: bulk_in=0x81(512) bulk_out=0x01(512) int=0x82
[ptp] detected split header/data mode (Zune/WMP)
[libzune] GetDeviceInfo: model='Zune' serial=[redacted]
[libzune] device family: Pavo (Zune HD) (0xD21A = 0x06040001)
[libzune] connected: [device name] (Zune), battery: 100%, storage: 10491396096/15543730176
[libzune-zmdb] totalSize=226688, payloadSize=226676, first chunk=12 bytes
[libzune-zmdb] received 226676/226676 bytes
[zunedb] scanned: 183 tracks, 0 videos, 0 photos, 9 albums, 10 artists, 0 playlists, 0 photo albums
[device] storage: 5.1/15.5 GB (used/total), attributed: music 4.88 + video 0.00 + photo 0.00 GB
```

So on Windows a Zune HD is opened over WinUSB, answers `GetDeviceInfo`, is
identified as Pavo by the same property read Linux uses, hands over all 226,676
bytes of ZMDB, and the application turns that into 183 tracks in 9 albums by 10
artists with per-category storage accounting and album thumbnails fetched off the
player - and the device page is operable, which is what the crash below was
blocking.  Unplugging and replugging produced a second identical read, so this is
the hotplug path working rather than one lucky connection.

Three things it is not, each a gate with its own name.

- **Not MTPZ authentication.**  The same log says `credentials unavailable;
  provide ~/.mtpz-data to authenticate a Zune` and `MTPZ authentication will
  fail`: this development checkout has no MTPZ credentials, so the library read
  happened unauthenticated, which the ZMDB path permits.  MTPZ authentication has
  therefore still never run on Windows, and no write path should be attempted
  until it has - see `docs/BUILD_WITH_MTPZ.md`.
- **Not a write.**  Nothing was sent, deleted, finalised or read back byte for
  byte.  Saving two device tracks into the library - still a read, but a
  different operation - answered `get_object 0x01000045: failed (response 0x200F)`
  and the app reported `AccessDenied`.  Observed, not retried, not diagnosed here.
- **Not clean.**  `get_object_info 0x07000002` returns `0x2002` three times per
  scan against the very atom the scanner reports as a playlist record, and
  `get_representative_sample` reports `bad AUINT8 count 0 in 4-byte payload` for
  objects whose thumbnails then fail every method.  Recorded rather than asserted;
  `libzune/docs/` is where a diagnosis belongs, and neither code has been chased
  on Linux either.

The GUI crash that stood in front of all of this turned out to be a build
artefact rather than a Windows fault in the application.  The account and the
command that clears it are in `BUILDING.md` under *Incremental builds and the
compiler's include note*.

So a Windows build may now be described as building, launching, painting,
scanning, encoding, persisting, opening a Zune HD and reading its music library.
It may not be described as authenticating over MTPZ, syncing, deleting, or playing
anything back from a device.

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
- Nothing on this machine rebuilds safely by accident.  `BUILDING.md` explains
  why an edit to a header can relink the object that was already there, and the
  first Windows device session died of exactly that.  When a change appears to do
  nothing, `cmake --build build --clean-first` before believing any diagnosis.

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

1. Everything past the library read.  MTPZ authentication comes first, because this
   machine has no credentials and every Linux write gate was run with them; then
   one send, one deletion, byte readback, the firmware categories and firmware
   resume, each a real gate rather than a ported assumption.  The unanswered
   `0x2002` and `0x200F` recorded above sit in front of parts of that.  WinUSB
   cannot reset a device either, so `zune_breach()`'s recovery from an OpenSession
   wedge does not exist here and an unplug/replug is the workaround - see the
   driver guide.
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
10. Library folders display the way Qt spells a path internally -
    `E:/Temp/testmusic`, not `E:\Temp\testmusic` - in Settings, in onboarding and
    in the spoken-import pages, because nothing in `src/` or `qml/` has ever called
    `QDir::toNativeSeparators()`.  Cosmetic here, invisible on Linux where the two
    spellings are the same string, and a display-layer fix: the stored value, the
    SQLite row and the scanner use one form on both platforms, and
    `qml/OnboardingView.qml` removes a staged folder by comparing that very string,
    so the conversion belongs at each label and nowhere else.

## Order to close it in

1. Supply MTPZ credentials and authenticate a Zune on Windows, per
   `docs/BUILD_WITH_MTPZ.md`.  Until that is done the write paths are not even
   addressable from this checkout, so nothing below it can be attempted honestly.
2. Repeat the Linux hardware gates in the order Linux used, from here: library
   read (done 2026-10-20), one album send, one deletion, readback, then the podcast
   and audiobook categories and firmware resume - each with its own evidence, none
   inherited.
3. Answer the two responses the first reads left unanswered, `0x2002` on
   `GetObjectInfo(0x07000002)` and `0x200F` on `GetObject`, in `libzune/docs/`,
   before either is assumed benign or changed.
4. Add the remaining software gates: audible (unmuted) playback and a
   `spokenmediatool` phase.  Measure NVDEC against software on a real 1080p item before
   quoting a speed-up for Windows.
5. Native Windows path display for library folders - item 10 above.
6. Only then talk about Windows packaging or a Windows release channel.
