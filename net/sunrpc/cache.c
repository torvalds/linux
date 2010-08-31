/*
 * net/sunrpc/cache.c
 *
 * Generic code for various authentication-related caches
 * used by sunrpc clients and servers.
 *
 * Copyright (C) 2002 Neil Brown <neilb@cse.unsw.edu.au>
 *
 * Released under terms in GPL version 2.  See COPYING.
 *
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
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/net.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <asm/ioctls.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#define	 RPCDBG_FACILITY RPCDBG_CACHE

static int cache_defer_req(struct cache_req *req, struct cache_head *item);
static void cache_revisit_request(struct cache_head *item);

static void cache_init(struct cache_head *h)
{
	time_t now = get_seconds();
	h->next = NULL;
	h->flags = 0;
	kref_init(&h->ref);
	h->expiry_time = now + CACHE_NEW_EXPIRY;
	h->last_refresh = now;
}

static inline int cache_is_expired(struct cache_detail *detail, struct cache_head *h)
{
	return  (h->expiry_time < get_seconds()) ||
		(detail->flush_time > h->last_refresh);
}

struct cache_head *sunrpc_cache_lookup(struct cache_detail *detail,
				       struct cache_head *key, int hash)
{
	struct cache_head **head,  **hp;
	struct cache_head *new = NULL, *freeme = NULL;

	head = &detail->hash_table[hash];

	read_lock(&detail->hash_lock);

	for (hp=head; *hp != NULL ; hp = &(*hp)->next) {
		struct cache_head *tmp = *hp;
		if (detail->match(tmp, key)) {
			if (cache_is_expired(detail, tmp))
				/* This entry is expired, we will discard it. */
				break;
			cache_get(tmp);
			read_unlock(&detail->hash_lock);
			return tmp;
		}
	}
	read_unlock(&detail->hash_lock);
	/* Didn't find anything, insert an empty entry */

	new = detail->alloc();
	if (!new)
		return NULL;
	/* must fully initialise 'new', else
	 * we might get lose if we need to
	 * cache_put it soon.
	 */
	cache_init(new);
	detail->init(new, key);

	write_lock(&detail->hash_lock);

	/* check if entry appeared while we slept */
	for (hp=head; *hp != NULL ; hp = &(*hp)->next) {
		struct cache_head *tmp = *hp;
		if (detail->match(tmp, key)) {
			if (cache_is_expired(detail, tmp)) {
				*hp = tmp->next;
				tmp->next = NULL;
				detail->entries --;
				freeme = tmp;
				break;
			}
			cache_get(tmp);
			write_unlock(&detail->hash_lock);
			cache_put(new, detail);
			return tmp;
		}
	}
	new->next = *head;
	*head = new;
	detail->entries++;
	cache_get(new);
	write_unlock(&detail->hash_lock);

	if (freeme)
		cache_put(freeme, detail);
	return new;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_lookup);


static void cache_dequeue(struct cache_detail *detail, struct cache_head *ch);

static void cache_fresh_locked(struct cache_head *head, time_t expiry)
{
	head->expiry_time = expiry;
	head->last_refresh = get_seconds();
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
	struct cache_head **head;
	struct cache_head *tmp;

	if (!test_bit(CACHE_VALID, &old->flags)) {
		write_lock(&detail->hash_lock);
		if (!test_bit(CACHE_VALID, &old->flags)) {
			if (test_bit(CACHE_NEGATIVE, &new->flags))
				set_bit(CACHE_NEGATIVE, &old->flags);
			else
				detail->update(old, new);
			cache_fresh_locked(old, new->expiry_time);
			write_unlock(&detail->hash_lock);
			cache_fresh_unlocked(old, detail);
			return old;
		}
		write_unlock(&detail->hash_lock);
	}
	/* We need to insert a new entry */
	tmp = detail->alloc();
	if (!tmp) {
		cache_put(old, detail);
		return NULL;
	}
	cache_init(tmp);
	detail->init(tmp, old);
	head = &detail->hash_table[hash];

	write_lock(&detail->hash_lock);
	if (test_bit(CACHE_NEGATIVE, &new->flags))
		set_bit(CACHE_NEGATIVE, &tmp->flags);
	else
		detail->update(tmp, new);
	tmp->next = *head;
	*head = tmp;
	detail->entries++;
	cache_get(tmp);
	cache_fresh_locked(tmp, new->expiry_time);
	cache_fresh_locked(old, 0);
	write_unlock(&detail->hash_lock);
	cache_fresh_unlocked(tmp, detail);
	cache_fresh_unlocked(old, detail);
	cache_put(old, detail);
	return tmp;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_update);

