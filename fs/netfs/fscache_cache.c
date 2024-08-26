// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache cache handling
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/export.h>
#include <linux/slab.h>
#include "internal.h"

static LIST_HEAD(fscache_caches);
DECLARE_RWSEM(fscache_addremove_sem);
EXPORT_SYMBOL(fscache_addremove_sem);
DECLARE_WAIT_QUEUE_HEAD(fscache_clearance_waiters);
EXPORT_SYMBOL(fscache_clearance_waiters);

static atomic_t fscache_cache_debug_id;

/*
 * Allocate a cache cookie.
 */
static struct fscache_cache *fscache_alloc_cache(const char *name)
{
	struct fscache_cache *cache;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (cache) {
		if (name) {
			cache->name = kstrdup(name, GFP_KERNEL);
			if (!cache->name) {
				kfree(cache);
				return NULL;
			}
		}
		refcount_set(&cache->ref, 1);
		INIT_LIST_HEAD(&cache->cache_link);
		cache->debug_id = atomic_inc_return(&fscache_cache_debug_id);
	}
	return cache;
}

static bool fscache_get_cache_maybe(struct fscache_cache *cache,
				    enum fscache_cache_trace where)
{
	bool success;
	int ref;

	success = __refcount_inc_not_zero(&cache->ref, &ref);
	if (success)
		trace_fscache_cache(cache->debug_id, ref + 1, where);
	return success;
}

/*
 * Look up a cache cookie.
 */
struct fscache_cache *fscache_lookup_cache(const char *name, bool is_cache)
{
	struct fscache_cache *candidate, *cache, *unnamed = NULL;

	/* firstly check for the existence of the cache under read lock */
	down_read(&fscache_addremove_sem);

