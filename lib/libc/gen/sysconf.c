/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__SCCSID("@(#)sysconf.c	8.2 (Berkeley) 3/20/94");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <pthread.h>		/* we just need the limits */
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include "un-namespace.h"

#include "../stdlib/atexit.h"
#include "tzfile.h"		/* from ../../../contrib/tzcode/stdtime */
#include "libc_private.h"

#define	_PATH_ZONEINFO	TZDIR	/* from tzfile.h */

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
	int mib[2], sverrno, value;
	long lvalue, defaultresult;
	const char *path;

	defaultresult = -1;

	switch (name) {
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
	case _SC_JOB_CONTROL:
		return (_POSIX_JOB_CONTROL);
	case _SC_SAVED_IDS:
		/* XXX - must be 1 */
		mib[0] = CTL_KERN;
		mib[1] = KERN_SAVED_IDS;
		goto yesno;
	case _SC_VERSION:
		mib[0] = CTL_KERN;
		mib[1] = KERN_POSIX1;
		break;
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
		/*
		 * This is something of a lie, but it would be silly at
		 * this point to try to deduce this from the contents
		 * of the filesystem.
		 */
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
	case _SC_TZNAME_MAX:
		path = _PATH_ZONEINFO;
do_NAME_MAX:
		sverrno = errno;
		errno = 0;
		lvalue = pathconf(path, _PC_NAME_MAX);
		if (lvalue == -1 && errno != 0)
			return (-1);
		errno = sverrno;
		return (lvalue);

	case _SC_ASYNCHRONOUS_IO:
#if _POSIX_ASYNCHRONOUS_IO == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_ASYNCHRONOUS_IO;
		break;
#else
		return (_POSIX_ASYNCHRONOUS_IO);
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
#if _POSIX_MESSAGE_PASSING == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_MESSAGE_PASSING;
		goto yesno;
#else
		return (_POSIX_MESSAGE_PASSING);
#endif
	case _SC_PRIORITIZED_IO:
#if _POSIX_PRIORITIZED_IO == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_PRIORITIZED_IO;
		goto yesno;
#else
		return (_POSIX_PRIORITIZED_IO);
#endif
	case _SC_PRIORITY_SCHEDULING:
#if _POSIX_PRIORITY_SCHEDULING == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_PRIORITY_SCHEDULING;
		goto yesno;
#else
		return (_POSIX_PRIORITY_SCHEDULING);
#endif
	case _SC_REALTIME_SIGNALS:
#if _POSIX_REALTIME_SIGNALS == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_REALTIME_SIGNALS;
		goto yesno;
#else
		return (_POSIX_REALTIME_SIGNALS);
#endif
	case _SC_SEMAPHORES:
#if _POSIX_SEMAPHORES == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_SEMAPHORES;
		goto yesno;
#else
		return (_POSIX_SEMAPHORES);
#endif
	case _SC_FSYNC:
		return (_POSIX_FSYNC);

	case _SC_SHARED_MEMORY_OBJECTS:
		return (_POSIX_SHARED_MEMORY_OBJECTS);
	case _SC_SYNCHRONIZED_IO:
#if _POSIX_SYNCHRONIZED_IO == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_SYNCHRONIZED_IO;
		goto yesno;
#else
		return (_POSIX_SYNCHRONIZED_IO);
#endif
	case _SC_TIMERS:
#if _POSIX_TIMERS == 0
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_TIMERS;
		goto yesno;
#else
		return (_POSIX_TIMERS);
#endif
	case _SC_AIO_LISTIO_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_AIO_LISTIO_MAX;
		break;
	case _SC_AIO_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_AIO_MAX;
		break;
	case _SC_AIO_PRIO_DELTA_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_AIO_PRIO_DELTA_MAX;
		break;
	case _SC_DELAYTIMER_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_DELAYTIMER_MAX;
		goto yesno;
	case _SC_MQ_OPEN_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_MQ_OPEN_MAX;
		goto yesno;
	case _SC_PAGESIZE:
		return (getpagesize());
	case _SC_RTSIG_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_RTSIG_MAX;
		goto yesno;
	case _SC_SEM_NSEMS_MAX:
		return (-1);
	case _SC_SEM_VALUE_MAX:
		return (SEM_VALUE_MAX);
	case _SC_SIGQUEUE_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_SIGQUEUE_MAX;
		goto yesno;
	case _SC_TIMER_MAX:
		mib[0] = CTL_P1003_1B;
		mib[1] = CTL_P1003_1B_TIMER_MAX;
yesno:
		len = sizeof(value);
		if (sysctl(mib, 2, &value, &len, NULL, 0) == -1)
			return (-1);
		if (value == 0)
			return (defaultresult);
		return ((long)value);

	case _SC_2_PBS:
	case _SC_2_PBS_ACCOUNTING:
	case _SC_2_PBS_CHECKPOINT:
	case _SC_2_PBS_LOCATE:
	case _SC_2_PBS_MESSAGE:
	case _SC_2_PBS_TRACK:
#if _POSIX2_PBS == 0
#error "don't know how to determine _SC_2_PBS"
		/*
		 * This probably requires digging through the filesystem
		 * to see if the appropriate package has been installed.
		 * Since we don't currently support this option at all,
		 * it's not worth the effort to write the code now.
		 * Figuring out which of the sub-options are supported
		 * would be even more difficult, so it's probably easier
		 * to always say ``no''.
		 */
#else
		return (_POSIX2_PBS);
#endif
	case _SC_ADVISORY_INFO:
#if _POSIX_ADVISORY_INFO == 0
#error "_POSIX_ADVISORY_INFO"
#else
		return (_POSIX_ADVISORY_INFO);
#endif
	case _SC_BARRIERS:
#if _POSIX_BARRIERS == 0
#error "_POSIX_BARRIERS"
#else
		return (_POSIX_BARRIERS);
#endif
	case _SC_CLOCK_SELECTION:
#if _POSIX_CLOCK_SELECTION == 0
#error "_POSIX_CLOCK_SELECTION"
#else
		return (_POSIX_CLOCK_SELECTION);
#endif
	case _SC_CPUTIME:
		return (_POSIX_CPUTIME);
#ifdef notdef
	case _SC_FILE_LOCKING:
		/*
		 * XXX - The standard doesn't tell us how to define
		 * _POSIX_FILE_LOCKING, so we can't answer this one.
		 */
#endif

	/*
	 * SUSv4tc1 says the following about _SC_GETGR_R_SIZE_MAX and
	 * _SC_GETPW_R_SIZE_MAX:
	 * Note that sysconf(_SC_GETGR_R_SIZE_MAX) may return -1 if
	 * there is no hard limit on the size of the buffer needed to
	 * store all the groups returned.
	 */
	case _SC_GETGR_R_SIZE_MAX:
	case _SC_GETPW_R_SIZE_MAX:
		return (-1);
	case _SC_HOST_NAME_MAX:
		return (MAXHOSTNAMELEN - 1); /* does not include \0 */
	case _SC_LOGIN_NAME_MAX:
		return (MAXLOGNAME);
	case _SC_MONOTONIC_CLOCK:
#if _POSIX_MONOTONIC_CLOCK == 0
#error "_POSIX_MONOTONIC_CLOCK"
#else
		return (_POSIX_MONOTONIC_CLOCK);
#endif
#if _POSIX_MESSAGE_PASSING > -1
	case _SC_MQ_PRIO_MAX:
		return (MQ_PRIO_MAX);
#endif
	case _SC_READER_WRITER_LOCKS:
		return (_POSIX_READER_WRITER_LOCKS);
	case _SC_REGEXP:
		return (_POSIX_REGEXP);
	case _SC_SHELL:
		return (_POSIX_SHELL);
	case _SC_SPAWN:
		return (_POSIX_SPAWN);
	case _SC_SPIN_LOCKS:
		return (_POSIX_SPIN_LOCKS);
	case _SC_SPORADIC_SERVER:
#if _POSIX_SPORADIC_SERVER == 0
#error "_POSIX_SPORADIC_SERVER"
#else
		return (_POSIX_SPORADIC_SERVER);
#endif
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
	case _SC_THREAD_SAFE_FUNCTIONS:
		return (_POSIX_THREAD_SAFE_FUNCTIONS);
	case _SC_THREAD_STACK_MIN:
		return (PTHREAD_STACK_MIN);
	case _SC_THREAD_THREADS_MAX:
		return (PTHREAD_THREADS_MAX); /* XXX wrong type! */
	case _SC_TIMEOUTS:
		return (_POSIX_TIMEOUTS);
	case _SC_THREADS:
		return (_POSIX_THREADS);
	case _SC_TRACE:
#if _POSIX_TRACE == 0
#error "_POSIX_TRACE"
		/* While you're implementing this, also do the ones below. */
#else
		return (_POSIX_TRACE);
#endif
#if _POSIX_TRACE > -1
	case _SC_TRACE_EVENT_FILTER:
		return (_POSIX_TRACE_EVENT_FILTER);
	case _SC_TRACE_INHERIT:
		return (_POSIX_TRACE_INHERIT);
	case _SC_TRACE_LOG:
		return (_POSIX_TRACE_LOG);
#endif
	case _SC_TTY_NAME_MAX:
		path = _PATH_DEV;
		goto do_NAME_MAX;
	case _SC_TYPED_MEMORY_OBJECTS:
#if _POSIX_TYPED_MEMORY_OBJECTS == 0
#error "_POSIX_TYPED_MEMORY_OBJECTS"
#else
		return (_POSIX_TYPED_MEMORY_OBJECTS);
#endif
	case _SC_V6_ILP32_OFF32:
#if _V6_ILP32_OFF32 == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(int) == sizeof(long) &&
		    sizeof(long) == sizeof(void *) &&
		    sizeof(void *) == sizeof(off_t))
			return 1;
		else
			return -1;
