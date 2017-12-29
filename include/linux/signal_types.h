/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_TYPES_H
#define _LINUX_SIGNAL_TYPES_H

/*
 * Basic signal handling related data type definitions:
 */

#include <linux/list.h>
#include <uapi/linux/signal.h>

/*
 * Real Time signals may be queued.
 */

struct sigqueue {
	struct list_head list;
	int flags;
	siginfo_t info;
	struct user_struct *user;
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
	siginfo_t info;
	int sig;
};

#endif /* _LINUX_SIGNAL_TYPES_H */
