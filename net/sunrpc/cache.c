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
#include <asm/ioctls.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/stats.h>

#define	 RPCDBG_FACILITY RPCDBG_CACHE

static void cache_defer_req(struct cache_req *req, struct cache_head *item);
static void cache_revisit_request(struct cache_head *item);

void cache_init(struct cache_head *h)
{
	time_t now = get_seconds();
	h->next = NULL;
	h->flags = 0;
	atomic_set(&h->refcnt, 1);
	h->expiry_time = now + CACHE_NEW_EXPIRY;
	h->last_refresh = now;
}


static int cache_make_upcall(struct cache_detail *detail, struct cache_head *h);
/*
 * This is the generic cache management routine for all
 * the authentication caches.
 * It checks the currency of a cache item and will (later)
 * initiate an upcall to fill it if needed.
 *
 *
 * Returns 0 if the cache_head can be used, or cache_puts it and returns
 * -EAGAIN if upcall is pending,
 * -ENOENT if cache entry was negative
 */
int cache_check(struct cache_detail *detail,
		    struct cache_head *h, struct cache_req *rqstp)
{
	int rv;
	long refresh_age, age;

	/* First decide return status as best we can */
	if (!test_bit(CACHE_VALID, &h->flags) ||
	    h->expiry_time < get_seconds())
		rv = -EAGAIN;
	else if (detail->flush_time > h->last_refresh)
		rv = -EAGAIN;
	else {
		/* entry is valid */
		if (test_bit(CACHE_NEGATIVE, &h->flags))
			rv = -ENOENT;
		else rv = 0;
	}

	/* now see if we want to start an upcall */
	refresh_age = (h->expiry_time - h->last_refresh);
	age = get_seconds() - h->last_refresh;

