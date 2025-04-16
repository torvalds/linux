/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * wait definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_SYS_WAIT_H
#define _NOLIBC_SYS_WAIT_H

#include "../arch.h"
#include "../std.h"
#include "../types.h"

/*
 * pid_t wait(int *status);
 * pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);
 * pid_t waitpid(pid_t pid, int *status, int options);
 */

static __attribute__((unused))
pid_t sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
#ifdef __NR_wait4
	return my_syscall4(__NR_wait4, pid, status, options, rusage);
#else
	return __nolibc_enosys(__func__, pid, status, options, rusage);
#endif
}

static __attribute__((unused))
pid_t wait(int *status)
{
	return __sysret(sys_wait4(-1, status, 0, NULL));
}

static __attribute__((unused))
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
	return __sysret(sys_wait4(pid, status, options, rusage));
}


static __attribute__((unused))
pid_t waitpid(pid_t pid, int *status, int options)
{
	return __sysret(sys_wait4(pid, status, options, NULL));
}


/*
 * int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
 */

static __attribute__((unused))
int sys_waitid(int which, pid_t pid, siginfo_t *infop, int options, struct rusage *rusage)
{
	return my_syscall5(__NR_waitid, which, pid, infop, options, rusage);
}

static __attribute__((unused))
int waitid(int which, pid_t pid, siginfo_t *infop, int options)
{
	return __sysret(sys_waitid(which, pid, infop, options, NULL));
}



/* make sure to include all global symbols */
#include "../nolibc.h"

#endif /* _NOLIBC_SYS_WAIT_H */
