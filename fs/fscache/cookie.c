// SPDX-License-Identifier: GPL-2.0-or-later
/* netfs cookie management
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See Documentation/filesystems/caching/netfs-api.rst for more information on
 * the netfs API.
 */

#define FSCACHE_DEBUG_LEVEL COOKIE
#include <linux/module.h>
#include <linux/slab.h>
#include "internal.h"

struct kmem_cache *fscache_cookie_jar;

static void fscache_drop_cookie(struct fscache_cookie *cookie);

#define fscache_cookie_hash_shift 15
static struct hlist_bl_head fscache_cookie_hash[1 << fscache_cookie_hash_shift];
static LIST_HEAD(fscache_cookies);
static DEFINE_RWLOCK(fscache_cookies_lock);
static const char fscache_cookie_states[FSCACHE_COOKIE_STATE__NR] = "-LCAFWRD";

void fscache_print_cookie(struct fscache_cookie *cookie, char prefix)
{
	const u8 *k;

	pr_err("%c-cookie c=%08x [fl=%lx na=%u nA=%u s=%c]\n",
	       prefix,
	       cookie->debug_id,
	       cookie->flags,
	       atomic_read(&cookie->n_active),
	       atomic_read(&cookie->n_accesses),
	       fscache_cookie_states[cookie->state]);
	pr_err("%c-cookie V=%08x [%s]\n",
	       prefix,
	       cookie->volume->debug_id,
	       cookie->volume->key);

	k = (cookie->key_len <= sizeof(cookie->inline_key)) ?
		cookie->inline_key : cookie->key;
	pr_err("%c-key=[%u] '%*phN'\n", prefix, cookie->key_len, cookie->key_len, k);
}

static void fscache_free_cookie(struct fscache_cookie *cookie)
{
	if (WARN_ON_ONCE(test_bit(FSCACHE_COOKIE_IS_HASHED, &cookie->flags))) {
		fscache_print_cookie(cookie, 'F');
		return;
	}

	write_lock(&fscache_cookies_lock);
	list_del(&cookie->proc_link);
	write_unlock(&fscache_cookies_lock);
	if (cookie->aux_len > sizeof(cookie->inline_aux))
		kfree(cookie->aux);
	if (cookie->key_len > sizeof(cookie->inline_key))
		kfree(cookie->key);
	fscache_stat_d(&fscache_n_cookies);
	kmem_cache_free(fscache_cookie_jar, cookie);
}

/*
 * Initialise the access gate on a cookie by setting a flag to prevent the
 * state machine from being queued when the access counter transitions to 0.
 * We're only interested in this when we withdraw caching services from the
 * cookie.
 */
static void fscache_init_access_gate(struct fscache_cookie *cookie)
{
	int n_accesses;

	n_accesses = atomic_read(&cookie->n_accesses);
	trace_fscache_access(cookie->debug_id, refcount_read(&cookie->ref),
			     n_accesses, fscache_access_cache_pin);
	set_bit(FSCACHE_COOKIE_NO_ACCESS_WAKE, &cookie->flags);
}

/**
 * fscache_end_cookie_access - Unpin a cache at the end of an access.
 * @cookie: A data file cookie
 * @why: An indication of the circumstances of the access for tracing
 *
 * Unpin a cache cookie after we've accessed it and bring a deferred
 * relinquishment or withdrawal state into effect.
 *
 * The @why indicator is provided for tracing purposes.
 */
void fscache_end_cookie_access(struct fscache_cookie *cookie,
			       enum fscache_access_trace why)
{
	int n_accesses;

	smp_mb__before_atomic();
	n_accesses = atomic_dec_return(&cookie->n_accesses);
	trace_fscache_access(cookie->debug_id, refcount_read(&cookie->ref),
			     n_accesses, why);
	if (n_accesses == 0 &&
	    !test_bit(FSCACHE_COOKIE_NO_ACCESS_WAKE, &cookie->flags)) {
		// PLACEHOLDER: Need to poke the state machine
	}
}
EXPORT_SYMBOL(fscache_end_cookie_access);

/*
 * Pin the cache behind a cookie so that we can access it.
 */
static void __fscache_begin_cookie_access(struct fscache_cookie *cookie,
					  enum fscache_access_trace why)
{
	int n_accesses;

