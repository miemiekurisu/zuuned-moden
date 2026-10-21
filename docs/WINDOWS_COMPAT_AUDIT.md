# Windows path and encoding compatibility audit

Dated 2026-10-21.  A whole-tree sweep for the Windows-only ways this application
can mis-spell a path or a byte string, done after three symptoms appeared on a
real Zune HD that Linux had never shown: an upload answered `Fail to save`, the
music root displayed as `/E:/Temp/testmusic`, and Settings reported
`watch folder unreachable` for a folder the user was looking at in Explorer.

All three were one defect.  None of them was a protocol defect: libzune was
handed a path that does not exist on Windows and reported that faithfully.  The
protocol, the driver and the device were right the whole time, which is worth
saying because every one of those symptoms reads like a device bug.

## The one defect: a path is not a URL

Qt's folder and file pickers hand QML a `QUrl`.  The UI stripped the scheme with
string arithmetic - `url.toString().replace("file://", "")` and friends - and
that is correct by luck on Linux and wrong on Windows:

| spelling | on Linux | on Windows |
| --- | --- | --- |
| `file:///home/user/Music` -> drop `file://` | `/home/user/Music`, absolute | - |
| `file:///E:/Temp/testmusic` -> drop `file://` | - | `/E:/Temp/testmusic` |

`/E:/Temp/testmusic` is not absolute.  It is a path relative to the current
directory of drive E:, so `QFileInfo::exists()` is false, `QDir::entryList()` is
empty, and every consequence follows mechanically: the scan indexes nothing, the
health check calls the folder unreachable on every pass, the row shows up with
Unix-looking slashes in the UI, and a transfer that resolves its destination
under that root cannot create the file - `Fail to save`.

`QUrl::toLocalFile()` is the decoder that gets both platforms right: it
percent-decodes and drops the leading slash exactly when a drive letter follows
it.  Hand-stripping does neither.

## The rule now: one spelling, three forms

`src/library/LibraryPaths.h` owns the conversion.  Header-only and QtCore-only,
so `LibraryDb` and the command-line `librarytool` use it without pulling QtQml
into a tool.

- **canonical** - `LibraryPaths::canonicalRoot(raw)`: storage and comparison
  form.  Decodes a `file:` URL, converts backslashes with
  `QDir::fromNativeSeparators()`, repairs an existing `/E:/` scar, and folds the
  drive letter to upper case.  This is what goes into `watch_folders.path`.
- **url** - `LibraryPaths::fileUrl(path)`: what `Image.source` and friends need.
  Wraps `QUrl::fromLocalFile()`, which emits `file:///E:/...` and escapes spaces
  and non-ASCII.  Never stored.
- **display** - `PathText::display(path)`: what a human reads.  Windows shows
  `E:\Temp\testmusic`; nothing is stored from it.

`src/PathText.h` is the QML-facing side: `canonical()` and `url()` delegate to
`LibraryPaths`, `display()` stays presentation-only.  Both
`canonicalRoot()` steps are the identity on Linux, so an existing Linux database
- and a backslash inside a Linux filename, where it is an ordinary character -
is byte-for-byte unchanged.

Entry points that now go through the rule, every one of them previously doing its
own string arithmetic:

- `qml/SettingsPage.qml:1616`, `qml/SettingsPage.qml:1675`,
  `qml/OnboardingView.qml:1854`, `qml/OnboardingView.qml:1867` - folder and file
  pickers storing a watch folder or a wallpaper.
- `qml/LibraryPhotosView.qml:234` - a device photo saved into the library.
- `qml/EnvironmentBackground.qml:51`, `qml/OnboardingView.qml:700`,
  `qml/SettingsPage.qml:455`, `qml/LibraryVideosView.qml:198`,
  `qml/QueueEntryView.qml:147` and `qml/QueueEntryView.qml:148` - the five places
  that built a URL by concatenating `"file://"` in front of a stored path.
