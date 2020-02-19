// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/sunrpc/cache.c
 *
 * Generic code for various authentication-related caches
 * used by sunrpc clients and servers.
 *
 * Copyright (C) 2002 Neil Brown <neilb@cse.unsw.edu.au>
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/net.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <asm/ioctls.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include "netns.h"

#define	 RPCDBG_FACILITY RPCDBG_CACHE

static bool cache_defer_req(struct cache_req *req, struct cache_head *item);
static void cache_revisit_request(struct cache_head *item);
static bool cache_listeners_exist(struct cache_detail *detail);

static void cache_init(struct cache_head *h, struct cache_detail *detail)
{
	time64_t now = seconds_since_boot();
	INIT_HLIST_NODE(&h->cache_list);
	h->flags = 0;
	kref_init(&h->ref);
	h->expiry_time = now + CACHE_NEW_EXPIRY;
	if (now <= detail->flush_time)
		/* ensure it isn't already expired */
		now = detail->flush_time + 1;
	h->last_refresh = now;
}

static void cache_fresh_unlocked(struct cache_head *head,
				struct cache_detail *detail);

static struct cache_head *sunrpc_cache_find_rcu(struct cache_detail *detail,
						struct cache_head *key,
						int hash)
{
	struct hlist_head *head = &detail->hash_table[hash];
	struct cache_head *tmp;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp, head, cache_list) {
		if (detail->match(tmp, key)) {
			if (cache_is_expired(detail, tmp))
				continue;
			tmp = cache_get_rcu(tmp);
			rcu_read_unlock();
			return tmp;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static void sunrpc_begin_cache_remove_entry(struct cache_head *ch,
					    struct cache_detail *cd)
{
	/* Must be called under cd->hash_lock */
	hlist_del_init_rcu(&ch->cache_list);
	set_bit(CACHE_CLEANED, &ch->flags);
	cd->entries --;
}

static void sunrpc_end_cache_remove_entry(struct cache_head *ch,
					  struct cache_detail *cd)
{
	cache_fresh_unlocked(ch, cd);
	cache_put(ch, cd);
}

static struct cache_head *sunrpc_cache_add_entry(struct cache_detail *detail,
						 struct cache_head *key,
						 int hash)
{
	struct cache_head *new, *tmp, *freeme = NULL;
	struct hlist_head *head = &detail->hash_table[hash];

	new = detail->alloc();
	if (!new)
		return NULL;
	/* must fully initialise 'new', else
	 * we might get lose if we need to
	 * cache_put it soon.
	 */
	cache_init(new, detail);
	detail->init(new, key);

	spin_lock(&detail->hash_lock);

	/* check if entry appeared while we slept */
	hlist_for_each_entry_rcu(tmp, head, cache_list,
				 lockdep_is_held(&detail->hash_lock)) {
		if (detail->match(tmp, key)) {
			if (cache_is_expired(detail, tmp)) {
				sunrpc_begin_cache_remove_entry(tmp, detail);
				freeme = tmp;
				break;
			}
			cache_get(tmp);
			spin_unlock(&detail->hash_lock);
			cache_put(new, detail);
			return tmp;
		}
	}

	hlist_add_head_rcu(&new->cache_list, head);
	detail->entries++;
	cache_get(new);
	spin_unlock(&detail->hash_lock);

	if (freeme)
		sunrpc_end_cache_remove_entry(freeme, detail);
	return new;
}

struct cache_head *sunrpc_cache_lookup_rcu(struct cache_detail *detail,
					   struct cache_head *key, int hash)
{
	struct cache_head *ret;

	ret = sunrpc_cache_find_rcu(detail, key, hash);
	if (ret)
		return ret;
	/* Didn't find anything, insert an empty entry */
	return sunrpc_cache_add_entry(detail, key, hash);
}
EXPORT_SYMBOL_GPL(sunrpc_cache_lookup_rcu);

static void cache_dequeue(struct cache_detail *detail, struct cache_head *ch);

static void cache_fresh_locked(struct cache_head *head, time64_t expiry,
			       struct cache_detail *detail)
{
	time64_t now = seconds_since_boot();
	if (now <= detail->flush_time)
		/* ensure it isn't immediately treated as expired */
		now = detail->flush_time + 1;
	head->expiry_time = expiry;
	head->last_refresh = now;
	smp_wmb(); /* paired with smp_rmb() in cache_is_valid() */
	set_bit(CACHE_VALID, &head->flags);
}

static void cache_fresh_unlocked(struct cache_head *head,
				 struct cache_detail *detail)
{
	if (test_and_clear_bit(CACHE_PENDING, &head->flags)) {
		cache_revisit_request(head);
		cache_dequeue(detail, head);
	}
}

struct cache_head *sunrpc_cache_update(struct cache_detail *detail,
				       struct cache_head *new, struct cache_head *old, int hash)
{
	/* The 'old' entry is to be replaced by 'new'.
	 * If 'old' is not VALID, we update it directly,
	 * otherwise we need to replace it
	 */
	struct cache_head *tmp;

	if (!test_bit(CACHE_VALID, &old->flags)) {
		spin_lock(&detail->hash_lock);
		if (!test_bit(CACHE_VALID, &old->flags)) {
			if (test_bit(CACHE_NEGATIVE, &new->flags))
				set_bit(CACHE_NEGATIVE, &old->flags);
			else
				detail->update(old, new);
			cache_fresh_locked(old, new->expiry_time, detail);
			spin_unlock(&detail->hash_lock);
			cache_fresh_unlocked(old, detail);
			return old;
		}
		spin_unlock(&detail->hash_lock);
	}
	/* We need to insert a new entry */
	tmp = detail->alloc();
	if (!tmp) {
		cache_put(old, detail);
		return NULL;
	}
	cache_init(tmp, detail);
	detail->init(tmp, old);

	spin_lock(&detail->hash_lock);
	if (test_bit(CACHE_NEGATIVE, &new->flags))
		set_bit(CACHE_NEGATIVE, &tmp->flags);
	else
		detail->update(tmp, new);
	hlist_add_head(&tmp->cache_list, &detail->hash_table[hash]);
	detail->entries++;
	cache_get(tmp);
	cache_fresh_locked(tmp, new->expiry_time, detail);
	cache_fresh_locked(old, 0, detail);
	spin_unlock(&detail->hash_lock);
	cache_fresh_unlocked(tmp, detail);
	cache_fresh_unlocked(old, detail);
	cache_put(old, detail);
	return tmp;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_update);

static int cache_make_upcall(struct cache_detail *cd, struct cache_head *h)
{
	if (cd->cache_upcall)
		return cd->cache_upcall(cd, h);
	return sunrpc_cache_pipe_upcall(cd, h);
}

static inline int cache_is_valid(struct cache_head *h)
{
	if (!test_bit(CACHE_VALID, &h->flags))
		return -EAGAIN;
	else {
		/* entry is valid */
		if (test_bit(CACHE_NEGATIVE, &h->flags))
			return -ENOENT;
		else {
			/*
			 * In combination with write barrier in
			 * sunrpc_cache_update, ensures that anyone
			 * using the cache entry after this sees the
			 * updated contents:
			 */
			smp_rmb();
			return 0;
		}
	}
}

static int try_to_negate_entry(struct cache_detail *detail, struct cache_head *h)
{
	int rv;

	spin_lock(&detail->hash_lock);
	rv = cache_is_valid(h);
	if (rv == -EAGAIN) {
		set_bit(CACHE_NEGATIVE, &h->flags);
		cache_fresh_locked(h, seconds_since_boot()+CACHE_NEW_EXPIRY,
				   detail);
		rv = -ENOENT;
	}
	spin_unlock(&detail->hash_lock);
	cache_fresh_unlocked(h, detail);
	return rv;
}

/*
 * This is the generic cache management routine for all
 * the authentication caches.
 * It checks the currency of a cache item and will (later)
 * initiate an upcall to fill it if needed.
 *
 *
 * Returns 0 if the cache_head can be used, or cache_puts it and returns
 * -EAGAIN if upcall is pending and request has been queued
 * -ETIMEDOUT if upcall failed or request could not be queue or
 *           upcall completed but item is still invalid (implying that
 *           the cache item has been replaced with a newer one).
 * -ENOENT if cache entry was negative
 */
int cache_check(struct cache_detail *detail,
		    struct cache_head *h, struct cache_req *rqstp)
{
	int rv;
	time64_t refresh_age, age;

	/* First decide return status as best we can */
	rv = cache_is_valid(h);

	/* now see if we want to start an upcall */
	refresh_age = (h->expiry_time - h->last_refresh);
	age = seconds_since_boot() - h->last_refresh;

	if (rqstp == NULL) {
		if (rv == -EAGAIN)
			rv = -ENOENT;
	} else if (rv == -EAGAIN ||
		   (h->expiry_time != 0 && age > refresh_age/2)) {
		dprintk("RPC:       Want update, refage=%lld, age=%lld\n",
				refresh_age, age);
		if (!test_and_set_bit(CACHE_PENDING, &h->flags)) {
			switch (cache_make_upcall(detail, h)) {
			case -EINVAL:
				rv = try_to_negate_entry(detail, h);
				break;
			case -EAGAIN:
				cache_fresh_unlocked(h, detail);
				break;
			}
		} else if (!cache_listeners_exist(detail))
			rv = try_to_negate_entry(detail, h);
	}

	if (rv == -EAGAIN) {
		if (!cache_defer_req(rqstp, h)) {
			/*
			 * Request was not deferred; handle it as best
			 * we can ourselves:
			 */
			rv = cache_is_valid(h);
			if (rv == -EAGAIN)
				rv = -ETIMEDOUT;
		}
	}
	if (rv)
		cache_put(h, detail);
	return rv;
}
EXPORT_SYMBOL_GPL(cache_check);

/*
 * caches need to be periodically cleaned.
 * For this we maintain a list of cache_detail and
 * a current pointer into that list and into the table
 * for that entry.
 *
 * Each time cache_clean is called it finds the next non-empty entry
 * in the current table and walks the list in that entry
 * looking for entries that can be removed.
 *
 * An entry gets removed if:
 * - The expiry is before current time
 * - The last_refresh time is before the flush_time for that cache
 *
 * later we might drop old entries with non-NEVER expiry if that table
 * is getting 'full' for some definition of 'full'
 *
 * The question of "how often to scan a table" is an interesting one
 * and is answered in part by the use of the "nextcheck" field in the
 * cache_detail.
 * When a scan of a table begins, the nextcheck field is set to a time
 * that is well into the future.
 * While scanning, if an expiry time is found that is earlier than the
 * current nextcheck time, nextcheck is set to that expiry time.
 * If the flush_time is ever set to a time earlier than the nextcheck
 * time, the nextcheck time is then set to that flush_time.
 *
 * A table is then only scanned if the current time is at least
 * the nextcheck time.
 *
 */

static LIST_HEAD(cache_list);
static DEFINE_SPINLOCK(cache_list_lock);
static struct cache_detail *current_detail;
static int current_index;

static void do_cache_clean(struct work_struct *work);
static struct delayed_work cache_cleaner;

void sunrpc_init_cache_detail(struct cache_detail *cd)
{
	spin_lock_init(&cd->hash_lock);
	INIT_LIST_HEAD(&cd->queue);
	spin_lock(&cache_list_lock);
	cd->nextcheck = 0;
	cd->entries = 0;
	atomic_set(&cd->writers, 0);
	cd->last_close = 0;
	cd->last_warn = -1;
	list_add(&cd->others, &cache_list);
	spin_unlock(&cache_list_lock);

	/* start the cleaning process */
	queue_delayed_work(system_power_efficient_wq, &cache_cleaner, 0);
}
EXPORT_SYMBOL_GPL(sunrpc_init_cache_detail);

void sunrpc_destroy_cache_detail(struct cache_detail *cd)
{
	cache_purge(cd);
	spin_lock(&cache_list_lock);
	spin_lock(&cd->hash_lock);
	if (current_detail == cd)
		current_detail = NULL;
	list_del_init(&cd->others);
	spin_unlock(&cd->hash_lock);
	spin_unlock(&cache_list_lock);
	if (list_empty(&cache_list)) {
		/* module must be being unloaded so its safe to kill the worker */
		cancel_delayed_work_sync(&cache_cleaner);
	}
}
EXPORT_SYMBOL_GPL(sunrpc_destroy_cache_detail);

/* clean cache tries to find something to clean
 * and cleans it.
 * It returns 1 if it cleaned something,
 *            0 if it didn't find anything this time
 *           -1 if it fell off the end of the list.
 */
static int cache_clean(void)
{
	int rv = 0;
	struct list_head *next;

	spin_lock(&cache_list_lock);

	/* find a suitable table if we don't already have one */
	while (current_detail == NULL ||
	    current_index >= current_detail->hash_size) {
		if (current_detail)
			next = current_detail->others.next;
		else
			next = cache_list.next;
		if (next == &cache_list) {
			current_detail = NULL;
			spin_unlock(&cache_list_lock);
			return -1;
		}
		current_detail = list_entry(next, struct cache_detail, others);
		if (current_detail->nextcheck > seconds_since_boot())
			current_index = current_detail->hash_size;
		else {
			current_index = 0;
			current_detail->nextcheck = seconds_since_boot()+30*60;
		}
	}

	/* find a non-empty bucket in the table */
	while (current_detail &&
	       current_index < current_detail->hash_size &&
	       hlist_empty(&current_detail->hash_table[current_index]))
		current_index++;

	/* find a cleanable entry in the bucket and clean it, or set to next bucket */

	if (current_detail && current_index < current_detail->hash_size) {
		struct cache_head *ch = NULL;
		struct cache_detail *d;
		struct hlist_head *head;
		struct hlist_node *tmp;

		spin_lock(&current_detail->hash_lock);

		/* Ok, now to clean this strand */

		head = &current_detail->hash_table[current_index];
		hlist_for_each_entry_safe(ch, tmp, head, cache_list) {
			if (current_detail->nextcheck > ch->expiry_time)
				current_detail->nextcheck = ch->expiry_time+1;
			if (!cache_is_expired(current_detail, ch))
				continue;

			sunrpc_begin_cache_remove_entry(ch, current_detail);
			rv = 1;
			break;
		}

		spin_unlock(&current_detail->hash_lock);
		d = current_detail;
		if (!ch)
			current_index ++;
		spin_unlock(&cache_list_lock);
		if (ch)
			sunrpc_end_cache_remove_entry(ch, d);
	} else
		spin_unlock(&cache_list_lock);

	return rv;
}

/*
 * We want to regularly clean the cache, so we need to schedule some work ...
 */
static void do_cache_clean(struct work_struct *work)
{
	int delay = 5;
	if (cache_clean() == -1)
		delay = round_jiffies_relative(30*HZ);

	if (list_empty(&cache_list))
		delay = 0;

	if (delay)
		queue_delayed_work(system_power_efficient_wq,
				   &cache_cleaner, delay);
}


/*
 * Clean all caches promptly.  This just calls cache_clean
 * repeatedly until we are sure that every cache has had a chance to
 * be fully cleaned
 */
void cache_flush(void)
{
	while (cache_clean() != -1)
		cond_resched();
	while (cache_clean() != -1)
		cond_resched();
}
EXPORT_SYMBOL_GPL(cache_flush);

void cache_purge(struct cache_detail *detail)
{
	struct cache_head *ch = NULL;
	struct hlist_head *head = NULL;
	struct hlist_node *tmp = NULL;
	int i = 0;

	spin_lock(&detail->hash_lock);
	if (!detail->entries) {
		spin_unlock(&detail->hash_lock);
		return;
	}

	dprintk("RPC: %d entries in %s cache\n", detail->entries, detail->name);
	for (i = 0; i < detail->hash_size; i++) {
		head = &detail->hash_table[i];
		hlist_for_each_entry_safe(ch, tmp, head, cache_list) {
			sunrpc_begin_cache_remove_entry(ch, detail);
			spin_unlock(&detail->hash_lock);
			sunrpc_end_cache_remove_entry(ch, detail);
			spin_lock(&detail->hash_lock);
		}
	}
	spin_unlock(&detail->hash_lock);
}
EXPORT_SYMBOL_GPL(cache_purge);


/*
 * Deferral and Revisiting of Requests.
 *
 * If a cache lookup finds a pending entry, we
 * need to defer the request and revisit it later.
 * All deferred requests are stored in a hash table,
 * indexed by "struct cache_head *".
 * As it may be wasteful to store a whole request
 * structure, we allow the request to provide a
 * deferred form, which must contain a
 * 'struct cache_deferred_req'
 * This cache_deferred_req contains a method to allow
 * it to be revisited when cache info is available
 */

#define	DFR_HASHSIZE	(PAGE_SIZE/sizeof(struct list_head))
#define	DFR_HASH(item)	((((long)item)>>4 ^ (((long)item)>>13)) % DFR_HASHSIZE)

#define	DFR_MAX	300	/* ??? */

static DEFINE_SPINLOCK(cache_defer_lock);
static LIST_HEAD(cache_defer_list);
static struct hlist_head cache_defer_hash[DFR_HASHSIZE];
static int cache_defer_cnt;

static void __unhash_deferred_req(struct cache_deferred_req *dreq)
{
	hlist_del_init(&dreq->hash);
	if (!list_empty(&dreq->recent)) {
		list_del_init(&dreq->recent);
		cache_defer_cnt--;
	}
}

static void __hash_deferred_req(struct cache_deferred_req *dreq, struct cache_head *item)
{
	int hash = DFR_HASH(item);

	INIT_LIST_HEAD(&dreq->recent);
	hlist_add_head(&dreq->hash, &cache_defer_hash[hash]);
}

static void setup_deferral(struct cache_deferred_req *dreq,
			   struct cache_head *item,
			   int count_me)
{

	dreq->item = item;

	spin_lock(&cache_defer_lock);

	__hash_deferred_req(dreq, item);

	if (count_me) {
		cache_defer_cnt++;
		list_add(&dreq->recent, &cache_defer_list);
	}

	spin_unlock(&cache_defer_lock);

}

struct thread_deferred_req {
	struct cache_deferred_req handle;
	struct completion completion;
};

static void cache_restart_thread(struct cache_deferred_req *dreq, int too_many)
{
	struct thread_deferred_req *dr =
		container_of(dreq, struct thread_deferred_req, handle);
	complete(&dr->completion);
}

static void cache_wait_req(struct cache_req *req, struct cache_head *item)
{
	struct thread_deferred_req sleeper;
	struct cache_deferred_req *dreq = &sleeper.handle;

	sleeper.completion = COMPLETION_INITIALIZER_ONSTACK(sleeper.completion);
	dreq->revisit = cache_restart_thread;

	setup_deferral(dreq, item, 0);

	if (!test_bit(CACHE_PENDING, &item->flags) ||
	    wait_for_completion_interruptible_timeout(
		    &sleeper.completion, req->thread_wait) <= 0) {
		/* The completion wasn't completed, so we need
		 * to clean up
		 */
		spin_lock(&cache_defer_lock);
		if (!hlist_unhashed(&sleeper.handle.hash)) {
			__unhash_deferred_req(&sleeper.handle);
			spin_unlock(&cache_defer_lock);
		} else {
			/* cache_revisit_request already removed
			 * this from the hash table, but hasn't
			 * called ->revisit yet.  It will very soon
			 * and we need to wait for it.
			 */
			spin_unlock(&cache_defer_lock);
			wait_for_completion(&sleeper.completion);
		}
	}
}

static void cache_limit_defers(void)
{
	/* Make sure we haven't exceed the limit of allowed deferred
	 * requests.
	 */
	struct cache_deferred_req *discard = NULL;

	if (cache_defer_cnt <= DFR_MAX)
		return;

	spin_lock(&cache_defer_lock);

	/* Consider removing either the first or the last */
	if (cache_defer_cnt > DFR_MAX) {
		if (prandom_u32() & 1)
			discard = list_entry(cache_defer_list.next,
					     struct cache_deferred_req, recent);
		else
			discard = list_entry(cache_defer_list.prev,
					     struct cache_deferred_req, recent);
		__unhash_deferred_req(discard);
	}
	spin_unlock(&cache_defer_lock);
	if (discard)
		discard->revisit(discard, 1);
}

/* Return true if and only if a deferred request is queued. */
static bool cache_defer_req(struct cache_req *req, struct cache_head *item)
{
	struct cache_deferred_req *dreq;

	if (req->thread_wait) {
		cache_wait_req(req, item);
		if (!test_bit(CACHE_PENDING, &item->flags))
			return false;
	}
	dreq = req->defer(req);
	if (dreq == NULL)
		return false;
	setup_deferral(dreq, item, 1);
	if (!test_bit(CACHE_PENDING, &item->flags))
		/* Bit could have been cleared before we managed to
		 * set up the deferral, so need to revisit just in case
		 */
		cache_revisit_request(item);

	cache_limit_defers();
	return true;
}

static void cache_revisit_request(struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	struct list_head pending;
	struct hlist_node *tmp;
	int hash = DFR_HASH(item);

	INIT_LIST_HEAD(&pending);
	spin_lock(&cache_defer_lock);

	hlist_for_each_entry_safe(dreq, tmp, &cache_defer_hash[hash], hash)
		if (dreq->item == item) {
			__unhash_deferred_req(dreq);
			list_add(&dreq->recent, &pending);
		}

	spin_unlock(&cache_defer_lock);

	while (!list_empty(&pending)) {
		dreq = list_entry(pending.next, struct cache_deferred_req, recent);
		list_del_init(&dreq->recent);
		dreq->revisit(dreq, 0);
	}
}

void cache_clean_deferred(void *owner)
{
	struct cache_deferred_req *dreq, *tmp;
	struct list_head pending;


	INIT_LIST_HEAD(&pending);
	spin_lock(&cache_defer_lock);

	list_for_each_entry_safe(dreq, tmp, &cache_defer_list, recent) {
		if (dreq->owner == owner) {
			__unhash_deferred_req(dreq);
			list_add(&dreq->recent, &pending);
		}
	}
	spin_unlock(&cache_defer_lock);

	while (!list_empty(&pending)) {
		dreq = list_entry(pending.next, struct cache_deferred_req, recent);
		list_del_init(&dreq->recent);
		dreq->revisit(dreq, 1);
	}
}

/*
 * communicate with user-space
 *
 * We have a magic /proc file - /proc/net/rpc/<cachename>/channel.
 * On read, you get a full request, or block.
 * On write, an update request is processed.
 * Poll works if anything to read, and always allows write.
 *
 * Implemented by linked list of requests.  Each open file has
 * a ->private that also exists in this list.  New requests are added
 * to the end and may wakeup and preceding readers.
 * New readers are added to the head.  If, on read, an item is found with
 * CACHE_UPCALLING clear, we free it from the list.
 *
 */

static DEFINE_SPINLOCK(queue_lock);
static DEFINE_MUTEX(queue_io_mutex);

struct cache_queue {
	struct list_head	list;
	int			reader;	/* if 0, then request */
};
struct cache_request {
	struct cache_queue	q;
	struct cache_head	*item;
	char			* buf;
	int			len;
	int			readers;
};
struct cache_reader {
	struct cache_queue	q;
	int			offset;	/* if non-0, we have a refcnt on next request */
};

static int cache_request(struct cache_detail *detail,
			       struct cache_request *crq)
{
	char *bp = crq->buf;
	int len = PAGE_SIZE;

	detail->cache_request(detail, crq->item, &bp, &len);
	if (len < 0)
		return -EAGAIN;
	return PAGE_SIZE - len;
}

static ssize_t cache_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *ppos, struct cache_detail *cd)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_request *rq;
	struct inode *inode = file_inode(filp);
	int err;

	if (count == 0)
		return 0;

	inode_lock(inode); /* protect against multiple concurrent
			      * readers on this file */
 again:
	spin_lock(&queue_lock);
	/* need to find next request */
	while (rp->q.list.next != &cd->queue &&
	       list_entry(rp->q.list.next, struct cache_queue, list)
	       ->reader) {
		struct list_head *next = rp->q.list.next;
		list_move(&rp->q.list, next);
	}
	if (rp->q.list.next == &cd->queue) {
		spin_unlock(&queue_lock);
		inode_unlock(inode);
		WARN_ON_ONCE(rp->offset);
		return 0;
	}
	rq = container_of(rp->q.list.next, struct cache_request, q.list);
	WARN_ON_ONCE(rq->q.reader);
	if (rp->offset == 0)
		rq->readers++;
	spin_unlock(&queue_lock);

	if (rq->len == 0) {
		err = cache_request(cd, rq);
		if (err < 0)
			goto out;
		rq->len = err;
	}

	if (rp->offset == 0 && !test_bit(CACHE_PENDING, &rq->item->flags)) {
		err = -EAGAIN;
		spin_lock(&queue_lock);
		list_move(&rp->q.list, &rq->q.list);
		spin_unlock(&queue_lock);
	} else {
		if (rp->offset + count > rq->len)
			count = rq->len - rp->offset;
		err = -EFAULT;
		if (copy_to_user(buf, rq->buf + rp->offset, count))
			goto out;
		rp->offset += count;
		if (rp->offset >= rq->len) {
			rp->offset = 0;
			spin_lock(&queue_lock);
			list_move(&rp->q.list, &rq->q.list);
			spin_unlock(&queue_lock);
		}
		err = 0;
	}
 out:
	if (rp->offset == 0) {
		/* need to release rq */
		spin_lock(&queue_lock);
		rq->readers--;
		if (rq->readers == 0 &&
		    !test_bit(CACHE_PENDING, &rq->item->flags)) {
			list_del(&rq->q.list);
			spin_unlock(&queue_lock);
			cache_put(rq->item, cd);
			kfree(rq->buf);
			kfree(rq);
		} else
			spin_unlock(&queue_lock);
	}
	if (err == -EAGAIN)
		goto again;
	inode_unlock(inode);
	return err ? err :  count;
}