	n_accesses = atomic_inc_return(&cookie->n_accesses);
	smp_mb__after_atomic(); /* (Future) read state after is-caching.
				 * Reread n_accesses after is-caching
				 */
	trace_fscache_access(cookie->debug_id, refcount_read(&cookie->ref),
			     n_accesses, why);
}

/**
 * fscache_begin_cookie_access - Pin a cache so data can be accessed
 * @cookie: A data file cookie
 * @why: An indication of the circumstances of the access for tracing
 *
 * Attempt to pin the cache to prevent it from going away whilst we're
 * accessing data and returns true if successful.  This works as follows:
 *
 *  (1) If the cookie is not being cached (ie. FSCACHE_COOKIE_IS_CACHING is not
 *      set), we return false to indicate access was not permitted.
 *
 *  (2) If the cookie is being cached, we increment its n_accesses count and
 *      then recheck the IS_CACHING flag, ending the access if it got cleared.
 *
 *  (3) When we end the access, we decrement the cookie's n_accesses and wake
 *      up the any waiters if it reaches 0.
 *
 *  (4) Whilst the cookie is actively being cached, its n_accesses is kept
 *      artificially incremented to prevent wakeups from happening.
 *
 *  (5) When the cache is taken offline or if the cookie is culled, the flag is
 *      cleared to prevent new accesses, the cookie's n_accesses is decremented
 *      and we wait for it to become 0.
 *
 * The @why indicator are merely provided for tracing purposes.
 */
bool fscache_begin_cookie_access(struct fscache_cookie *cookie,
				 enum fscache_access_trace why)
{
	if (!test_bit(FSCACHE_COOKIE_IS_CACHING, &cookie->flags))
		return false;
	__fscache_begin_cookie_access(cookie, why);
	if (!test_bit(FSCACHE_COOKIE_IS_CACHING, &cookie->flags) ||
	    !fscache_cache_is_live(cookie->volume->cache)) {
		fscache_end_cookie_access(cookie, fscache_access_unlive);
		return false;
	}
	return true;
}

static inline void wake_up_cookie_state(struct fscache_cookie *cookie)
{
	/* Use a barrier to ensure that waiters see the state variable
	 * change, as spin_unlock doesn't guarantee a barrier.
	 *
	 * See comments over wake_up_bit() and waitqueue_active().
	 */
	smp_mb();
	wake_up_var(&cookie->state);
}

static void __fscache_set_cookie_state(struct fscache_cookie *cookie,
				       enum fscache_cookie_state state)
{
	cookie->state = state;
}

/*
 * Change the state a cookie is at and wake up anyone waiting for that - but
 * only if the cookie isn't already marked as being in a cleanup state.
 */
void fscache_set_cookie_state(struct fscache_cookie *cookie,
			      enum fscache_cookie_state state)
{
	bool changed = false;

	spin_lock(&cookie->lock);
	switch (cookie->state) {
	case FSCACHE_COOKIE_STATE_RELINQUISHING:
		break;
	default:
		__fscache_set_cookie_state(cookie, state);
		changed = true;
		break;
	}
	spin_unlock(&cookie->lock);
	if (changed)
		wake_up_cookie_state(cookie);
}
EXPORT_SYMBOL(fscache_set_cookie_state);

/*
 * Set the index key in a cookie.  The cookie struct has space for a 16-byte
 * key plus length and hash, but if that's not big enough, it's instead a
 * pointer to a buffer containing 3 bytes of hash, 1 byte of length and then
 * the key data.
 */