- `src/LibraryService.cpp:772` and `src/LibraryService.cpp:785` - the C++ add and
  reload boundary, so a path arriving from anywhere is canonical before it is
  compared with a stored one.
- `src/LibraryService.cpp:2268` and `src/LibraryService.cpp:1090` - the health-check
  detail line and the `displayPath` each Settings row reads, so a folder appears the
  way the user names it and the stored form never leaks into a label.
- `qml/SettingsPage.qml:761`, `qml/SettingsPage.qml:600`,
  `qml/OnboardingView.qml:1822`, `qml/SpokenFolderImport.qml:61` and
  `qml/SpokenFolderImport.qml:78` - every label that prints a stored path, so a
  Windows user reads backslashes and a Linux user reads what was always there.
- `qml/OnboardingView.qml:1511` and `qml/SettingsPage.qml:852` - the typed-path
  placeholders, which suggested a Linux mount path to someone on Windows.

## Repair, because the scar is already in real databases

`LibraryDb::open()` calls `repairWatchFolderRoots()`
(`src/library/LibraryDb.cpp:410`) on every launch.  For each `watch_folders` row
whose canonical spelling differs from the stored one:

- if another row already holds the repaired spelling, this row is deleted - the
  `UNIQUE(path)` constraint would refuse the update, and the duplicate is exactly
  what the user just recreated by re-picking the folder;
- otherwise the path is updated in place.

Repairing the string is the whole job because a mangled root can never have
produced media - the scanner asked the OS whether it existed and the answer was
no - so there is nothing underneath it whose prefix has to follow.  Live
evidence, first launch after the fix:

```
[librarydb] repaired watch folder /E:/Temp/testmusic to E:/Temp/testmusic
```

## Why the drive letter is folded to upper case

Windows paths are case-insensitive; `watch_folders` dedupes with a byte
`UNIQUE(path)`.  `e:/Music` and `E:/Music` would therefore be two identities for
one folder: a track scanned under one never matches a file resolved under the
other, each row keeps its own unreachable state, and no health pass or watcher
can retire either.  The drive letter is the one component where folding is free.

This is deliberately narrow.  Full case-insensitive dedup of the rest of the path
is **not** done: a SQLite collation change would also make the `substr` prefix
matching the purge and stale-sweep paths case-insensitive on Linux, where case is
meaningful.  Two folders differing only in letter case are a user choice, and
both remain legal rows.

## Second sweep: a name the drive can hold, and a shell that is not sh

With the library root repaired the app scanned, browsed and synced, which moved the
symptoms somewhere else.  A second whole-tree pass looked at the other direction -
not a path the app *reads*, but a name the app *writes* and a command the app *runs*.
Three defects came out of it.  All three are found by reading; none of them was
reproduced on the Zune HD at hand, so each one below says what it would cost rather
than claiming a fixed failure.

**A device tag is not necessarily a legal Windows filename.**  An artist, album or
photo title is arbitrary text that came off another device: `P:44 / Vol. 2`,
`what?` or `a<b>` is an ordinary album name and an impossible Windows path
component.  Every save-to-library site sanitised device text for `/` alone, so
`QDir::mkpath()` returned false and the transfer answered `Fail to save` for a track
with nothing wrong with it - the same message the root-path defect produced, from an
unrelated cause, which is worth remembering the next time that string appears.

`LibraryPaths::safeComponent(name, fallback)` (`src/library/LibraryPaths.h:148`) is
now the one answer, applied at `src/DeviceService.cpp:797`,
`src/DeviceService.cpp:829`, `src/DeviceService.cpp:835`, `src/DeviceService.cpp:849`
and `src/DeviceService.cpp:854` - video, photo (subfolder *and* raw filename) and
track (artist/album *and* filename), which is every place device text becomes a file
name:

- Windows: the shell's illegal set `<>:"/\|?*` and the control characters
  `\x00-\x1f` become `_`; trailing dots and spaces are stripped because the shell
  hides them; a component spelling a reserved stem (`CON`, `PRN`, `AUX`, `CLOCK$`,
  `NUL`, `COM0-9`, `LPT0-9`) takes a `_` prefix, because those names belong to the
  namespace and a file called `NUL.mp3` cannot be reopened by path; a component that
  comes out empty, `.` or `..` takes the caller's fallback instead.
