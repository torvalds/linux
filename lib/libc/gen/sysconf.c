/*	$OpenBSD: sysconf.c,v 1.28 2022/07/19 09:25:44 claudio Exp $ */
/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Sean Eric Fagan of Cygnus Support.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/sem.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <errno.h>
#include <grp.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

/*
 * sysconf --
 *	get configurable system variables.
 *
 * XXX
 * POSIX 1003.1 (ISO/IEC 9945-1, 4.8.1.3) states that the variable values
 * not change during the lifetime of the calling process.  This would seem
 * to require that any change to system limits kill all running processes.
 * A workaround might be to cache the values when they are first retrieved
 * and then simply return the cached value on subsequent calls.  This is
 * less useful than returning up-to-date values, however.
 */
long
sysconf(int name)
{
	struct rlimit rl;
	size_t len;
	int mib[3], value, namelen, sverrno;

	len = sizeof(value);
	namelen = 2;

	switch (name) {
/* 1003.1 */
	case _SC_ARG_MAX:
		mib[0] = CTL_KERN;
		mib[1] = KERN_ARGMAX;
		break;
	case _SC_CHILD_MAX:
		if (getrlimit(RLIMIT_NPROC, &rl) != 0)
			return (-1);
		if (rl.rlim_cur == RLIM_INFINITY)
			return (-1);
		if (rl.rlim_cur > LONG_MAX) {
			errno = EOVERFLOW;
			return (-1);
		}
		return ((long)rl.rlim_cur);
	case _SC_CLK_TCK:
		return (CLK_TCK);
	case _SC_JOB_CONTROL:
		return (_POSIX_JOB_CONTROL);
	case _SC_NGROUPS_MAX:
		mib[0] = CTL_KERN;
		mib[1] = KERN_NGROUPS;
		break;
	case _SC_OPEN_MAX:
		if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
			return (-1);
		if (rl.rlim_cur == RLIM_INFINITY)
			return (-1);
		if (rl.rlim_cur > LONG_MAX) {
			errno = EOVERFLOW;
			return (-1);
		}
		return ((long)rl.rlim_cur);
	case _SC_STREAM_MAX:
		if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
			return (-1);
		if (rl.rlim_cur == RLIM_INFINITY)
			return (-1);
		if (rl.rlim_cur > LONG_MAX) {
			errno = EOVERFLOW;
			return (-1);
		}
		/*
		 * struct __sFILE currently has a limitation that
		 * file descriptors must fit in a signed short.
		 * This doesn't precisely capture the letter of POSIX
		 * but approximates the spirit.
		 */
		if (rl.rlim_cur > SHRT_MAX)
			return (SHRT_MAX);

		return ((long)rl.rlim_cur);
	case _SC_TZNAME_MAX:
		return (NAME_MAX);
	case _SC_SAVED_IDS:
		return (_POSIX_SAVED_IDS);
	case _SC_VERSION:
		mib[0] = CTL_KERN;
		mib[1] = KERN_POSIX1;
		break;

/* 1003.1b */
	case _SC_PAGESIZE:
		if (_pagesize != 0)
			return (_pagesize);
		mib[0] = CTL_HW;
		mib[1] = HW_PAGESIZE;
		break;
	case _SC_FSYNC:
		return (_POSIX_FSYNC);

/* 1003.1c */
	case _SC_LOGIN_NAME_MAX:
		return (LOGIN_NAME_MAX);

	case _SC_THREAD_SAFE_FUNCTIONS:
		return (_POSIX_THREAD_SAFE_FUNCTIONS);

	case _SC_GETGR_R_SIZE_MAX:
		return (_GR_BUF_LEN);

	case _SC_GETPW_R_SIZE_MAX:
		return (_PW_BUF_LEN);

/* 1003.2 */
	case _SC_BC_BASE_MAX:
		return (BC_BASE_MAX);
	case _SC_BC_DIM_MAX:
		return (BC_DIM_MAX);
	case _SC_BC_SCALE_MAX:
		return (BC_SCALE_MAX);
	case _SC_BC_STRING_MAX:
		return (BC_STRING_MAX);
	case _SC_COLL_WEIGHTS_MAX:
		return (COLL_WEIGHTS_MAX);
	case _SC_EXPR_NEST_MAX:
		return (EXPR_NEST_MAX);
	case _SC_LINE_MAX:
		return (LINE_MAX);
	case _SC_RE_DUP_MAX:
		return (RE_DUP_MAX);
	case _SC_2_VERSION:
		return (_POSIX2_VERSION);
	case _SC_2_C_BIND:
		return (_POSIX2_C_BIND);
	case _SC_2_C_DEV:
		return (_POSIX2_C_DEV);
	case _SC_2_CHAR_TERM:
		return (_POSIX2_CHAR_TERM);
	case _SC_2_FORT_DEV:
		return (_POSIX2_FORT_DEV);
	case _SC_2_FORT_RUN:
		return (_POSIX2_FORT_RUN);
	case _SC_2_LOCALEDEF:
		return (_POSIX2_LOCALEDEF);
	case _SC_2_SW_DEV:
		return (_POSIX2_SW_DEV);
	case _SC_2_UPE:
		return (_POSIX2_UPE);

/* XPG 4.2 */
	case _SC_XOPEN_SHM:
		mib[0] = CTL_KERN;
		mib[1] = KERN_SYSVSHM;
		if (sysctl(mib, namelen, &value, &len, NULL, 0) == -1)
			return (-1);
		if (value == 0)
			return (-1);
		return (value);
		break;
	case _SC_SEM_NSEMS_MAX:
		return (-1);
	case _SC_SEM_VALUE_MAX:
		return (SEM_VALUE_MAX);

/* Unsorted */
	case _SC_HOST_NAME_MAX:
		return (HOST_NAME_MAX);	/* does not include \0 */
	case _SC_MONOTONIC_CLOCK:
		return (_POSIX_MONOTONIC_CLOCK);
	case _SC_2_PBS:
	case _SC_2_PBS_ACCOUNTING:
	case _SC_2_PBS_CHECKPOINT:
	case _SC_2_PBS_LOCATE:
	case _SC_2_PBS_MESSAGE:
	case _SC_2_PBS_TRACK:
		return (_POSIX2_PBS);
	case _SC_ADVISORY_INFO:
		return (_POSIX_ADVISORY_INFO);
	case _SC_AIO_LISTIO_MAX:
	case _SC_AIO_MAX:
	case _SC_AIO_PRIO_DELTA_MAX:
		return (-1);
	case _SC_ASYNCHRONOUS_IO:
		return (_POSIX_ASYNCHRONOUS_IO);
	case _SC_ATEXIT_MAX:
		return (-1);
	case _SC_BARRIERS:
		return (_POSIX_BARRIERS);
	case _SC_CLOCK_SELECTION:
		return (_POSIX_CLOCK_SELECTION);
	case _SC_CPUTIME:
		return (_POSIX_CPUTIME);
	case _SC_DELAYTIMER_MAX:
		return (-1);
	case _SC_IOV_MAX:
		return (IOV_MAX);
	case _SC_IPV6:
#if _POSIX_IPV6 == 0
		sverrno = errno;
		mib[0] = CTL_NET;
		mib[1] = PF_INET6;
		mib[2] = 0;
		namelen = 3;
		value = 0;
		if (sysctl(mib, 3, NULL, 0, NULL, 0) == -1)
			value = errno;
		errno = sverrno;
		if (value != ENOPROTOOPT)
			return (200112L);
		else
			return (0);
#else
		return (_POSIX_IPV6);
#endif
	case _SC_MAPPED_FILES:
		return (_POSIX_MAPPED_FILES);
	case _SC_MEMLOCK:
		return (_POSIX_MEMLOCK);
	case _SC_MEMLOCK_RANGE:
		return (_POSIX_MEMLOCK_RANGE);
	case _SC_MEMORY_PROTECTION:
		return (_POSIX_MEMORY_PROTECTION);
	case _SC_MESSAGE_PASSING:
		return (_POSIX_MESSAGE_PASSING);
	case _SC_PRIORITIZED_IO:
		return (_POSIX_PRIORITIZED_IO);
	case _SC_PRIORITY_SCHEDULING:
		return (_POSIX_PRIORITY_SCHEDULING);
	case _SC_RAW_SOCKETS:
		return (_POSIX_RAW_SOCKETS);
	case _SC_READER_WRITER_LOCKS:
		return (_POSIX_READER_WRITER_LOCKS);
	case _SC_REALTIME_SIGNALS:
		return (_POSIX_REALTIME_SIGNALS);
	case _SC_REGEXP:
		return (_POSIX_REGEXP);
	case _SC_SEMAPHORES:
		return (_POSIX_SEMAPHORES);
	case _SC_SHARED_MEMORY_OBJECTS:
		return (_POSIX_SHARED_MEMORY_OBJECTS);
	case _SC_SHELL:
		return (_POSIX_SHELL);
	case _SC_SIGQUEUE_MAX:
		return (-1);
	case _SC_SPAWN:
		return (_POSIX_SPAWN);
	case _SC_SPIN_LOCKS:
		return (_POSIX_SPIN_LOCKS);
	case _SC_SPORADIC_SERVER:
		return (_POSIX_SPORADIC_SERVER);
	case _SC_SYNCHRONIZED_IO:
		return (_POSIX_SYNCHRONIZED_IO);
	case _SC_SYMLOOP_MAX:
		return (SYMLOOP_MAX);
	case _SC_THREAD_ATTR_STACKADDR:
		return (_POSIX_THREAD_ATTR_STACKADDR);
	case _SC_THREAD_ATTR_STACKSIZE:
		return (_POSIX_THREAD_ATTR_STACKSIZE);
	case _SC_THREAD_CPUTIME:
		return (_POSIX_THREAD_CPUTIME);
	case _SC_THREAD_DESTRUCTOR_ITERATIONS:
		return (PTHREAD_DESTRUCTOR_ITERATIONS);
	case _SC_THREAD_KEYS_MAX:
		return (PTHREAD_KEYS_MAX);
	case _SC_THREAD_PRIO_INHERIT:
		return (_POSIX_THREAD_PRIO_INHERIT);
	case _SC_THREAD_PRIO_PROTECT:
		return (_POSIX_THREAD_PRIO_PROTECT);
	case _SC_THREAD_PRIORITY_SCHEDULING:
		return (_POSIX_THREAD_PRIORITY_SCHEDULING);
	case _SC_THREAD_PROCESS_SHARED:
		return (_POSIX_THREAD_PROCESS_SHARED);
	case _SC_THREAD_ROBUST_PRIO_INHERIT:
		return (_POSIX_THREAD_ROBUST_PRIO_INHERIT);
	case _SC_THREAD_ROBUST_PRIO_PROTECT:
		return (_POSIX_THREAD_ROBUST_PRIO_PROTECT);
	case _SC_THREAD_SPORADIC_SERVER:
		return (_POSIX_THREAD_SPORADIC_SERVER);
	case _SC_THREAD_STACK_MIN:
		return (PTHREAD_STACK_MIN);
	case _SC_THREAD_THREADS_MAX:
		return (PTHREAD_THREADS_MAX);
	case _SC_THREADS:
		return (_POSIX_THREADS);
	case _SC_TIMEOUTS:
		return (_POSIX_TIMEOUTS);
	case _SC_TIMER_MAX:
		return (-1);
	case _SC_TIMERS:
		return (_POSIX_TIMERS);
	case _SC_TRACE:
	case _SC_TRACE_EVENT_FILTER:
	case _SC_TRACE_EVENT_NAME_MAX:
	case _SC_TRACE_INHERIT:
	case _SC_TRACE_LOG:
		return (_POSIX_TRACE);
	case _SC_TTY_NAME_MAX:
		return (TTY_NAME_MAX);
	case _SC_TYPED_MEMORY_OBJECTS:
		return (_POSIX_TYPED_MEMORY_OBJECTS);
	case _SC_V6_ILP32_OFF32:
		return (_POSIX_V6_ILP32_OFF32);
	case _SC_V6_ILP32_OFFBIG:
#if _POSIX_V6_ILP32_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(long) * CHAR_BIT == 32 &&
		    sizeof(void *) * CHAR_BIT == 32 &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V6_ILP32_OFFBIG);
#endif
	case _SC_V6_LP64_OFF64:
#if _POSIX_V6_LP64_OFF64 == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(long) * CHAR_BIT == 64 &&
		    sizeof(void *) * CHAR_BIT == 64 &&
		    sizeof(off_t) * CHAR_BIT == 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V6_LP64_OFF64);
