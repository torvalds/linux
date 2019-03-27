/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Kip Macy kmacy@FreeBSD.org
 * Copyright (c) 2006 Kris Kennaway kris@FreeBSD.org
 * Copyright (c) 2006 Dag-Erling Smorgrav des@des.no
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHAL THE AUTHORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#ifndef _SYS_LOCK_PROFILE_H_
#define _SYS_LOCK_PROFILE_H_

struct lock_profile_object;
LIST_HEAD(lpohead, lock_profile_object);

#ifdef _KERNEL
#ifdef LOCK_PROFILING
#include <machine/cpufunc.h>
#include <sys/lock.h>

#ifndef USE_CPU_NANOSECONDS
u_int64_t nanoseconds(void);
#endif

extern volatile int lock_prof_enable;

void lock_profile_obtain_lock_success(struct lock_object *lo, int contested,
    uint64_t waittime, const char *file, int line);
void lock_profile_release_lock(struct lock_object *lo);
void lock_profile_thread_exit(struct thread *td);


static inline void
lock_profile_obtain_lock_failed(struct lock_object *lo, int *contested,
    uint64_t *waittime)
{
	if (!lock_prof_enable || (lo->lo_flags & LO_NOPROFILE) || *contested)
		return;
	*waittime = nanoseconds();
	*contested = 1;
}

#else /* !LOCK_PROFILING */

#define	lock_profile_release_lock(lo)					(void)0
#define lock_profile_obtain_lock_failed(lo, contested, waittime)	(void)0
#define lock_profile_obtain_lock_success(lo, contested, waittime, file, line)	(void)0
#define	lock_profile_thread_exit(td)					(void)0

#endif  /* !LOCK_PROFILING */

#endif /* _KERNEL */

#endif /* _SYS_LOCK_PROFILE_H_ */