static int fscache_set_key(struct fscache_cookie *cookie,
			   const void *index_key, size_t index_key_len)
{
	void *buf;
	size_t buf_size;

	buf_size = round_up(index_key_len, sizeof(__le32));

	if (index_key_len > sizeof(cookie->inline_key)) {
		buf = kzalloc(buf_size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		cookie->key = buf;
	} else {
		buf = cookie->inline_key;
	}

	memcpy(buf, index_key, index_key_len);
	cookie->key_hash = fscache_hash(cookie->volume->key_hash,
					buf, buf_size);
	return 0;
}

static bool fscache_cookie_same(const struct fscache_cookie *a,
				const struct fscache_cookie *b)
{
	const void *ka, *kb;

	if (a->key_hash	!= b->key_hash ||
	    a->volume	!= b->volume ||
	    a->key_len	!= b->key_len)
		return false;

	if (a->key_len <= sizeof(a->inline_key)) {
		ka = &a->inline_key;
		kb = &b->inline_key;
	} else {
		ka = a->key;
		kb = b->key;
	}
	return memcmp(ka, kb, a->key_len) == 0;
}

static atomic_t fscache_cookie_debug_id = ATOMIC_INIT(1);

/*
 * Allocate a cookie.
 */
static struct fscache_cookie *fscache_alloc_cookie(
	struct fscache_volume *volume,
	u8 advice,
	const void *index_key, size_t index_key_len,
	const void *aux_data, size_t aux_data_len,
	loff_t object_size)
{
	struct fscache_cookie *cookie;

	/* allocate and initialise a cookie */
	cookie = kmem_cache_zalloc(fscache_cookie_jar, GFP_KERNEL);
	if (!cookie)
		return NULL;
	fscache_stat(&fscache_n_cookies);

	cookie->volume		= volume;
	cookie->advice		= advice;
	cookie->key_len		= index_key_len;
	cookie->aux_len		= aux_data_len;
	cookie->object_size	= object_size;
	if (object_size == 0)
		__set_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags);

	if (fscache_set_key(cookie, index_key, index_key_len) < 0)
		goto nomem;

	if (cookie->aux_len <= sizeof(cookie->inline_aux)) {
		memcpy(cookie->inline_aux, aux_data, cookie->aux_len);
	} else {
		cookie->aux = kmemdup(aux_data, cookie->aux_len, GFP_KERNEL);
		if (!cookie->aux)
			goto nomem;
	}

	refcount_set(&cookie->ref, 1);
	cookie->debug_id = atomic_inc_return(&fscache_cookie_debug_id);
	cookie->state = FSCACHE_COOKIE_STATE_QUIESCENT;
	spin_lock_init(&cookie->lock);
	INIT_LIST_HEAD(&cookie->commit_link);
	INIT_WORK(&cookie->work, NULL /* PLACEHOLDER */);

	write_lock(&fscache_cookies_lock);
	list_add_tail(&cookie->proc_link, &fscache_cookies);
	write_unlock(&fscache_cookies_lock);
	fscache_see_cookie(cookie, fscache_cookie_new_acquire);
	return cookie;

nomem:
	fscache_free_cookie(cookie);
	return NULL;
}

static void fscache_wait_on_collision(struct fscache_cookie *candidate,
				      struct fscache_cookie *wait_for)
{
	enum fscache_cookie_state *statep = &wait_for->state;

	wait_var_event_timeout(statep, READ_ONCE(*statep) == FSCACHE_COOKIE_STATE_DROPPED,
			       20 * HZ);
	if (READ_ONCE(*statep) != FSCACHE_COOKIE_STATE_DROPPED) {
		pr_notice("Potential collision c=%08x old: c=%08x",
			  candidate->debug_id, wait_for->debug_id);
		wait_var_event(statep, READ_ONCE(*statep) == FSCACHE_COOKIE_STATE_DROPPED);
	}
}

/*
 * Attempt to insert the new cookie into the hash.  If there's a collision, we
 * wait for the old cookie to complete if it's being relinquished and an error
 * otherwise.
 */
static bool fscache_hash_cookie(struct fscache_cookie *candidate)
{
	struct fscache_cookie *cursor, *wait_for = NULL;
	struct hlist_bl_head *h;
	struct hlist_bl_node *p;
	unsigned int bucket;

	bucket = candidate->key_hash & (ARRAY_SIZE(fscache_cookie_hash) - 1);
	h = &fscache_cookie_hash[bucket];

	hlist_bl_lock(h);
	hlist_bl_for_each_entry(cursor, p, h, hash_link) {
		if (fscache_cookie_same(candidate, cursor)) {
			if (!test_bit(FSCACHE_COOKIE_RELINQUISHED, &cursor->flags))
				goto collision;
			wait_for = fscache_get_cookie(cursor,
						      fscache_cookie_get_hash_collision);
			break;
		}
	}

	fscache_get_volume(candidate->volume, fscache_volume_get_cookie);
	atomic_inc(&candidate->volume->n_cookies);
	hlist_bl_add_head(&candidate->hash_link, h);
	set_bit(FSCACHE_COOKIE_IS_HASHED, &candidate->flags);
	hlist_bl_unlock(h);

	if (wait_for) {
		fscache_wait_on_collision(candidate, wait_for);
		fscache_put_cookie(wait_for, fscache_cookie_put_hash_collision);
	}
	return true;

collision:
	trace_fscache_cookie(cursor->debug_id, refcount_read(&cursor->ref),
			     fscache_cookie_collision);
	pr_err("Duplicate cookie detected\n");
	fscache_print_cookie(cursor, 'O');
	fscache_print_cookie(candidate, 'N');
	hlist_bl_unlock(h);
	return false;
}

