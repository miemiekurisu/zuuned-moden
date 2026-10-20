# Windows port status

Dated 2026-10-20, updated 2026-10-21.  This is where the native Windows build of Zuuned actually
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
  until it has - see `docs/BUILD_WITH_MTPZ.md`.  That was a missing credential
  file rather than a platform defect, and it is resolved below.
- **Not a write.**  Nothing was sent, deleted, finalised or read back byte for
  byte.  Saving two device tracks into the library - still a read, but a
  different operation - answered `get_object 0x01000045: failed (response 0x200F)`
  and the app reported `AccessDenied`.  Observed, not retried, not diagnosed here,
  which was the right call: the answer was authentication, and the same save
  completed once MTPZ credentials were on the machine.
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
scanning, encoding, persisting, opening a Zune HD, reading its music library,
authenticating over MTPZ, and moving audio in both directions by hand.
It may not be described as syncing a queue unattended, deleting, or playing
anything back from a device.

## MTPZ, and the first writes (2026-10-21)

**MTPZ authenticated on Windows, and the first Windows device write completed.**

The `credentials unavailable` line had the same cause on both platforms and only
one platform showed it: the public source has no `libzune/src/mtpz_keys.h`, so
`ZUNE_HAS_EMBEDDED_MTPZ` is undefined and a build from it has no compiled keys at
all.  The development Linux checkout carries that header privately; the Windows
checkout does not.  Putting the five-line file at `%USERPROFILE%\.mtpz-data`, which
the loader already looks at because Windows never sets `HOME`, changed the log to

```
[mtpz] loaded keys from ~/.mtpz-data
[mtpz] handshake complete
[libzune] MTPZ authentication successful
```

With authentication in place the operator hand-tested both directions against the
same Zune HD: a track from `E:\Temp\testmusic` sent to the player, and a device
track saved back into the library.  Both completed and the operator confirmed both.
That is the first Windows write and the first Windows read-back of bytes this app did
not write on Linux.  Nobody has certified what the player does with either file:
device-side playback stays its own gate below and is not claimed here.

Three defects stood in front of it, all Windows-only, all root-caused, all now
gated.  They were never protocol bugs - libzune was asked for a path that did not
exist and answered truthfully:

- **The `/E:/` watch-folder scar.**  Folder pickers hand QML a `file:` URL and the
  UI stripped the scheme with string arithmetic, which on Linux happens to produce
  the correct `/home/user/Music` and on Windows produces `/E:/Music` - not an
  absolute path there, but a path relative to the current directory of drive E:.
  The folder therefore scanned nothing, reported itself unreachable on every
  health check, and any save built underneath it failed.  `src/library/LibraryPaths.h`
  is now the single spelling rule and `LibraryDb` repairs rows an older build
  already stored; `docs/WINDOWS_COMPAT_AUDIT.md` is the whole account.
- **file:// built by concatenation.**  Prefixing a stored path with the file://
  scheme by hand spells a Windows path as file://E:/Music, which parses as host
  `E:` and loads nothing, so album art and wallpapers were blank for reasons that
  had nothing to do with the image.  `PathText.url()` wraps
  `QUrl::fromLocalFile()` instead.
- **`librarytool` printing paths as `?`.**  The console renders the ANSI code page,
  so a gate that scans a Hebrew album folder could not read its own output.

Still not proven on Windows, each its own gate: a Classic, Draco or 80/120 (every
Windows device fact here is one Pavo Zune HD); deletion or purge; podcast and
audiobook transfer; photo and video transfer; device-side playback.  At that
point the finalise, re-index and byte readback evidence was still Linux
evidence; the next section is what closed part of it the same day.

## The round trip, logged (2026-10-21)

Those two directions were confirmed by an operator watching a window.  Both have
since been run again so that the log says it, and the bytes that came back were
compared with the bytes that went out.

`qml/Main.qml` accepts `--qa-device "<file>|<artist>|<album>|<title>"`: it queues
that one track through the ordinary `SyncEngine` admission, starts a sync, and
reports every decision as a `[qa-device]` line.  With the file field left empty
the same flag instead looks `<title>` up in the device inventory and runs the
ordinary Save-to-Library download for it.  Without the flag the timer never
starts and neither `Connections` block enables, so an ordinary launch behaves
identically on both platforms.

The upload used a folder and a file name in Chinese under `E:/Temp/testmusic`:

```
[qa-device] add added=1 duplicates=0 onDevice=0 rejected=0 ambiguous=0
[sync] forge done: 1 ok, 0 failed
[qa-device] sync-summary elapsedMs=1590 tracks=1 videos=0 photos=0 skipped=0 failed=0
[qa-device] upload-settled deviceTracks=0 failed=0 tracks=1
```

The player went from 186 tracks to 187.  The next launch found 187, and the pull
pass saved that title back into the library, where the MP3 audio stream is
byte-for-byte the 401,239 bytes that went in.  The difference between the two
files is metadata only: the source carries a 45-byte ID3v2.4 tag, the copy
carries the 1,096-byte ID3v2.3 tag this app writes before a send plus the
128-byte ID3v1 it appends.

