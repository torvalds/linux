/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Jake Burkholder <jake@freebsd.org>.
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

#ifndef	_SYS_CONDVAR_H_
#define	_SYS_CONDVAR_H_

#ifndef	LOCORE
#include <sys/queue.h>

struct lock_object;
struct thread;

TAILQ_HEAD(cv_waitq, thread);

/*
 * Condition variable.  The waiters count is protected by the mutex that
 * protects the condition; that is, the mutex that is passed to cv_wait*()
 * and is held across calls to cv_signal() and cv_broadcast().  It is an
 * optimization to avoid looking up the sleep queue if there are no waiters.
 */
struct cv {
	const char	*cv_description;
	int		cv_waiters;
};

#ifdef _KERNEL
void	cv_init(struct cv *cvp, const char *desc);
void	cv_destroy(struct cv *cvp);

void	_cv_wait(struct cv *cvp, struct lock_object *lock);
void	_cv_wait_unlock(struct cv *cvp, struct lock_object *lock);
int	_cv_wait_sig(struct cv *cvp, struct lock_object *lock);
int	_cv_timedwait_sbt(struct cv *cvp, struct lock_object *lock,
	    sbintime_t sbt, sbintime_t pr, int flags);
int	_cv_timedwait_sig_sbt(struct cv *cvp, struct lock_object *lock,
	    sbintime_t sbt, sbintime_t pr, int flags);

void	cv_signal(struct cv *cvp);
void	cv_broadcastpri(struct cv *cvp, int pri);

#define	cv_wait(cvp, lock)						\
	_cv_wait((cvp), &(lock)->lock_object)
#define	cv_wait_unlock(cvp, lock)					\
	_cv_wait_unlock((cvp), &(lock)->lock_object)
#define	cv_wait_sig(cvp, lock)						\
	_cv_wait_sig((cvp), &(lock)->lock_object)
#define	cv_timedwait(cvp, lock, timo)					\
	_cv_timedwait_sbt((cvp), &(lock)->lock_object,			\
	    tick_sbt * (timo), 0, C_HARDCLOCK)
#define	cv_timedwait_sbt(cvp, lock, sbt, pr, flags)			\
	_cv_timedwait_sbt((cvp), &(lock)->lock_object, (sbt), (pr), (flags))
#define	cv_timedwait_sig(cvp, lock, timo)				\
	_cv_timedwait_sig_sbt((cvp), &(lock)->lock_object,		\
	    tick_sbt * (timo), 0, C_HARDCLOCK)
#define	cv_timedwait_sig_sbt(cvp, lock, sbt, pr, flags)			\
	_cv_timedwait_sig_sbt((cvp), &(lock)->lock_object, (sbt), (pr), (flags))

#define cv_broadcast(cvp)	cv_broadcastpri(cvp, 0)

#define	cv_wmesg(cvp)		((cvp)->cv_description)

#endif	/* _KERNEL */
#endif	/* !LOCORE */
#endif	/* _SYS_CONDVAR_H_ */