	if (rqstp == NULL) {
		if (rv == -EAGAIN)
			rv = -ENOENT;
	} else if (rv == -EAGAIN || age > refresh_age/2) {
		dprintk("Want update, refage=%ld, age=%ld\n", refresh_age, age);
		if (!test_and_set_bit(CACHE_PENDING, &h->flags)) {
			switch (cache_make_upcall(detail, h)) {
			case -EINVAL:
				clear_bit(CACHE_PENDING, &h->flags);
				if (rv == -EAGAIN) {
					set_bit(CACHE_NEGATIVE, &h->flags);
					cache_fresh(detail, h, get_seconds()+CACHE_NEW_EXPIRY);
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

	if (rv == -EAGAIN)
		cache_defer_req(rqstp, h);

	if (rv && h)
		detail->cache_put(h, detail);
	return rv;
}

static void queue_loose(struct cache_detail *detail, struct cache_head *ch);

void cache_fresh(struct cache_detail *detail,
		 struct cache_head *head, time_t expiry)
{

	head->expiry_time = expiry;
	head->last_refresh = get_seconds();
	if (!test_and_set_bit(CACHE_VALID, &head->flags))
		cache_revisit_request(head);
	if (test_and_clear_bit(CACHE_PENDING, &head->flags))
		queue_loose(detail, head);
}

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

static struct file_operations cache_file_operations;
static struct file_operations content_file_operations;
static struct file_operations cache_flush_operations;

static void do_cache_clean(void *data);
static DECLARE_WORK(cache_cleaner, do_cache_clean, NULL);

void cache_register(struct cache_detail *cd)
{
	cd->proc_ent = proc_mkdir(cd->name, proc_net_rpc);
	if (cd->proc_ent) {
		struct proc_dir_entry *p;
		cd->proc_ent->owner = cd->owner;
		cd->channel_ent = cd->content_ent = NULL;
		
 		p = create_proc_entry("flush", S_IFREG|S_IRUSR|S_IWUSR,
 				      cd->proc_ent);
		cd->flush_ent =  p;
 		if (p) {
 			p->proc_fops = &cache_flush_operations;
 			p->owner = cd->owner;
 			p->data = cd;
 		}
 
		if (cd->cache_request || cd->cache_parse) {
			p = create_proc_entry("channel", S_IFREG|S_IRUSR|S_IWUSR,
					      cd->proc_ent);
			cd->channel_ent = p;
			if (p) {
				p->proc_fops = &cache_file_operations;
				p->owner = cd->owner;
				p->data = cd;
			}
		}
 		if (cd->cache_show) {
 			p = create_proc_entry("content", S_IFREG|S_IRUSR|S_IWUSR,
 					      cd->proc_ent);
			cd->content_ent = p;
 			if (p) {
 				p->proc_fops = &content_file_operations;
 				p->owner = cd->owner;
 				p->data = cd;
 			}
 		}
	}
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
	schedule_work(&cache_cleaner);
}

int cache_unregister(struct cache_detail *cd)
{
	cache_purge(cd);
	spin_lock(&cache_list_lock);
	write_lock(&cd->hash_lock);
	if (cd->entries || atomic_read(&cd->inuse)) {
		write_unlock(&cd->hash_lock);
		spin_unlock(&cache_list_lock);
		return -EBUSY;
	}
	if (current_detail == cd)
		current_detail = NULL;
	list_del_init(&cd->others);
	write_unlock(&cd->hash_lock);
	spin_unlock(&cache_list_lock);
	if (cd->proc_ent) {
		if (cd->flush_ent)
			remove_proc_entry("flush", cd->proc_ent);
		if (cd->channel_ent)
			remove_proc_entry("channel", cd->proc_ent);
		if (cd->content_ent)
			remove_proc_entry("content", cd->proc_ent);

		cd->proc_ent = NULL;
		remove_proc_entry(cd->name, proc_net_rpc);
	}
	if (list_empty(&cache_list)) {
		/* module must be being unloaded so its safe to kill the worker */
		cancel_delayed_work(&cache_cleaner);
		flush_scheduled_work();
	}
	return 0;
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
		ch = *cp;
		for (; ch; cp= & ch->next, ch= *cp) {
			if (current_detail->nextcheck > ch->expiry_time)
				current_detail->nextcheck = ch->expiry_time+1;
			if (ch->expiry_time >= get_seconds()
			    && ch->last_refresh >= current_detail->flush_time
				)
				continue;
			if (test_and_clear_bit(CACHE_PENDING, &ch->flags))
				queue_loose(current_detail, ch);

			if (atomic_read(&ch->refcnt) == 1)
				break;
		}
		if (ch) {
			*cp = ch->next;
			ch->next = NULL;
			current_detail->entries--;
			rv = 1;
		}
		write_unlock(&current_detail->hash_lock);
		d = current_detail;
		if (!ch)
			current_index ++;
		spin_unlock(&cache_list_lock);
		if (ch)
			d->cache_put(ch, d);
	} else
		spin_unlock(&cache_list_lock);

	return rv;
}

/*
 * We want to regularly clean the cache, so we need to schedule some work ...
 */
static void do_cache_clean(void *data)
{
	int delay = 5;
	if (cache_clean() == -1)
		delay = 30*HZ;

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

void cache_purge(struct cache_detail *detail)
{
	detail->flush_time = LONG_MAX;
	detail->nextcheck = get_seconds();
	cache_flush();
	detail->flush_time = 1;
}



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

static void cache_defer_req(struct cache_req *req, struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	int hash = DFR_HASH(item);

	dreq = req->defer(req);
	if (dreq == NULL)
		return;

	dreq->item = item;
	dreq->recv_time = get_seconds();

	spin_lock(&cache_defer_lock);

	list_add(&dreq->recent, &cache_defer_list);

	if (cache_defer_hash[hash].next == NULL)
		INIT_LIST_HEAD(&cache_defer_hash[hash]);
	list_add(&dreq->hash, &cache_defer_hash[hash]);

	/* it is in, now maybe clean up */
	dreq = NULL;
	if (++cache_defer_cnt > DFR_MAX) {
		/* too much in the cache, randomly drop
		 * first or last
		 */
		if (net_random()&1) 
			dreq = list_entry(cache_defer_list.next,
					  struct cache_deferred_req,
					  recent);
		else
			dreq = list_entry(cache_defer_list.prev,
					  struct cache_deferred_req,
					  recent);
		list_del(&dreq->recent);
		list_del(&dreq->hash);
		cache_defer_cnt--;
	}
	spin_unlock(&cache_defer_lock);

	if (dreq) {
		/* there was one too many */
		dreq->revisit(dreq, 1);
	}
	if (test_bit(CACHE_VALID, &item->flags)) {
		/* must have just been validated... */
		cache_revisit_request(item);
	}
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
				list_del(&dreq->hash);
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
			list_del(&dreq->hash);
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
 * We have a magic /proc file - /proc/sunrpc/cache
 * On read, you get a full request, or block
 * On write, an update request is processed
 * Poll works if anything to read, and always allows write
 *
 * Implemented by linked list of requests.  Each open file has 
 * a ->private that also exists in this list.  New request are added
 * to the end and may wakeup and preceding readers.
 * New readers are added to the head.  If, on read, an item is found with
 * CACHE_UPCALLING clear, we free it from the list.
 *
 */

static DEFINE_SPINLOCK(queue_lock);
static DECLARE_MUTEX(queue_io_sem);

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

static ssize_t
cache_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_request *rq;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;
	int err;

	if (count == 0)
		return 0;

	down(&queue_io_sem); /* protect against multiple concurrent
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
		up(&queue_io_sem);
		if (rp->offset)
			BUG();
		return 0;
	}
	rq = container_of(rp->q.list.next, struct cache_request, q.list);
	if (rq->q.reader) BUG();
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
			cd->cache_put(rq->item, cd);
			kfree(rq->buf);
			kfree(rq);
		} else
			spin_unlock(&queue_lock);
	}
	if (err == -EAGAIN)
		goto again;
	up(&queue_io_sem);
	return err ? err :  count;
}

static char write_buf[8192]; /* protected by queue_io_sem */

static ssize_t
cache_write(struct file *filp, const char __user *buf, size_t count,
	    loff_t *ppos)
{
	int err;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;

	if (count == 0)
		return 0;
	if (count >= sizeof(write_buf))
		return -EINVAL;

	down(&queue_io_sem);

	if (copy_from_user(write_buf, buf, count)) {
		up(&queue_io_sem);
		return -EFAULT;
	}
	write_buf[count] = '\0';
	if (cd->cache_parse)
		err = cd->cache_parse(cd, write_buf, count);
	else
		err = -EINVAL;

	up(&queue_io_sem);
	return err ? err : count;
}

static DECLARE_WAIT_QUEUE_HEAD(queue_wait);

static unsigned int
cache_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;

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

static int
cache_ioctl(struct inode *ino, struct file *filp,
	    unsigned int cmd, unsigned long arg)
{
	int len = 0;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;
	struct cache_detail *cd = PDE(ino)->data;

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

static int
cache_open(struct inode *inode, struct file *filp)
{
	struct cache_reader *rp = NULL;

	nonseekable_open(inode, filp);
	if (filp->f_mode & FMODE_READ) {
		struct cache_detail *cd = PDE(inode)->data;

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

static int
cache_release(struct inode *inode, struct file *filp)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_detail *cd = PDE(inode)->data;

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
	return 0;
}



static struct file_operations cache_file_operations = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= cache_read,
	.write		= cache_write,
	.poll		= cache_poll,
	.ioctl		= cache_ioctl, /* for FIONREAD */
	.open		= cache_open,
	.release	= cache_release,
};


static void queue_loose(struct cache_detail *detail, struct cache_head *ch)
{
	struct cache_queue *cq;
	spin_lock(&queue_lock);
	list_for_each_entry(cq, &detail->queue, list)
		if (!cq->reader) {
			struct cache_request *cr = container_of(cq, struct cache_request, q);
			if (cr->item != ch)
				continue;
			if (cr->readers != 0)
				break;
			list_del(&cr->q.list);
			spin_unlock(&queue_lock);
			detail->cache_put(cr->item, detail);
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

static void warn_no_listener(struct cache_detail *detail)
{
	if (detail->last_warn != detail->last_close) {
		detail->last_warn = detail->last_close;
		if (detail->warn_no_listener)
			detail->warn_no_listener(detail);
	}
}

/*
 * register an upcall request to user-space.
 * Each request is at most one page long.
 */
static int cache_make_upcall(struct cache_detail *detail, struct cache_head *h)
{

	char *buf;
	struct cache_request *crq;
	char *bp;
	int len;

	if (detail->cache_request == NULL)
		return -EINVAL;

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

	detail->cache_request(detail, h, &bp, &len);

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
		seq_printf(m, "# expiry=%ld refcnt=%d\n",
			   cp->expiry_time, atomic_read(&cp->refcnt));
	cache_get(cp);
	if (cache_check(cd, cp, NULL))
		/* cache_check does a cache_put on failure */
		seq_printf(m, "# ");
	else
		cache_put(cp, cd);

	return cd->cache_show(m, cd, cp);
}

static struct seq_operations cache_content_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show,
};

static int content_open(struct inode *inode, struct file *file)
{
	int res;
	struct handle *han;
	struct cache_detail *cd = PDE(inode)->data;

	han = kmalloc(sizeof(*han), GFP_KERNEL);
	if (han == NULL)
		return -ENOMEM;

	han->cd = cd;

	res = seq_open(file, &cache_content_op);
	if (res)
		kfree(han);
	else
		((struct seq_file *)file->private_data)->private = han;

	return res;
}
static int content_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct handle *han = m->private;
	kfree(han);
	m->private = NULL;
	return seq_release(inode, file);
}

static struct file_operations content_file_operations = {
	.open		= content_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= content_release,
};

static ssize_t read_flush(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(file->f_dentry->d_inode)->data;
	char tbuf[20];
	unsigned long p = *ppos;
	int len;

	sprintf(tbuf, "%lu\n", cd->flush_time);
	len = strlen(tbuf);
	if (p >= len)
		return 0;
	len -= p;
	if (len > count) len = count;
	if (copy_to_user(buf, (void*)(tbuf+p), len))
		len = -EFAULT;
	else
		*ppos += len;
	return len;
}

static ssize_t write_flush(struct file * file, const char __user * buf,
			     size_t count, loff_t *ppos)
{
	struct cache_detail *cd = PDE(file->f_dentry->d_inode)->data;
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

static struct file_operations cache_flush_operations = {
	.open		= nonseekable_open,
	.read		= read_flush,
	.write		= write_flush,
};