Two launches are what a finished sync forces rather than a shortcut taken: it
finalizes with CleanDataStore and releases the device, and `awaitingDisconnect`
refuses to breach back in.  The second run is therefore also the evidence that
the upload survived the device's own re-index and was not merely accepted.

Getting a legible log out of this was itself two Windows-only fixes.
`qPrintable()` is `toLocal8Bit()`, so on this CP936 machine every `[sync]` line
naming that Chinese path arrived as U+FFFD while the C layer printed the same
path correctly - `src/` now uses `qUtf8Printable()` throughout.  And the harness
first tested its own verdict behind a device-presence guard that the completed
sync had already cleared, so a clean upload printed no verdict of its own; it
now latches on the sync summary.  Both are accounted for in
`docs/WINDOWS_COMPAT_AUDIT.md`.

What this does not say: nobody has listened to either file.  Device-side
playback stays its own gate, below.

## The ordinary controls, both directions, in one sitting (2026-10-21)

The `--qa-device` harness above puts one track on the wire because that is what
a harness is for.  What a person does with the application is the rest of it,
and the rest of it was then done by hand through the ordinary controls on the
same machine and the same Pavo, against the build that carries MTPZ credentials.

Two files went onto the queue from the Music page with `+ all music` - one from
`E:/Temp/testmusic/QA Windows`, one from the folder named in Chinese - and
`sync now` was the only command given:

```
[sync] retagged to ID3v2.3: E:/Temp/testmusic/QA Windows/Sine QA 440.mp3
[sync] retagged to ID3v2.3: E:/Temp/testmusic/测试音乐/正弦 660.mp3
[libzune] created album "QA Windows" by "Unknown Artist" (1 tracks, id=100663563)
[libzune] created album "测试音乐" by "Unknown Artist" (1 tracks, id=100663564)
[sync] forge done: 2 ok, 0 failed
[libzune-finalize] 0x9108 result: 0 (0=ok)
[libusb] closed
```

Two albums, the second named in Chinese, `2 ok, 0 failed`, and the device browser
header counted from 187 tracks to 189 while the window stayed open - that number
is the player's listing, not this app's idea of it.  The queue tab reads
`last sync: 2 tracks` over `no items queued`.  Vendor finalise answered `0x9201`
and `0x9202` with `0x2005` exactly as it does on Linux, CleanDataStore answered 0.

The other direction was a right-click on a row of the device browser:
`Save to Library` wrote 20,992,730 bytes to

```
C:\Users\Kurisu Chan\Music\Zuuned Imports\J. S. Bach\Messe in h-Moll, BWV 232\ Gloria- Cum Sancto Spiritu.mp3
```

and the library re-counted itself to `8 source rows, 6 displayed songs` and pulled
the embedded artwork out of the file.  Three facts from that one file are worth
keeping because none of them was expected:

- the first four bytes on disk are `30 26 b2 75`, the ASF GUID, while the object
  name the device gave ends `.mp3` - the WMA-inside-an-mp3 case the sync layer
  already sniffs for.  A pull keeps the device's own name and the scanner judged
  the payload, so the track indexed and gained art.  Anything that trusted the
  extension would have called that file broken.
- the album name the device reported ends in a space, visible in the app's own
  log line as `.../Messe in h-Moll, BWV 232/ Gloria-...`.  A Windows directory
  cannot keep a trailing space and the one on disk reads
  `Messe in h-Moll, BWV 232`, so the name landed in the only spelling this
  platform accepts.  Nothing was written back to the device to agree with it.
- `~/Music/Zuuned Imports/...` in that same log line is
  `src/diagnostics/Redactor.cpp` masking the home directory, not a mangled path:
  the file sits where the row says it does.  The same false lead is recorded in
  `docs/WINDOWS_COMPAT_AUDIT.md`.

Settings said the rest without being asked: the Library tab lists both roots the
way Windows spells them (`C:\Users\Kurisu Chan\Music\Zuuned Imports` and
`E:\Temp\testmusic`), and the Health tab reads `Watch folders reachable` `2 of 2`
and `Zune connected` `ready`.  That display has assertions behind it now rather
than a screenshot - see the path-spelling bullet under what the software gates
prove.

What a finished sync used to cost here is closed.  The panel no longer sits on
`Disconnected - the Zune is re-indexing` once the last write is finalised - the first
rung replaces it with `reconnecting in 6s` - and
`libusb_hotplug` still reports no capability on this machine, so
`src/UsbWatcher.cpp` still polls the device list every 500 ms for a change that a
player re-indexing without leaving the bus never produces.  What changed is that
the application no longer waits for an arrival that cannot happen on this
platform: the finalize chain arms its own reconnect.
`DeviceService::scheduleReprobeAfterReindex()` runs a bounded ladder of 6, 12,
20, 30, 45 and 60 seconds, and each attempt IS a real `zune_breach()`, because an
OpenSession is the only honest readiness probe and a player mid-re-index answers
late rather than never.  The gaps grow because a breach already auto-resets a
wedged session once, so hammering the same player every second would trade one
wedge for another.  A real arrival or an unplug owns the reconnect and cancels the
ladder outright; a manual `connect` that fails stays failed rather than turning
into a retry loop on a device the user can see is not answering; sync, purge,
pull and eject reset or drop the budget when they take the device.  When the
ladder runs out it logs `re-index reconnect ladder exhausted - connect manually`
and leaves the panel on its last real error instead of inventing a state.  Seen on
the Pavo the same day with nobody touching `connect`:

