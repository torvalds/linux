/*-
 * Copyright (c) 2014 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-11-C-0249
 * ("MRC2"), as part of the DARPA MRC research programme.
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

#ifndef	_LINUX_TIMER_H
#define	_LINUX_TIMER_H

#ifndef	__LINUX_ARCH_SIGEV_PREAMBLE_SIZE
#define	__LINUX_ARCH_SIGEV_PREAMBLE_SIZE	\
	(sizeof(l_int) * 2 + sizeof(l_sigval_t))
#endif

#define	LINUX_SIGEV_MAX_SIZE			64
#define	LINUX_SIGEV_PAD_SIZE			\
	((LINUX_SIGEV_MAX_SIZE - __LINUX_ARCH_SIGEV_PREAMBLE_SIZE) / \
	sizeof(l_int))

#define	LINUX_CLOCK_REALTIME			0
#define	LINUX_CLOCK_MONOTONIC			1
#define	LINUX_CLOCK_PROCESS_CPUTIME_ID		2
#define	LINUX_CLOCK_THREAD_CPUTIME_ID		3
#define	LINUX_CLOCK_MONOTONIC_RAW		4
#define	LINUX_CLOCK_REALTIME_COARSE		5
#define	LINUX_CLOCK_MONOTONIC_COARSE		6
#define	LINUX_CLOCK_BOOTTIME			7
#define	LINUX_CLOCK_REALTIME_ALARM		8
#define	LINUX_CLOCK_BOOTTIME_ALARM		9
#define	LINUX_CLOCK_SGI_CYCLE			10
#define	LINUX_CLOCK_TAI				11

#define	LINUX_CPUCLOCK_PERTHREAD_MASK		4
#define	LINUX_CPUCLOCK_MASK			3
#define	LINUX_CPUCLOCK_WHICH(clock)		\
	((clock) & (clockid_t) LINUX_CPUCLOCK_MASK)
#define	LINUX_CPUCLOCK_PROF			0
#define	LINUX_CPUCLOCK_VIRT			1
#define	LINUX_CPUCLOCK_SCHED			2
#define	LINUX_CPUCLOCK_MAX			3
#define	LINUX_CLOCKFD				LINUX_CPUCLOCK_MAX
#define	LINUX_CLOCKFD_MASK			\
	(LINUX_CPUCLOCK_PERTHREAD_MASK|LINUX_CPUCLOCK_MASK)

#define	LINUX_CPUCLOCK_ID(clock)		((pid_t) ~((clock) >> 3))
#define	LINUX_CPUCLOCK_PERTHREAD(clock)		\
	(((clock) & (clockid_t) LINUX_CPUCLOCK_PERTHREAD_MASK) != 0)

#define	LINUX_TIMER_ABSTIME			0x01

#define	L_SIGEV_SIGNAL				0
#define	L_SIGEV_NONE				1
#define	L_SIGEV_THREAD				2
#define	L_SIGEV_THREAD_ID			4

#define	TS_CP(src,dst,fld) do {			\
	CP((src).fld,(dst).fld,tv_sec);		\
	CP((src).fld,(dst).fld,tv_nsec);	\
} while (0)

#define	ITS_CP(src, dst) do {			\
	TS_CP((src), (dst), it_interval);	\
	TS_CP((src), (dst), it_value);		\
} while (0)

struct l_sigevent {
	l_sigval_t sigev_value;
	l_int sigev_signo;
	l_int sigev_notify;
	union {
		l_int _pad[LINUX_SIGEV_PAD_SIZE];
		l_int _tid;
		struct {
			l_uintptr_t _function;
			l_uintptr_t _attribute;
		} _l_sigev_thread;
	} _l_sigev_un;
}
#if defined(__amd64__) && defined(COMPAT_LINUX32)
__packed
#endif
;

struct l_itimerspec {
	struct l_timespec it_interval;
	struct l_timespec it_value;
};

int native_to_linux_timespec(struct l_timespec *,
				     struct timespec *);
int linux_to_native_timespec(struct timespec *,
				     struct l_timespec *);
int linux_to_native_clockid(clockid_t *, clockid_t);
int native_to_linux_itimerspec(struct l_itimerspec *,
				     struct itimerspec *);
int linux_to_native_itimerspec(struct itimerspec *,
				     struct l_itimerspec *);
int linux_to_native_timerflags(int *, int);

#endif	/* _LINUX_TIMER_H */
