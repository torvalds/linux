/*	$OpenBSD: futex.h,v 1.3 2025/05/07 00:39:09 dlg Exp $ */

/*
 * Copyright (c) 2016 Martin Pieuchot
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

#ifndef	_SYS_FUTEX_H_
#define	_SYS_FUTEX_H_

#ifndef _KERNEL
#include <sys/cdefs.h>

__BEGIN_DECLS
int futex(volatile uint32_t *, int, int, const struct timespec *,
    volatile uint32_t *);
__END_DECLS
#endif /* ! _KERNEL */

#define	FUTEX_OP_MASK		0x007f

#define	FUTEX_WAIT		1
#define	FUTEX_WAKE		2
#define	FUTEX_REQUEUE		3

#define	FUTEX_FLAG_MASK		0xff80

#define	FUTEX_PRIVATE_FLAG	0x0080

#define	FUTEX_WAIT_PRIVATE	(FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define	FUTEX_WAKE_PRIVATE	(FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#define	FUTEX_REQUEUE_PRIVATE	(FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG)

#endif	/* _SYS_FUTEX_H_ */