static ssize_t cache_do_downcall(char *kaddr, const char __user *buf,
				 size_t count, struct cache_detail *cd)
{
	ssize_t ret;

	if (count == 0)
		return -EINVAL;
	if (copy_from_user(kaddr, buf, count))
		return -EFAULT;
	kaddr[count] = '\0';
	ret = cd->cache_parse(cd, kaddr, count);
	if (!ret)
		ret = count;
	return ret;
}

static ssize_t cache_slow_downcall(const char __user *buf,
				   size_t count, struct cache_detail *cd)
{
	static char write_buf[8192]; /* protected by queue_io_mutex */
	ssize_t ret = -EINVAL;

	if (count >= sizeof(write_buf))
		goto out;
	mutex_lock(&queue_io_mutex);
	ret = cache_do_downcall(write_buf, buf, count, cd);
	mutex_unlock(&queue_io_mutex);
out:
	return ret;
}

static ssize_t cache_downcall(struct address_space *mapping,
			      const char __user *buf,
			      size_t count, struct cache_detail *cd)
{
	struct page *page;
	char *kaddr;
	ssize_t ret = -ENOMEM;

	if (count >= PAGE_SIZE)
		goto out_slow;

	page = find_or_create_page(mapping, 0, GFP_KERNEL);
	if (!page)
		goto out_slow;

	kaddr = kmap(page);
	ret = cache_do_downcall(kaddr, buf, count, cd);
	kunmap(page);
	unlock_page(page);
	put_page(page);
	return ret;
out_slow:
	return cache_slow_downcall(buf, count, cd);
}

