// SPDX-License-Identifier: GPL-2.0-or-later
/* Volume-level cache cookie handling.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL COOKIE
#include <linux/export.h>
#include <linux/slab.h>
#include "internal.h"

#define fscache_volume_hash_shift 10
static struct hlist_bl_head fscache_volume_hash[1 << fscache_volume_hash_shift];
static atomic_t fscache_volume_debug_id;
static LIST_HEAD(fscache_volumes);

static void fscache_create_volume_work(struct work_struct *work);

struct fscache_volume *fscache_get_volume(struct fscache_volume *volume,
					  enum fscache_volume_trace where)
{
	int ref;

	__refcount_inc(&volume->ref, &ref);
	trace_fscache_volume(volume->debug_id, ref + 1, where);
	return volume;
}

struct fscache_volume *fscache_try_get_volume(struct fscache_volume *volume,
					      enum fscache_volume_trace where)
{
	int ref;

	if (!__refcount_inc_not_zero(&volume->ref, &ref))
		return NULL;

	trace_fscache_volume(volume->debug_id, ref + 1, where);
	return volume;
}
EXPORT_SYMBOL(fscache_try_get_volume);

static void fscache_see_volume(struct fscache_volume *volume,
			       enum fscache_volume_trace where)
{
	int ref = refcount_read(&volume->ref);

	trace_fscache_volume(volume->debug_id, ref, where);
}

/*
 * Pin the cache behind a volume so that we can access it.
 */
static void __fscache_begin_volume_access(struct fscache_volume *volume,
					  struct fscache_cookie *cookie,
					  enum fscache_access_trace why)
{
	int n_accesses;

	n_accesses = atomic_inc_return(&volume->n_accesses);
	smp_mb__after_atomic();
	trace_fscache_access_volume(volume->debug_id, cookie ? cookie->debug_id : 0,
				    refcount_read(&volume->ref),
				    n_accesses, why);
}

/**
 * fscache_begin_volume_access - Pin a cache so a volume can be accessed
 * @volume: The volume cookie
 * @cookie: A datafile cookie for a tracing reference (or NULL)
 * @why: An indication of the circumstances of the access for tracing
 *
 * Attempt to pin the cache to prevent it from going away whilst we're
 * accessing a volume and returns true if successful.  This works as follows:
 *
 *  (1) If the cache tests as not live (state is not FSCACHE_CACHE_IS_ACTIVE),
 *      then we return false to indicate access was not permitted.
 *
 *  (2) If the cache tests as live, then we increment the volume's n_accesses
 *      count and then recheck the cache liveness, ending the access if it
 *      ceased to be live.
 *
 *  (3) When we end the access, we decrement the volume's n_accesses and wake
 *      up the any waiters if it reaches 0.
 *
 *  (4) Whilst the cache is caching, the volume's n_accesses is kept
 *      artificially incremented to prevent wakeups from happening.
 *
 *  (5) When the cache is taken offline, the state is changed to prevent new
 *      accesses, the volume's n_accesses is decremented and we wait for it to
 *      become 0.
 *
 * The datafile @cookie and the @why indicator are merely provided for tracing
 * purposes.
 */
bool fscache_begin_volume_access(struct fscache_volume *volume,
				 struct fscache_cookie *cookie,
				 enum fscache_access_trace why)
{
	if (!fscache_cache_is_live(volume->cache))
		return false;
	__fscache_begin_volume_access(volume, cookie, why);
	if (!fscache_cache_is_live(volume->cache)) {
		fscache_end_volume_access(volume, cookie, fscache_access_unlive);
		return false;
	}
	return true;
}

/**
 * fscache_end_volume_access - Unpin a cache at the end of an access.
 * @volume: The volume cookie
 * @cookie: A datafile cookie for a tracing reference (or NULL)
 * @why: An indication of the circumstances of the access for tracing
 *
 * Unpin a cache volume after we've accessed it.  The datafile @cookie and the
 * @why indicator are merely provided for tracing purposes.
 */
void fscache_end_volume_access(struct fscache_volume *volume,
			       struct fscache_cookie *cookie,
			       enum fscache_access_trace why)
{
	int n_accesses;

	smp_mb__before_atomic();
	n_accesses = atomic_dec_return(&volume->n_accesses);
	trace_fscache_access_volume(volume->debug_id, cookie ? cookie->debug_id : 0,
				    refcount_read(&volume->ref),
				    n_accesses, why);
	if (n_accesses == 0)
		wake_up_var(&volume->n_accesses);
}
EXPORT_SYMBOL(fscache_end_volume_access);

