# Windows capture tool

`winshot.cpp` is the eye of the Windows usability gate.  It captures what a
window of a running `zuuned.exe` actually paints, with no device, no user audio
and without disturbing the desktop session, because a Qt Quick window can start
with a perfectly clean stderr and still render nothing.  Every launch case in
the gate is meant to end with a capture and a blankness check.

Build it from an MSVC x64 developer prompt at the checkout root:

    cl /nologo /W3 /Od /Zi /MD /EHsc /std:c++17 /DUNICODE /D_UNICODE ^
       tests\win-usability\winshot.cpp /Fe:build\win-usability\winshot.exe ^
       /Fo:build\win-usability\ /Fd:build\win-usability\winshot.pdb ^
       /link user32.lib gdi32.lib gdiplus.lib

`/Fd` is not decoration: without it the compiler drops an incremental
`vc140.pdb` into the checkout.  `<objidl.h>` has to precede `<gdiplus.h>`
because this file defines `WIN32_LEAN_AND_MEAN`, and `GetImageEncoders` takes
its `size` argument by value.

Usage:

- `winshot <hwndDecimal> <out.png>` - capture, write a PNG, and print
  `ok src=... rect=... distinct=... dominant=... mean=... black=... white=...`.
- `winshot --move <hwndDecimal> <w> <h>` - resize a window, so the compact
  Now-Playing layout can be reached without asking the user to drag anything.
- `winshot --foreground <hwndDecimal>` - raise a window before a capture that
  needs the screen fallback.
- `winshot --pid <processId>` - print `hwnd=<dec> pid=<dec> titled=<0|1>` for that
  process's visible window and capture nothing, for a harness that has a pid but
  cannot trust `Process.MainWindowHandle`.  A visible non-tool window with a title
  wins; an untitled visible window is still reported rather than nothing.

Qt Quick on Windows presents through the window's own swapchain, so `PrintWindow`
with `PW_RENDERFULLCONTENT` is the primary path and a screen `BitBlt` of the
window rect is the fallback; the process is per-monitor-v2 DPI aware so the
rect and the pixels agree.  Exit codes: `0` captured, `1` API failure, `2`
usage, `3` captured but blank - fewer than four distinct colours, or one colour
over 99.5% of the frame.  A healthy music page measures about 9,500 distinct
colours with the single most common colour near 61% of the frame.

## Companions

`inspect_scan_db.py` reads a `librarytool scan` database and prints ASCII
`key=value` lines - `tracks`, `fingerprints`, `probe_failures`, `watch_folders`,
`min_duration`, escaped `paths`/`titles`, then `OVERALL=ok|fail`. Expectations go
in as code points (`--expect-codepoints 0x6F22,0x8A5E`) and as path-length
expectations (`--min-longest-path 200`, or `--expect-long-segments` when the
fixture uses a repeated segment marker), so neither the gate's arguments nor its
console output has to survive a non-ASCII round trip. It is what proves a scan
stored a Chinese folder name and hashed the audio behind it, rather than merely
counting rows.

`make_scan_fixture.py` builds the library fixture those checks turn on: a plain
tone, a WMA stream inside a file named `.mp3`, an album folder named in Chinese
characters, and twelve nested folders that push the leaf path to 277 characters. It
writes the CJK name from code points so this file stays ASCII too, and it needs the
FFmpeg from `ZUUNED_WIN_DEPS` to make the audio. `BUILDING.md` has the three-command
scan gate built from it, currently `created=4` then `4 tracks added` then
`OVERALL=ok`.

`qml/Main.qml` answers to `--qa-probe`: while the flag is present the window logs
one line per state change and then every 1.5 s, naming the page, the onboarding
sheet, the Now-Playing island, the collapse state, the device panel, the window
size, the track and photo counts, and the queue with its position. `--qa-play
<file>` puts one real file in the queue so a gate can reach the ribbon and, after
`winshot --move`, the compact island. A capture proves that a window drew
something; only these lines prove that `--page` took effect and that the frame
belongs to the page rather than to the splash or the onboarding sheet. Both stay
silent unless asked, on either platform.

## Status

`run.ps1` is that gate and it is complete.  An unfiltered run makes fifteen
instrumented launches plus the tool probes and ends `launches=15 checks=53
failures=0 skips=0 killed=0` and `OVERALL=ok`:

    pwsh -NoProfile -ExecutionPolicy Bypass -File tests\win-usability\run.ps1

Phases run in this order.  `-Only` takes a comma list, and the scratch tree is
rebuilt at the start of every run, so pair a phase with `fixture` whenever it needs
the seeded library (`-Only fixture,pages`).

