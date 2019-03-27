/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Roman Divacky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_MISC_H_
#define	_LINUX_MISC_H_

#include <sys/sysctl.h>

				/* bits per mask */
#define	LINUX_NFDBITS		sizeof(l_fd_mask) * 8

/*
 * Miscellaneous
 */
#define	LINUX_NAME_MAX		255
#define	LINUX_MAX_UTSNAME	65

#define	LINUX_CTL_MAXNAME	10

/* defines for prctl */
#define	LINUX_PR_SET_PDEATHSIG  1	/* Second arg is a signal. */
#define	LINUX_PR_GET_PDEATHSIG  2	/*
					 * Second arg is a ptr to return the
					 * signal.
					 */
#define	LINUX_PR_GET_KEEPCAPS	7	/* Get drop capabilities on setuid */
#define	LINUX_PR_SET_KEEPCAPS	8	/* Set drop capabilities on setuid */
#define	LINUX_PR_SET_NAME	15	/* Set process name. */
#define	LINUX_PR_GET_NAME	16	/* Get process name. */

#define	LINUX_MAX_COMM_LEN	16	/* Maximum length of the process name. */

#define	LINUX_MREMAP_MAYMOVE	1
#define	LINUX_MREMAP_FIXED	2

#define	LINUX_PATH_MAX		4096

extern const char *linux_kplatform;

/*
 * Non-standard aux entry types used in Linux ELF binaries.
 */

#define	LINUX_AT_PLATFORM	15	/* String identifying CPU */
#define	LINUX_AT_HWCAP		16	/* CPU capabilities */
#define	LINUX_AT_CLKTCK		17	/* frequency at which times() increments */
#define	LINUX_AT_SECURE		23	/* secure mode boolean */
#define	LINUX_AT_BASE_PLATFORM	24	/* string identifying real platform, may
					 * differ from AT_PLATFORM.
					 */
#define	LINUX_AT_RANDOM		25	/* address of random bytes */
#define	LINUX_AT_EXECFN		31	/* filename of program */
#define	LINUX_AT_SYSINFO	32	/* vsyscall */
#define	LINUX_AT_SYSINFO_EHDR	33	/* vdso header */

#define	LINUX_AT_RANDOM_LEN	16	/* size of random bytes */

/* Linux sets the i387 to extended precision. */
#if defined(__i386__) || defined(__amd64__)
#define	__LINUX_NPXCW__		0x37f
#endif

#define	LINUX_CLONE_VM			0x00000100
#define	LINUX_CLONE_FS			0x00000200
#define	LINUX_CLONE_FILES		0x00000400
#define	LINUX_CLONE_SIGHAND		0x00000800
#define	LINUX_CLONE_PID			0x00001000	/* No longer exist in Linux */
#define	LINUX_CLONE_VFORK		0x00004000
#define	LINUX_CLONE_PARENT		0x00008000
#define	LINUX_CLONE_THREAD		0x00010000
#define	LINUX_CLONE_SETTLS		0x00080000
#define	LINUX_CLONE_PARENT_SETTID	0x00100000
#define	LINUX_CLONE_CHILD_CLEARTID	0x00200000
#define	LINUX_CLONE_CHILD_SETTID	0x01000000

/* Scheduling policies */
#define	LINUX_SCHED_OTHER	0
#define	LINUX_SCHED_FIFO	1
#define	LINUX_SCHED_RR		2

struct l_new_utsname {
	char	sysname[LINUX_MAX_UTSNAME];
	char	nodename[LINUX_MAX_UTSNAME];
	char	release[LINUX_MAX_UTSNAME];
	char	version[LINUX_MAX_UTSNAME];
	char	machine[LINUX_MAX_UTSNAME];
	char	domainname[LINUX_MAX_UTSNAME];
};

#define LINUX_UTIME_NOW			0x3FFFFFFF
#define LINUX_UTIME_OMIT		0x3FFFFFFE

extern int stclohz;

#define	LINUX_WNOHANG		0x00000001
#define	LINUX_WUNTRACED		0x00000002
#define	LINUX_WSTOPPED		LINUX_WUNTRACED
#define	LINUX_WEXITED		0x00000004
#define	LINUX_WCONTINUED	0x00000008
#define	LINUX_WNOWAIT		0x01000000


#define	__WNOTHREAD		0x20000000
#define	__WALL			0x40000000
#define	__WCLONE		0x80000000

/* Linux waitid idtype  */
#define	LINUX_P_ALL		0
#define	LINUX_P_PID		1
#define	LINUX_P_PGID		2

#define	LINUX_RLIMIT_LOCKS	RLIM_NLIMITS + 1
#define	LINUX_RLIMIT_SIGPENDING	RLIM_NLIMITS + 2
#define	LINUX_RLIMIT_MSGQUEUE	RLIM_NLIMITS + 3
#define	LINUX_RLIMIT_NICE	RLIM_NLIMITS + 4
#define	LINUX_RLIMIT_RTPRIO	RLIM_NLIMITS + 5
#define	LINUX_RLIMIT_RTTIME	RLIM_NLIMITS + 6

#define	LINUX_RLIM_INFINITY	(~0UL)

/* Linux getrandom flags */
#define	LINUX_GRND_NONBLOCK	0x0001
#define	LINUX_GRND_RANDOM	0x0002

int linux_common_wait(struct thread *td, int pid, int *status,
			int options, struct rusage *ru);
void linux_to_bsd_waitopts(int options, int *bsdopts);
int linux_set_upcall_kse(struct thread *td, register_t stack);
int linux_set_cloned_tls(struct thread *td, void *desc);
struct thread	*linux_tdfind(struct thread *, lwpid_t, pid_t);

int linux_sysctl_debug(SYSCTL_HANDLER_ARGS);

#endif	/* _LINUX_MISC_H_ */
