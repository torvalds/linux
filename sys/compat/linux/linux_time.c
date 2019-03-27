/*	$NetBSD: linux_time.c,v 1.14 2006/05/14 03:40:54 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if 0
__KERNEL_RCSID(0, "$NetBSD: linux_time.c,v 1.14 2006/05/14 03:40:54 christos Exp $");
#endif

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/ucred.h>
#include <sys/limits.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/resourcevar.h>
#include <sys/sdt.h>
#include <sys/signal.h>
#include <sys/stdint.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/proc.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif

#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_timer.h>

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

/**
 * DTrace probes in this module.
 */
LIN_SDT_PROBE_DEFINE2(time, native_to_linux_timespec, entry,
    "struct l_timespec *", "struct timespec *");
LIN_SDT_PROBE_DEFINE0(time, native_to_linux_timespec, return);
LIN_SDT_PROBE_DEFINE2(time, linux_to_native_timespec, entry,
    "struct timespec *", "struct l_timespec *");
LIN_SDT_PROBE_DEFINE1(time, linux_to_native_timespec, return, "int");
LIN_SDT_PROBE_DEFINE2(time, linux_to_native_clockid, entry, "clockid_t *",
    "clockid_t");
LIN_SDT_PROBE_DEFINE1(time, linux_to_native_clockid, unsupported_clockid,
    "clockid_t");
LIN_SDT_PROBE_DEFINE1(time, linux_to_native_clockid, unknown_clockid,
    "clockid_t");
LIN_SDT_PROBE_DEFINE1(time, linux_to_native_clockid, return, "int");
LIN_SDT_PROBE_DEFINE2(time, linux_clock_gettime, entry, "clockid_t",
    "struct l_timespec *");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_gettime, conversion_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_gettime, gettime_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_gettime, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_gettime, return, "int");
LIN_SDT_PROBE_DEFINE2(time, linux_clock_settime, entry, "clockid_t",
    "struct l_timespec *");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_settime, conversion_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_settime, settime_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_settime, copyin_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_settime, return, "int");
LIN_SDT_PROBE_DEFINE2(time, linux_clock_getres, entry, "clockid_t",
    "struct l_timespec *");
LIN_SDT_PROBE_DEFINE0(time, linux_clock_getres, nullcall);
LIN_SDT_PROBE_DEFINE1(time, linux_clock_getres, conversion_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_getres, getres_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_getres, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_getres, return, "int");
LIN_SDT_PROBE_DEFINE2(time, linux_nanosleep, entry, "const struct l_timespec *",
    "struct l_timespec *");
LIN_SDT_PROBE_DEFINE1(time, linux_nanosleep, conversion_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_nanosleep, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_nanosleep, copyin_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_nanosleep, return, "int");
LIN_SDT_PROBE_DEFINE4(time, linux_clock_nanosleep, entry, "clockid_t", "int",
    "struct l_timespec *", "struct l_timespec *");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, conversion_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, copyin_error, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, unsupported_flags, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, unsupported_clockid, "int");
LIN_SDT_PROBE_DEFINE1(time, linux_clock_nanosleep, return, "int");


int
native_to_linux_timespec(struct l_timespec *ltp, struct timespec *ntp)
{

	LIN_SDT_PROBE2(time, native_to_linux_timespec, entry, ltp, ntp);
#ifdef COMPAT_LINUX32
	if (ntp->tv_sec > INT_MAX || ntp->tv_sec < INT_MIN)
		return (EOVERFLOW);
#endif
	ltp->tv_sec = ntp->tv_sec;
	ltp->tv_nsec = ntp->tv_nsec;

	LIN_SDT_PROBE0(time, native_to_linux_timespec, return);
	return (0);
}

int
linux_to_native_timespec(struct timespec *ntp, struct l_timespec *ltp)
{

	LIN_SDT_PROBE2(time, linux_to_native_timespec, entry, ntp, ltp);

	if (ltp->tv_sec < 0 || ltp->tv_nsec < 0 || ltp->tv_nsec > 999999999) {
		LIN_SDT_PROBE1(time, linux_to_native_timespec, return, EINVAL);
		return (EINVAL);
	}
	ntp->tv_sec = ltp->tv_sec;
	ntp->tv_nsec = ltp->tv_nsec;

	LIN_SDT_PROBE1(time, linux_to_native_timespec, return, 0);
	return (0);
}

int
native_to_linux_itimerspec(struct l_itimerspec *ltp, struct itimerspec *ntp)
{
	int error;

	error = native_to_linux_timespec(&ltp->it_interval, &ntp->it_interval);
	if (error == 0)
		error = native_to_linux_timespec(&ltp->it_value, &ntp->it_interval);
	return (error);
}