- `winshot` - compiles this tool into `build\win-usability\` and proves the build
  left no incremental `vc140.pdb` in the checkout.  Built on every run, filtered or
  not, for the run deletes the scratch tree first.
- `fixture` - the fixture, a real `librarytool scan` over it, and the inspector: four
  tracks, four fingerprints, zero probe failures, the Chinese album folder and a path
  past 260 characters present in the database.  Also seeds the library the page cases
  copy into their profiles.
- `pages` - one launch each for music, videos, photos, podcasts and audiobooks with
  the seeded library, plus the device page with no device attached.  Every one ends
  with a capture, a paint verdict and the app's own `[qa]` report.
- `settings` - the five settings tabs, each proving it is on the settings page.
- `collapse` - the header pinned at 0 and at 1, with the value the window reports.
- `compact` - one real file in the queue, then `winshot --move` through tall, narrow
  and tall again, reading the app's own ribbon and island state at each stage: the
  ribbon is retired below the headroom breakpoint, a queue plays through the island,
  growing the window brings the ribbon back, and shrinking it retires the ribbon with
  the island already up.
- `tools` - `transcodetool probe` on a real MP3 and on a WMA stream named `.mp3`,
  `librarytool names`, `librarytool customize`.
- `seek` - `playertool seek` and `play`, on the null audio driver.
- `single` - a second instance pointed at the same profile and the same lock suffix
  refuses in its own words and never builds a scene, while the holder keeps its window
  and still closes politely.
- `profile` - session log, settings ini, both databases, the instance lock and the Qt
  pipeline cache inside the profile, and the real profile unchanged before and after.

Four things it cannot certify, and does not claim to:

- **No Zune has ever been connected to a Windows build.**  Nothing here opens USB,
  and the device page is checked only in its healthy no-device state.
- Dismissing the Now Playing island by clicking its X needs a mouse.  The gate proves
  the island opens on resize and that the ribbon retires and returns with the window.
- Nothing needs the network, but a GUI launch does reach musicbrainz.org for artwork
  discovery exactly as it does on Linux, and a 503 there is recorded, not asserted.
- On a 1080p-class logical desktop the default 1280x800 window is already below
  `Theme.headroomCompactBelow`, so the compact island is the ordinary Windows view and
  the top ribbon needs an enlarged window by design.

Profile isolation, the piece that made any of that impossible to run honestly, is
now in place.  Every profile-owned path in the application goes through
`src/ProfilePaths.h`, so `ZUUNED_PROFILE_DIR` keeps a launch's `library.db`,
`spoken/spoken.db`, `State/logs/*.log`, caches and `Zuuned/Zuuned.ini` inside one
folder; a probe run proved that while the real `%APPDATA%\Zuuned` stayed
untouched.  `ZUUNED_LOCK_SUFFIX` isolates the single-instance lock so an
instrumented instance can run beside the daily one, `ZUUNED_MPV_AO=null` keeps it
silent, and with the variables unset every path stays where Qt would put it.

Qt's automatic shader/pipeline cache is the one profile file this application does
not open itself: Qt6Quick asks `QStandardPaths::CacheLocation` directly, so it wrote
into the real `%LOCALAPPDATA%` while the rest of the profile was elsewhere.
`src/main.cpp` now names `QSG_RHI_PIPELINE_CACHE_LOAD` and `QSG_RHI_PIPELINE_CACHE_SAVE`
inside the profile whenever `ZUUNED_PROFILE_DIR` is set.  A brand new profile logs one
`Could not open pipeline cache source file` warning before Qt has written that file
once: expected, recorded, and the price of leaving the real cache alone.

Three probe findings shape the gate.  A fresh profile shows the onboarding wizard
instead of the requested `--page`, so a page case has to dismiss onboarding first
or report the onboarding state rather than claim a page rendered.  And the splash
lasts about two seconds plus a fade, so a window captured 1.5 s after it appears
is the splash, not the page - settle for several seconds, or poll the capture
statistics until the frame stops being one flat colour.  And a handle that the
harness reports is not trustworthy by itself: PowerShell 7 reads
`Process.MainWindowHandle` once and keeps the answer, so a probe that looks at it
while the splash is still mapping keeps saying "no window" for an app the user can
see - one probe found no handle on 30 consecutive healthy launches, while
`winshot --pid 77180` returned the same handle that a fresh `Get-Process` reported.
`run.ps1` now refreshes the property, falls back to the pid scan, and records which
route answered.  For the same reason `Stop-Zuuned` reports `124` rather than
`ExitCode` after it had to destroy a process: a killed app must not be able to
report a clean shutdown.

A handle above 0x7FFFFFFF is the same trap somewhere else: it arrives negative from
`Process.MainWindowHandle` and positive from `winshot --pid`, so widening it has to
mask rather than cast, and only a run that needed the pid scan could find that.  And
because a case can die halfway, the script carries a trap that closes every instance
it started - an aborted run must not leave a live Zuuned holding a profile folder the
next run intends to delete.

No Zune has ever been connected to a Windows build, and nothing in this
directory touches USB.
