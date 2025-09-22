/*	$OpenBSD: pthread.h,v 1.5 2018/03/31 22:06:22 guenther Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#ifndef _LIBC_PTHREAD_H_
#define	_LIBC_PTHREAD_H_

#include_next <pthread.h>

PROTO_STD_DEPRECATED(pthread_atfork);
PROTO_STD_DEPRECATED(pthread_cond_broadcast);
PROTO_STD_DEPRECATED(pthread_cond_destroy);
PROTO_NORMAL(pthread_cond_init);
PROTO_STD_DEPRECATED(pthread_cond_signal);
PROTO_STD_DEPRECATED(pthread_cond_timedwait);
PROTO_STD_DEPRECATED(pthread_cond_wait);
PROTO_STD_DEPRECATED(pthread_condattr_destroy);
PROTO_STD_DEPRECATED(pthread_condattr_getclock);
PROTO_STD_DEPRECATED(pthread_condattr_init);
PROTO_STD_DEPRECATED(pthread_condattr_setclock);
PROTO_STD_DEPRECATED(pthread_equal);
PROTO_NORMAL(pthread_exit);
PROTO_NORMAL(pthread_getspecific);
PROTO_NORMAL(pthread_key_create);
PROTO_STD_DEPRECATED(pthread_key_delete);
PROTO_NORMAL(pthread_mutex_destroy);
PROTO_NORMAL(pthread_mutex_init);
PROTO_NORMAL(pthread_mutex_lock);
PROTO_STD_DEPRECATED(pthread_mutex_timedlock);
PROTO_STD_DEPRECATED(pthread_mutex_trylock);
PROTO_NORMAL(pthread_mutex_unlock);
PROTO_STD_DEPRECATED(pthread_once);
PROTO_NORMAL(pthread_self);
PROTO_NORMAL(pthread_setspecific);

#endif /* !_LIBC_PTHREAD_H_ */
