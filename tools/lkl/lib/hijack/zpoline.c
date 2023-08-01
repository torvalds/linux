// SPDX-License-Identifier: GPL-2.0
/*
 * zpoline related code for hijack
 * Copyright (c) 2023 Hajime Tazaki
 *
 * Author: Hajime Tazaki <thehajime@gmail.com>
 *
 * Note: https://github.com/yasukata/zpoline
 */

/* zpoline only works on x86_64 architecture */
#ifdef __x86_64__
#include <lkl.h>
#include <lkl_host.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/syscall.h>

#include "init.h"
#include "xlate.h"
#include "hijack.h"

#include <errno.h>

/* XXX: include <unistd.h> doesn't do the job.. */
extern __pid_t gettid(void) __THROW;

#define CALL_LKL_FD_SYSCALL(x)					\
{								\
	case __NR_##x:						\
	if (!is_lklfd(a2))					\
		ret = syscall(a1, a2, a3, a4, a5, a6, a7);	\
	else {							\
		long p[6] = {a2, a3, a4, a5, a6, a7};		\
		ret = lkl_syscall(__lkl__NR_##x, p);		\
	}							\
	break;							\
}


#define ZPOLINE_DEBUG 0
#define dprintf(fmt, ...)		       \
	do {						\
		if (ZPOLINE_DEBUG) {			\
			printf(fmt, ##__VA_ARGS__);	\
		}					\
	} while (0)

long zpoline_lkl_hook(int64_t a1, int64_t a2, int64_t a3,
		      int64_t a4, int64_t a5, int64_t a6,
		      int64_t a7)
{
	int ret;

	dprintf("syscall %ld: tid: %d\n", a1, gettid());
	if (!lkl_running) {
		if (a1 == __NR_clone) {
			if (a2 & CLONE_VM) { // pthread creation
				/* push return address to the stack */
				a3 -= sizeof(uint64_t);
				*((uint64_t *) a3) = a7;
			}
		}
		return syscall(a1, a2, a3, a4, a5, a6);
	}

	switch (a1) {
		CALL_LKL_FD_SYSCALL(sendmsg);
		CALL_LKL_FD_SYSCALL(recvmsg);
		CALL_LKL_FD_SYSCALL(sendmmsg);
		CALL_LKL_FD_SYSCALL(recvmmsg);
		CALL_LKL_FD_SYSCALL(bind);
		CALL_LKL_FD_SYSCALL(connect);
		CALL_LKL_FD_SYSCALL(getsockopt);
		CALL_LKL_FD_SYSCALL(setsockopt);
		CALL_LKL_FD_SYSCALL(getsockname);
		CALL_LKL_FD_SYSCALL(sendto);
		CALL_LKL_FD_SYSCALL(recvfrom);
		CALL_LKL_FD_SYSCALL(listen);
		CALL_LKL_FD_SYSCALL(accept);
		CALL_LKL_FD_SYSCALL(close);
		CALL_LKL_FD_SYSCALL(ioctl);
		CALL_LKL_FD_SYSCALL(fcntl);
		CALL_LKL_FD_SYSCALL(read);
		CALL_LKL_FD_SYSCALL(write);
		CALL_LKL_FD_SYSCALL(pread64);
	case __NR_socket:
		ret = lkl_sys_socket(a2, a3, a4);
		if (ret < 0)
			syscall(a1, a2, a3, a4, a5, a6, a7);
		break;
	case __NR_openat:
		if (!lkl_running)
			ret = syscall(a1, a2, a3, a4, a5, a6, a7);
		else {
			ret = lkl_sys_open((char *)a3, a4, a5);
			/* open to host libraries should not hijack */
			if (ret < 0 && (strncmp((char *)a3, "/lib", 4) == 0))
				ret = syscall(a1, a2, a3, a4, a5, a6, a7);
		}
		break;
	case __NR_newfstatat:
		if (!lkl_running)
			ret = syscall(a1, a2, a3, a4, a5, a6, a7);
		else
			ret = lkl_sys_newfstatat(a2, (char *)a3, (void *)a4, a5);
		break;
	case __NR_epoll_create1:
		return hijack_epoll_create1(a2);
	case __NR_epoll_ctl:
		return hijack_epoll_ctl(a2, a3, a4, (void *)a5);
	case __NR_epoll_wait:
		return hijack_epoll_wait(a2, (void *)a3, a4, a5);
	case __NR_poll:
		return hijack_poll((void *)a2, a3, a4);
	case __NR_select:
		return hijack_select(a2, (void *)a3, (void *)a4, (void *)a5, (void *)a6);
	case __NR_eventfd2:
		return hijack_eventfd(a2, a3);
	case __NR_futex:
		ret = syscall(a1, a2, a3, a4, a5, a6, a7);
		if (ret < 0)
			return -errno;
		break;
	default:
		return syscall(a1, a2, a3, a4, a5, a6, a7);
	}

	if (ret == LKL_ENOSYS)
		fprintf(stderr, "no syscall defined in LKL (syscall=%ld)\n", a1);

	return ret;
}

void  __attribute__((destructor))
hook_exit(void)
{
	__hijack_fini();
}

typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);
int __hook_init(long placeholder __attribute__ ((__unused__)),
		void *default_hook)
{
	*((syscall_fn_t *) default_hook) = zpoline_lkl_hook;

	/**
	 * XXX: this library is expected to be load via dlmopen of zpoline, thus
	 * we need to patch a workaorund to handle thread specific data.
	 */
	lkl_change_tls_mode();

	__hijack_init();
	return 0;
}
#endif /* __x86_64__ */
