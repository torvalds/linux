/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_TYPES_H
#define _LINUX_SIGNAL_TYPES_H

/*
 * Basic signal handling related data type definitions:
 */

#include <linux/types.h>
#include <uapi/linux/signal.h>

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct ucounts;

/*
 * Real Time signals may be queued.
 */

struct sigqueue {
	struct list_head list;
	int flags;
	kernel_siginfo_t info;
	struct ucounts *ucounts;
};

/* flags values. */
#define SIGQUEUE_PREALLOC	1

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
#ifndef __ARCH_HAS_IRIX_SIGACTION
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#else
	unsigned int	sa_flags;
	__sighandler_t	sa_handler;
#endif
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;	/* mask last for extensibility */
};

struct k_sigaction {
	struct sigaction sa;
#ifdef __ARCH_HAS_KA_RESTORER
	__sigrestore_t ka_restorer;
#endif
};

#ifdef CONFIG_OLD_SIGACTION
struct old_sigaction {
	__sighandler_t sa_handler;
	old_sigset_t sa_mask;
	unsigned long sa_flags;
	__sigrestore_t sa_restorer;
};
#endif

struct ksignal {
	struct k_sigaction ka;
	kernel_siginfo_t info;
	int sig;
};

/* Used to kill the race between sigaction and forced signals */
#define SA_IMMUTABLE		0x00800000

#ifndef __ARCH_UAPI_SA_FLAGS
#ifdef SA_RESTORER
#define __ARCH_UAPI_SA_FLAGS	SA_RESTORER
#else
#define __ARCH_UAPI_SA_FLAGS	0
#endif
#endif

#define UAPI_SA_FLAGS                                                          \
	(SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO | SA_ONSTACK | SA_RESTART |  \
	 SA_NODEFER | SA_RESETHAND | SA_EXPOSE_TAGBITS | __ARCH_UAPI_SA_FLAGS)

#endif /* _LINUX_SIGNAL_TYPES_H */