static ssize_t cache_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos,
			   struct cache_detail *cd)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = file_inode(filp);
	ssize_t ret = -EINVAL;

	if (!cd->cache_parse)
		goto out;

	inode_lock(inode);
	ret = cache_downcall(mapping, buf, count, cd);
	inode_unlock(inode);
out:
	return ret;
}

static DECLARE_WAIT_QUEUE_HEAD(queue_wait);

static __poll_t cache_poll(struct file *filp, poll_table *wait,
			       struct cache_detail *cd)
{
	__poll_t mask;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;

	poll_wait(filp, &queue_wait, wait);

	/* alway allow write */
	mask = EPOLLOUT | EPOLLWRNORM;

	if (!rp)
		return mask;

	spin_lock(&queue_lock);

	for (cq= &rp->q; &cq->list != &cd->queue;
	     cq = list_entry(cq->list.next, struct cache_queue, list))
		if (!cq->reader) {
			mask |= EPOLLIN | EPOLLRDNORM;
			break;
		}
	spin_unlock(&queue_lock);
	return mask;
}

static int cache_ioctl(struct inode *ino, struct file *filp,
		       unsigned int cmd, unsigned long arg,
		       struct cache_detail *cd)
{
	int len = 0;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;

	if (cmd != FIONREAD || !rp)
		return -EINVAL;