- Linux: `/` becomes `_` and nothing else changes, which is what the app did before,
  so an existing Linux library keeps the folder names it already has.

`LibraryPaths::caseKey()` (`src/library/LibraryPaths.h:96`) ships with it - fold
case on Windows, identity on POSIX - for anywhere a stored `filepath` meets a path
that arrived from outside.

**`system()` on Windows is `cmd.exe`, and `cmd.exe` does not speak sh.**
`zune_grab_photo_thumb()` (`libzune/src/thumbnail.c`) has an ffmpeg fallback that
builds a command line.  It quoted both paths with single quotes and redirected with
`2>/dev/null`.  `cmd.exe` does not treat `'` as a quote character and takes
`2>/dev/null` as "redirect stderr to a file named dev/null", so on Windows that
branch failed on every single call whatever it was pointed at, and did so silently -
the return code was checked, the reason was not.  It now has a `_WIN32` branch
(`libzune/src/thumbnail.c:144`): double quotes with each inner quote doubled by
`shell_quote_path()` (`libzune/src/thumbnail.c:34`), which refuses to emit anything
rather than emit a truncated command line, and `>nul 2>&1`.  The Linux and macOS
command strings are byte-for-byte what they were.

**An m3u line is a claim about a file, not a string equal to a `filepath`.**
`LibraryService::importPlaylistM3U()` (`src/LibraryService.cpp:2220`) compared each
entry verbatim against the database.  That holds only for a playlist this app itself
wrote, on Linux, with absolute paths.  Windows Media Player writes
`E:\Music\A.mp3`, iTunes writes `file://` URLs, and a hand-made or foreign-exported
playlist lists files relative to itself; compared as text, all three come back
"missing" and the user is handed an empty playlist out of a library that contains
every track on it.  Relative entries are wrong on Linux too, so the resolution is
unconditional rather than Windows-branched: decode a URL, take
`QDir::fromNativeSeparators()` (a no-op on Linux, where a backslash is an ordinary
character), resolve a relative entry against the playlist's own folder, then
`QDir::cleanPath()` - the form `LibraryScanner` stores - and compare through
`caseKey()` on both sides.

## Third sweep: what a playlist is written in, and a name nobody can reopen

The second sweep left two holes of its own making and one claim it had overstated.
All three came from re-reading that diff a day later rather than from a new symptom,
which is the check worth running on your own fix the day after it ships.

**Deciding what an m3u line *means* was half the job; deciding what its bytes *are* was
the other half.**  The fix above still read every line with `QString::fromUtf8()`.  m3u
declares no encoding and carries no magic.  This app writes UTF-8 without a BOM, so its
own exports were always fine; a playlist the user points at usually came from somebody
else, and on Windows somebody else defaults to the ANSI code page - Notepad's "ANSI"
save, Windows Media Player exports, most shareware converters - which is CP1252 in the
west and CP936 here.  Run through `fromUtf8()` those bytes arrive as U+FFFD runs, no
entry matches a row, and the import answers "0 added, 41 missing" for a playlist naming
forty-one songs the library plainly holds.  That is the same symptom as the resolution
defect, from an unrelated cause, one function deep.  A BOM is the milder instance of the
same carelessness: Notepad, PowerShell and Visual Studio all add one to a UTF-8 file, and
it corrupts exactly one entry - the first - which reads like a bug in the matching code
and not in the decoding.

