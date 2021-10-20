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
	unsigned int debug_id = cache->debug_id;
	bool zero;
	int ref;

	if (IS_ERR_OR_NULL(cache))
		return;

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

	cache->cache_priv = NULL;
	smp_store_release(&cache->state, FSCACHE_CACHE_IS_NOT_PRESENT);
	fscache_put_cache(cache, where);
}
EXPORT_SYMBOL(fscache_relinquish_cache);

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