static int cache_make_upcall(struct cache_detail *cd, struct cache_head *h)
{
	if (!cd->cache_upcall)
		return -EINVAL;
	return cd->cache_upcall(cd, h);
}

static inline int cache_is_valid(struct cache_detail *detail, struct cache_head *h)
{
	if (!test_bit(CACHE_VALID, &h->flags))
		return -EAGAIN;
	else {
		/* entry is valid */
		if (test_bit(CACHE_NEGATIVE, &h->flags))
			return -ENOENT;
		else
			return 0;
	}
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
	long refresh_age, age;

	/* First decide return status as best we can */
	rv = cache_is_valid(detail, h);

	/* now see if we want to start an upcall */
	refresh_age = (h->expiry_time - h->last_refresh);
	age = get_seconds() - h->last_refresh;

	if (rqstp == NULL) {
		if (rv == -EAGAIN)
			rv = -ENOENT;
	} else if (rv == -EAGAIN || age > refresh_age/2) {
		dprintk("RPC:       Want update, refage=%ld, age=%ld\n",
				refresh_age, age);
		if (!test_and_set_bit(CACHE_PENDING, &h->flags)) {
			switch (cache_make_upcall(detail, h)) {
			case -EINVAL:
				clear_bit(CACHE_PENDING, &h->flags);
				cache_revisit_request(h);
				if (rv == -EAGAIN) {
					set_bit(CACHE_NEGATIVE, &h->flags);
					cache_fresh_locked(h, get_seconds()+CACHE_NEW_EXPIRY);
					cache_fresh_unlocked(h, detail);
					rv = -ENOENT;
				}
				break;

			case -EAGAIN:
				clear_bit(CACHE_PENDING, &h->flags);
				cache_revisit_request(h);
				break;
			}
		}
	}

	if (rv == -EAGAIN) {
		if (cache_defer_req(rqstp, h) < 0) {
			/* Request is not deferred */
			rv = cache_is_valid(detail, h);
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
 * Each time clean_cache is called it finds the next non-empty entry
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

static void sunrpc_init_cache_detail(struct cache_detail *cd)
{
	rwlock_init(&cd->hash_lock);
	INIT_LIST_HEAD(&cd->queue);
	spin_lock(&cache_list_lock);
	cd->nextcheck = 0;
	cd->entries = 0;
	atomic_set(&cd->readers, 0);
	cd->last_close = 0;
	cd->last_warn = -1;
	list_add(&cd->others, &cache_list);
	spin_unlock(&cache_list_lock);

	/* start the cleaning process */
	schedule_delayed_work(&cache_cleaner, 0);
}

static void sunrpc_destroy_cache_detail(struct cache_detail *cd)
{
	cache_purge(cd);
	spin_lock(&cache_list_lock);
	write_lock(&cd->hash_lock);
	if (cd->entries || atomic_read(&cd->inuse)) {
		write_unlock(&cd->hash_lock);
		spin_unlock(&cache_list_lock);
		goto out;
	}
	if (current_detail == cd)
		current_detail = NULL;
	list_del_init(&cd->others);
	write_unlock(&cd->hash_lock);
	spin_unlock(&cache_list_lock);
	if (list_empty(&cache_list)) {
		/* module must be being unloaded so its safe to kill the worker */
		cancel_delayed_work_sync(&cache_cleaner);
	}
	return;
out:
	printk(KERN_ERR "nfsd: failed to unregister %s cache\n", cd->name);
}

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
		if (current_detail->nextcheck > get_seconds())
			current_index = current_detail->hash_size;
		else {
			current_index = 0;
			current_detail->nextcheck = get_seconds()+30*60;
		}
	}

	/* find a non-empty bucket in the table */
	while (current_detail &&
	       current_index < current_detail->hash_size &&
	       current_detail->hash_table[current_index] == NULL)
		current_index++;

	/* find a cleanable entry in the bucket and clean it, or set to next bucket */

	if (current_detail && current_index < current_detail->hash_size) {
		struct cache_head *ch, **cp;
		struct cache_detail *d;

		write_lock(&current_detail->hash_lock);

		/* Ok, now to clean this strand */

		cp = & current_detail->hash_table[current_index];
		for (ch = *cp ; ch ; cp = & ch->next, ch = *cp) {
			if (current_detail->nextcheck > ch->expiry_time)
				current_detail->nextcheck = ch->expiry_time+1;
			if (!cache_is_expired(current_detail, ch))
				continue;

			*cp = ch->next;
			ch->next = NULL;
			current_detail->entries--;
			rv = 1;
			break;
		}

		write_unlock(&current_detail->hash_lock);
		d = current_detail;
		if (!ch)
			current_index ++;
		spin_unlock(&cache_list_lock);
		if (ch) {
			if (test_and_clear_bit(CACHE_PENDING, &ch->flags))
				cache_dequeue(current_detail, ch);
			cache_revisit_request(ch);
			cache_put(ch, d);
		}
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
		schedule_delayed_work(&cache_cleaner, delay);
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
	detail->flush_time = LONG_MAX;
	detail->nextcheck = get_seconds();
	cache_flush();
	detail->flush_time = 1;
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
static struct list_head cache_defer_hash[DFR_HASHSIZE];
static int cache_defer_cnt;

static int cache_defer_req(struct cache_req *req, struct cache_head *item)
{
	struct cache_deferred_req *dreq, *discard;
	int hash = DFR_HASH(item);

	if (cache_defer_cnt >= DFR_MAX) {
		/* too much in the cache, randomly drop this one,
		 * or continue and drop the oldest below
		 */
		if (net_random()&1)
			return -ENOMEM;
	}
	dreq = req->defer(req);
	if (dreq == NULL)
		return -ENOMEM;

	dreq->item = item;

	spin_lock(&cache_defer_lock);

	list_add(&dreq->recent, &cache_defer_list);

	if (cache_defer_hash[hash].next == NULL)
		INIT_LIST_HEAD(&cache_defer_hash[hash]);
	list_add(&dreq->hash, &cache_defer_hash[hash]);

	/* it is in, now maybe clean up */
	discard = NULL;
	if (++cache_defer_cnt > DFR_MAX) {
		discard = list_entry(cache_defer_list.prev,
				     struct cache_deferred_req, recent);
		list_del_init(&discard->recent);
		list_del_init(&discard->hash);
		cache_defer_cnt--;
	}
	spin_unlock(&cache_defer_lock);

	if (discard)
		/* there was one too many */
		discard->revisit(discard, 1);

	if (!test_bit(CACHE_PENDING, &item->flags)) {
		/* must have just been validated... */
		cache_revisit_request(item);
		return -EAGAIN;
	}
	return 0;
}

static void cache_revisit_request(struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	struct list_head pending;

	struct list_head *lp;
	int hash = DFR_HASH(item);

	INIT_LIST_HEAD(&pending);
	spin_lock(&cache_defer_lock);

	lp = cache_defer_hash[hash].next;
	if (lp) {
		while (lp != &cache_defer_hash[hash]) {
			dreq = list_entry(lp, struct cache_deferred_req, hash);
			lp = lp->next;
			if (dreq->item == item) {
				list_del_init(&dreq->hash);
				list_move(&dreq->recent, &pending);
				cache_defer_cnt--;
			}
		}
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
			list_del_init(&dreq->hash);
			list_move(&dreq->recent, &pending);
			cache_defer_cnt--;
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
 * We have a magic /proc file - /proc/sunrpc/<cachename>/channel.
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

static ssize_t cache_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *ppos, struct cache_detail *cd)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_request *rq;
	struct inode *inode = filp->f_path.dentry->d_inode;
	int err;

	if (count == 0)
		return 0;

	mutex_lock(&inode->i_mutex); /* protect against multiple concurrent
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
		mutex_unlock(&inode->i_mutex);
		BUG_ON(rp->offset);
		return 0;
	}
	rq = container_of(rp->q.list.next, struct cache_request, q.list);
	BUG_ON(rq->q.reader);
	if (rp->offset == 0)
		rq->readers++;
	spin_unlock(&queue_lock);

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
	mutex_unlock(&inode->i_mutex);
	return err ? err :  count;
}

static ssize_t cache_do_downcall(char *kaddr, const char __user *buf,
				 size_t count, struct cache_detail *cd)
{
	ssize_t ret;

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

	if (count >= PAGE_CACHE_SIZE)
		goto out_slow;

	page = find_or_create_page(mapping, 0, GFP_KERNEL);
	if (!page)
		goto out_slow;

	kaddr = kmap(page);
	ret = cache_do_downcall(kaddr, buf, count, cd);
	kunmap(page);
	unlock_page(page);
	page_cache_release(page);
	return ret;
out_slow:
	return cache_slow_downcall(buf, count, cd);
}

static ssize_t cache_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos,
			   struct cache_detail *cd)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = filp->f_path.dentry->d_inode;
	ssize_t ret = -EINVAL;

	if (!cd->cache_parse)
		goto out;

	mutex_lock(&inode->i_mutex);
	ret = cache_downcall(mapping, buf, count, cd);
	mutex_unlock(&inode->i_mutex);
out:
	return ret;
}

static DECLARE_WAIT_QUEUE_HEAD(queue_wait);

static unsigned int cache_poll(struct file *filp, poll_table *wait,
			       struct cache_detail *cd)
{
	unsigned int mask;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;

	poll_wait(filp, &queue_wait, wait);

	/* alway allow write */
	mask = POLL_OUT | POLLWRNORM;

	if (!rp)
		return mask;

	spin_lock(&queue_lock);

	for (cq= &rp->q; &cq->list != &cd->queue;
	     cq = list_entry(cq->list.next, struct cache_queue, list))
		if (!cq->reader) {
			mask |= POLLIN | POLLRDNORM;
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
		if (!rp)
			return -ENOMEM;
		rp->offset = 0;
		rp->q.reader = 1;
		atomic_inc(&cd->readers);
		spin_lock(&queue_lock);
		list_add(&rp->q.list, &cd->queue);
		spin_unlock(&queue_lock);
	}
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

		cd->last_close = get_seconds();
		atomic_dec(&cd->readers);
	}
	module_put(cd->owner);
	return 0;
}



static void cache_dequeue(struct cache_detail *detail, struct cache_head *ch)
{
	struct cache_queue *cq;
	spin_lock(&queue_lock);
	list_for_each_entry(cq, &detail->queue, list)
		if (!cq->reader) {
			struct cache_request *cr = container_of(cq, struct cache_request, q);
			if (cr->item != ch)
				continue;
			if (cr->readers != 0)
				continue;
			list_del(&cr->q.list);
			spin_unlock(&queue_lock);
			cache_put(cr->item, detail);
			kfree(cr->buf);
			kfree(cr);
			return;
		}
	spin_unlock(&queue_lock);
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
	char c;

	if (len < 0) return;

	while ((c=*str++) && len)
		switch(c) {
		case ' ':
		case '\t':
		case '\n':
		case '\\':
			if (len >= 4) {
				*bp++ = '\\';
				*bp++ = '0' + ((c & 0300)>>6);
				*bp++ = '0' + ((c & 0070)>>3);
				*bp++ = '0' + ((c & 0007)>>0);
			}
			len -= 4;
			break;
		default:
			*bp++ = c;
			len--;
		}
	if (c || len <1) len = -1;
	else {
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
			unsigned char c = *buf++;
			*bp++ = '0' + ((c&0xf0)>>4) + (c>=0xa0)*('a'-'9'-1);
			*bp++ = '0' + (c&0x0f) + ((c&0x0f)>=0x0a)*('a'-'9'-1);
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

/*
 * register an upcall request to user-space and queue it up for read() by the
 * upcall daemon.
 *
 * Each request is at most one page long.
 */
int sunrpc_cache_pipe_upcall(struct cache_detail *detail, struct cache_head *h,
		void (*cache_request)(struct cache_detail *,
				      struct cache_head *,
				      char **,
				      int *))
{

	char *buf;
	struct cache_request *crq;
	char *bp;
	int len;

	if (atomic_read(&detail->readers) == 0 &&
	    detail->last_close < get_seconds() - 30) {
			warn_no_listener(detail);
			return -EINVAL;
	}

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -EAGAIN;

	crq = kmalloc(sizeof (*crq), GFP_KERNEL);
	if (!crq) {
		kfree(buf);
		return -EAGAIN;
	}

	bp = buf; len = PAGE_SIZE;

	cache_request(detail, h, &bp, &len);

	if (len < 0) {
		kfree(buf);
		kfree(crq);
		return -EAGAIN;
	}
	crq->q.reader = 0;
	crq->item = cache_get(h);
	crq->buf = buf;
	crq->len = PAGE_SIZE - len;
	crq->readers = 0;
	spin_lock(&queue_lock);
	list_add_tail(&crq->q.list, &detail->queue);
	spin_unlock(&queue_lock);
	wake_up(&queue_wait);
	return 0;
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

#define isodigit(c) (isdigit(c) && c <= '7')
int qword_get(char **bpp, char *dest, int bufsize)
{
	/* return bytes copied, or -1 on error */
	char *bp = *bpp;
	int len = 0;

	while (*bp == ' ') bp++;

	if (bp[0] == '\\' && bp[1] == 'x') {
		/* HEX STRING */
		bp += 2;
		while (isxdigit(bp[0]) && isxdigit(bp[1]) && len < bufsize) {
			int byte = isdigit(*bp) ? *bp-'0' : toupper(*bp)-'A'+10;
			bp++;
			byte <<= 4;
			byte |= isdigit(*bp) ? *bp-'0' : toupper(*bp)-'A'+10;
			*dest++ = byte;
			bp++;
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
 * support /proc/sunrpc/cache/$CACHENAME/content
 * as a seqfile.
 * We call ->cache_show passing NULL for the item to
 * get a header, then pass each real item in the cache
 */

struct handle {
	struct cache_detail *cd;
};

static void *c_start(struct seq_file *m, loff_t *pos)
	__acquires(cd->hash_lock)
{
	loff_t n = *pos;
	unsigned hash, entry;
	struct cache_head *ch;
	struct cache_detail *cd = ((struct handle*)m->private)->cd;


	read_lock(&cd->hash_lock);
	if (!n--)
		return SEQ_START_TOKEN;
	hash = n >> 32;
	entry = n & ((1LL<<32) - 1);

	for (ch=cd->hash_table[hash]; ch; ch=ch->next)
		if (!entry--)
			return ch;
	n &= ~((1LL<<32) - 1);
	do {
		hash++;
		n += 1LL<<32;
	} while(hash < cd->hash_size &&
		cd->hash_table[hash]==NULL);
	if (hash >= cd->hash_size)
		return NULL;
	*pos = n+1;
	return cd->hash_table[hash];
}

static void *c_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct cache_head *ch = p;
	int hash = (*pos >> 32);
	struct cache_detail *cd = ((struct handle*)m->private)->cd;

	if (p == SEQ_START_TOKEN)
		hash = 0;
	else if (ch->next == NULL) {
		hash++;
		*pos += 1LL<<32;
	} else {
		++*pos;
		return ch->next;
	}
	*pos &= ~((1LL<<32) - 1);
	while (hash < cd->hash_size &&
	       cd->hash_table[hash] == NULL) {
		hash++;
		*pos += 1LL<<32;
	}
	if (hash >= cd->hash_size)
		return NULL;
	++*pos;
	return cd->hash_table[hash];
}

static void c_stop(struct seq_file *m, void *p)
	__releases(cd->hash_lock)
{
	struct cache_detail *cd = ((struct handle*)m->private)->cd;
	read_unlock(&cd->hash_lock);
}

static int c_show(struct seq_file *m, void *p)
{
	struct cache_head *cp = p;
	struct cache_detail *cd = ((struct handle*)m->private)->cd;

	if (p == SEQ_START_TOKEN)
		return cd->cache_show(m, cd, NULL);

	ifdebug(CACHE)
		seq_printf(m, "# expiry=%ld refcnt=%d flags=%lx\n",
			   cp->expiry_time, atomic_read(&cp->ref.refcount), cp->flags);
	cache_get(cp);
	if (cache_check(cd, cp, NULL))
		/* cache_check does a cache_put on failure */
		seq_printf(m, "# ");
	else
		cache_put(cp, cd);

	return cd->cache_show(m, cd, cp);
}

static const struct seq_operations cache_content_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show,
};

static int content_open(struct inode *inode, struct file *file,
			struct cache_detail *cd)
{
	struct handle *han;

	if (!cd || !try_module_get(cd->owner))
		return -EACCES;
	han = __seq_open_private(file, &cache_content_op, sizeof(*han));
	if (han == NULL) {
		module_put(cd->owner);
		return -ENOMEM;
	}

	han->cd = cd;
	return 0;
}

static int content_release(struct inode *inode, struct file *file,
		struct cache_detail *cd)
{
	int ret = seq_release_private(inode, file);
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
	char tbuf[20];
	unsigned long p = *ppos;
	size_t len;

	sprintf(tbuf, "%lu\n", cd->flush_time);
	len = strlen(tbuf);
	if (p >= len)
		return 0;
	len -= p;
	if (len > count)
		len = count;
	if (copy_to_user(buf, (void*)(tbuf+p), len))
		return -EFAULT;
	*ppos += len;
	return len;
}

static ssize_t write_flush(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos,
			   struct cache_detail *cd)
{
	char tbuf[20];
	char *ep;
	long flushtime;
	if (*ppos || count > sizeof(tbuf)-1)
		return -EINVAL;
	if (copy_from_user(tbuf, buf, count))
		return -EFAULT;
	tbuf[count] = 0;
	flushtime = simple_strtoul(tbuf, &ep, 0);
	if (*ep && *ep != '\n')
		return -EINVAL;

	cd->flush_time = flushtime;
	cd->nextcheck = get_seconds();
	cache_flush();

	*ppos += count;
	return count;
}

static ssize_t cache_read_procfs(struct file *filp, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(filp->f_path.dentry->d_inode)->data;

	return cache_read(filp, buf, count, ppos, cd);
}

static ssize_t cache_write_procfs(struct file *filp, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(filp->f_path.dentry->d_inode)->data;

	return cache_write(filp, buf, count, ppos, cd);
}

static unsigned int cache_poll_procfs(struct file *filp, poll_table *wait)
{
	struct cache_detail *cd = PDE(filp->f_path.dentry->d_inode)->data;

	return cache_poll(filp, wait, cd);
}

static long cache_ioctl_procfs(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	long ret;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct cache_detail *cd = PDE(inode)->data;

	lock_kernel();
	ret = cache_ioctl(inode, filp, cmd, arg, cd);
	unlock_kernel();

	return ret;
}

static int cache_open_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return cache_open(inode, filp, cd);
}

static int cache_release_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return cache_release(inode, filp, cd);
}

static const struct file_operations cache_file_operations_procfs = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= cache_read_procfs,
	.write		= cache_write_procfs,
	.poll		= cache_poll_procfs,
	.unlocked_ioctl	= cache_ioctl_procfs, /* for FIONREAD */
	.open		= cache_open_procfs,
	.release	= cache_release_procfs,
};

static int content_open_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return content_open(inode, filp, cd);
}

static int content_release_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return content_release(inode, filp, cd);
}

static const struct file_operations content_file_operations_procfs = {
	.open		= content_open_procfs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= content_release_procfs,
};

static int open_flush_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return open_flush(inode, filp, cd);
}