`PlaylistText::decode()` (`src/library/PlaylistText.h:39`) owns that decision now: honour
a BOM, take UTF-8 when the bytes really are UTF-8, otherwise fall back to the locale's own
code page, which is precisely what "no BOM and not valid UTF-8" means on the machine that
wrote the file.  The UTF-8 test is `fromUtf8()` followed by the round trip: Qt replaces an
invalid sequence with U+FFFD, which re-encodes to `EF BF BD` and so never reproduces the
input, making the comparison evidence rather than a tautology.  The resolution moved out
of the lambda into `LibraryPaths::resolveEntry()` (`src/library/LibraryPaths.h:179`),
beside the helpers it shares with the scanner, so a command line can reach both; the import
is now decode, split, resolve, `caseKey()` (`src/LibraryService.cpp:2264`,
`src/LibraryService.cpp:2270`).

**`safeFilePart()` did not in fact "already apply this shape".**  The comment in
`src/library/LibraryPaths.h` said so of the reserved-stem rule, and it was inexact:
`VideoIdentity::safeFilePart` applied the illegal set and the trailing dot, and not the
device namespace.  So a Zune holding an object named `NUL.wmv` still imported to a file
that extraction reports as saved and nothing can afterwards open - the write succeeds, the
read fails, and the file sits there in Explorer.  The rule is now one function,
`LibraryPaths::isReservedWindowsName()` (`src/library/LibraryPaths.h:115`), called from
`safeComponent()` (`src/library/LibraryPaths.h:148`) and from `safeFilePart()`
(`src/library/VideoIdentity.h:50`).  It answers false on POSIX, where `NUL.wmv` is an
ordinary filename and the import does exactly what it did before.

**None of the second sweep's helpers had a test, and neither gate could have given them
one.**  `tests/win-compat` is deliberately Qt-free - it compiles the shim with the MSVC C
compiler and nothing else - so it cannot reach a C++ header, and the bash suites in
`ci/check.sh` do not run here.  The helpers are header-only, run on every save, and fail by
printing "Fail to save", the least diagnosable message the app has.  `librarytool paths`
(`src/library/librarytool.cpp:319`) is their home now, in the shape the file already used
for `names` and `customize`: 28 checks, with the expectation branched `#ifdef Q_OS_WIN`
wherever the right answer genuinely differs, so one source gates both platforms and a
Linux run is told in as many words that `NUL` stays `NUL`, that a backslash stays an
ordinary filename character, and that `caseKey()` folds nothing there.  Both gates still
reach the headers only through the compiled tool, so `paths` now runs from the usability
gate's tools phase beside `names` and `customize` rather than being a command that has to
be remembered.

## Console output

The console renders the active ANSI code page, so a gate that prints a Hebrew or
emoji path reads its own output as `?`.  The application already fixes this through
`zuuned_win_runtime_init()` (`src/main.cpp:50`).  `librarytool` did not, and on a
CP936 host its scan output was unreadable for precisely the folders worth
testing; it now calls `zuuned_win_console_utf8()` directly
(`src/library/librarytool.cpp:311`).

`devicetool`, `spokenmediatool` and `playertool` now put their console in UTF-8
as well, each calling `zuuned_win_console_utf8()` at the top of its own `main()`.
None of the three uses the POSIX surface, so each declares the helper itself
rather than forcing `compat/win/wincompat.h` through the Qt and vendor headers,
and `CMakeLists.txt` links those three targets against the compat objects.

## Diagnostics print UTF-8, not the ANSI code page

`qPrintable()` is `toLocal8Bit()`.  On a CP936 host every `[sync]`,
`[sync-canon]` and `[worker]` line naming a Chinese path therefore arrived in the
log as U+FFFD while the C layer beside it, which is handed `toUtf8()`, printed the
same path correctly.  The mojibake is the smaller half of the problem: the channel
an operator reads to find out why a save failed was naming a path that does not
exist on disk, for exactly the roots worth testing.  Everything under `src/` now
uses `qUtf8Printable()`, which is the same bytes on Linux.  The one call among
them that is not logging is mpv's `alang` option (`src/player/MpvController.cpp:53`),
where UTF-8 is what mpv expects anyway.  The harnesses under `tests/` and `tools/`
keep `qPrintable()` - their lines are assertion text, not user paths.

## Checked and cleared - do not re-litigate these