static bool fscache_volume_same(const struct fscache_volume *a,
				const struct fscache_volume *b)
{
	size_t klen;

	if (a->key_hash	!= b->key_hash ||
	    a->cache	!= b->cache ||
	    a->key[0]	!= b->key[0])
		return false;

	klen = round_up(a->key[0] + 1, sizeof(__le32));
	return memcmp(a->key, b->key, klen) == 0;
}

static bool fscache_is_acquire_pending(struct fscache_volume *volume)
{
	return test_bit(FSCACHE_VOLUME_ACQUIRE_PENDING, &volume->flags);
}

static void fscache_wait_on_volume_collision(struct fscache_volume *candidate,
					     unsigned int collidee_debug_id)
{
	wait_on_bit_timeout(&candidate->flags, FSCACHE_VOLUME_ACQUIRE_PENDING,
			    TASK_UNINTERRUPTIBLE, 20 * HZ);
	if (fscache_is_acquire_pending(candidate)) {
		pr_notice("Potential volume collision new=%08x old=%08x",
			  candidate->debug_id, collidee_debug_id);
		fscache_stat(&fscache_n_volumes_collision);
		wait_on_bit(&candidate->flags, FSCACHE_VOLUME_ACQUIRE_PENDING,
			    TASK_UNINTERRUPTIBLE);
	}
}

/*
 * Attempt to insert the new volume into the hash.  If there's a collision, we
 * wait for the old volume to complete if it's being relinquished and an error
 * otherwise.
 */
static bool fscache_hash_volume(struct fscache_volume *candidate)
{
	struct fscache_volume *cursor;
	struct hlist_bl_head *h;
	struct hlist_bl_node *p;
	unsigned int bucket, collidee_debug_id = 0;

	bucket = candidate->key_hash & (ARRAY_SIZE(fscache_volume_hash) - 1);
	h = &fscache_volume_hash[bucket];

	hlist_bl_lock(h);
	hlist_bl_for_each_entry(cursor, p, h, hash_link) {
		if (fscache_volume_same(candidate, cursor)) {
			if (!test_bit(FSCACHE_VOLUME_RELINQUISHED, &cursor->flags))
				goto collision;
			fscache_see_volume(cursor, fscache_volume_get_hash_collision);
			set_bit(FSCACHE_VOLUME_COLLIDED_WITH, &cursor->flags);
			set_bit(FSCACHE_VOLUME_ACQUIRE_PENDING, &candidate->flags);
			collidee_debug_id = cursor->debug_id;
			break;
		}
	}

	hlist_bl_add_head(&candidate->hash_link, h);
	hlist_bl_unlock(h);

	if (fscache_is_acquire_pending(candidate))
		fscache_wait_on_volume_collision(candidate, collidee_debug_id);
	return true;

collision:
	fscache_see_volume(cursor, fscache_volume_collision);
	hlist_bl_unlock(h);
	return false;
}

/*
 * Allocate and initialise a volume representation cookie.
 */
static struct fscache_volume *fscache_alloc_volume(const char *volume_key,
						   const char *cache_name,
						   const void *coherency_data,
						   size_t coherency_len)
{
	struct fscache_volume *volume;
	struct fscache_cache *cache;
	size_t klen, hlen;
	u8 *key;

	klen = strlen(volume_key);
	if (klen > NAME_MAX)
		return NULL;

	if (!coherency_data)
		coherency_len = 0;

	cache = fscache_lookup_cache(cache_name, false);
	if (IS_ERR(cache))
		return NULL;

	volume = kzalloc(struct_size(volume, coherency, coherency_len),
			 GFP_KERNEL);
	if (!volume)
		goto err_cache;

	volume->cache = cache;
	volume->coherency_len = coherency_len;
	if (coherency_data)
		memcpy(volume->coherency, coherency_data, coherency_len);
	INIT_LIST_HEAD(&volume->proc_link);
	INIT_WORK(&volume->work, fscache_create_volume_work);
	refcount_set(&volume->ref, 1);
	spin_lock_init(&volume->lock);

	/* Stick the length on the front of the key and pad it out to make
	 * hashing easier.
	 */
	hlen = round_up(1 + klen + 1, sizeof(__le32));
	key = kzalloc(hlen, GFP_KERNEL);
	if (!key)
		goto err_vol;
	key[0] = klen;
	memcpy(key + 1, volume_key, klen);

	volume->key = key;
	volume->key_hash = fscache_hash(0, key, hlen);

	volume->debug_id = atomic_inc_return(&fscache_volume_debug_id);
	down_write(&fscache_addremove_sem);
	atomic_inc(&cache->n_volumes);
	list_add_tail(&volume->proc_link, &fscache_volumes);
	fscache_see_volume(volume, fscache_volume_new_acquire);
	fscache_stat(&fscache_n_volumes);
	up_write(&fscache_addremove_sem);
	_leave(" = v=%x", volume->debug_id);
	return volume;

err_vol:
	kfree(volume);
err_cache:
	fscache_put_cache(cache, fscache_cache_put_alloc_volume);
	fscache_stat(&fscache_n_volumes_nomem);
	return NULL;
}