int
linux_to_native_itimerspec(struct itimerspec *ntp, struct l_itimerspec *ltp)
{
	int error;

	error = linux_to_native_timespec(&ntp->it_interval, &ltp->it_interval);
	if (error == 0)
		error = linux_to_native_timespec(&ntp->it_value, &ltp->it_value);
	return (error);
}

int
linux_to_native_clockid(clockid_t *n, clockid_t l)
{

	LIN_SDT_PROBE2(time, linux_to_native_clockid, entry, n, l);

	if (l < 0) {
		/* cpu-clock */
		if ((l & LINUX_CLOCKFD_MASK) == LINUX_CLOCKFD)
			return (EINVAL);
		if (LINUX_CPUCLOCK_WHICH(l) >= LINUX_CPUCLOCK_MAX)
			return (EINVAL);

		if (LINUX_CPUCLOCK_PERTHREAD(l))
			*n = CLOCK_THREAD_CPUTIME_ID;
		else
			*n = CLOCK_PROCESS_CPUTIME_ID;
		return (0);
	}

	switch (l) {
	case LINUX_CLOCK_REALTIME:
		*n = CLOCK_REALTIME;
		break;
	case LINUX_CLOCK_MONOTONIC:
		*n = CLOCK_MONOTONIC;
		break;
	case LINUX_CLOCK_REALTIME_COARSE:
		*n = CLOCK_REALTIME_FAST;
		break;
	case LINUX_CLOCK_MONOTONIC_COARSE:
		*n = CLOCK_MONOTONIC_FAST;
		break;
	case LINUX_CLOCK_BOOTTIME:
		*n = CLOCK_UPTIME;
		break;
	case LINUX_CLOCK_MONOTONIC_RAW:
	case LINUX_CLOCK_REALTIME_ALARM:
	case LINUX_CLOCK_BOOTTIME_ALARM:
	case LINUX_CLOCK_SGI_CYCLE:
	case LINUX_CLOCK_TAI:
		LIN_SDT_PROBE1(time, linux_to_native_clockid,
		    unsupported_clockid, l);
		LIN_SDT_PROBE1(time, linux_to_native_clockid, return, EINVAL);
		return (EINVAL);
	default:
		LIN_SDT_PROBE1(time, linux_to_native_clockid,
		    unknown_clockid, l);
		LIN_SDT_PROBE1(time, linux_to_native_clockid, return, EINVAL);
		return (EINVAL);
	}

	LIN_SDT_PROBE1(time, linux_to_native_clockid, return, 0);
	return (0);
}

int
linux_to_native_timerflags(int *nflags, int flags)
{

	if (flags & ~LINUX_TIMER_ABSTIME)
		return (EINVAL);
	*nflags = 0;
	if (flags & LINUX_TIMER_ABSTIME)
		*nflags |= TIMER_ABSTIME;
	return (0);
}

