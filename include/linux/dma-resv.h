/*
 * Header file for reservations for dma-buf and ttm
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Copyright (C) 2012-2013 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 * Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/ww_mutex.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>

extern struct ww_class reservation_ww_class;

/**
 * struct dma_resv_list - a list of shared fences
 * @rcu: for internal use
 * @shared_count: table of shared fences
 * @shared_max: for growing shared fence table
 * @shared: shared fence table
 */
struct dma_resv_list {
	struct rcu_head rcu;
	u32 shared_count, shared_max;
	struct dma_fence __rcu *shared[];
};

/**
 * struct dma_resv - a reservation object manages fences for a buffer
 *
 * There are multiple uses for this, with sometimes slightly different rules in
 * how the fence slots are used.
 *
 * One use is to synchronize cross-driver access to a struct dma_buf, either for
 * dynamic buffer management or just to handle implicit synchronization between
 * different users of the buffer in userspace. See &dma_buf.resv for a more
 * in-depth discussion.
 *
 * The other major use is to manage access and locking within a driver in a
 * buffer based memory manager. struct ttm_buffer_object is the canonical
 * example here, since this is where reservation objects originated from. But
 * use in drivers is spreading and some drivers also manage struct
 * drm_gem_object with the same scheme.
 */
struct dma_resv {
	/**
	 * @lock:
	 *
	 * Update side lock. Don't use directly, instead use the wrapper
	 * functions like dma_resv_lock() and dma_resv_unlock().
	 *
	 * Drivers which use the reservation object to manage memory dynamically
	 * also use this lock to protect buffer object state like placement,
	 * allocation policies or throughout command submission.
	 */
	struct ww_mutex lock;

	/**
	 * @seq:
	 *
	 * Sequence count for managing RCU read-side synchronization, allows
	 * read-only access to @fence_excl and @fence while ensuring we take a
	 * consistent snapshot.
	 */
	seqcount_ww_mutex_t seq;

	/**
	 * @fence_excl:
	 *
	 * The exclusive fence, if there is one currently.
	 *
	 * There are two ways to update this fence:
	 *
	 * - First by calling dma_resv_add_excl_fence(), which replaces all
	 *   fences attached to the reservation object. To guarantee that no
	 *   fences are lost, this new fence must signal only after all previous
	 *   fences, both shared and exclusive, have signalled. In some cases it
	 *   is convenient to achieve that by attaching a struct dma_fence_array
	 *   with all the new and old fences.
	 *
	 * - Alternatively the fence can be set directly, which leaves the
	 *   shared fences unchanged. To guarantee that no fences are lost, this
	 *   new fence must signal only after the previous exclusive fence has
	 *   signalled. Since the shared fences are staying intact, it is not
	 *   necessary to maintain any ordering against those. If semantically
	 *   only a new access is added without actually treating the previous
	 *   one as a dependency the exclusive fences can be strung together
	 *   using struct dma_fence_chain.
	 *
	 * Note that actual semantics of what an exclusive or shared fence mean
	 * is defined by the user, for reservation objects shared across drivers
	 * see &dma_buf.resv.
	 */
	struct dma_fence __rcu *fence_excl;

	/**
	 * @fence:
	 *
	 * List of current shared fences.
	 *
	 * There are no ordering constraints of shared fences against the
	 * exclusive fence slot. If a waiter needs to wait for all access, it
	 * has to wait for both sets of fences to signal.
	 *
	 * A new fence is added by calling dma_resv_add_shared_fence(). Since
	 * this often needs to be done past the point of no return in command
	 * submission it cannot fail, and therefore sufficient slots need to be
	 * reserved by calling dma_resv_reserve_shared().
	 *
	 * Note that actual semantics of what an exclusive or shared fence mean
	 * is defined by the user, for reservation objects shared across drivers
	 * see &dma_buf.resv.
	 */
	struct dma_resv_list __rcu *fence;
};

#define dma_resv_held(obj) lockdep_is_held(&(obj)->lock.base)
#define dma_resv_assert_held(obj) lockdep_assert_held(&(obj)->lock.base)

#ifdef CONFIG_DEBUG_MUTEXES
void dma_resv_reset_shared_max(struct dma_resv *obj);
#else
static inline void dma_resv_reset_shared_max(struct dma_resv *obj) {}
#endif

/**
 * dma_resv_lock - lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Locks the reservation object for exclusive access and modification. Note,
 * that the lock is only against other writers, readers will run concurrently
 * with a writer under RCU. The seqlock is used to notify readers if they
 * overlap with a writer.
 *
 * As the reservation object may be locked by multiple parties in an
 * undefined order, a #ww_acquire_ctx is passed to unwind if a cycle
 * is detected. See ww_mutex_lock() and ww_acquire_init(). A reservation
 * object may be locked by itself by passing NULL as @ctx.
 *
 * When a die situation is indicated by returning -EDEADLK all locks held by
 * @ctx must be unlocked and then dma_resv_lock_slow() called on @obj.
 *
 * Unlocked by calling dma_resv_unlock().
 *
 * See also dma_resv_lock_interruptible() for the interruptible variant.
 */
static inline int dma_resv_lock(struct dma_resv *obj,
				struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock(&obj->lock, ctx);
}

/**
 * dma_resv_lock_interruptible - lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Locks the reservation object interruptible for exclusive access and
 * modification. Note, that the lock is only against other writers, readers
 * will run concurrently with a writer under RCU. The seqlock is used to
 * notify readers if they overlap with a writer.
 *
 * As the reservation object may be locked by multiple parties in an
 * undefined order, a #ww_acquire_ctx is passed to unwind if a cycle
 * is detected. See ww_mutex_lock() and ww_acquire_init(). A reservation
 * object may be locked by itself by passing NULL as @ctx.
 *
 * When a die situation is indicated by returning -EDEADLK all locks held by
 * @ctx must be unlocked and then dma_resv_lock_slow_interruptible() called on
 * @obj.
 *
 * Unlocked by calling dma_resv_unlock().
 */
