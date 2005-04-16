/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2001, 2003 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGINFO_H
#define _ASM_SIGINFO_H

#include <linux/config.h>

#define SIGEV_HEAD_SIZE	(sizeof(long) + 2*sizeof(int))
#define SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE-SIGEV_HEAD_SIZE) / sizeof(int))
#undef __ARCH_SI_TRAPNO	/* exception code needs to fill this ...  */

#define HAVE_ARCH_SIGINFO_T

/*
 * We duplicate the generic versions - <asm-generic/siginfo.h> is just borked
 * by design ...
 */
#define HAVE_ARCH_COPY_SIGINFO
struct siginfo;

/*
 * Careful to keep union _sifields from shifting ...
 */
#ifdef CONFIG_MIPS32
#define __ARCH_SI_PREAMBLE_SIZE (3 * sizeof(int))
#endif
#ifdef CONFIG_MIPS64
#define __ARCH_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

typedef struct siginfo {
	int si_signo;
	int si_code;
	int si_errno;
	int __pad0[SI_MAX_SIZE / sizeof(int) - SI_PAD_SIZE - 3];

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			char _pad[sizeof( __ARCH_SI_UID_T) - sizeof(int)];
			sigval_t _sigval;	/* same as below */
			int _sys_private;       /* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
			sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
			int _status;		/* exit code */
			clock_t _utime;
			clock_t _stime;
		} _sigchld;

		/* IRIX SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			clock_t _utime;
			int _status;		/* exit code */
			clock_t _stime;
		} _irix_sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			void __user *_addr; /* faulting insn/memory ref. */
#ifdef __ARCH_SI_TRAPNO
			int _trapno;	/* TRAP # which caused the signal */
#endif
		} _sigfault;

		/* SIGPOLL, SIGXFSZ (To do ...)  */
		struct {
			__ARCH_SI_BAND_T _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t;

/*
 * si_code values
 * Again these have been choosen to be IRIX compatible.
 */
#undef SI_ASYNCIO
#undef SI_TIMER
#undef SI_MESGQ
#define SI_ASYNCIO	-2	/* sent by AIO completion */
#define SI_TIMER __SI_CODE(__SI_TIMER,-3) /* sent by timer expiration */
#define SI_MESGQ __SI_CODE(__SI_MESGQ,-4) /* sent by real time mesq state change */

#ifdef __KERNEL__

/*
 * Duplicated here because of <asm-generic/siginfo.h> braindamage ...
 */
#include <linux/string.h>

static inline void copy_siginfo(struct siginfo *to, struct siginfo *from)
{
	if (from->si_code < 0)
		memcpy(to, from, sizeof(*to));
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, 3*sizeof(int) + sizeof(from->_sifields._sigchld));
}

#endif

#endif /* _ASM_SIGINFO_H */