static int release_flush_procfs(struct inode *inode, struct file *filp)
{
	struct cache_detail *cd = PDE(inode)->data;

	return release_flush(inode, filp, cd);
}

static ssize_t read_flush_procfs(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(filp->f_path.dentry->d_inode)->data;

	return read_flush(filp, buf, count, ppos, cd);
}

static ssize_t write_flush_procfs(struct file *filp,
				  const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(filp->f_path.dentry->d_inode)->data;

	return write_flush(filp, buf, count, ppos, cd);
}

static const struct file_operations cache_flush_operations_procfs = {
	.open		= open_flush_procfs,
	.read		= read_flush_procfs,
	.write		= write_flush_procfs,
	.release	= release_flush_procfs,
};

static void remove_cache_proc_entries(struct cache_detail *cd)
{
	if (cd->u.procfs.proc_ent == NULL)
		return;
	if (cd->u.procfs.flush_ent)
		remove_proc_entry("flush", cd->u.procfs.proc_ent);
	if (cd->u.procfs.channel_ent)
		remove_proc_entry("channel", cd->u.procfs.proc_ent);
	if (cd->u.procfs.content_ent)
		remove_proc_entry("content", cd->u.procfs.proc_ent);
	cd->u.procfs.proc_ent = NULL;
	remove_proc_entry(cd->name, proc_net_rpc);
}