int
linux_clock_gettime(struct thread *td, struct linux_clock_gettime_args *args)
{
	struct l_timespec lts;
	struct timespec tp;
	struct rusage ru;
	struct thread *targettd;
	struct proc *p;
	int error, clockwhich;
	clockid_t nwhich = 0;	/* XXX: GCC */
	pid_t pid;
	lwpid_t tid;

	LIN_SDT_PROBE2(time, linux_clock_gettime, entry, args->which, args->tp);

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_gettime, conversion_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_gettime, return, error);
		return (error);
	}

	switch (nwhich) {
	case CLOCK_PROCESS_CPUTIME_ID:
		clockwhich = LINUX_CPUCLOCK_WHICH(args->which);
		pid = LINUX_CPUCLOCK_ID(args->which);
		if (pid == 0) {
			p = td->td_proc;
			PROC_LOCK(p);
		} else {
			error = pget(pid, PGET_CANSEE, &p);
			if (error != 0)
				return (EINVAL);
		}
		switch (clockwhich) {
		case LINUX_CPUCLOCK_PROF:
			PROC_STATLOCK(p);
			calcru(p, &ru.ru_utime, &ru.ru_stime);
			PROC_STATUNLOCK(p);
			PROC_UNLOCK(p);
			timevaladd(&ru.ru_utime, &ru.ru_stime);
			TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &tp);
			break;
		case LINUX_CPUCLOCK_VIRT:
			PROC_STATLOCK(p);
			calcru(p, &ru.ru_utime, &ru.ru_stime);
			PROC_STATUNLOCK(p);
			PROC_UNLOCK(p);
			TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &tp);
			break;
		case LINUX_CPUCLOCK_SCHED:
			PROC_UNLOCK(p);
			error = kern_clock_getcpuclockid2(td, pid,
			    CPUCLOCK_WHICH_PID, &nwhich);
			if (error != 0)
				return (EINVAL);
			error = kern_clock_gettime(td, nwhich, &tp);
			break;
		default:
			PROC_UNLOCK(p);
			return (EINVAL);
		}

		break;

	case CLOCK_THREAD_CPUTIME_ID:
		clockwhich = LINUX_CPUCLOCK_WHICH(args->which);
		p = td->td_proc;
		tid = LINUX_CPUCLOCK_ID(args->which);
		if (tid == 0) {
			targettd = td;
			PROC_LOCK(p);
		} else {
			targettd = tdfind(tid, p->p_pid);
			if (targettd == NULL)
				return (EINVAL);
		}
		switch (clockwhich) {
		case LINUX_CPUCLOCK_PROF:
			PROC_STATLOCK(p);
			thread_lock(targettd);
			rufetchtd(targettd, &ru);
			thread_unlock(targettd);
			PROC_STATUNLOCK(p);
			PROC_UNLOCK(p);
			timevaladd(&ru.ru_utime, &ru.ru_stime);
			TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &tp);
			break;
		case LINUX_CPUCLOCK_VIRT:
			PROC_STATLOCK(p);
			thread_lock(targettd);
			rufetchtd(targettd, &ru);
			thread_unlock(targettd);
			PROC_STATUNLOCK(p);
			PROC_UNLOCK(p);
			TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &tp);
			break;
		case LINUX_CPUCLOCK_SCHED:
			error = kern_clock_getcpuclockid2(td, tid,
			    CPUCLOCK_WHICH_TID, &nwhich);
			PROC_UNLOCK(p);
			if (error != 0)
				return (EINVAL);
			error = kern_clock_gettime(td, nwhich, &tp);
			break;
		default:
			PROC_UNLOCK(p);
			return (EINVAL);
		}
		break;

	default:
		error = kern_clock_gettime(td, nwhich, &tp);
		break;
	}
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_gettime, gettime_error, error);
		LIN_SDT_PROBE1(time, linux_clock_gettime, return, error);
		return (error);
	}
	error = native_to_linux_timespec(&lts, &tp);
	if (error != 0)
		return (error);
	error = copyout(&lts, args->tp, sizeof lts);
	if (error != 0)
		LIN_SDT_PROBE1(time, linux_clock_gettime, copyout_error, error);

	LIN_SDT_PROBE1(time, linux_clock_gettime, return, error);
	return (error);
}

int
linux_clock_settime(struct thread *td, struct linux_clock_settime_args *args)
{
	struct timespec ts;
	struct l_timespec lts;
	int error;
	clockid_t nwhich = 0;	/* XXX: GCC */

	LIN_SDT_PROBE2(time, linux_clock_settime, entry, args->which, args->tp);

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_settime, conversion_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_settime, return, error);
		return (error);
	}
	error = copyin(args->tp, &lts, sizeof lts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_settime, copyin_error, error);
		LIN_SDT_PROBE1(time, linux_clock_settime, return, error);
		return (error);
	}
	error = linux_to_native_timespec(&ts, &lts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_settime, conversion_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_settime, return, error);
		return (error);
	}

	error = kern_clock_settime(td, nwhich, &ts);
	if (error != 0)
		LIN_SDT_PROBE1(time, linux_clock_settime, settime_error, error);

	LIN_SDT_PROBE1(time, linux_clock_settime, return, error);
	return (error);
}

int
linux_clock_getres(struct thread *td, struct linux_clock_getres_args *args)
{
	struct proc *p;
	struct timespec ts;
	struct l_timespec lts;
	int error, clockwhich;
	clockid_t nwhich = 0;	/* XXX: GCC */
	pid_t pid;
	lwpid_t tid;

	LIN_SDT_PROBE2(time, linux_clock_getres, entry, args->which, args->tp);

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_getres, conversion_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_getres, return, error);
		return (error);
	}

	/*
	 * Check user supplied clock id in case of per-process
	 * or thread-specific cpu-time clock.
	 */
	switch (nwhich) {
	case CLOCK_THREAD_CPUTIME_ID:
		tid = LINUX_CPUCLOCK_ID(args->which);
		if (tid != 0) {
			p = td->td_proc;
			if (tdfind(tid, p->p_pid) == NULL)
				return (ESRCH);
			PROC_UNLOCK(p);
		}
		break;
	case CLOCK_PROCESS_CPUTIME_ID:
		pid = LINUX_CPUCLOCK_ID(args->which);
		if (pid != 0) {
			error = pget(pid, PGET_CANSEE, &p);
			if (error != 0)
				return (EINVAL);
			PROC_UNLOCK(p);
		}
		break;
	}

	if (args->tp == NULL) {
		LIN_SDT_PROBE0(time, linux_clock_getres, nullcall);
		LIN_SDT_PROBE1(time, linux_clock_getres, return, 0);
		return (0);
	}

	switch (nwhich) {
	case CLOCK_THREAD_CPUTIME_ID:
	case CLOCK_PROCESS_CPUTIME_ID:
		clockwhich = LINUX_CPUCLOCK_WHICH(args->which);
		switch (clockwhich) {
		case LINUX_CPUCLOCK_PROF:
			nwhich = CLOCK_PROF;
			break;
		case LINUX_CPUCLOCK_VIRT:
			nwhich = CLOCK_VIRTUAL;
			break;
		case LINUX_CPUCLOCK_SCHED:
			break;
		default:
			return (EINVAL);
		}
		break;

	default:
		break;
	}
	error = kern_clock_getres(td, nwhich, &ts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_getres, getres_error, error);
		LIN_SDT_PROBE1(time, linux_clock_getres, return, error);
		return (error);
	}
	error = native_to_linux_timespec(&lts, &ts);
	if (error != 0)
		return (error);
	error = copyout(&lts, args->tp, sizeof lts);
	if (error != 0)
		LIN_SDT_PROBE1(time, linux_clock_getres, copyout_error, error);

	LIN_SDT_PROBE1(time, linux_clock_getres, return, error);
	return (error);
}

