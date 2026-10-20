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

`winshot` and the two companions above work.  `run.ps1` is the launch gate being
written around them and is **not finished**: it implements the capture-tool phase,
then prints the cases it still owes and exits non-zero, so a partial run cannot be
read as a green light.  What it still owes is one case per `--page`, the five
settings tabs, both collapse states, the CJK and long-path `librarytool scan`
fixture, `transcodetool probe`, a silent `playertool` seek, single-instance
refusal, session-log creation and settings persistence, and a before-and-after
comparison of the real profile - each asserting a painted frame plus a stderr free
of `qml:`/`TypeError`/`ReferenceError`.

Profile isolation, the piece that made any of that impossible to run honestly, is
now in place.  Every profile-owned path in the application goes through
`src/ProfilePaths.h`, so `ZUUNED_PROFILE_DIR` keeps a launch's `library.db`,
`spoken/spoken.db`, `State/logs/*.log`, caches and `Zuuned/Zuuned.ini` inside one
folder; a probe run proved that while the real `%APPDATA%\Zuuned` stayed
untouched.  `ZUUNED_LOCK_SUFFIX` isolates the single-instance lock so an
instrumented instance can run beside the daily one, `ZUUNED_MPV_AO=null` keeps it
silent, and with the variables unset every path stays where Qt would put it.

Two probe findings shape the gate.  A fresh profile shows the onboarding wizard
instead of the requested `--page`, so a page case has to dismiss onboarding first
or report the onboarding state rather than claim a page rendered.  And the splash
lasts about two seconds plus a fade, so a window captured 1.5 s after it appears
is the splash, not the page - settle for several seconds, or poll the capture
statistics until the frame stops being one flat colour.

No Zune has ever been connected to a Windows build, and nothing in this
directory touches USB.
