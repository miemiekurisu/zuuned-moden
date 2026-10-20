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

## Status

The capture tool works.  The launch gate that drives it is not written yet.  It
is meant to cover one case per `--page`, the five settings tabs, both collapse
states, a CJK and long-path `librarytool scan` fixture, `transcodetool probe`,
a silent `playertool` seek, single-instance refusal, session-log creation and
settings persistence, all asserting a painted frame plus a stderr free of
`qml:`/`TypeError`/`ReferenceError`.

One piece is missing before any of that can run honestly: **Windows has no
profile isolation**.  Every native gate relocates its profile with `$HOME`, but
`QStandardPaths` on Windows asks the shell known folders and ignores
`LOCALAPPDATA`/`APPDATA` overrides, so an instrumented launch reads and writes
the real profile and loses the diagnostic-capture `flock` to the instance that
is already running.  `src/ProfilePaths.h` declares the intended fix -
`ZUUNED_PROFILE_DIR` owns every profile file including `QSettings`, while the
user media folders, the temp dir and the Linux line stay exactly as they are -
and the implementation is still to come.

No Zune has ever been connected to a Windows build, and nothing in this
directory touches USB.