/*
 * Create a volume's representation on disk.  Have a volume ref and a cache
 * access we have to release.
 */
static void fscache_create_volume_work(struct work_struct *work)
{
	const struct fscache_cache_ops *ops;
	struct fscache_volume *volume =
		container_of(work, struct fscache_volume, work);

	fscache_see_volume(volume, fscache_volume_see_create_work);

	ops = volume->cache->ops;
	if (ops->acquire_volume)
		ops->acquire_volume(volume);
	fscache_end_cache_access(volume->cache,
				 fscache_access_acquire_volume_end);

	clear_and_wake_up_bit(FSCACHE_VOLUME_CREATING, &volume->flags);
	fscache_put_volume(volume, fscache_volume_put_create_work);
}

/*
 * Dispatch a worker thread to create a volume's representation on disk.
 */
void fscache_create_volume(struct fscache_volume *volume, bool wait)
{
	if (test_and_set_bit(FSCACHE_VOLUME_CREATING, &volume->flags))
		goto maybe_wait;
	if (volume->cache_priv)
		goto no_wait; /* We raced */
	if (!fscache_begin_cache_access(volume->cache,
					fscache_access_acquire_volume))
		goto no_wait;

	fscache_get_volume(volume, fscache_volume_get_create_work);
	if (!schedule_work(&volume->work))
		fscache_put_volume(volume, fscache_volume_put_create_work);

maybe_wait:
	if (wait) {
		fscache_see_volume(volume, fscache_volume_wait_create_work);
		wait_on_bit(&volume->flags, FSCACHE_VOLUME_CREATING,
			    TASK_UNINTERRUPTIBLE);
	}
	return;
no_wait:
	clear_and_wake_up_bit(FSCACHE_VOLUME_CREATING, &volume->flags);
}

/*
 * Acquire a volume representation cookie and link it to a (proposed) cache.
 */
struct fscache_volume *__fscache_acquire_volume(const char *volume_key,
						const char *cache_name,
						const void *coherency_data,
						size_t coherency_len)
{
	struct fscache_volume *volume;

	volume = fscache_alloc_volume(volume_key, cache_name,
				      coherency_data, coherency_len);
	if (!volume)
		return ERR_PTR(-ENOMEM);

	if (!fscache_hash_volume(volume)) {
		fscache_put_volume(volume, fscache_volume_put_hash_collision);
		return ERR_PTR(-EBUSY);
	}

	fscache_create_volume(volume, false);
	return volume;
}
EXPORT_SYMBOL(__fscache_acquire_volume);

static void fscache_wake_pending_volume(struct fscache_volume *volume,
					struct hlist_bl_head *h)
{
	struct fscache_volume *cursor;
	struct hlist_bl_node *p;

	hlist_bl_for_each_entry(cursor, p, h, hash_link) {
		if (fscache_volume_same(cursor, volume)) {
			fscache_see_volume(cursor, fscache_volume_see_hash_wake);
			clear_and_wake_up_bit(FSCACHE_VOLUME_ACQUIRE_PENDING,
					      &cursor->flags);
			return;
		}
	}
}

/*
 * Remove a volume cookie from the hash table.
 */
static void fscache_unhash_volume(struct fscache_volume *volume)
{
	struct hlist_bl_head *h;
	unsigned int bucket;

	bucket = volume->key_hash & (ARRAY_SIZE(fscache_volume_hash) - 1);
	h = &fscache_volume_hash[bucket];

	hlist_bl_lock(h);
	hlist_bl_del(&volume->hash_link);
	if (test_bit(FSCACHE_VOLUME_COLLIDED_WITH, &volume->flags))
		fscache_wake_pending_volume(volume, h);
	hlist_bl_unlock(h);
}

/*
 * Drop a cache's volume attachments.
 */
