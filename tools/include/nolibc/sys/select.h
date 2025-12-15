/* SPDX-License-Identifier: LGPL-2.1 OR MIT */

#include "../nolibc.h"

#ifndef _NOLIBC_SYS_SELECT_H
#define _NOLIBC_SYS_SELECT_H

#include <linux/time.h>
#include <linux/unistd.h>

/* commonly an fd_set represents 256 FDs */
#ifndef FD_SETSIZE
#define FD_SETSIZE     256
#endif

#define FD_SETIDXMASK (8 * sizeof(unsigned long))
#define FD_SETBITMASK (8 * sizeof(unsigned long)-1)

/* for select() */
typedef struct {
	unsigned long fds[(FD_SETSIZE + FD_SETBITMASK) / FD_SETIDXMASK];
} fd_set;

#define FD_CLR(fd, set) do {						\
		fd_set *__set = (set);					\
		int __fd = (fd);					\
		if (__fd >= 0)						\
			__set->fds[__fd / FD_SETIDXMASK] &=		\
				~(1U << (__fd & FD_SETBITMASK));	\
	} while (0)

#define FD_SET(fd, set) do {						\
		fd_set *__set = (set);					\
		int __fd = (fd);					\
		if (__fd >= 0)						\
			__set->fds[__fd / FD_SETIDXMASK] |=		\
				1 << (__fd & FD_SETBITMASK);		\
	} while (0)

#define FD_ISSET(fd, set) ({						\
			fd_set *__set = (set);				\
			int __fd = (fd);				\
		int __r = 0;						\
		if (__fd >= 0)						\
			__r = !!(__set->fds[__fd / FD_SETIDXMASK] &	\
1U << (__fd & FD_SETBITMASK));						\
		__r;							\
	})

#define FD_ZERO(set) do {						\
		fd_set *__set = (set);					\
		int __idx;						\
		int __size = (FD_SETSIZE+FD_SETBITMASK) / FD_SETIDXMASK;\
		for (__idx = 0; __idx < __size; __idx++)		\
			__set->fds[__idx] = 0;				\
	} while (0)

/*
 * int select(int nfds, fd_set *read_fds, fd_set *write_fds,
 *            fd_set *except_fds, struct timeval *timeout);
 */

static __attribute__((unused))
int sys_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *timeout)
{
#if defined(__ARCH_WANT_SYS_OLD_SELECT) && !defined(__NR__newselect)
	struct sel_arg_struct {
		unsigned long n;
		fd_set *r, *w, *e;
		struct timeval *t;
	} arg = { .n = nfds, .r = rfds, .w = wfds, .e = efds, .t = timeout };
	return my_syscall1(__NR_select, &arg);
#elif defined(__NR__newselect)
	return my_syscall5(__NR__newselect, nfds, rfds, wfds, efds, timeout);
#elif defined(__NR_select)
	return my_syscall5(__NR_select, nfds, rfds, wfds, efds, timeout);
#elif defined(__NR_pselect6)
	struct timespec t;

	if (timeout) {
		t.tv_sec  = timeout->tv_sec;
		t.tv_nsec = timeout->tv_usec * 1000;
	}
	return my_syscall6(__NR_pselect6, nfds, rfds, wfds, efds, timeout ? &t : NULL, NULL);
#else
	struct __kernel_timespec t;

	if (timeout) {
		t.tv_sec  = timeout->tv_sec;
		t.tv_nsec = timeout->tv_usec * 1000;
	}
	return my_syscall6(__NR_pselect6_time64, nfds, rfds, wfds, efds, timeout ? &t : NULL, NULL);
#endif
}

static __attribute__((unused))
int select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *timeout)
{
	return __sysret(sys_select(nfds, rfds, wfds, efds, timeout));
}


#endif /* _NOLIBC_SYS_SELECT_H */
