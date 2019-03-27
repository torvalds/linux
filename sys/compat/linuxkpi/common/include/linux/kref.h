/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2015 Mellanox Technologies, Ltd.
 * Copyright (c) 2013 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _LINUX_KREF_H_
#define _LINUX_KREF_H_

#include <sys/types.h>
#include <sys/refcount.h>

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#include <asm/atomic.h>

struct kref {
	atomic_t refcount;
};

static inline void
kref_init(struct kref *kref)
{

	refcount_init(&kref->refcount.counter, 1);
}

static inline unsigned int
kref_read(const struct kref *kref)
{

	return (atomic_read(&kref->refcount));
}

static inline void
kref_get(struct kref *kref)
{

	refcount_acquire(&kref->refcount.counter);
}

static inline int
kref_put(struct kref *kref, void (*rel)(struct kref *kref))
{

	if (refcount_release(&kref->refcount.counter)) {
		rel(kref);
		return 1;
	}
	return 0;
}

static inline int
kref_sub(struct kref *kref, unsigned int count,
    void (*rel)(struct kref *kref))
{

	while (count--) {
		if (refcount_release(&kref->refcount.counter)) {
			rel(kref);
			return 1;
		}
	}
	return 0;
}

static inline int __must_check
kref_get_unless_zero(struct kref *kref)
{

	return atomic_add_unless(&kref->refcount, 1, 0);
}

static inline int kref_put_mutex(struct kref *kref,
    void (*release)(struct kref *kref), struct mutex *lock)
{
	WARN_ON(release == NULL);
	if (unlikely(!atomic_add_unless(&kref->refcount, -1, 1))) {
		mutex_lock(lock);
		if (unlikely(!atomic_dec_and_test(&kref->refcount))) {
			mutex_unlock(lock);
			return 0;
		}
		release(kref);
		return 1;
	}
	return 0;
}

#endif /* _LINUX_KREF_H_ */
