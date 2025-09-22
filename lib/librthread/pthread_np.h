/*	$OpenBSD: pthread_np.h,v 1.2 2019/02/04 17:18:08 tedu Exp $	*/
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

#ifndef _LIBPTHREAD_PTHREAD_NP_H_
#define	_LIBPTHREAD_PTHREAD_NP_H_

#include_next <pthread_np.h>

PROTO_DEPRECATED(pthread_main_np);
PROTO_DEPRECATED(pthread_mutexattr_getkind_np);
PROTO_DEPRECATED(pthread_mutexattr_setkind_np);
PROTO_DEPRECATED(pthread_get_name_np);
PROTO_DEPRECATED(pthread_set_name_np);
PROTO_DEPRECATED(pthread_stackseg_np);

#endif /* !_LIBPTHREAD_PTHREAD_NP_H_ */