#ifdef CONFIG_PROC_FS
static int create_cache_proc_entries(struct cache_detail *cd)
{
	struct proc_dir_entry *p;

	cd->u.procfs.proc_ent = proc_mkdir(cd->name, proc_net_rpc);
	if (cd->u.procfs.proc_ent == NULL)
		goto out_nomem;
	cd->u.procfs.channel_ent = NULL;
	cd->u.procfs.content_ent = NULL;

	p = proc_create_data("flush", S_IFREG|S_IRUSR|S_IWUSR,
			     cd->u.procfs.proc_ent,
			     &cache_flush_operations_procfs, cd);
	cd->u.procfs.flush_ent = p;
	if (p == NULL)
		goto out_nomem;

	if (cd->cache_upcall || cd->cache_parse) {
		p = proc_create_data("channel", S_IFREG|S_IRUSR|S_IWUSR,
				     cd->u.procfs.proc_ent,
				     &cache_file_operations_procfs, cd);
		cd->u.procfs.channel_ent = p;
		if (p == NULL)
			goto out_nomem;
	}
	if (cd->cache_show) {
		p = proc_create_data("content", S_IFREG|S_IRUSR|S_IWUSR,
				cd->u.procfs.proc_ent,
				&content_file_operations_procfs, cd);
		cd->u.procfs.content_ent = p;
		if (p == NULL)
			goto out_nomem;
	}
	return 0;
out_nomem:
	remove_cache_proc_entries(cd);
	return -ENOMEM;
}
#else /* CONFIG_PROC_FS */
static int create_cache_proc_entries(struct cache_detail *cd)
{
	return 0;
}
#endif

