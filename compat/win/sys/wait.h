/* POSIX <sys/wait.h> stand-in.  The only fork()/waitpid() caller in the shared
 * sources is the external-ffprobe path in libzune, which is compiled out when
 * USE_LIBAV is on; the macros exist so the header stays includable.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_SYS_WAIT_H
#define ZUUNED_WIN_SYS_WAIT_H
#include "posix_shim.h"

#ifndef WNOHANG
#define WNOHANG   1
#define WUNTRACED 2
#endif
#ifndef WIFEXITED
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WCOREDUMP(s)   ((s) & 0x80)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) ((((s) & 0x7f) + 1) >> 1 > 0)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)    WEXITSTATUS(s)
#endif

#endif /* ZUUNED_WIN_SYS_WAIT_H */