	spin_lock(&queue_lock);

	/* only find the length remaining in current request,
	 * or the length of the next request
	 */
	for (cq= &rp->q; &cq->list != &cd->queue;
	     cq = list_entry(cq->list.next, struct cache_queue, list))
		if (!cq->reader) {
			struct cache_request *cr =
				container_of(cq, struct cache_request, q);
			len = cr->len - rp->offset;
			break;
		}
	spin_unlock(&queue_lock);

	return put_user(len, (int __user *)arg);
}

static int cache_open(struct inode *inode, struct file *filp,
		      struct cache_detail *cd)
{
	struct cache_reader *rp = NULL;

	if (!cd || !try_module_get(cd->owner))
		return -EACCES;
	nonseekable_open(inode, filp);
	if (filp->f_mode & FMODE_READ) {
		rp = kmalloc(sizeof(*rp), GFP_KERNEL);
		if (!rp) {
			module_put(cd->owner);
			return -ENOMEM;
		}
		rp->offset = 0;
		rp->q.reader = 1;

		spin_lock(&queue_lock);
		list_add(&rp->q.list, &cd->queue);
		spin_unlock(&queue_lock);
	}
	if (filp->f_mode & FMODE_WRITE)
		atomic_inc(&cd->writers);
	filp->private_data = rp;
	return 0;
}

