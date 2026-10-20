/* POSIX <pthread.h> stand-in for the one thing the app uses it for: blocking
 * SIGPIPE around a capture-thread write.  MSVC has no pthreads and Windows
 * never raises SIGPIPE - a write to a closed pipe fails with EPIPE instead -
 * so the mask is a documented no-op rather than a fake guarantee.
 * Linux and macOS never compile this directory. */
#ifndef ZUUNED_WIN_PTHREAD_H
#define ZUUNED_WIN_PTHREAD_H
#include "posix_shim.h"
#include <signal.h>

#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGHUP
#define SIGHUP  1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGUSR1
#define SIGUSR1 30
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif
#ifndef SIG_BLOCK
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

typedef struct { unsigned long __bits[2]; } sigset_t;

static __inline int sigemptyset(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    set->__bits[0] = 0;
    set->__bits[1] = 0;
    return 0;
}

static __inline int sigfillset(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    set->__bits[0] = ~0UL;
    set->__bits[1] = ~0UL;
    return 0;
}

static __inline int sigaddset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum > 64) { errno = EINVAL; return -1; }
    set->__bits[(signum - 1) / 32] |= 1UL << ((signum - 1) % 32);
    return 0;
}

static __inline int sigdelset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum > 64) { errno = EINVAL; return -1; }
    set->__bits[(signum - 1) / 32] &= ~(1UL << ((signum - 1) % 32));
    return 0;
}

static __inline int sigismember(const sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum > 64) { errno = EINVAL; return -1; }
    return (int)((set->__bits[(signum - 1) / 32] >> ((signum - 1) % 32)) & 1UL);
}

/* Nothing to block: Windows reports closed-pipe writes as EPIPE, which the
 * callers already handle, and SIGPIPE is never delivered. */
static __inline int pthread_sigmask(int how, const sigset_t *set, sigset_t *previous)
{
    (void)how;
    (void)set;
    if (previous) sigemptyset(previous);
    return 0;
}

#endif /* ZUUNED_WIN_PTHREAD_H */