```
[sync] forge done: 1 ok, 0 failed
[libzune-finalize] 0x9108 result: 0 (0=ok)
[libusb] closed
[device] re-index reconnect: attempt 1 of 6 in 6000ms
[mtpz] handshake complete
[libzune] MTPZ authentication successful
[zunedb] scanned: 190 tracks, 0 videos, 0 photos, 16 albums, 12 artists, 0 playlists, 0 photo albums
[device] storage: 5.1/15.5 GB (used/total), attributed: music 4.88 + video 0.00 + photo 0.00 GB
```

That run went through `--qa-device`, which drives the ordinary
`SyncEngine.addTracks` / `startSync` admission and the ordinary
`DeviceService.saveTrackToLibrary`, so the same sitting carried both directions
again on this build: one 193,833-byte mp3 sent with `forge done: 1 ok, 0 failed`
and the device's own listing up from 189 tracks to 190, then that object read
back as `SineQA550.mp3` at 195,015 bytes with `readbackDurationMs=8000` - the
duration this app declared on the way out - into `Music\Zuuned Imports`, where
the library counted itself from 6 tracks to 7.  `awaitingDisconnect` still
refuses to breach back in *while* the player re-indexes, on both platforms; the
ladder is what runs after it.

Neither QA run cleaned up after itself, and this sitting does not claim it could:
device-side deletion is an open gate on Windows, so `ZuunedQA / SineQA550` is still on
the Pavo and the copy pulled back from it is still in `Music\Zuuned Imports`.  A
future run inherits both; removing them is part of closing the deletion gate, not
housekeeping to do by hand in the meantime.

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
  encoder and profile probes and ends `launches=17 checks=94 failures=0
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
- Native path spelling, asserted by the running application rather than read off a
  screenshot.  The `music` page case reads fields the app reports about itself
  through `--qa-probe` and pins five of them: `wfRows >= 1` that the seeded library
  reached it as a watch folder, `wfRaw = 0` and `wfNative = wfRows` that every
  watch-folder label is spelled the way this platform spells it and that no forward
  slash survived into one, `wfStored = wfRows` that storage still holds one
  canonical forward-slash identity per folder, and `wfLabel = 1`, which asks
  `PathText::display()` (`src/PathText.h:49`) what it would make of the stored path
  and refuses unless the row is showing exactly that string.  The live profile
  answers `wfRows=2 wfNative=2 wfRaw=0 wfStored=2 wfLabel=1`.  What this retires is
  the weakest claim this document ever carried: `/E:/Music` reached a release
  because a person read the label instead of a gate reading it.
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

1. Everything past audio.  Deletion and purge, the firmware categories and
   firmware resume, podcast, audiobook, photo and video transfer, and every model
   but this Pavo Zune HD - each a real gate rather than a ported assumption.  Of
   the two responses the first reads left open, `0x200F` on `GetObject` is
   explained: the same save that added MTPZ credentials made that write succeed,
   so it was an unauthenticated save and not a refusal.  One is still unanswered,
   `0x2002` on `GetObjectInfo(0x07000002)`, three times per scan.  WinUSB
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
   Native path display, which was the last entry in this list, is gated now - see
   the `wf*` bullet above.  Where it reaches without a gate is narrower than it
   was: the labels at `qml/OnboardingView.qml:1822`,
   `qml/SpokenFolderImport.qml:61` and `src/LibraryService.cpp:2268` share
   `PathText::display()` with the Settings row the gate does pin, and the repair
   recorded in `docs/WINDOWS_COMPAT_AUDIT.md` is a stored-path concern that the
   `watch_folder_repair` phase already covers.

## Order to close it in

1. Play back on the player what the round trip above put on it.  Both files are
   there, in both directions, and nobody has heard either, so "it transfers"
   still does not mean "it plays".
2. Repeat the remaining Linux hardware gates in the order Linux used, from here,
   each with its own evidence and none inherited: library read (done 2026-10-20),
   one track send with byte readback (done 2026-10-21), then one deletion, then
   the podcast and audiobook categories and firmware resume, then photos and
   video, then a Classic, a Draco and an 80/120.
3. Answer `0x2002` on `GetObjectInfo(0x07000002)` in `libzune/docs/` before it is
   assumed benign or changed.  `0x200F` needs nothing further - see above.
4. Add the remaining software gates: audible (unmuted) playback and a
   `spokenmediatool` phase.  Measure NVDEC against software on a real 1080p item before
   quoting a speed-up for Windows.
5. Only then talk about Windows packaging or a Windows release channel.