int
linux_nanosleep(struct thread *td, struct linux_nanosleep_args *args)
{
	struct timespec *rmtp;
	struct l_timespec lrqts, lrmts;
	struct timespec rqts, rmts;
	int error, error2;

	LIN_SDT_PROBE2(time, linux_nanosleep, entry, args->rqtp, args->rmtp);

	error = copyin(args->rqtp, &lrqts, sizeof lrqts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_nanosleep, copyin_error, error);
		LIN_SDT_PROBE1(time, linux_nanosleep, return, error);
		return (error);
	}

	if (args->rmtp != NULL)
		rmtp = &rmts;
	else
		rmtp = NULL;

	error = linux_to_native_timespec(&rqts, &lrqts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_nanosleep, conversion_error, error);
		LIN_SDT_PROBE1(time, linux_nanosleep, return, error);
		return (error);
	}
	error = kern_nanosleep(td, &rqts, rmtp);
	if (error == EINTR && args->rmtp != NULL) {
		error2 = native_to_linux_timespec(&lrmts, rmtp);
		if (error2 != 0)
			return (error2);
		error2 = copyout(&lrmts, args->rmtp, sizeof(lrmts));
		if (error2 != 0) {
			LIN_SDT_PROBE1(time, linux_nanosleep, copyout_error,
			    error2);
			LIN_SDT_PROBE1(time, linux_nanosleep, return, error2);
			return (error2);
		}
	}

	LIN_SDT_PROBE1(time, linux_nanosleep, return, error);
	return (error);
}

int
linux_clock_nanosleep(struct thread *td, struct linux_clock_nanosleep_args *args)
{
	struct timespec *rmtp;
	struct l_timespec lrqts, lrmts;
	struct timespec rqts, rmts;
	int error, error2, flags;
	clockid_t clockid;

	LIN_SDT_PROBE4(time, linux_clock_nanosleep, entry, args->which,
	    args->flags, args->rqtp, args->rmtp);

	error = linux_to_native_timerflags(&flags, args->flags);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, unsupported_flags,
		    args->flags);
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, return, error);
		return (error);
	}

	error = linux_to_native_clockid(&clockid, args->which);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, unsupported_clockid,
		    args->which);
		LIN_SDT_PROBE1(time, linux_clock_settime, return, error);
		return (error);
	}

	error = copyin(args->rqtp, &lrqts, sizeof(lrqts));
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, copyin_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, return, error);
		return (error);
	}

	if (args->rmtp != NULL)
		rmtp = &rmts;
	else
		rmtp = NULL;

	error = linux_to_native_timespec(&rqts, &lrqts);
	if (error != 0) {
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, conversion_error,
		    error);
		LIN_SDT_PROBE1(time, linux_clock_nanosleep, return, error);
		return (error);
	}
	error = kern_clock_nanosleep(td, clockid, flags, &rqts, rmtp);
	if (error == EINTR && (flags & TIMER_ABSTIME) == 0 &&
	    args->rmtp != NULL) {
		error2 = native_to_linux_timespec(&lrmts, rmtp);
		if (error2 != 0)
			return (error2);
		error2 = copyout(&lrmts, args->rmtp, sizeof(lrmts));
		if (error2 != 0) {
			LIN_SDT_PROBE1(time, linux_clock_nanosleep,
			    copyout_error, error2);
			LIN_SDT_PROBE1(time, linux_clock_nanosleep,
			    return, error2);
			return (error2);
		}
	}

	LIN_SDT_PROBE1(time, linux_clock_nanosleep, return, error);
	return (error);
}
