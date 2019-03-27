/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Stacey Son <sson@FreeBSD.org> 
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
 
/*
 * DTrace lockstat provider definitions
 */

#ifndef _SYS_LOCKSTAT_H
#define	_SYS_LOCKSTAT_H

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(lockstat);

SDT_PROBE_DECLARE(lockstat, , , adaptive__acquire);
SDT_PROBE_DECLARE(lockstat, , , adaptive__release);
SDT_PROBE_DECLARE(lockstat, , , adaptive__spin);
SDT_PROBE_DECLARE(lockstat, , , adaptive__block);

SDT_PROBE_DECLARE(lockstat, , , spin__acquire);
SDT_PROBE_DECLARE(lockstat, , , spin__release);
SDT_PROBE_DECLARE(lockstat, , , spin__spin);

SDT_PROBE_DECLARE(lockstat, , , rw__acquire);
SDT_PROBE_DECLARE(lockstat, , , rw__release);
SDT_PROBE_DECLARE(lockstat, , , rw__block);
SDT_PROBE_DECLARE(lockstat, , , rw__spin);
SDT_PROBE_DECLARE(lockstat, , , rw__upgrade);
SDT_PROBE_DECLARE(lockstat, , , rw__downgrade);

SDT_PROBE_DECLARE(lockstat, , , sx__acquire);
SDT_PROBE_DECLARE(lockstat, , , sx__release);
SDT_PROBE_DECLARE(lockstat, , , sx__block);
SDT_PROBE_DECLARE(lockstat, , , sx__spin);
SDT_PROBE_DECLARE(lockstat, , , sx__upgrade);
SDT_PROBE_DECLARE(lockstat, , , sx__downgrade);

SDT_PROBE_DECLARE(lockstat, , , thread__spin);

#define	LOCKSTAT_WRITER		0
#define	LOCKSTAT_READER		1

extern volatile bool lockstat_enabled;

#ifdef KDTRACE_HOOKS

#define	LOCKSTAT_RECORD0(probe, lp)					\
	SDT_PROBE1(lockstat, , , probe, lp)

#define	LOCKSTAT_RECORD1(probe, lp, arg1)				\
	SDT_PROBE2(lockstat, , , probe, lp, arg1)

#define	LOCKSTAT_RECORD2(probe, lp, arg1, arg2)				\
	SDT_PROBE3(lockstat, , , probe, lp, arg1, arg2)

#define	LOCKSTAT_RECORD3(probe, lp, arg1, arg2, arg3)			\
	SDT_PROBE4(lockstat, , , probe, lp, arg1, arg2, arg3)

#define	LOCKSTAT_RECORD4(probe, lp, arg1, arg2, arg3, arg4)		\
	SDT_PROBE5(lockstat, , , probe, lp, arg1, arg2, arg3, arg4)

#define	LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(probe, lp, c, wt, f, l) do { \
	lock_profile_obtain_lock_success(&(lp)->lock_object, c, wt, f, l); \
	LOCKSTAT_RECORD0(probe, lp);					\
} while (0)

#define	LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(probe, lp, c, wt, f, l, a) do { \
	lock_profile_obtain_lock_success(&(lp)->lock_object, c, wt, f, l); \
	LOCKSTAT_RECORD1(probe, lp, a);					\
} while (0)

#define	LOCKSTAT_PROFILE_RELEASE_LOCK(probe, lp) do {			\
	lock_profile_release_lock(&(lp)->lock_object);			\
	LOCKSTAT_RECORD0(probe, lp);					\
} while (0)

#define	LOCKSTAT_PROFILE_RELEASE_RWLOCK(probe, lp, a) do {		\
	lock_profile_release_lock(&(lp)->lock_object);			\
	LOCKSTAT_RECORD1(probe, lp, a);					\
} while (0)

#define	LOCKSTAT_PROFILE_ENABLED(probe)		__predict_false(lockstat_enabled)

struct lock_object;
uint64_t lockstat_nsecs(struct lock_object *);

#else /* !KDTRACE_HOOKS */

#define	LOCKSTAT_RECORD0(probe, lp)
#define	LOCKSTAT_RECORD1(probe, lp, arg1)
#define	LOCKSTAT_RECORD2(probe, lp, arg1, arg2)
#define	LOCKSTAT_RECORD3(probe, lp, arg1, arg2, arg3)
#define	LOCKSTAT_RECORD4(probe, lp, arg1, arg2, arg3, arg4)

#define	LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(probe, lp, c, wt, f, l)	\
	lock_profile_obtain_lock_success(&(lp)->lock_object, c, wt, f, l)

#define	LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(probe, lp, c, wt, f, l, a) \
	LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(probe, lp, c, wt, f, l)

#define	LOCKSTAT_PROFILE_RELEASE_LOCK(probe, lp)  			\
	lock_profile_release_lock(&(lp)->lock_object)

#define	LOCKSTAT_PROFILE_RELEASE_RWLOCK(probe, lp, a)  			\
	LOCKSTAT_PROFILE_RELEASE_LOCK(probe, lp)

#define	LOCKSTAT_PROFILE_ENABLED(probe)		0

#endif /* !KDTRACE_HOOKS */

#endif /* _KERNEL */
#endif /* _SYS_LOCKSTAT_H */