#endif
	case _SC_V6_LPBIG_OFFBIG:
#if _POSIX_V6_LPBIG_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT >= 32 &&
		    sizeof(long) * CHAR_BIT >= 64 &&
		    sizeof(void *) * CHAR_BIT >= 64 &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V6_LPBIG_OFFBIG);
#endif
	case _SC_V7_ILP32_OFF32:
		return (_POSIX_V7_ILP32_OFF32);
	case _SC_V7_ILP32_OFFBIG:
#if _POSIX_V7_ILP32_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(long) * CHAR_BIT == 32 &&
		    sizeof(void *) * CHAR_BIT == 32 &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V7_ILP32_OFFBIG);
#endif
	case _SC_V7_LP64_OFF64:
#if _POSIX_V7_LP64_OFF64 == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(long) * CHAR_BIT == 64 &&
		    sizeof(void *) * CHAR_BIT == 64 &&
		    sizeof(off_t) * CHAR_BIT == 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V7_LP64_OFF64);
#endif
	case _SC_V7_LPBIG_OFFBIG:
#if _POSIX_V7_LPBIG_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT >= 32 &&
		    sizeof(long) * CHAR_BIT >= 64 &&
		    sizeof(void *) * CHAR_BIT >= 64 &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_POSIX_V7_LPBIG_OFFBIG);
