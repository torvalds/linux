/* Public domain. */

#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>

#define signal_pending_state(s, x) \
    ((s) & TASK_INTERRUPTIBLE ? SIGPENDING(curproc) : 0)
#define signal_pending(y) SIGPENDING(curproc)

#endif
