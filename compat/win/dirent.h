/* POSIX <dirent.h> stand-in: opendir/readdir/closedir/dirfd come from
 * posix_shim.h and are implemented in wincompat.c.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_DIRENT_H
#define ZUUNED_WIN_DIRENT_H
#include "posix_shim.h"
#endif /* ZUUNED_WIN_DIRENT_H */