Every narrow-path and hand-rolled-URL site in the tree was read, not grepped for a
pattern.  These look like defects and are not:

- `src/diagnostics/SupportReport.cpp:113` - `QFile::encodeName()` plus CRT
  `access()`, in the `#else` of `#if defined(Q_OS_WIN)`.  Local 8-bit is right
  there: that branch is the Linux one, and the paths are ASCII sysfs entries.
- `src/DeviceService.cpp:895` `udevRulesApply()` - returns false on Windows and
  the access card then names WinUSB and Zadig instead of a rule file.  The udev
  chain is Linux-only by design and never runs here.  `src/DeviceWorker.cpp:203`
  was the one leftover that still told a Windows user to check a udev rule when no
  device answered; it now names the WinUSB binding.
- `LibraryService::ensureGvfsMounts()` - its regex anchors on `/run/user/<uid>/gvfs/`,
  which no Windows path can match, so it is inert rather than wrong.
- Picker-to-C++ URLs (`qml/CustomizeSheet.qml:1273`, `qml/SpokenFolderImport.qml:108`,
  `qml/SettingsPage.qml:1686`, `qml/SpokenCoverSheet.qml:88`) - the C++ side calls
  `QUrl::toLocalFile()`.  Correct all along; only the QML-side hand-stripping was
  not.
- `compat/win/wincompat.c` - decodes UTF-8 with `MB_ERR_INVALID_CHARS` first and
  the ANSI code page second, so both byte conventions arrive intact.  Its
  invalid-parameter handler installs lazily, so tools need no runtime init for it.
- `src/artwork/ArtworkPipeline.cpp:56` `stamp()` compares `st_dev` and `st_ino` to
  refuse a cache image that is its own source.  That is sound here, not a POSIX
  leftover: `fill_stat_from_handle()` (`compat/win/wincompat.c:261`) fills the pair
  from the volume serial and the NTFS file index, and the `file_identity` phase of
  `tests/win-compat` pins it - two files never share an identity, neither identity
  is zero, and it survives the rename `QSaveFile` publishes with.
- The remaining `file://` mentions under `src/` and `qml/` all go through
  `QUrl::toLocalFile()` (`src/LibraryService.cpp:1633`, `src/LibraryService.cpp:1942`,
  `src/spoken/SpokenMediaService.cpp:33` and the rest of them), which is the decoder
  that gets a drive letter right.  Only the hand-stripping was ever wrong.
- `QFile::rename()` at `src/library/LibraryDb.cpp:362` has no POSIX replace semantics
  on Windows, and does not need them: it is guarded by `!QFile::exists(backup)` in
  the same statement.
- `QStandardPaths` Music/Pictures/Movies and the `QLockFile` in RuntimeLocation
  resolve to real Windows locations; nothing in the tree hardcodes a `$HOME` subpath
  for the app's own data.
- `QT_QPA_PLATFORMTHEME=xdgdesktopportal` is inside `#if !defined(Q_OS_WIN)`
  (`src/main.cpp:89`), so a Windows run never asks for a portal that is not there.
- `ci/check.sh` and everything under `packaging/` is Linux-only by construction, and
  there is no CI workflow to run it - `.github/` holds issue templates only.

## Encoding: `QFile::encodeName()` is not UTF-8

Four call sites handed the POSIX surface Qt's local 8-bit codec, which on Windows
is the ANSI code page.  Measured on this CP936 machine across five scan roots -
ASCII, Chinese, Hebrew, Hangul, emoji - the scanner found its track under the
ASCII and Chinese roots and nothing under the other three: `stat()` failed, the
copy fingerprint returned false, and the scan exited 0 looking complete.  CP936
covers Han, so a Chinese host cannot see this defect; an English host meets it on
its first Hebrew album.  `LibraryScanner`, `ArtworkPipeline`, `ArtistImageService`
and `OnlineAlbumArtService` now pass `toUtf8()` - the same bytes on Linux.  Full
account in `BUILDING.md` under *Path encodings on a non-English Windows*.

