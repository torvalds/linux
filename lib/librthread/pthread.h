/*	$OpenBSD: pthread.h,v 1.6 2017/11/04 22:53:57 jca Exp $	*/
/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LIBPTHREAD_PTHREAD_H_
#define	_LIBPTHREAD_PTHREAD_H_

#include_next <pthread.h>

/*
 * Functions with PROTO_NORMAL() here MUST have matching
 * DEF_STD() or DEF_NONSTD() in the file where they are defined!
 */

PROTO_STD_DEPRECATED(pthread_attr_destroy);
PROTO_STD_DEPRECATED(pthread_attr_getdetachstate);
PROTO_STD_DEPRECATED(pthread_attr_getguardsize);
PROTO_STD_DEPRECATED(pthread_attr_getinheritsched);
PROTO_STD_DEPRECATED(pthread_attr_getschedparam);
PROTO_STD_DEPRECATED(pthread_attr_getschedpolicy);
PROTO_STD_DEPRECATED(pthread_attr_getscope);
PROTO_STD_DEPRECATED(pthread_attr_getstack);
PROTO_STD_DEPRECATED(pthread_attr_getstacksize);
PROTO_STD_DEPRECATED(pthread_attr_init);
PROTO_STD_DEPRECATED(pthread_attr_setdetachstate);
PROTO_STD_DEPRECATED(pthread_attr_setguardsize);
PROTO_STD_DEPRECATED(pthread_attr_setinheritsched);
PROTO_STD_DEPRECATED(pthread_attr_setschedparam);
PROTO_STD_DEPRECATED(pthread_attr_setschedpolicy);
PROTO_STD_DEPRECATED(pthread_attr_setscope);
PROTO_STD_DEPRECATED(pthread_attr_setstack);
PROTO_STD_DEPRECATED(pthread_attr_setstacksize);
PROTO_STD_DEPRECATED(pthread_barrier_destroy);
PROTO_STD_DEPRECATED(pthread_barrier_init);
PROTO_STD_DEPRECATED(pthread_barrier_wait);
PROTO_STD_DEPRECATED(pthread_barrierattr_destroy);
PROTO_STD_DEPRECATED(pthread_barrierattr_getpshared);
PROTO_STD_DEPRECATED(pthread_barrierattr_init);
PROTO_STD_DEPRECATED(pthread_barrierattr_setpshared);
PROTO_STD_DEPRECATED(pthread_cancel);
PROTO_STD_DEPRECATED(pthread_cleanup_pop);
PROTO_STD_DEPRECATED(pthread_cleanup_push);
PROTO_STD_DEPRECATED(pthread_condattr_getclock);
PROTO_STD_DEPRECATED(pthread_condattr_setclock);
PROTO_STD_DEPRECATED(pthread_create);
PROTO_STD_DEPRECATED(pthread_detach);
PROTO_STD_DEPRECATED(pthread_getconcurrency);
PROTO_STD_DEPRECATED(pthread_getcpuclockid);
PROTO_STD_DEPRECATED(pthread_getschedparam);
PROTO_STD_DEPRECATED(pthread_join);
PROTO_STD_DEPRECATED(pthread_kill);
PROTO_STD_DEPRECATED(pthread_mutex_getprioceiling);
PROTO_STD_DEPRECATED(pthread_mutex_setprioceiling);
PROTO_STD_DEPRECATED(pthread_mutexattr_destroy);
PROTO_STD_DEPRECATED(pthread_mutexattr_getprioceiling);
PROTO_STD_DEPRECATED(pthread_mutexattr_getprotocol);
PROTO_STD_DEPRECATED(pthread_mutexattr_gettype);
PROTO_STD_DEPRECATED(pthread_mutexattr_init);
PROTO_STD_DEPRECATED(pthread_mutexattr_setprioceiling);
PROTO_STD_DEPRECATED(pthread_mutexattr_setprotocol);
PROTO_STD_DEPRECATED(pthread_mutexattr_settype);
PROTO_STD_DEPRECATED(pthread_rwlock_destroy);
PROTO_NORMAL(pthread_rwlock_init);
PROTO_STD_DEPRECATED(pthread_rwlock_rdlock);
PROTO_STD_DEPRECATED(pthread_rwlock_timedrdlock);
PROTO_STD_DEPRECATED(pthread_rwlock_timedwrlock);
PROTO_STD_DEPRECATED(pthread_rwlock_tryrdlock);
PROTO_STD_DEPRECATED(pthread_rwlock_trywrlock);
PROTO_STD_DEPRECATED(pthread_rwlock_unlock);
PROTO_STD_DEPRECATED(pthread_rwlock_wrlock);
PROTO_STD_DEPRECATED(pthread_rwlockattr_destroy);
PROTO_STD_DEPRECATED(pthread_rwlockattr_getpshared);
PROTO_STD_DEPRECATED(pthread_rwlockattr_init);
PROTO_STD_DEPRECATED(pthread_rwlockattr_setpshared);
PROTO_NORMAL(pthread_setcancelstate);
PROTO_STD_DEPRECATED(pthread_setcanceltype);
PROTO_STD_DEPRECATED(pthread_setconcurrency);
PROTO_STD_DEPRECATED(pthread_setschedparam);
PROTO_STD_DEPRECATED(pthread_spin_destroy);
PROTO_STD_DEPRECATED(pthread_spin_init);
PROTO_STD_DEPRECATED(pthread_spin_lock);
PROTO_STD_DEPRECATED(pthread_spin_trylock);
PROTO_STD_DEPRECATED(pthread_spin_unlock);
PROTO_STD_DEPRECATED(pthread_testcancel);

/*
 * Obsolete, non-portable
 */
PROTO_DEPRECATED(pthread_setprio);
PROTO_DEPRECATED(pthread_getprio);
PROTO_DEPRECATED(pthread_attr_getstackaddr);
PROTO_NORMAL(pthread_attr_setstackaddr);
PROTO_DEPRECATED(pthread_yield);

#endif /* !_LIBPTHREAD_PTHREAD_H_ */
