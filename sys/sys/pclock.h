/*	$OpenBSD: pclock.h,v 1.1 2025/05/31 10:24:50 dlg Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#ifndef _SYS_PCLOCK_H
#define _SYS_PCLOCK_H

#include <sys/_lock.h>

struct pc_lock {
	volatile unsigned int	 pcl_gen;
};

#ifdef _KERNEL

#define PC_LOCK_INITIALIZER() { .pcl_gen = 0 }

void		pc_lock_init(struct pc_lock *);

/* single (non-interlocking) producer */
unsigned int	pc_sprod_enter(struct pc_lock *);
void		pc_sprod_leave(struct pc_lock *, unsigned int);

/* multiple (interlocking) producers */
unsigned int	pc_mprod_enter(struct pc_lock *);
void		pc_mprod_leave(struct pc_lock *, unsigned int);

/* consumer */
void		pc_cons_enter(struct pc_lock *, unsigned int *);
__warn_unused_result int
		pc_cons_leave(struct pc_lock *, unsigned int *);

#endif /* _KERNEL */

#endif /* _SYS_PCLOCK_H */