/*
 * Request a cookie to represent a data storage object within a volume.
 *
 * We never let on to the netfs about errors.  We may set a negative cookie
 * pointer, but that's okay
 */
struct fscache_cookie *__fscache_acquire_cookie(
	struct fscache_volume *volume,
	u8 advice,
	const void *index_key, size_t index_key_len,
	const void *aux_data, size_t aux_data_len,
	loff_t object_size)
{
	struct fscache_cookie *cookie;

	_enter("V=%x", volume->debug_id);

	if (!index_key || !index_key_len || index_key_len > 255 || aux_data_len > 255)
		return NULL;
	if (!aux_data || !aux_data_len) {
		aux_data = NULL;
		aux_data_len = 0;
	}

	fscache_stat(&fscache_n_acquires);

	cookie = fscache_alloc_cookie(volume, advice,
				      index_key, index_key_len,
				      aux_data, aux_data_len,
				      object_size);
	if (!cookie) {
		fscache_stat(&fscache_n_acquires_oom);
		return NULL;
	}

	if (!fscache_hash_cookie(cookie)) {
		fscache_see_cookie(cookie, fscache_cookie_discard);
		fscache_free_cookie(cookie);
		return NULL;
	}

	trace_fscache_acquire(cookie);
	fscache_stat(&fscache_n_acquires_ok);
	_leave(" = c=%08x", cookie->debug_id);
	return cookie;
}
EXPORT_SYMBOL(__fscache_acquire_cookie);

/*
 * Remove a cookie from the hash table.
 */
static void fscache_unhash_cookie(struct fscache_cookie *cookie)
{
	struct hlist_bl_head *h;
	unsigned int bucket;

	bucket = cookie->key_hash & (ARRAY_SIZE(fscache_cookie_hash) - 1);
	h = &fscache_cookie_hash[bucket];

	hlist_bl_lock(h);
	hlist_bl_del(&cookie->hash_link);
	clear_bit(FSCACHE_COOKIE_IS_HASHED, &cookie->flags);
	hlist_bl_unlock(h);
}

/*
 * Finalise a cookie after all its resources have been disposed of.
 */
static void fscache_drop_cookie(struct fscache_cookie *cookie)
{
	spin_lock(&cookie->lock);
	__fscache_set_cookie_state(cookie, FSCACHE_COOKIE_STATE_DROPPED);
	spin_unlock(&cookie->lock);
	wake_up_cookie_state(cookie);

	fscache_unhash_cookie(cookie);
	fscache_stat(&fscache_n_relinquishes_dropped);
}

/*
 * Allow the netfs to release a cookie back to the cache.
 * - the object will be marked as recyclable on disk if retire is true
 */
void __fscache_relinquish_cookie(struct fscache_cookie *cookie, bool retire)
{
	fscache_stat(&fscache_n_relinquishes);
	if (retire)
		fscache_stat(&fscache_n_relinquishes_retire);

	_enter("c=%08x{%d},%d",
	       cookie->debug_id, atomic_read(&cookie->n_active), retire);

	if (WARN(test_and_set_bit(FSCACHE_COOKIE_RELINQUISHED, &cookie->flags),
		 "Cookie c=%x already relinquished\n", cookie->debug_id))
		return;

	if (retire)
		set_bit(FSCACHE_COOKIE_RETIRED, &cookie->flags);
	trace_fscache_relinquish(cookie, retire);

	ASSERTCMP(atomic_read(&cookie->n_active), ==, 0);
	ASSERTCMP(atomic_read(&cookie->volume->n_cookies), >, 0);
	atomic_dec(&cookie->volume->n_cookies);

	set_bit(FSCACHE_COOKIE_DO_RELINQUISH, &cookie->flags);

	if (test_bit(FSCACHE_COOKIE_HAS_BEEN_CACHED, &cookie->flags))
		; // PLACEHOLDER: Do something here if the cookie was cached
	else
		fscache_drop_cookie(cookie);
	fscache_put_cookie(cookie, fscache_cookie_put_relinquish);
}
EXPORT_SYMBOL(__fscache_relinquish_cookie);