void __init cache_initialize(void)
{
	INIT_DELAYED_WORK_DEFERRABLE(&cache_cleaner, do_cache_clean);
}

int cache_register(struct cache_detail *cd)
{
	int ret;

	sunrpc_init_cache_detail(cd);
	ret = create_cache_proc_entries(cd);
	if (ret)
		sunrpc_destroy_cache_detail(cd);
	return ret;
}
EXPORT_SYMBOL_GPL(cache_register);

void cache_unregister(struct cache_detail *cd)
{
	remove_cache_proc_entries(cd);
	sunrpc_destroy_cache_detail(cd);
}
EXPORT_SYMBOL_GPL(cache_unregister);

static ssize_t cache_read_pipefs(struct file *filp, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(filp->f_path.dentry->d_inode)->private;

	return cache_read(filp, buf, count, ppos, cd);
}

static ssize_t cache_write_pipefs(struct file *filp, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(filp->f_path.dentry->d_inode)->private;

	return cache_write(filp, buf, count, ppos, cd);
}

static unsigned int cache_poll_pipefs(struct file *filp, poll_table *wait)
{
	struct cache_detail *cd = RPC_I(filp->f_path.dentry->d_inode)->private;

	return cache_poll(filp, wait, cd);
}

static long cache_ioctl_pipefs(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct cache_detail *cd = RPC_I(inode)->private;
	long ret;

	lock_kernel();
	ret = cache_ioctl(inode, filp, cmd, arg, cd);
	unlock_kernel();

	return ret;
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
	struct cache_detail *cd = RPC_I(filp->f_path.dentry->d_inode)->private;

	return read_flush(filp, buf, count, ppos, cd);
}

