/*	$OpenBSD: refcnt.h,v 1.16 2025/08/05 12:52:20 bluhm Exp $ */

/*
 * Copyright (c) 2015 David Gwynne <dlg@openbsd.org>
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

#ifndef _SYS_REFCNT_H_
#define _SYS_REFCNT_H_

/*
 * Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	a	atomic operations
 */

struct refcnt {
	unsigned int	r_refs;		/* [a] reference counter */
	int		r_traceidx;	/* [I] index for dt(4) tracing  */
};

#define REFCNT_INITIALIZER()		{ .r_refs = 1, .r_traceidx = 0 }

#ifdef _KERNEL

void	refcnt_init(struct refcnt *);
void	refcnt_init_trace(struct refcnt *, int);
void	refcnt_take(struct refcnt *);
int	refcnt_rele(struct refcnt *);
void	refcnt_rele_wake(struct refcnt *);
void	refcnt_finalize(struct refcnt *, const char *);
unsigned int	refcnt_read(const struct refcnt *);

#define refcnt_shared(_r) (refcnt_read((_r)) > 1)

/* sorted alphabetically, keep in sync with dev/dt/dt_prov_static.c */
#define DT_REFCNT_IDX_ETHMULTI	1
#define DT_REFCNT_IDX_IFADDR	2
#define DT_REFCNT_IDX_IFMADDR	3
#define DT_REFCNT_IDX_INPCB	4
#define DT_REFCNT_IDX_RTENTRY	5
#define DT_REFCNT_IDX_SOCKET	6
#define DT_REFCNT_IDX_SYNCACHE	7
#define DT_REFCNT_IDX_TDB	8

#endif /* _KERNEL */

#endif /* _SYS_REFCNT_H_ */