	list_for_each_entry(cache, &fscache_caches, cache_link) {
		if (cache->name && name && strcmp(cache->name, name) == 0 &&
		    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
			goto got_cache_r;
		if (!cache->name && !name &&
		    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
			goto got_cache_r;
	}

	if (!name) {
		list_for_each_entry(cache, &fscache_caches, cache_link) {
			if (cache->name &&
			    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
				goto got_cache_r;
		}
	}

	up_read(&fscache_addremove_sem);

	/* the cache does not exist - create a candidate */
	candidate = fscache_alloc_cache(name);
	if (!candidate)
		return ERR_PTR(-ENOMEM);

	/* write lock, search again and add if still not present */
	down_write(&fscache_addremove_sem);

	list_for_each_entry(cache, &fscache_caches, cache_link) {
		if (cache->name && name && strcmp(cache->name, name) == 0 &&
		    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
			goto got_cache_w;
		if (!cache->name) {
			unnamed = cache;
			if (!name &&
			    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
				goto got_cache_w;
		}
	}

	if (unnamed && is_cache &&
	    fscache_get_cache_maybe(unnamed, fscache_cache_get_acquire))
		goto use_unnamed_cache;

	if (!name) {
		list_for_each_entry(cache, &fscache_caches, cache_link) {
			if (cache->name &&
			    fscache_get_cache_maybe(cache, fscache_cache_get_acquire))
				goto got_cache_w;
		}
	}

	list_add_tail(&candidate->cache_link, &fscache_caches);
	trace_fscache_cache(candidate->debug_id,
			    refcount_read(&candidate->ref),
			    fscache_cache_new_acquire);
	up_write(&fscache_addremove_sem);
	return candidate;

got_cache_r:
	up_read(&fscache_addremove_sem);
	return cache;
use_unnamed_cache:
	cache = unnamed;
	cache->name = candidate->name;
	candidate->name = NULL;
got_cache_w:
	up_write(&fscache_addremove_sem);
	kfree(candidate->name);
	kfree(candidate);
	return cache;
}

/**
 * fscache_acquire_cache - Acquire a cache-level cookie.
 * @name: The name of the cache.
 *
 * Get a cookie to represent an actual cache.  If a name is given and there is
 * a nameless cache record available, this will acquire that and set its name,
 * directing all the volumes using it to this cache.
 *
 * The cache will be switched over to the preparing state if not currently in
 * use, otherwise -EBUSY will be returned.
 */
struct fscache_cache *fscache_acquire_cache(const char *name)
{
	struct fscache_cache *cache;

	ASSERT(name);
	cache = fscache_lookup_cache(name, true);
	if (IS_ERR(cache))
		return cache;

	if (!fscache_set_cache_state_maybe(cache,
					   FSCACHE_CACHE_IS_NOT_PRESENT,
					   FSCACHE_CACHE_IS_PREPARING)) {
		pr_warn("Cache tag %s in use\n", name);
		fscache_put_cache(cache, fscache_cache_put_cache);
		return ERR_PTR(-EBUSY);
	}

	return cache;
}
EXPORT_SYMBOL(fscache_acquire_cache);

/**
 * fscache_put_cache - Release a cache-level cookie.
 * @cache: The cache cookie to be released
 * @where: An indication of where the release happened
 *
 * Release the caller's reference on a cache-level cookie.  The @where
 * indication should give information about the circumstances in which the call
 * occurs and will be logged through a tracepoint.
 */
void fscache_put_cache(struct fscache_cache *cache,
		       enum fscache_cache_trace where)
{
	unsigned int debug_id;
	bool zero;
	int ref;

	if (IS_ERR_OR_NULL(cache))
		return;

	debug_id = cache->debug_id;
	zero = __refcount_dec_and_test(&cache->ref, &ref);
	trace_fscache_cache(debug_id, ref - 1, where);

	if (zero) {
		down_write(&fscache_addremove_sem);
		list_del_init(&cache->cache_link);
		up_write(&fscache_addremove_sem);
		kfree(cache->name);
		kfree(cache);
	}
}

/**
 * fscache_relinquish_cache - Reset cache state and release cookie
 * @cache: The cache cookie to be released
 *
 * Reset the state of a cache and release the caller's reference on a cache
 * cookie.
 */
void fscache_relinquish_cache(struct fscache_cache *cache)
{
	enum fscache_cache_trace where =
		(cache->state == FSCACHE_CACHE_IS_PREPARING) ?
		fscache_cache_put_prep_failed :
		fscache_cache_put_relinquish;

	cache->ops = NULL;
	cache->cache_priv = NULL;
	fscache_set_cache_state(cache, FSCACHE_CACHE_IS_NOT_PRESENT);
	fscache_put_cache(cache, where);
}
EXPORT_SYMBOL(fscache_relinquish_cache);

/**
 * fscache_add_cache - Declare a cache as being open for business
 * @cache: The cache-level cookie representing the cache
 * @ops: Table of cache operations to use
 * @cache_priv: Private data for the cache record
 *
 * Add a cache to the system, making it available for netfs's to use.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
int fscache_add_cache(struct fscache_cache *cache,
		      const struct fscache_cache_ops *ops,
		      void *cache_priv)
{
	int n_accesses;

	kenter("{%s,%s}", ops->name, cache->name);

	BUG_ON(fscache_cache_state(cache) != FSCACHE_CACHE_IS_PREPARING);

	/* Get a ref on the cache cookie and keep its n_accesses counter raised
	 * by 1 to prevent wakeups from transitioning it to 0 until we're
	 * withdrawing caching services from it.
	 */
	n_accesses = atomic_inc_return(&cache->n_accesses);
	trace_fscache_access_cache(cache->debug_id, refcount_read(&cache->ref),
				   n_accesses, fscache_access_cache_pin);

	down_write(&fscache_addremove_sem);

	cache->ops = ops;
	cache->cache_priv = cache_priv;
	fscache_set_cache_state(cache, FSCACHE_CACHE_IS_ACTIVE);

	up_write(&fscache_addremove_sem);
	pr_notice("Cache \"%s\" added (type %s)\n", cache->name, ops->name);
	kleave(" = 0 [%s]", cache->name);
	return 0;
}
EXPORT_SYMBOL(fscache_add_cache);

/**
 * fscache_begin_cache_access - Pin a cache so it can be accessed
 * @cache: The cache-level cookie
 * @why: An indication of the circumstances of the access for tracing
 *
 * Attempt to pin the cache to prevent it from going away whilst we're
 * accessing it and returns true if successful.  This works as follows:
 *
 *  (1) If the cache tests as not live (state is not FSCACHE_CACHE_IS_ACTIVE),
 *      then we return false to indicate access was not permitted.
 *
 *  (2) If the cache tests as live, then we increment the n_accesses count and
 *      then recheck the liveness, ending the access if it ceased to be live.
 *
 *  (3) When we end the access, we decrement n_accesses and wake up the any
 *      waiters if it reaches 0.
 *
 *  (4) Whilst the cache is caching, n_accesses is kept artificially
 *      incremented to prevent wakeups from happening.
 *
 *  (5) When the cache is taken offline, the state is changed to prevent new
 *      accesses, n_accesses is decremented and we wait for n_accesses to
 *      become 0.
 */
bool fscache_begin_cache_access(struct fscache_cache *cache, enum fscache_access_trace why)
{
	int n_accesses;

	if (!fscache_cache_is_live(cache))
		return false;

	n_accesses = atomic_inc_return(&cache->n_accesses);
	smp_mb__after_atomic(); /* Reread live flag after n_accesses */
	trace_fscache_access_cache(cache->debug_id, refcount_read(&cache->ref),
				   n_accesses, why);
	if (!fscache_cache_is_live(cache)) {
		fscache_end_cache_access(cache, fscache_access_unlive);
		return false;
	}
	return true;
}

/**
 * fscache_end_cache_access - Unpin a cache at the end of an access.
 * @cache: The cache-level cookie
 * @why: An indication of the circumstances of the access for tracing
 *
 * Unpin a cache after we've accessed it.  The @why indicator is merely
 * provided for tracing purposes.
 */
void fscache_end_cache_access(struct fscache_cache *cache, enum fscache_access_trace why)
{
	int n_accesses;

	smp_mb__before_atomic();
	n_accesses = atomic_dec_return(&cache->n_accesses);
	trace_fscache_access_cache(cache->debug_id, refcount_read(&cache->ref),
				   n_accesses, why);
	if (n_accesses == 0)
		wake_up_var(&cache->n_accesses);
}

/**
 * fscache_io_error - Note a cache I/O error
 * @cache: The record describing the cache
 *
 * Note that an I/O error occurred in a cache and that it should no longer be
 * used for anything.  This also reports the error into the kernel log.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
void fscache_io_error(struct fscache_cache *cache)
{
	if (fscache_set_cache_state_maybe(cache,
					  FSCACHE_CACHE_IS_ACTIVE,
					  FSCACHE_CACHE_GOT_IOERROR))
		pr_err("Cache '%s' stopped due to I/O error\n",
		       cache->name);
}
EXPORT_SYMBOL(fscache_io_error);

/**
 * fscache_withdraw_cache - Withdraw a cache from the active service
 * @cache: The cache cookie
 *
 * Begin the process of withdrawing a cache from service.  This stops new
 * cache-level and volume-level accesses from taking place and waits for
 * currently ongoing cache-level accesses to end.
 */
void fscache_withdraw_cache(struct fscache_cache *cache)
{
	int n_accesses;

	pr_notice("Withdrawing cache \"%s\" (%u objs)\n",
		  cache->name, atomic_read(&cache->object_count));

	fscache_set_cache_state(cache, FSCACHE_CACHE_IS_WITHDRAWN);

	/* Allow wakeups on dec-to-0 */
	n_accesses = atomic_dec_return(&cache->n_accesses);
	trace_fscache_access_cache(cache->debug_id, refcount_read(&cache->ref),
				   n_accesses, fscache_access_cache_unpin);

	wait_var_event(&cache->n_accesses,
		       atomic_read(&cache->n_accesses) == 0);
}
EXPORT_SYMBOL(fscache_withdraw_cache);

#ifdef CONFIG_PROC_FS
static const char fscache_cache_states[NR__FSCACHE_CACHE_STATE] = "-PAEW";

/*
 * Generate a list of caches in /proc/fs/fscache/caches
 */
static int fscache_caches_seq_show(struct seq_file *m, void *v)
{
	struct fscache_cache *cache;

	if (v == &fscache_caches) {
		seq_puts(m,
			 "CACHE    REF   VOLS  OBJS  ACCES S NAME\n"
			 "======== ===== ===== ===== ===== = ===============\n"
			 );
		return 0;
	}

	cache = list_entry(v, struct fscache_cache, cache_link);
	seq_printf(m,
		   "%08x %5d %5d %5d %5d %c %s\n",
		   cache->debug_id,
		   refcount_read(&cache->ref),
		   atomic_read(&cache->n_volumes),
		   atomic_read(&cache->object_count),
		   atomic_read(&cache->n_accesses),
		   fscache_cache_states[cache->state],
		   cache->name ?: "-");
	return 0;
}

static void *fscache_caches_seq_start(struct seq_file *m, loff_t *_pos)
	__acquires(fscache_addremove_sem)
{
	down_read(&fscache_addremove_sem);
	return seq_list_start_head(&fscache_caches, *_pos);
}

static void *fscache_caches_seq_next(struct seq_file *m, void *v, loff_t *_pos)
{
	return seq_list_next(v, &fscache_caches, _pos);
}

static void fscache_caches_seq_stop(struct seq_file *m, void *v)
	__releases(fscache_addremove_sem)
{
	up_read(&fscache_addremove_sem);
}

const struct seq_operations fscache_caches_seq_ops = {
	.start  = fscache_caches_seq_start,
	.next   = fscache_caches_seq_next,
	.stop   = fscache_caches_seq_stop,
	.show   = fscache_caches_seq_show,
};
#endif /* CONFIG_PROC_FS */