static ssize_t write_flush_pipefs(struct file *filp,
				  const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct cache_detail *cd = RPC_I(filp->f_path.dentry->d_inode)->private;

	return write_flush(filp, buf, count, ppos, cd);
}

const struct file_operations cache_flush_operations_pipefs = {
	.open		= open_flush_pipefs,
	.read		= read_flush_pipefs,
	.write		= write_flush_pipefs,
	.release	= release_flush_pipefs,
};

int sunrpc_cache_register_pipefs(struct dentry *parent,
				 const char *name, mode_t umode,
				 struct cache_detail *cd)
{
	struct qstr q;
	struct dentry *dir;
	int ret = 0;

	sunrpc_init_cache_detail(cd);
	q.name = name;
	q.len = strlen(name);
	q.hash = full_name_hash(q.name, q.len);
	dir = rpc_create_cache_dir(parent, &q, umode, cd);
	if (!IS_ERR(dir))
		cd->u.pipefs.dir = dir;
	else {
		sunrpc_destroy_cache_detail(cd);
		ret = PTR_ERR(dir);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sunrpc_cache_register_pipefs);

void sunrpc_cache_unregister_pipefs(struct cache_detail *cd)
{
	rpc_remove_cache_dir(cd->u.pipefs.dir);
	cd->u.pipefs.dir = NULL;
	sunrpc_destroy_cache_detail(cd);
}
EXPORT_SYMBOL_GPL(sunrpc_cache_unregister_pipefs);