## What is proven, and by which gate

- `tests/win-usability/watch_folder_repair.py` seeds four roots - a `/E:/` scar, a
  `/e:/` scar whose repaired spelling collides with a healthy row, that healthy
  row, and a clean path - then opens the database with a real `librarytool scan`
  and asserts the survivors.  The collision row is the interesting one: without
  the drive-case fold it repairs to `e:/Scar Duplicate`, a *different* UNIQUE
  value, and two identities for one folder survive.  On POSIX the same four
  strings are ordinary absolute paths and the gate asserts they are untouched.
- The `fixture` phase of `tests/win-usability` scans one root per script into its
  own database, so a root that works cannot mask one that does not - the exact
  way the Chinese root used to hide the other three.
- The `file_identity` phase of `tests/win-compat` is the contract the artwork cache
  identity check depends on, asserted against the shim itself rather than assumed
  from the Win32 documentation.
- `tests/win-compat` and `tests/win-usability` are source and runtime gates;
  neither opens a USB handle.  The compat gate is currently 184 checks.
- `build\librarytool.exe paths` is 28 checks over the helpers above, and
  `names`/`customize` still pass on the same build.  All three ran here, against the
  executables produced by `cmake --build build --clean-first`.
- **"The executable was relinked" is not evidence, and that is a finding of this
  sweep.**  The Ninja rules carry `deps = msvc`: Ninja learns a translation unit's
  includes by parsing the `/showIncludes` echo from `cl`, against the literal English
  prefix.  On this localized host the notes arrive as `注意: 包含文件:`, nothing matches,
  and `ninja -t deps CMakeFiles\zuuned.dir\src\LibraryService.cpp.obj` answers `#deps 0` -
  not one header dependency recorded for any object in the build.  Editing a header-only
  helper above and rebuilding therefore recompiles nothing, relinks anyway, and prints
  enough AUTOMOC steps to look entirely honest, which means a gate can pass against a
  binary that predates the very fix it is supposed to prove.  Every counter in this file
  comes from a `--clean-first` rebuild instead; `BUILDING.md` has the diagnosis and the
  habit.
- `tests\win-usability` ran against that `build\zuuned.exe`: 17 launches,
  94 checks, 0 failures, `OVERALL=ok`, including the Han-spelled profile folder and
  the two profile-isolation checks that compare the real profile byte-for-byte
  before and after.  One qualification, because the gate's own `SCOPE` line only
  speaks for the harness: a Zune HD was plugged in while it ran, so the application's
  own hotplug auto-connect opened and read the device inside every isolated profile.
  Those profiles hold no library and no transfer queue, so nothing was written to the
  player, but "no device handle" is true of the harness and not of the app.
- Device facts are in `docs/WINDOWS_PORT.md` and are hand-tested, not gated.

## Not re-checked by this sweep, and what it would take

Two findings were left as findings on purpose, because the honest fix is bigger than
the defect and pretending otherwise would put an unverified claim in the tree.

- **Four libzune entry points still build POSIX command lines, and none of them is
  called.**  `zune_arm_photo()` (`libzune/src/photo.c:316`) writes a
  `/tmp/zunelinux_photo_*` path and shells out to `sips` or `ffmpeg` with
  `2>/dev/null`; `zune_snap_thumb()` (`libzune/src/util.c:379`) does the same to
  `ffmpeg`; the `popen()` sites in `libzune/src/transcode.c` carry a hardcoded
  `/opt/homebrew/bin/ffmpeg`.  All of them repeat the single-quote and `/dev/null`
  assumptions the photo-thumbnail branch above just lost, and `transcode.c` adds an
  absolute macOS path no Windows `PATH` satisfies - `/FIposix_shim.h` does make its
  `popen` resolve to `_popen`, which is exactly how it would fail: the shell starts,
  the program is not there.  They are public API with **no callers** in `src/`,
  `tools/` or `tests/`.  The app owns its `transcode/` layer and probes media
  in-process instead, including the one probe that is live: `zune_probe()`, called
  from `src/library/LibraryScanner.cpp:185`, compiles here as the `USE_LIBAV`
  in-process libavformat variant (`libzune/src/util.c:623`, with the define at
  `CMakeLists.txt:181`) - the `fork()`/`execl()` variant beside it
  (`libzune/src/util.c:712`) is not compiled on Windows at all.  Patching the shell
  sites would mean patching code nobody has run here; whoever wires one up meets
  this first.
