/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * poll definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_POLL_H
#define _NOLIBC_POLL_H

#include "arch.h"
#include "sys.h"

#include <linux/poll.h>
#include <linux/time.h>

/*
 * int poll(struct pollfd *fds, int nfds, int timeout);
 */

static __attribute__((unused))
int sys_poll(struct pollfd *fds, int nfds, int timeout)
{
#if defined(__NR_ppoll)
	struct timespec t;

	if (timeout >= 0) {
		t.tv_sec  = timeout / 1000;
		t.tv_nsec = (timeout % 1000) * 1000000;
	}
	return my_syscall5(__NR_ppoll, fds, nfds, (timeout >= 0) ? &t : NULL, NULL, 0);
#elif defined(__NR_ppoll_time64)
	struct __kernel_timespec t;

	if (timeout >= 0) {
		t.tv_sec  = timeout / 1000;
		t.tv_nsec = (timeout % 1000) * 1000000;
	}
	return my_syscall5(__NR_ppoll_time64, fds, nfds, (timeout >= 0) ? &t : NULL, NULL, 0);
#else
	return my_syscall3(__NR_poll, fds, nfds, timeout);
#endif
}

static __attribute__((unused))
int poll(struct pollfd *fds, int nfds, int timeout)
{
	return __sysret(sys_poll(fds, nfds, timeout));
}

#endif /* _NOLIBC_POLL_H */
