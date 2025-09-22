/*	$OpenBSD: cancel.h,v 1.5 2017/09/05 02:40:54 guenther Exp $ */
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

#ifndef _CANCEL_H_
#define _CANCEL_H_

#include <tib.h>
#include "thread_private.h"

/* process a cancel request at a cancel point */
__dead void	_thread_canceled(void);

#ifdef __LIBC__
PROTO_NORMAL(_thread_canceled);
#endif

#if defined(__LIBC__) && !defined(TCB_HAVE_MD_GET)
/*
 * Override TIB_GET macro to use the caching callback
 */
#undef TIB_GET
#define TIB_GET()	TCB_TO_TIB(_thread_cb.tc_tcb())
#endif

#define PREP_CANCEL_POINT(tib)						\
	int _cantcancel = (tib)->tib_cantcancel

#define	ENTER_CANCEL_POINT_INNER(tib, can_cancel, delay)		\
	if (_cantcancel == 0) {						\
		(tib)->tib_cancel_point = (delay) ?			\
		    CANCEL_POINT_DELAYED : CANCEL_POINT;		\
		if (can_cancel) {					\
			__asm volatile("":::"memory");			\
			if (__predict_false((tib)->tib_canceled))	\
				_thread_canceled();			\
		}							\
	}

#define	LEAVE_CANCEL_POINT_INNER(tib, can_cancel)			\
	if (_cantcancel == 0) {						\
		(tib)->tib_cancel_point = 0;				\
		if (can_cancel) {					\
			__asm volatile("":::"memory");			\
			if (__predict_false((tib)->tib_canceled))	\
				_thread_canceled();			\
		}							\
	}

/*
 * Enter or leave a cancelation point, optionally processing pending
 * cancelation requests.  Note that ENTER_CANCEL_POINT opens a block
 * and LEAVE_CANCEL_POINT must close that same block.
 */
#define	ENTER_CANCEL_POINT(can_cancel)					\
    {									\
	struct tib *_tib = TIB_GET();					\
	PREP_CANCEL_POINT(_tib);					\
	ENTER_CANCEL_POINT_INNER(_tib, can_cancel, 0)

#define	LEAVE_CANCEL_POINT(can_cancel)					\
	LEAVE_CANCEL_POINT_INNER(_tib, can_cancel);			\
    }

#endif /* _CANCEL_H_ */