static inline int dma_resv_lock_interruptible(struct dma_resv *obj,
					      struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock_interruptible(&obj->lock, ctx);
}

/**
 * dma_resv_lock_slow - slowpath lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Acquires the reservation object after a die case. This function
 * will sleep until the lock becomes available. See dma_resv_lock() as
 * well.
 *
 * See also dma_resv_lock_slow_interruptible() for the interruptible variant.
 */
static inline void dma_resv_lock_slow(struct dma_resv *obj,
				      struct ww_acquire_ctx *ctx)
{
	ww_mutex_lock_slow(&obj->lock, ctx);
}

/**
 * dma_resv_lock_slow_interruptible - slowpath lock the reservation
 * object, interruptible
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Acquires the reservation object interruptible after a die case. This function
 * will sleep until the lock becomes available. See
 * dma_resv_lock_interruptible() as well.
 */
static inline int dma_resv_lock_slow_interruptible(struct dma_resv *obj,
						   struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock_slow_interruptible(&obj->lock, ctx);
}

/**
 * dma_resv_trylock - trylock the reservation object
 * @obj: the reservation object
 *
 * Tries to lock the reservation object for exclusive access and modification.
 * Note, that the lock is only against other writers, readers will run
 * concurrently with a writer under RCU. The seqlock is used to notify readers
 * if they overlap with a writer.
 *
 * Also note that since no context is provided, no deadlock protection is
 * possible, which is also not needed for a trylock.
 *
 * Returns true if the lock was acquired, false otherwise.
 */
static inline bool __must_check dma_resv_trylock(struct dma_resv *obj)
{
	return ww_mutex_trylock(&obj->lock);
}

/**
 * dma_resv_is_locked - is the reservation object locked
 * @obj: the reservation object
 *
 * Returns true if the mutex is locked, false if unlocked.
 */
static inline bool dma_resv_is_locked(struct dma_resv *obj)
{
	return ww_mutex_is_locked(&obj->lock);
}

/**
 * dma_resv_locking_ctx - returns the context used to lock the object
 * @obj: the reservation object
 *
 * Returns the context used to lock a reservation object or NULL if no context
 * was used or the object is not locked at all.
 *
 * WARNING: This interface is pretty horrible, but TTM needs it because it
 * doesn't pass the struct ww_acquire_ctx around in some very long callchains.
 * Everyone else just uses it to check whether they're holding a reservation or
 * not.
 */
static inline struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *obj)
{
	return READ_ONCE(obj->lock.ctx);
}

/**
 * dma_resv_unlock - unlock the reservation object
 * @obj: the reservation object
 *
 * Unlocks the reservation object following exclusive access.
 */
static inline void dma_resv_unlock(struct dma_resv *obj)
{
	dma_resv_reset_shared_max(obj);
	ww_mutex_unlock(&obj->lock);
}

/**
 * dma_resv_excl_fence - return the object's exclusive fence
 * @obj: the reservation object
 *
 * Returns the exclusive fence (if any). Caller must either hold the objects
 * through dma_resv_lock() or the RCU read side lock through rcu_read_lock(),
 * or one of the variants of each
 *
 * RETURNS
 * The exclusive fence or NULL
 */
static inline struct dma_fence *
dma_resv_excl_fence(struct dma_resv *obj)
{
	return rcu_dereference_check(obj->fence_excl, dma_resv_held(obj));
}

/**
 * dma_resv_get_excl_unlocked - get the reservation object's
 * exclusive fence, without lock held.
 * @obj: the reservation object
 *
 * If there is an exclusive fence, this atomically increments it's
 * reference count and returns it.
 *
 * RETURNS
 * The exclusive fence or NULL if none
 */
static inline struct dma_fence *
dma_resv_get_excl_unlocked(struct dma_resv *obj)
{
	struct dma_fence *fence;

	if (!rcu_access_pointer(obj->fence_excl))
		return NULL;

	rcu_read_lock();
	fence = dma_fence_get_rcu_safe(&obj->fence_excl);
	rcu_read_unlock();

	return fence;
}

/**
 * dma_resv_shared_list - get the reservation object's shared fence list
 * @obj: the reservation object
 *
 * Returns the shared fence list. Caller must either hold the objects
 * through dma_resv_lock() or the RCU read side lock through rcu_read_lock(),
 * or one of the variants of each
 */
static inline struct dma_resv_list *dma_resv_shared_list(struct dma_resv *obj)
{
	return rcu_dereference_check(obj->fence, dma_resv_held(obj));
}

void dma_resv_init(struct dma_resv *obj);
void dma_resv_fini(struct dma_resv *obj);
int dma_resv_reserve_shared(struct dma_resv *obj, unsigned int num_fences);
void dma_resv_add_shared_fence(struct dma_resv *obj, struct dma_fence *fence);
void dma_resv_add_excl_fence(struct dma_resv *obj, struct dma_fence *fence);
int dma_resv_get_fences(struct dma_resv *obj, struct dma_fence **pfence_excl,
			unsigned *pshared_count, struct dma_fence ***pshared);
int dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src);
long dma_resv_wait_timeout(struct dma_resv *obj, bool wait_all, bool intr,
			   unsigned long timeout);
bool dma_resv_test_signaled(struct dma_resv *obj, bool test_all);

#endif /* _LINUX_RESERVATION_H */