/*
 * Drop a reference to a cookie.
 */
void fscache_put_cookie(struct fscache_cookie *cookie,
			enum fscache_cookie_trace where)
{
	struct fscache_volume *volume = cookie->volume;
	unsigned int cookie_debug_id = cookie->debug_id;
	bool zero;
	int ref;

	zero = __refcount_dec_and_test(&cookie->ref, &ref);
	trace_fscache_cookie(cookie_debug_id, ref - 1, where);
	if (zero) {
		fscache_free_cookie(cookie);
		fscache_put_volume(volume, fscache_volume_put_cookie);
	}
}
EXPORT_SYMBOL(fscache_put_cookie);

/*
 * Get a reference to a cookie.
 */
struct fscache_cookie *fscache_get_cookie(struct fscache_cookie *cookie,
					  enum fscache_cookie_trace where)
{
	int ref;

	__refcount_inc(&cookie->ref, &ref);
	trace_fscache_cookie(cookie->debug_id, ref + 1, where);
	return cookie;
}
EXPORT_SYMBOL(fscache_get_cookie);

/*
 * Generate a list of extant cookies in /proc/fs/fscache/cookies
 */
static int fscache_cookies_seq_show(struct seq_file *m, void *v)
{
	struct fscache_cookie *cookie;
	unsigned int keylen = 0, auxlen = 0;
	u8 *p;

	if (v == &fscache_cookies) {
		seq_puts(m,
			 "COOKIE   VOLUME   REF ACT ACC S FL DEF             \n"
			 "======== ======== === === === = == ================\n"
			 );
		return 0;
	}

	cookie = list_entry(v, struct fscache_cookie, proc_link);

	seq_printf(m,
		   "%08x %08x %3d %3d %3d %c %02lx",
		   cookie->debug_id,
		   cookie->volume->debug_id,
		   refcount_read(&cookie->ref),
		   atomic_read(&cookie->n_active),
		   atomic_read(&cookie->n_accesses),
		   fscache_cookie_states[cookie->state],
		   cookie->flags);

	keylen = cookie->key_len;
	auxlen = cookie->aux_len;

	if (keylen > 0 || auxlen > 0) {
		seq_puts(m, " ");
		p = keylen <= sizeof(cookie->inline_key) ?
			cookie->inline_key : cookie->key;
		for (; keylen > 0; keylen--)
			seq_printf(m, "%02x", *p++);
		if (auxlen > 0) {
			seq_puts(m, ", ");
			p = auxlen <= sizeof(cookie->inline_aux) ?
				cookie->inline_aux : cookie->aux;
			for (; auxlen > 0; auxlen--)
				seq_printf(m, "%02x", *p++);
		}
	}

	seq_puts(m, "\n");
	return 0;
}

static void *fscache_cookies_seq_start(struct seq_file *m, loff_t *_pos)
	__acquires(fscache_cookies_lock)
{
	read_lock(&fscache_cookies_lock);
	return seq_list_start_head(&fscache_cookies, *_pos);
}

static void *fscache_cookies_seq_next(struct seq_file *m, void *v, loff_t *_pos)
{
	return seq_list_next(v, &fscache_cookies, _pos);
}

static void fscache_cookies_seq_stop(struct seq_file *m, void *v)
	__releases(rcu)
{
	read_unlock(&fscache_cookies_lock);
}


const struct seq_operations fscache_cookies_seq_ops = {
	.start  = fscache_cookies_seq_start,
	.next   = fscache_cookies_seq_next,
	.stop   = fscache_cookies_seq_stop,
	.show   = fscache_cookies_seq_show,
};
