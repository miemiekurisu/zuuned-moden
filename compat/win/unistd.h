/* POSIX <unistd.h> stand-in for MSVC builds.  Everything lives in posix_shim.h,
 * which CMake force-includes; this file only satisfies `#include <unistd.h>`.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_UNISTD_H
#define ZUUNED_WIN_UNISTD_H
#include "posix_shim.h"
#endif /* ZUUNED_WIN_UNISTD_H */