#endif
	case _SC_XOPEN_CRYPT:
		return (_XOPEN_CRYPT);
	case _SC_XOPEN_ENH_I18N:
		return (_XOPEN_ENH_I18N);
	case _SC_XOPEN_LEGACY:
		return (_XOPEN_LEGACY);
	case _SC_XOPEN_REALTIME:
		return (_XOPEN_REALTIME);
	case _SC_XOPEN_REALTIME_THREADS:
		return (_XOPEN_REALTIME_THREADS);
	case _SC_XOPEN_STREAMS:
		return (_XOPEN_STREAMS);
	case _SC_XOPEN_UNIX:
		return (_XOPEN_UNIX);
	case _SC_XOPEN_UUCP:
		return (_XOPEN_UUCP);
#ifdef _XOPEN_VERSION
	case _SC_XOPEN_VERSION:
		return (_XOPEN_VERSION);
#endif

/* Extensions */
	case _SC_PHYS_PAGES:
	{
		int64_t physmem;

		mib[0] = CTL_HW;
		mib[1] = HW_PHYSMEM64;
		len = sizeof(physmem);
		if (sysctl(mib, namelen, &physmem, &len, NULL, 0) == -1)
			return (-1);
		return (physmem / getpagesize());
	}
	case _SC_AVPHYS_PAGES:
	{
		struct uvmexp uvmexp;

		mib[0] = CTL_VM;
		mib[1] = VM_UVMEXP;
		len = sizeof(uvmexp);
		if (sysctl(mib, namelen, &uvmexp, &len, NULL, 0) == -1)
			return (-1);
		return (uvmexp.free);
	}

	case _SC_NPROCESSORS_CONF:
		mib[0] = CTL_HW;
		mib[1] = HW_NCPU;
		break;
	case _SC_NPROCESSORS_ONLN:
		mib[0] = CTL_HW;
		mib[1] = HW_NCPUONLINE;
		break;

	default:
		errno = EINVAL;
		return (-1);
	}
	return (sysctl(mib, namelen, &value, &len, NULL, 0) == -1 ? -1 : value); 
}
DEF_WEAK(sysconf);