static int cache_release(struct inode *inode, struct file *filp,
			 struct cache_detail *cd)
{
	struct cache_reader *rp = filp->private_data;

	if (rp) {
		spin_lock(&queue_lock);
		if (rp->offset) {
			struct cache_queue *cq;
			for (cq= &rp->q; &cq->list != &cd->queue;
			     cq = list_entry(cq->list.next, struct cache_queue, list))
				if (!cq->reader) {
					container_of(cq, struct cache_request, q)
						->readers--;
					break;
				}
			rp->offset = 0;
		}
		list_del(&rp->q.list);
		spin_unlock(&queue_lock);

		filp->private_data = NULL;
		kfree(rp);

	}
	if (filp->f_mode & FMODE_WRITE) {
		atomic_dec(&cd->writers);
		cd->last_close = seconds_since_boot();
	}
	module_put(cd->owner);
	return 0;
}



static void cache_dequeue(struct cache_detail *detail, struct cache_head *ch)
{
	struct cache_queue *cq, *tmp;
	struct cache_request *cr;
	struct list_head dequeued;

	INIT_LIST_HEAD(&dequeued);
	spin_lock(&queue_lock);
	list_for_each_entry_safe(cq, tmp, &detail->queue, list)
		if (!cq->reader) {
			cr = container_of(cq, struct cache_request, q);
			if (cr->item != ch)
				continue;
			if (test_bit(CACHE_PENDING, &ch->flags))
				/* Lost a race and it is pending again */
				break;
			if (cr->readers != 0)
				continue;
			list_move(&cr->q.list, &dequeued);
		}
	spin_unlock(&queue_lock);
	while (!list_empty(&dequeued)) {
		cr = list_entry(dequeued.next, struct cache_request, q.list);
		list_del(&cr->q.list);
		cache_put(cr->item, detail);
		kfree(cr->buf);
		kfree(cr);
	}
}

