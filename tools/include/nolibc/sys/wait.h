/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * wait definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_WAIT_H
#define _NOLIBC_SYS_WAIT_H

#include "../arch.h"
#include "../std.h"
#include "../types.h"

/*
 * pid_t wait(int *status);
 * pid_t waitpid(pid_t pid, int *status, int options);
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


static __attribute__((unused))
pid_t waitpid(pid_t pid, int *status, int options)
{
	int idtype, ret;
	siginfo_t info;
	pid_t id;

	if (pid == INT_MIN) {
		SET_ERRNO(ESRCH);
		return -1;
	} else if (pid < -1) {
		idtype = P_PGID;
		id = -pid;
	} else if (pid == -1) {
		idtype = P_ALL;
		id = 0;
	} else if (pid == 0) {
		idtype = P_PGID;
		id = 0;
	} else {
		idtype = P_PID;
		id = pid;
	}

	options |= WEXITED;

	ret = waitid(idtype, id, &info, options);
	if (ret)
		return -1;

	switch (info.si_code) {
	case 0:
		*status = 0;
		break;
	case CLD_EXITED:
		*status = (info.si_status & 0xff) << 8;
		break;
	case CLD_KILLED:
		*status = info.si_status & 0x7f;
		break;
	case CLD_DUMPED:
		*status = (info.si_status & 0x7f) | 0x80;
		break;
	case CLD_STOPPED:
	case CLD_TRAPPED:
		*status = (info.si_status << 8) + 0x7f;
		break;
	case CLD_CONTINUED:
		*status = 0xffff;
		break;
	default:
		return -1;
	}

	return info.si_pid;
}

static __attribute__((unused))
pid_t wait(int *status)
{
	return waitpid(-1, status, 0);
}

#endif /* _NOLIBC_SYS_WAIT_H */
