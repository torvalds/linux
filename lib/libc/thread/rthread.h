/*	$OpenBSD: rthread.h,v 1.2 2017/09/05 02:40:54 guenther Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * All Rights Reserved.
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

#ifndef _RTHREAD_H_
#define _RTHREAD_H_

#include "thread_private.h"

__BEGIN_HIDDEN_DECLS
void	_rthread_tls_destructors(pthread_t);

extern int _rthread_debug_level;
extern struct pthread _initial_thread;
__END_HIDDEN_DECLS

PROTO_NORMAL(__threxit);
PROTO_NORMAL(__thrsigdivert);
PROTO_NORMAL(__thrsleep);
PROTO_NORMAL(__thrwakeup);

PROTO_NORMAL(_spinlock);
PROTO_STD_DEPRECATED(_spinlocktry);
PROTO_NORMAL(_spinunlock);
PROTO_NORMAL(_rthread_debug);

#endif /* _RTHREAD_H_ */
