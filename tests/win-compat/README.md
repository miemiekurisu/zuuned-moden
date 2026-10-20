# Windows POSIX shim gate

Run `pwsh -NoProfile -File tests/win-compat/run.ps1` from the checkout. It
compiles `compat/win/wincompat.c` together with `wincompat_probe.c` into a
console probe and runs it under a wall-clock guard, so a shim bug that blocks
fails the gate with the last section marker visible instead of hanging the
caller (timeout is `-TimeoutSeconds`, default 120, exit code 124).

The gate needs the MSVC x64 developer environment and the PCRE2 headers.
`run.ps1` sources `D:\dev\zuuned-deps\msvc-env.ps1` when `cl` is not already on
`PATH`; `-Deps` and `-Pcre2` override those locations. It uses no Qt, no device,
no network and no user media, and writes only below `ZUUNED_TMPDIR`.

The probe is built with the same `/FIposix_shim.h` force-include and the same
include directories as the `zuuned_wincompat` CMake target, so the shim is
compiled under exactly the conditions the application uses. MSVC treats that
option as case-sensitive: the lowercase spelling is a different option, which cl
ignores with warning `D9007`, and the shim silently stops being force-included.
Keep the uppercase form and treat any `D9007` in this gate output as a broken
gate rather than a passing run.

A good run prints one `--- section` marker per group and ends with
`171 checks, 0 failures, 0 skips` followed by `win-compat gate passed`. The
groups are:

- `tmpdir` - `ZUUNED_TMPDIR` resolves to a directory the shim can write into and
  a removal is visible to a later `stat()`.
- `mkstemps` - template fill, retained suffix, usable descriptor, `fstat()` size
  through the descriptor, and a leaf name short enough for the CRT.
- `utf8_name` - narrow UTF-8 open/stat/read/write round-trip plus the wide
  decode of the same name, so `stat()` and `_wstat64()` agree on size and type.
- `long_path` - paths past 260 characters through `openat()`, `unlinkat()` and
  the `\\?\` extended prefix, including `O_NOFOLLOW`.
- `large_file` - 64-bit `off_t`: a 3 GiB sparse fixture reports the right size
  to `stat()`/`fstat()` and `pread()` reaches the end of the sparse tail.
- `directory_descriptor` - `O_DIRECTORY` via `zuuned_open_directory()`,
  `fstatat()`, `flock()`, `opendir()` on the descriptor and `unlinkat()` with
  `AT_REMOVEDIR`, plus both readings of a number passed to `fstat()`: a raw NT
  handle and a descriptor the shim never handed out. The second is what
  `QFile::handle()` reports on Windows - Qt opens a `QFile` with `_wopen()` - and
  the audiobook importer is dead without it. The guard the raw-handle question
  needs is cleared at the start of `main()` and asserted standing again here, so
  the probe proves the shim reinstates it rather than a `main()` installing it.
- `opendir` - enumeration returns names rather than paths, distinguishes
  `DT_REG` from `DT_DIR`, survives `rewinddir()` and `dirfd()` fstats as a
  directory.
- `pipes` - `pipe()`/`poll()`/`read()`/`write()` plus `zuuned_fd_readable()`,
  including that a closed write end wakes a reader with `POLLIN|POLLHUP`
  (the log drain deadlocks without it) and that an invalid descriptor is
  reported instead of blocking.
- `mapping_and_publish` - `mmap()` contents match the file, `pread()` does not
  move the file position, and `zuuned_link()` publishes a staged file so both
  names observe the same bytes.
- `miscellany` - `strcasecmp`/`strncasecmp`/`strcasestr`/`strndup`, `getline()`
  over a text file, `O_CREAT|O_EXCL`, `rename()`, `unlinkat()`,
  `usleep()`/`clock_gettime()` monotonic progress and `zuuned_open_self_fd()`.
- `wide_stdio` - plain CRT `fopen`/`fread`/`fwrite`/`remove`/`rename` on
  non-ASCII names and past the 260-character limit, which the CRT only honours
  after the runtime-wide binary stdio mode and UTF-8 locale the shim installs.

Sections that the volume or environment cannot support report a skip, which is
why the probe distinguishes failures from skips; the accepted run above reports
no skips.

This gate certifies the portability layer only. It does not certify libusb
device access, MTPZ authentication, mpv or FFmpeg runtime loading, Qt rendering,
exFAT/FAT32 device-side filesystem behaviour, reparse-point and symlink
semantics, antivirus or cloud-provider path interference, or any hardware
readback. Those gates are `tests/win-*` runtime exercises, the existing
software gates and the hardware evidence documents.