/*
 * Support routines for text-based upcalls.
 * Fields are separated by spaces.
 * Fields are either mangled to quote space tab newline slosh with slosh
 * or a hexified with a leading \x
 * Record is terminated with newline.
 *
 */

void qword_add(char **bpp, int *lp, char *str)
{
	char *bp = *bpp;
	int len = *lp;
	int ret;

	if (len < 0) return;

	ret = string_escape_str(str, bp, len, ESCAPE_OCTAL, "\\ \n\t");
	if (ret >= len) {
		bp += len;
		len = -1;
	} else {
		bp += ret;
		len -= ret;
		*bp++ = ' ';
		len--;
	}
	*bpp = bp;
	*lp = len;
}
EXPORT_SYMBOL_GPL(qword_add);

void qword_addhex(char **bpp, int *lp, char *buf, int blen)
{
	char *bp = *bpp;
	int len = *lp;

	if (len < 0) return;

	if (len > 2) {
		*bp++ = '\\';
		*bp++ = 'x';
		len -= 2;
		while (blen && len >= 2) {
			bp = hex_byte_pack(bp, *buf++);
			len -= 2;
			blen--;
		}
	}
	if (blen || len<1) len = -1;
	else {
		*bp++ = ' ';
		len--;
	}
	*bpp = bp;
	*lp = len;
}
EXPORT_SYMBOL_GPL(qword_addhex);

static void warn_no_listener(struct cache_detail *detail)
{
	if (detail->last_warn != detail->last_close) {
		detail->last_warn = detail->last_close;
		if (detail->warn_no_listener)
			detail->warn_no_listener(detail, detail->last_close != 0);
	}
}

static bool cache_listeners_exist(struct cache_detail *detail)
{
	if (atomic_read(&detail->writers))
		return true;
	if (detail->last_close == 0)
		/* This cache was never opened */
		return false;
	if (detail->last_close < seconds_since_boot() - 30)
		/*
		 * We allow for the possibility that someone might
		 * restart a userspace daemon without restarting the
		 * server; but after 30 seconds, we give up.
		 */
		 return false;
	return true;
}

/*
 * register an upcall request to user-space and queue it up for read() by the
 * upcall daemon.
 *
 * Each request is at most one page long.
 */
int sunrpc_cache_pipe_upcall(struct cache_detail *detail, struct cache_head *h)
{

	char *buf;
	struct cache_request *crq;
	int ret = 0;

	if (!detail->cache_request)
		return -EINVAL;

	if (!cache_listeners_exist(detail)) {
		warn_no_listener(detail);
		return -EINVAL;
	}
	if (test_bit(CACHE_CLEANED, &h->flags))
		/* Too late to make an upcall */
		return -EAGAIN;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -EAGAIN;

	crq = kmalloc(sizeof (*crq), GFP_KERNEL);
	if (!crq) {
		kfree(buf);
		return -EAGAIN;
	}

	crq->q.reader = 0;
	crq->buf = buf;
	crq->len = 0;
	crq->readers = 0;
	spin_lock(&queue_lock);
	if (test_bit(CACHE_PENDING, &h->flags)) {
		crq->item = cache_get(h);
		list_add_tail(&crq->q.list, &detail->queue);
	} else
		/* Lost a race, no longer PENDING, so don't enqueue */
		ret = -EAGAIN;
	spin_unlock(&queue_lock);
	wake_up(&queue_wait);
	if (ret == -EAGAIN) {
		kfree(buf);
		kfree(crq);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_pipe_upcall);

/*
 * parse a message from user-space and pass it
 * to an appropriate cache
 * Messages are, like requests, separated into fields by
 * spaces and dequotes as \xHEXSTRING or embedded \nnn octal
 *
 * Message is
 *   reply cachename expiry key ... content....
 *
 * key and content are both parsed by cache
 */

int qword_get(char **bpp, char *dest, int bufsize)
{
	/* return bytes copied, or -1 on error */
	char *bp = *bpp;
	int len = 0;

	while (*bp == ' ') bp++;

	if (bp[0] == '\\' && bp[1] == 'x') {
		/* HEX STRING */
		bp += 2;
		while (len < bufsize - 1) {
			int h, l;

			h = hex_to_bin(bp[0]);
			if (h < 0)
				break;

			l = hex_to_bin(bp[1]);
			if (l < 0)
				break;

			*dest++ = (h << 4) | l;
			bp += 2;
			len++;
		}
	} else {
		/* text with \nnn octal quoting */
		while (*bp != ' ' && *bp != '\n' && *bp && len < bufsize-1) {
			if (*bp == '\\' &&
			    isodigit(bp[1]) && (bp[1] <= '3') &&
			    isodigit(bp[2]) &&
			    isodigit(bp[3])) {
				int byte = (*++bp -'0');
				bp++;
				byte = (byte << 3) | (*bp++ - '0');
				byte = (byte << 3) | (*bp++ - '0');
				*dest++ = byte;
				len++;
			} else {
				*dest++ = *bp++;
				len++;
			}
		}
	}

	if (*bp != ' ' && *bp != '\n' && *bp != '\0')
		return -1;
	while (*bp == ' ') bp++;
	*bpp = bp;
	*dest = '\0';
	return len;
}
EXPORT_SYMBOL_GPL(qword_get);


/*
 * support /proc/net/rpc/$CACHENAME/content
 * as a seqfile.
 * We call ->cache_show passing NULL for the item to
 * get a header, then pass each real item in the cache
 */

static void *__cache_seq_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	unsigned int hash, entry;
	struct cache_head *ch;
	struct cache_detail *cd = m->private;

	if (!n--)
		return SEQ_START_TOKEN;
	hash = n >> 32;
	entry = n & ((1LL<<32) - 1);

	hlist_for_each_entry_rcu(ch, &cd->hash_table[hash], cache_list)
		if (!entry--)
			return ch;
	n &= ~((1LL<<32) - 1);
	do {
		hash++;
		n += 1LL<<32;
	} while(hash < cd->hash_size &&
		hlist_empty(&cd->hash_table[hash]));
	if (hash >= cd->hash_size)
		return NULL;
	*pos = n+1;
	return hlist_entry_safe(rcu_dereference_raw(
				hlist_first_rcu(&cd->hash_table[hash])),
				struct cache_head, cache_list);
}