#else
		return (_V6_ILP32_OFF32);
#endif
	case _SC_V6_ILP32_OFFBIG:
#if _V6_ILP32_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(int) == sizeof(long) &&
		    sizeof(long) == sizeof(void *) &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_V6_ILP32_OFFBIG);
#endif
	case _SC_V6_LP64_OFF64:
#if _V6_LP64_OFF64 == 0
		if (sizeof(int) * CHAR_BIT == 32 &&
		    sizeof(long) * CHAR_BIT == 64 &&
		    sizeof(long) == sizeof(void *) &&
		    sizeof(void *) == sizeof(off_t))
			return 1;
		else
			return -1;
#else
		return (_V6_LP64_OFF64);
#endif
	case _SC_V6_LPBIG_OFFBIG:
#if _V6_LPBIG_OFFBIG == 0
		if (sizeof(int) * CHAR_BIT >= 32 &&
		    sizeof(long) * CHAR_BIT >= 64 &&
		    sizeof(void *) * CHAR_BIT >= 64 &&
		    sizeof(off_t) * CHAR_BIT >= 64)
			return 1;
		else
			return -1;
#else
		return (_V6_LPBIG_OFFBIG);
#endif
	case _SC_ATEXIT_MAX:
		return (ATEXIT_SIZE);
	case _SC_IOV_MAX:
		mib[0] = CTL_KERN;
		mib[1] = KERN_IOV_MAX;
		break;
	case _SC_XOPEN_CRYPT:
		return (_XOPEN_CRYPT);
	case _SC_XOPEN_ENH_I18N:
		return (_XOPEN_ENH_I18N);
	case _SC_XOPEN_LEGACY:
		return (_XOPEN_LEGACY);
	case _SC_XOPEN_REALTIME:
#if _XOPEN_REALTIME == 0
		sverrno = errno;
		value = sysconf(_SC_ASYNCHRONOUS_IO) > 0 &&
			sysconf(_SC_MEMLOCK) > 0 &&
			sysconf(_SC_MEMLOCK_RANGE) > 0 &&
			sysconf(_SC_MESSAGE_PASSING) > 0 &&
			sysconf(_SC_PRIORITY_SCHEDULING) > 0 &&
			sysconf(_SC_REALTIME_SIGNALS) > 0 &&
			sysconf(_SC_SEMAPHORES) > 0 &&
			sysconf(_SC_SHARED_MEMORY_OBJECTS) > 0 &&
			sysconf(_SC_SYNCHRONIZED_IO) > 0 &&
			sysconf(_SC_TIMERS) > 0;
		errno = sverrno;
		if (value)
			return (200112L);
		else
			return (-1);
#else
		return (_XOPEN_REALTIME);
#endif
	case _SC_XOPEN_REALTIME_THREADS:
#if _XOPEN_REALTIME_THREADS == 0
#error "_XOPEN_REALTIME_THREADS"
#else
		return (_XOPEN_REALTIME_THREADS);
#endif
	case _SC_XOPEN_SHM:
		len = sizeof(lvalue);
		sverrno = errno;
		if (sysctlbyname("kern.ipc.shmmin", &lvalue, &len, NULL,
		    0) == -1) {
			errno = sverrno;
			return (-1);
		}
		errno = sverrno;
		return (1);
	case _SC_XOPEN_STREAMS:
		return (_XOPEN_STREAMS);
	case _SC_XOPEN_UNIX:
		return (_XOPEN_UNIX);
#ifdef _XOPEN_VERSION
	case _SC_XOPEN_VERSION:
		return (_XOPEN_VERSION);
#endif
#ifdef _XOPEN_XCU_VERSION
	case _SC_XOPEN_XCU_VERSION:
		return (_XOPEN_XCU_VERSION);
#endif
	case _SC_SYMLOOP_MAX:
		return (MAXSYMLINKS);
	case _SC_RAW_SOCKETS:
		return (_POSIX_RAW_SOCKETS);
	case _SC_IPV6:
#if _POSIX_IPV6 == 0
		sverrno = errno;
		value = _socket(PF_INET6, SOCK_DGRAM, 0);
		errno = sverrno;
		if (value >= 0) {
			_close(value);
			return (200112L);
		} else
			return (0);
#else
		return (_POSIX_IPV6);
#endif

	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:
		if (_elf_aux_info(AT_NCPUS, &value, sizeof(value)) == 0)
			return ((long)value);
		mib[0] = CTL_HW;
		mib[1] = HW_NCPU;
		break;

#ifdef _SC_PHYS_PAGES
	case _SC_PHYS_PAGES:
		len = sizeof(lvalue);
		if (sysctlbyname("hw.availpages", &lvalue, &len, NULL, 0) == -1)
			return (-1);
		return (lvalue);
#endif

#ifdef _SC_CPUSET_SIZE
	case _SC_CPUSET_SIZE:
		len = sizeof(value);
		if (sysctlbyname("kern.sched.cpusetsize", &value, &len, NULL,
		    0) == -1)
			return (-1);
		return ((long)value);
#endif

	default:
		errno = EINVAL;
		return (-1);
	}
	len = sizeof(value);
	if (sysctl(mib, 2, &value, &len, NULL, 0) == -1)
		value = -1;
	return ((long)value);
}