- **`zuuned.exe` has no `<longPathAware>` in its embedded manifest.**  Dumped with
  `mt.exe -inputresource:"build\zuuned.exe;#1" -out:manifest.xml`, it contains
  `asInvoker` and nothing else.  So the C layer escapes the 260-character limit -
  that is what the `\\?\` prefix in `compat/win/wincompat.c` buys - while the Qt/C++
  side (scanner, artwork cache, database, exports) stays capped unless the machine
  sets `LongPathsEnabled`.  A deep album folder on a long drive name is the failure
  this leaves live.  The fix is a manifest input, which has to be verified by
  relinking and running the app:
  `target_link_options(zuuned PRIVATE /MANIFEST:EMBED
  /MANIFESTINPUT:packaging/win/zuuned.manifest)`, keeping the existing `asInvoker`
  block, then re-dumping the resource to confirm the entry landed.  Deliberately not
  shipped unverified.
- The scanner's cycle guard is `QFileInfo::isSymLink()`
  (`src/library/LibraryScanner.cpp:328`), and Qt does not report a Windows junction
  as a link there, although the shim's own `lstat()` does classify
  `IO_REPARSE_TAG_MOUNT_POINT` as `S_IFLNK` (`compat/win/wincompat.c:249`).  Making
  a junction takes privileges or developer mode and nothing this app indexes is
  expected to contain one; the asymmetry is recorded rather than guessed at.
  Untested either way.
- **`safeComponent()` bounds nothing but spelling.**  A component longer than 255
  characters - legal on a Zune, since MTP has no such limit - still fails `mkpath()`
  on Windows and the save still reports a plain failure.  Truncating a name is a
  visible change to a user's folder layout and needs a collision rule with it, which
  is a bigger decision than this sweep.
- **Playlist export names the file with its own hand-rolled rule.**
  `LibraryService::exportPlaylists()` replaces the illegal set but not a reserved stem
  or a trailing dot/space, so a playlist called `NUL` exports to a file nobody can
  reopen.  Routing it through `safeComponent()` would fix that and would also rename
  existing Linux exports, whose names contain characters Linux allows and the inline
  rule did not, so the directory would fill with both old and new spellings.  Left as
  it is, deliberately.
- **UTF-16 m3u is still not decoded.**  Notepad's retired "Unicode" option writes
  `FF FE` and interleaves a NUL through every entry; `PlaylistText::decode()` takes it
  for bytes in some code page and every entry comes back missing.  The export this app
  writes, Notepad's current default, WMP and iTunes are all covered; this is the one
  spelling that is not.

## Known gaps, on purpose

- Case-insensitive dedup below the drive letter: see the reasoning above.
- UNC roots are produced for `file://host/share` on Windows and are untested
  against a real share; the code path is unit-probed, not device-probed.
- `~/Music` in a log is the redactor, not a mangled path: see
  `src/diagnostics/Redactor.cpp`. It was the most convincing false lead in this
  investigation.
- Two Linux-only facts a Windows support report cannot state.  `graphics_devices`
  is read from sysfs and is empty here - nothing enumerates WMI or DXGI in its
  place - and the `st_uid == geteuid()` owner checks in
  `src/diagnostics/SessionLog.cpp` are vacuous, because the shim reports uid 0
  for every file and `geteuid()` answers 0 (`compat/win/wincompat.c` says why).
  Neither loses or corrupts data; both simply report less than Linux does.
- One Windows device, one family.  Every device statement here is a Pavo Zune HD
  statement.