static void *cache_seq_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct cache_head *ch = p;
	int hash = (*pos >> 32);
	struct cache_detail *cd = m->private;

	if (p == SEQ_START_TOKEN)
		hash = 0;
	else if (ch->cache_list.next == NULL) {
		hash++;
		*pos += 1LL<<32;
	} else {
		++*pos;
		return hlist_entry_safe(rcu_dereference_raw(
					hlist_next_rcu(&ch->cache_list)),
					struct cache_head, cache_list);
	}
	*pos &= ~((1LL<<32) - 1);
	while (hash < cd->hash_size &&
	       hlist_empty(&cd->hash_table[hash])) {
		hash++;
		*pos += 1LL<<32;
	}
	if (hash >= cd->hash_size)
		return NULL;
	++*pos;
	return hlist_entry_safe(rcu_dereference_raw(
				hlist_first_rcu(&cd->hash_table[hash])),
				struct cache_head, cache_list);
}

void *cache_seq_start_rcu(struct seq_file *m, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return __cache_seq_start(m, pos);
}
EXPORT_SYMBOL_GPL(cache_seq_start_rcu);

void *cache_seq_next_rcu(struct seq_file *file, void *p, loff_t *pos)
{
	return cache_seq_next(file, p, pos);
}
EXPORT_SYMBOL_GPL(cache_seq_next_rcu);

void cache_seq_stop_rcu(struct seq_file *m, void *p)
	__releases(RCU)
{
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(cache_seq_stop_rcu);

static int c_show(struct seq_file *m, void *p)
{
	struct cache_head *cp = p;
	struct cache_detail *cd = m->private;

	if (p == SEQ_START_TOKEN)
		return cd->cache_show(m, cd, NULL);

	ifdebug(CACHE)
		seq_printf(m, "# expiry=%lld refcnt=%d flags=%lx\n",
			   convert_to_wallclock(cp->expiry_time),
			   kref_read(&cp->ref), cp->flags);
	cache_get(cp);
	if (cache_check(cd, cp, NULL))
		/* cache_check does a cache_put on failure */
		seq_printf(m, "# ");
	else {
		if (cache_is_expired(cd, cp))
			seq_printf(m, "# ");
		cache_put(cp, cd);
	}

	return cd->cache_show(m, cd, cp);
}

static const struct seq_operations cache_content_op = {
	.start	= cache_seq_start_rcu,
	.next	= cache_seq_next_rcu,
	.stop	= cache_seq_stop_rcu,
	.show	= c_show,
};

static int content_open(struct inode *inode, struct file *file,
			struct cache_detail *cd)
{
	struct seq_file *seq;
	int err;

	if (!cd || !try_module_get(cd->owner))
		return -EACCES;

	err = seq_open(file, &cache_content_op);
	if (err) {
		module_put(cd->owner);
		return err;
	}

	seq = file->private_data;
	seq->private = cd;
	return 0;
}

static int content_release(struct inode *inode, struct file *file,
		struct cache_detail *cd)
{
	int ret = seq_release(inode, file);
	module_put(cd->owner);
	return ret;
}

static int open_flush(struct inode *inode, struct file *file,
			struct cache_detail *cd)
{
	if (!cd || !try_module_get(cd->owner))
		return -EACCES;
	return nonseekable_open(inode, file);
}

static int release_flush(struct inode *inode, struct file *file,
			struct cache_detail *cd)
{
	module_put(cd->owner);
	return 0;
}

static ssize_t read_flush(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos,
			  struct cache_detail *cd)
{
	char tbuf[22];
	size_t len;

	len = snprintf(tbuf, sizeof(tbuf), "%llu\n",
			convert_to_wallclock(cd->flush_time));
	return simple_read_from_buffer(buf, count, ppos, tbuf, len);
}

static ssize_t write_flush(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos,
			   struct cache_detail *cd)
{
	char tbuf[20];
	char *ep;
	time64_t now;

	if (*ppos || count > sizeof(tbuf)-1)
		return -EINVAL;
	if (copy_from_user(tbuf, buf, count))
		return -EFAULT;
	tbuf[count] = 0;
	simple_strtoul(tbuf, &ep, 0);
	if (*ep && *ep != '\n')
		return -EINVAL;
	/* Note that while we check that 'buf' holds a valid number,
	 * we always ignore the value and just flush everything.
	 * Making use of the number leads to races.
	 */

	now = seconds_since_boot();
	/* Always flush everything, so behave like cache_purge()
	 * Do this by advancing flush_time to the current time,
	 * or by one second if it has already reached the current time.
	 * Newly added cache entries will always have ->last_refresh greater
	 * that ->flush_time, so they don't get flushed prematurely.
	 */

	if (cd->flush_time >= now)
		now = cd->flush_time + 1;

	cd->flush_time = now;
	cd->nextcheck = now;
	cache_flush();

	if (cd->flush)
		cd->flush();

	*ppos += count;
	return count;
}

static ssize_t cache_read_procfs(struct file *filp, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE_DATA(file_inode(filp));

	return cache_read(filp, buf, count, ppos, cd);
}

static ssize_t cache_write_procfs(struct file *filp, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE_DATA(file_inode(filp));

	return cache_write(filp, buf, count, ppos, cd);
}

static __poll_t cache_poll_procfs(struct file *filp, poll_table *wait)
{
	struct cache_detail *cd = PDE_DATA(file_inode(filp));

	return cache_poll(filp, wait, cd);
}

static long cache_ioctl_procfs(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct cache_detail *cd = PDE_DATA(inode);

	return cache_ioctl(inode, filp, cmd, arg, cd);
}

static int cache_open_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return cache_open(inode, filp, cd);
}

static int cache_release_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return cache_release(inode, filp, cd);
}

static const struct proc_ops cache_channel_proc_ops = {
	.proc_lseek	= no_llseek,
	.proc_read	= cache_read_procfs,
	.proc_write	= cache_write_procfs,
	.proc_poll	= cache_poll_procfs,
	.proc_ioctl	= cache_ioctl_procfs, /* for FIONREAD */
	.proc_open	= cache_open_procfs,
	.proc_release	= cache_release_procfs,
};

static int content_open_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return content_open(inode, filp, cd);
}

static int content_release_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return content_release(inode, filp, cd);
}

static const struct proc_ops content_proc_ops = {
	.proc_open	= content_open_procfs,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= content_release_procfs,
};

static int open_flush_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return open_flush(inode, filp, cd);
}

static int release_flush_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE_DATA(inode);

	return release_flush(inode, filp, cd);
}

static ssize_t read_flush_procfs(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE_DATA(file_inode(filp));

	return read_flush(filp, buf, count, ppos, cd);
}

static ssize_t write_flush_procfs(struct file *filp,
				  const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE_DATA(file_inode(filp));

	return write_flush(filp, buf, count, ppos, cd);
}

