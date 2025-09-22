/*	$OpenBSD: kref.h,v 1.6 2023/03/21 09:44:35 jsg Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
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

#ifndef _LINUX_KREF_H
#define _LINUX_KREF_H

#include <sys/types.h>
#include <sys/rwlock.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>

struct kref {
	uint32_t refcount;
};

static inline void
kref_init(struct kref *ref)
{
	atomic_set(&ref->refcount, 1);
}

static inline unsigned int
kref_read(const struct kref *ref)
{
	return atomic_read(&ref->refcount);
}

static inline void
kref_get(struct kref *ref)
{
	atomic_inc_int(&ref->refcount);
}

static inline int
kref_get_unless_zero(struct kref *ref)
{
	if (ref->refcount != 0) {
		atomic_inc_int(&ref->refcount);
		return (1);
	} else {
		return (0);
	}
}

static inline int
kref_put(struct kref *ref, void (*release)(struct kref *ref))
{
	if (atomic_dec_int_nv(&ref->refcount) == 0) {
		release(ref);
		return 1;
	}
	return 0;
}

static inline int
kref_put_mutex(struct kref *kref, void (*release)(struct kref *kref),
    struct rwlock *lock)
{
	if (!atomic_add_unless(&kref->refcount, -1, 1)) {
		rw_enter_write(lock);
		if (likely(atomic_dec_and_test(&kref->refcount))) {
			release(kref);
			return 1;
		}
		rw_exit_write(lock);
		return 0;
	}

	return 0;
}

static inline int
kref_put_lock(struct kref *kref, void (*release)(struct kref *kref),
    struct mutex *lock)
{
	if (!atomic_add_unless(&kref->refcount, -1, 1)) {
		mtx_enter(lock);
		if (likely(atomic_dec_and_test(&kref->refcount))) {
			release(kref);
			return 1;
		}
		mtx_leave(lock);
		return 0;
	}

	return 0;
}

#endif
