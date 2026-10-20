/* POSIX <sys/file.h> stand-in: flock() and the LOCK_ constants live in
 * posix_shim.h.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_SYS_FILE_H
#define ZUUNED_WIN_SYS_FILE_H
#include "../posix_shim.h"
#endif /* ZUUNED_WIN_SYS_FILE_H */