static const struct proc_ops cache_flush_proc_ops = {
	.proc_open	= open_flush_procfs,
	.proc_read	= read_flush_procfs,
	.proc_write	= write_flush_procfs,
	.proc_release	= release_flush_procfs,
	.proc_lseek	= no_llseek,
};

static void remove_cache_proc_entries(struct cache_detail *cd)
{
	if (cd->procfs) {
		proc_remove(cd->procfs);
		cd->procfs = NULL;
	}
}

#ifdef CONFIG_PROC_FS
static int create_cache_proc_entries(struct cache_detail *cd, struct net *net)
{
	struct proc_dir_entry *p;
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	cd->procfs = proc_mkdir(cd->name, sn->proc_net_rpc);
	if (cd->procfs == NULL)
		goto out_nomem;

	p = proc_create_data("flush", S_IFREG | 0600,
			     cd->procfs, &cache_flush_proc_ops, cd);
	if (p == NULL)
		goto out_nomem;

	if (cd->cache_request || cd->cache_parse) {
		p = proc_create_data("channel", S_IFREG | 0600, cd->procfs,
				     &cache_channel_proc_ops, cd);
		if (p == NULL)
			goto out_nomem;
	}
	if (cd->cache_show) {
		p = proc_create_data("content", S_IFREG | 0400, cd->procfs,
				     &content_proc_ops, cd);
		if (p == NULL)
			goto out_nomem;
	}
	return 0;
out_nomem:
	remove_cache_proc_entries(cd);
	return -ENOMEM;
}
#else /* CONFIG_PROC_FS */
static int create_cache_proc_entries(struct cache_detail *cd, struct net *net)
{
	return 0;
}
#endif

void __init cache_initialize(void)
{
	INIT_DEFERRABLE_WORK(&cache_cleaner, do_cache_clean);
}

int cache_register_net(struct cache_detail *cd, struct net *net)
{
	int ret;

	sunrpc_init_cache_detail(cd);
	ret = create_cache_proc_entries(cd, net);
	if (ret)
		sunrpc_destroy_cache_detail(cd);
	return ret;
}
EXPORT_SYMBOL_GPL(cache_register_net);

void cache_unregister_net(struct cache_detail *cd, struct net *net)
{
	remove_cache_proc_entries(cd);
	sunrpc_destroy_cache_detail(cd);
}
EXPORT_SYMBOL_GPL(cache_unregister_net);

struct cache_detail *cache_create_net(const struct cache_detail *tmpl, struct net *net)
{
	struct cache_detail *cd;
	int i;

	cd = kmemdup(tmpl, sizeof(struct cache_detail), GFP_KERNEL);
	if (cd == NULL)
		return ERR_PTR(-ENOMEM);

	cd->hash_table = kcalloc(cd->hash_size, sizeof(struct hlist_head),
				 GFP_KERNEL);
	if (cd->hash_table == NULL) {
		kfree(cd);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < cd->hash_size; i++)
		INIT_HLIST_HEAD(&cd->hash_table[i]);
	cd->net = net;
	return cd;
}
EXPORT_SYMBOL_GPL(cache_create_net);

void cache_destroy_net(struct cache_detail *cd, struct net *net)
{
	kfree(cd->hash_table);
	kfree(cd);
}
EXPORT_SYMBOL_GPL(cache_destroy_net);

static ssize_t cache_read_pipefs(struct file *filp, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(file_inode(filp))->private;

	return cache_read(filp, buf, count, ppos, cd);
}

static ssize_t cache_write_pipefs(struct file *filp, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(file_inode(filp))->private;

	return cache_write(filp, buf, count, ppos, cd);
}

static __poll_t cache_poll_pipefs(struct file *filp, poll_table *wait)
{
	struct cache_detail *cd = RPC_I(file_inode(filp))->private;

	return cache_poll(filp, wait, cd);
}

static long cache_ioctl_pipefs(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct cache_detail *cd = RPC_I(inode)->private;

	return cache_ioctl(inode, filp, cmd, arg, cd);
}

static int cache_open_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return cache_open(inode, filp, cd);
}

static int cache_release_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return cache_release(inode, filp, cd);
}

const struct file_operations cache_file_operations_pipefs = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= cache_read_pipefs,
	.write		= cache_write_pipefs,
	.poll		= cache_poll_pipefs,
	.unlocked_ioctl	= cache_ioctl_pipefs, /* for FIONREAD */
	.open		= cache_open_pipefs,
	.release	= cache_release_pipefs,
};

static int content_open_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return content_open(inode, filp, cd);
}

static int content_release_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return content_release(inode, filp, cd);
}

const struct file_operations content_file_operations_pipefs = {
	.open		= content_open_pipefs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= content_release_pipefs,
};

static int open_flush_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return open_flush(inode, filp, cd);
}

static int release_flush_pipefs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = RPC_I(inode)->private;

	return release_flush(inode, filp, cd);
}

static ssize_t read_flush_pipefs(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(file_inode(filp))->private;

	return read_flush(filp, buf, count, ppos, cd);
}

static ssize_t write_flush_pipefs(struct file *filp,
				  const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(file_inode(filp))->private;

	return write_flush(filp, buf, count, ppos, cd);
}

const struct file_operations cache_flush_operations_pipefs = {
	.open		= open_flush_pipefs,
	.read		= read_flush_pipefs,
	.write		= write_flush_pipefs,
	.release	= release_flush_pipefs,
	.llseek		= no_llseek,
};

int sunrpc_cache_register_pipefs(struct dentry *parent,
				 const char *name, umode_t umode,
				 struct cache_detail *cd)
{
	struct dentry *dir = rpc_create_cache_dir(parent, name, umode, cd);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	cd->pipefs = dir;
	return 0;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_register_pipefs);

void sunrpc_cache_unregister_pipefs(struct cache_detail *cd)
{
	if (cd->pipefs) {
		rpc_remove_cache_dir(cd->pipefs);
		cd->pipefs = NULL;
	}
}
EXPORT_SYMBOL_GPL(sunrpc_cache_unregister_pipefs);

void sunrpc_cache_unhash(struct cache_detail *cd, struct cache_head *h)
{
	spin_lock(&cd->hash_lock);
	if (!hlist_unhashed(&h->cache_list)){
		sunrpc_begin_cache_remove_entry(h, cd);
		spin_unlock(&cd->hash_lock);
		sunrpc_end_cache_remove_entry(h, cd);
	} else
		spin_unlock(&cd->hash_lock);
}
EXPORT_SYMBOL_GPL(sunrpc_cache_unhash);