static void fscache_free_volume(struct fscache_volume *volume)
{
	struct fscache_cache *cache = volume->cache;

	if (volume->cache_priv) {
		__fscache_begin_volume_access(volume, NULL,
					      fscache_access_relinquish_volume);
		if (volume->cache_priv)
			cache->ops->free_volume(volume);
		fscache_end_volume_access(volume, NULL,
					  fscache_access_relinquish_volume_end);
	}

	down_write(&fscache_addremove_sem);
	list_del_init(&volume->proc_link);
	atomic_dec(&volume->cache->n_volumes);
	up_write(&fscache_addremove_sem);

	if (!hlist_bl_unhashed(&volume->hash_link))
		fscache_unhash_volume(volume);

	trace_fscache_volume(volume->debug_id, 0, fscache_volume_free);
	kfree(volume->key);
	kfree(volume);
	fscache_stat_d(&fscache_n_volumes);
	fscache_put_cache(cache, fscache_cache_put_volume);
}

/*
 * Drop a reference to a volume cookie.
 */
void fscache_put_volume(struct fscache_volume *volume,
			enum fscache_volume_trace where)
{
	if (volume) {
		unsigned int debug_id = volume->debug_id;
		bool zero;
		int ref;

		zero = __refcount_dec_and_test(&volume->ref, &ref);
		trace_fscache_volume(debug_id, ref - 1, where);
		if (zero)
			fscache_free_volume(volume);
	}
}
EXPORT_SYMBOL(fscache_put_volume);

/*
 * Relinquish a volume representation cookie.
 */
void __fscache_relinquish_volume(struct fscache_volume *volume,
				 const void *coherency_data,
				 bool invalidate)
{
	if (WARN_ON(test_and_set_bit(FSCACHE_VOLUME_RELINQUISHED, &volume->flags)))
		return;

	if (invalidate) {
		set_bit(FSCACHE_VOLUME_INVALIDATE, &volume->flags);
	} else if (coherency_data) {
		memcpy(volume->coherency, coherency_data, volume->coherency_len);
	}

	fscache_put_volume(volume, fscache_volume_put_relinquish);
}
EXPORT_SYMBOL(__fscache_relinquish_volume);

/**
 * fscache_withdraw_volume - Withdraw a volume from being cached
 * @volume: Volume cookie
 *
 * Withdraw a cache volume from service, waiting for all accesses to complete
 * before returning.
 */
void fscache_withdraw_volume(struct fscache_volume *volume)
{
	int n_accesses;

	_debug("withdraw V=%x", volume->debug_id);

	/* Allow wakeups on dec-to-0 */
	n_accesses = atomic_dec_return(&volume->n_accesses);
	trace_fscache_access_volume(volume->debug_id, 0,
				    refcount_read(&volume->ref),
				    n_accesses, fscache_access_cache_unpin);

	wait_var_event(&volume->n_accesses,
		       atomic_read(&volume->n_accesses) == 0);
}
EXPORT_SYMBOL(fscache_withdraw_volume);

#ifdef CONFIG_PROC_FS
/*
 * Generate a list of volumes in /proc/fs/fscache/volumes
 */
static int fscache_volumes_seq_show(struct seq_file *m, void *v)
{
	struct fscache_volume *volume;

	if (v == &fscache_volumes) {
		seq_puts(m,
			 "VOLUME   REF   nCOOK ACC FL CACHE           KEY\n"
			 "======== ===== ===== === == =============== ================\n");
		return 0;
	}

	volume = list_entry(v, struct fscache_volume, proc_link);
	seq_printf(m,
		   "%08x %5d %5d %3d %02lx %-15.15s %s\n",
		   volume->debug_id,
		   refcount_read(&volume->ref),
		   atomic_read(&volume->n_cookies),
		   atomic_read(&volume->n_accesses),
		   volume->flags,
		   volume->cache->name ?: "-",
		   volume->key + 1);
	return 0;
}

static void *fscache_volumes_seq_start(struct seq_file *m, loff_t *_pos)
	__acquires(&fscache_addremove_sem)
{
	down_read(&fscache_addremove_sem);
	return seq_list_start_head(&fscache_volumes, *_pos);
}

static void *fscache_volumes_seq_next(struct seq_file *m, void *v, loff_t *_pos)
{
	return seq_list_next(v, &fscache_volumes, _pos);
}

static void fscache_volumes_seq_stop(struct seq_file *m, void *v)
	__releases(&fscache_addremove_sem)
{
	up_read(&fscache_addremove_sem);
}

const struct seq_operations fscache_volumes_seq_ops = {
	.start  = fscache_volumes_seq_start,
	.next   = fscache_volumes_seq_next,
	.stop   = fscache_volumes_seq_stop,
	.show   = fscache_volumes_seq_show,
};
#endif /* CONFIG_PROC_FS */
