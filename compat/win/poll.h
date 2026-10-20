/* POSIX <poll.h> stand-in: struct pollfd, the POLL* bits and poll() live in
 * posix_shim.h.  Windows has no poll(); wincompat.c emulates it for pipes.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_POLL_H
#define ZUUNED_WIN_POLL_H
#include "posix_shim.h"
#endif /* ZUUNED_WIN_POLL_H */