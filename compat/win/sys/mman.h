/* POSIX <sys/mman.h> stand-in: mmap/munmap/msync and the PROT_, MAP_ and MS_
 * constants live in posix_shim.h.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_SYS_MMAN_H
#define ZUUNED_WIN_SYS_MMAN_H
#include "../posix_shim.h"
#endif /* ZUUNED_WIN_SYS_MMAN_H */