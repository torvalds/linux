// SPDX-License-Identifier: GPL-2.0
/* Watch queue and general notification mechanism, built on pipes
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See Documentation/core-api/watch_queue.rst
 */

#define pr_fmt(fmt) "watchq: " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/file.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/sched/signal.h>
#include <linux/watch_queue.h>
#include <linux/pipe_fs_i.h>

MODULE_DESCRIPTION("Watch queue");
MODULE_AUTHOR("Red Hat, Inc.");

#define WATCH_QUEUE_NOTE_SIZE 128
#define WATCH_QUEUE_NOTES_PER_PAGE (PAGE_SIZE / WATCH_QUEUE_NOTE_SIZE)

/*
 * This must be called under the RCU read-lock, which makes
 * sure that the wqueue still exists. It can then take the lock,
 * and check that the wqueue hasn't been destroyed, which in
 * turn makes sure that the notification pipe still exists.
 */
static inline bool lock_wqueue(struct watch_queue *wqueue)
{
	spin_lock_bh(&wqueue->lock);
	if (unlikely(!wqueue->pipe)) {
		spin_unlock_bh(&wqueue->lock);
		return false;
	}
	return true;
}

static inline void unlock_wqueue(struct watch_queue *wqueue)
{
	spin_unlock_bh(&wqueue->lock);
}

static void watch_queue_pipe_buf_release(struct pipe_inode_info *pipe,
					 struct pipe_buffer *buf)
{
	struct watch_queue *wqueue = (struct watch_queue *)buf->private;
	struct page *page;
	unsigned int bit;

	/* We need to work out which note within the page this refers to, but
	 * the note might have been maximum size, so merely ANDing the offset
	 * off doesn't work.  OTOH, the note must've been more than zero size.
	 */
	bit = buf->offset + buf->len;
	if ((bit & (WATCH_QUEUE_NOTE_SIZE - 1)) == 0)
		bit -= WATCH_QUEUE_NOTE_SIZE;
	bit /= WATCH_QUEUE_NOTE_SIZE;

	page = buf->page;
	bit += page->private;

	set_bit(bit, wqueue->notes_bitmap);
	generic_pipe_buf_release(pipe, buf);
}

// No try_steal function => no stealing
#define watch_queue_pipe_buf_try_steal NULL

/* New data written to a pipe may be appended to a buffer with this type. */
static const struct pipe_buf_operations watch_queue_pipe_buf_ops = {
	.release	= watch_queue_pipe_buf_release,
	.try_steal	= watch_queue_pipe_buf_try_steal,
	.get		= generic_pipe_buf_get,
};

/*
 * Post a notification to a watch queue.
 *
 * Must be called with the RCU lock for reading, and the
 * watch_queue lock held, which guarantees that the pipe
 * hasn't been released.
 */
static bool post_one_notification(struct watch_queue *wqueue,
				  struct watch_notification *n)
{
	void *p;
	struct pipe_inode_info *pipe = wqueue->pipe;
	struct pipe_buffer *buf;
	struct page *page;
	unsigned int head, tail, mask, note, offset, len;
	bool done = false;

	spin_lock_irq(&pipe->rd_wait.lock);

	mask = pipe->ring_size - 1;
	head = pipe->head;
	tail = pipe->tail;
	if (pipe_full(head, tail, pipe->ring_size))
		goto lost;

	note = find_first_bit(wqueue->notes_bitmap, wqueue->nr_notes);
	if (note >= wqueue->nr_notes)
		goto lost;

	page = wqueue->notes[note / WATCH_QUEUE_NOTES_PER_PAGE];
	offset = note % WATCH_QUEUE_NOTES_PER_PAGE * WATCH_QUEUE_NOTE_SIZE;
	get_page(page);
	len = n->info & WATCH_INFO_LENGTH;
	p = kmap_atomic(page);
	memcpy(p + offset, n, len);
	kunmap_atomic(p);

	buf = &pipe->bufs[head & mask];
	buf->page = page;
	buf->private = (unsigned long)wqueue;
	buf->ops = &watch_queue_pipe_buf_ops;
	buf->offset = offset;
	buf->len = len;
	buf->flags = PIPE_BUF_FLAG_WHOLE;
	smp_store_release(&pipe->head, head + 1); /* vs pipe_read() */

	if (!test_and_clear_bit(note, wqueue->notes_bitmap)) {
		spin_unlock_irq(&pipe->rd_wait.lock);
		BUG();
	}
	wake_up_interruptible_sync_poll_locked(&pipe->rd_wait, EPOLLIN | EPOLLRDNORM);
	done = true;

out:
	spin_unlock_irq(&pipe->rd_wait.lock);
	if (done)
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	return done;

lost:
	buf = &pipe->bufs[(head - 1) & mask];
	buf->flags |= PIPE_BUF_FLAG_LOSS;
	goto out;
}

/*
 * Apply filter rules to a notification.
 */
static bool filter_watch_notification(const struct watch_filter *wf,
				      const struct watch_notification *n)
{
	const struct watch_type_filter *wt;
	unsigned int st_bits = sizeof(wt->subtype_filter[0]) * 8;
	unsigned int st_index = n->subtype / st_bits;
	unsigned int st_bit = 1U << (n->subtype % st_bits);
	int i;

	if (!test_bit(n->type, wf->type_filter))
		return false;

	for (i = 0; i < wf->nr_filters; i++) {
		wt = &wf->filters[i];
		if (n->type == wt->type &&
		    (wt->subtype_filter[st_index] & st_bit) &&
		    (n->info & wt->info_mask) == wt->info_filter)
			return true;
	}

	return false; /* If there is a filter, the default is to reject. */
}

/**
 * __post_watch_notification - Post an event notification
 * @wlist: The watch list to post the event to.
 * @n: The notification record to post.
 * @cred: The creds of the process that triggered the notification.
 * @id: The ID to match on the watch.
 *
 * Post a notification of an event into a set of watch queues and let the users
 * know.
 *
 * The size of the notification should be set in n->info & WATCH_INFO_LENGTH and
 * should be in units of sizeof(*n).
 */
void __post_watch_notification(struct watch_list *wlist,
			       struct watch_notification *n,
			       const struct cred *cred,
			       u64 id)
{
	const struct watch_filter *wf;
	struct watch_queue *wqueue;
	struct watch *watch;

	if (((n->info & WATCH_INFO_LENGTH) >> WATCH_INFO_LENGTH__SHIFT) == 0) {
		WARN_ON(1);
		return;
	}

	rcu_read_lock();

	hlist_for_each_entry_rcu(watch, &wlist->watchers, list_node) {
		if (watch->id != id)
			continue;
		n->info &= ~WATCH_INFO_ID;
		n->info |= watch->info_id;

		wqueue = rcu_dereference(watch->queue);
		wf = rcu_dereference(wqueue->filter);
		if (wf && !filter_watch_notification(wf, n))
			continue;

		if (security_post_notification(watch->cred, cred, n) < 0)
			continue;

		if (lock_wqueue(wqueue)) {
			post_one_notification(wqueue, n);
			unlock_wqueue(wqueue);
		}
	}

	rcu_read_unlock();
}
EXPORT_SYMBOL(__post_watch_notification);

/*
 * Allocate sufficient pages to preallocation for the requested number of
 * notifications.
 */
long watch_queue_set_size(struct pipe_inode_info *pipe, unsigned int nr_notes)
{
	struct watch_queue *wqueue = pipe->watch_queue;
	struct page **pages;
	unsigned long *bitmap;
	unsigned long user_bufs;
	int ret, i, nr_pages;

	if (!wqueue)
		return -ENODEV;
	if (wqueue->notes)
		return -EBUSY;

	if (nr_notes < 1 ||
	    nr_notes > 512) /* TODO: choose a better hard limit */
		return -EINVAL;

	nr_pages = (nr_notes + WATCH_QUEUE_NOTES_PER_PAGE - 1);
	nr_pages /= WATCH_QUEUE_NOTES_PER_PAGE;
	user_bufs = account_pipe_buffers(pipe->user, pipe->nr_accounted, nr_pages);

	if (nr_pages > pipe->max_usage &&
	    (too_many_pipe_buffers_hard(user_bufs) ||
	     too_many_pipe_buffers_soft(user_bufs)) &&
	    pipe_is_unprivileged_user()) {
		ret = -EPERM;
		goto error;
	}

	nr_notes = nr_pages * WATCH_QUEUE_NOTES_PER_PAGE;
	ret = pipe_resize_ring(pipe, roundup_pow_of_two(nr_notes));
	if (ret < 0)
		goto error;

	ret = -ENOMEM;
	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto error;

	for (i = 0; i < nr_pages; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			goto error_p;
		pages[i]->private = i * WATCH_QUEUE_NOTES_PER_PAGE;
	}

	bitmap = bitmap_alloc(nr_notes, GFP_KERNEL);
	if (!bitmap)
		goto error_p;

	bitmap_fill(bitmap, nr_notes);
	wqueue->notes = pages;
	wqueue->notes_bitmap = bitmap;
	wqueue->nr_pages = nr_pages;
	wqueue->nr_notes = nr_notes;
	return 0;

error_p:
	while (--i >= 0)
		__free_page(pages[i]);
	kfree(pages);
error:
	(void) account_pipe_buffers(pipe->user, nr_pages, pipe->nr_accounted);
	return ret;
}

/*
 * Set the filter on a watch queue.
 */
long watch_queue_set_filter(struct pipe_inode_info *pipe,
			    struct watch_notification_filter __user *_filter)
{
	struct watch_notification_type_filter *tf;
	struct watch_notification_filter filter;
	struct watch_type_filter *q;
	struct watch_filter *wfilter;
	struct watch_queue *wqueue = pipe->watch_queue;
	int ret, nr_filter = 0, i;

	if (!wqueue)
		return -ENODEV;

	if (!_filter) {
		/* Remove the old filter */
		wfilter = NULL;
		goto set;
	}

	/* Grab the user's filter specification */
	if (copy_from_user(&filter, _filter, sizeof(filter)) != 0)
		return -EFAULT;
	if (filter.nr_filters == 0 ||
	    filter.nr_filters > 16 ||
	    filter.__reserved != 0)
		return -EINVAL;

	tf = memdup_array_user(_filter->filters, filter.nr_filters, sizeof(*tf));
	if (IS_ERR(tf))
		return PTR_ERR(tf);

	ret = -EINVAL;
	for (i = 0; i < filter.nr_filters; i++) {
		if ((tf[i].info_filter & ~tf[i].info_mask) ||
		    tf[i].info_mask & WATCH_INFO_LENGTH)
			goto err_filter;
		/* Ignore any unknown types */
		if (tf[i].type >= WATCH_TYPE__NR)
			continue;
		nr_filter++;
	}

	/* Now we need to build the internal filter from only the relevant
	 * user-specified filters.
	 */
	ret = -ENOMEM;
	wfilter = kzalloc(struct_size(wfilter, filters, nr_filter), GFP_KERNEL);
	if (!wfilter)
		goto err_filter;
	wfilter->nr_filters = nr_filter;

	q = wfilter->filters;
	for (i = 0; i < filter.nr_filters; i++) {
		if (tf[i].type >= WATCH_TYPE__NR)
			continue;

		q->type			= tf[i].type;
		q->info_filter		= tf[i].info_filter;
		q->info_mask		= tf[i].info_mask;
		q->subtype_filter[0]	= tf[i].subtype_filter[0];
		__set_bit(q->type, wfilter->type_filter);
		q++;
	}

	kfree(tf);
set:
	pipe_lock(pipe);
	wfilter = rcu_replace_pointer(wqueue->filter, wfilter,
				      lockdep_is_held(&pipe->mutex));
	pipe_unlock(pipe);
	if (wfilter)
		kfree_rcu(wfilter, rcu);
	return 0;

err_filter:
	kfree(tf);
	return ret;
}

static void __put_watch_queue(struct kref *kref)
{
	struct watch_queue *wqueue =
		container_of(kref, struct watch_queue, usage);
	struct watch_filter *wfilter;
	int i;

	for (i = 0; i < wqueue->nr_pages; i++)
		__free_page(wqueue->notes[i]);
	kfree(wqueue->notes);
	bitmap_free(wqueue->notes_bitmap);

	wfilter = rcu_access_pointer(wqueue->filter);
	if (wfilter)
		kfree_rcu(wfilter, rcu);
	kfree_rcu(wqueue, rcu);
}

/**
 * put_watch_queue - Dispose of a ref on a watchqueue.
 * @wqueue: The watch queue to unref.
 */
void put_watch_queue(struct watch_queue *wqueue)
{
	kref_put(&wqueue->usage, __put_watch_queue);
}
EXPORT_SYMBOL(put_watch_queue);

static void free_watch(struct rcu_head *rcu)
{
	struct watch *watch = container_of(rcu, struct watch, rcu);

	put_watch_queue(rcu_access_pointer(watch->queue));
	atomic_dec(&watch->cred->user->nr_watches);
	put_cred(watch->cred);
	kfree(watch);
}

static void __put_watch(struct kref *kref)
{
	struct watch *watch = container_of(kref, struct watch, usage);

	call_rcu(&watch->rcu, free_watch);
}

/*
 * Discard a watch.
 */
static void put_watch(struct watch *watch)
{
	kref_put(&watch->usage, __put_watch);
}

/**
 * init_watch - Initialise a watch
 * @watch: The watch to initialise.
 * @wqueue: The queue to assign.
 *
 * Initialise a watch and set the watch queue.
 */
void init_watch(struct watch *watch, struct watch_queue *wqueue)
{
	kref_init(&watch->usage);
	INIT_HLIST_NODE(&watch->list_node);
	INIT_HLIST_NODE(&watch->queue_node);
	rcu_assign_pointer(watch->queue, wqueue);
}

static int add_one_watch(struct watch *watch, struct watch_list *wlist, struct watch_queue *wqueue)
{
	const struct cred *cred;
	struct watch *w;

	hlist_for_each_entry(w, &wlist->watchers, list_node) {
		struct watch_queue *wq = rcu_access_pointer(w->queue);
		if (wqueue == wq && watch->id == w->id)
			return -EBUSY;
	}

	cred = current_cred();
	if (atomic_inc_return(&cred->user->nr_watches) > task_rlimit(current, RLIMIT_NOFILE)) {
		atomic_dec(&cred->user->nr_watches);
		return -EAGAIN;
	}

	watch->cred = get_cred(cred);
	rcu_assign_pointer(watch->watch_list, wlist);

	kref_get(&wqueue->usage);
	kref_get(&watch->usage);
	hlist_add_head(&watch->queue_node, &wqueue->watches);
	hlist_add_head_rcu(&watch->list_node, &wlist->watchers);
	return 0;
}

/**
 * add_watch_to_object - Add a watch on an object to a watch list
 * @watch: The watch to add
 * @wlist: The watch list to add to
 *
 * @watch->queue must have been set to point to the queue to post notifications
 * to and the watch list of the object to be watched.  @watch->cred must also
 * have been set to the appropriate credentials and a ref taken on them.
 *
 * The caller must pin the queue and the list both and must hold the list
 * locked against racing watch additions/removals.
 */
int add_watch_to_object(struct watch *watch, struct watch_list *wlist)
{
	struct watch_queue *wqueue;
	int ret = -ENOENT;

	rcu_read_lock();

	wqueue = rcu_access_pointer(watch->queue);
	if (lock_wqueue(wqueue)) {
		spin_lock(&wlist->lock);
		ret = add_one_watch(watch, wlist, wqueue);
		spin_unlock(&wlist->lock);
		unlock_wqueue(wqueue);
	}

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(add_watch_to_object);

/**
 * remove_watch_from_object - Remove a watch or all watches from an object.
 * @wlist: The watch list to remove from
 * @wq: The watch queue of interest (ignored if @all is true)
 * @id: The ID of the watch to remove (ignored if @all is true)
 * @all: True to remove all objects
 *
 * Remove a specific watch or all watches from an object.  A notification is
 * sent to the watcher to tell them that this happened.
 */
int remove_watch_from_object(struct watch_list *wlist, struct watch_queue *wq,
			     u64 id, bool all)
{
	struct watch_notification_removal n;
	struct watch_queue *wqueue;
	struct watch *watch;
	int ret = -EBADSLT;

	rcu_read_lock();

again:
	spin_lock(&wlist->lock);
	hlist_for_each_entry(watch, &wlist->watchers, list_node) {
		if (all ||
		    (watch->id == id && rcu_access_pointer(watch->queue) == wq))
			goto found;
	}
	spin_unlock(&wlist->lock);
	goto out;

found:
	ret = 0;
	hlist_del_init_rcu(&watch->list_node);
	rcu_assign_pointer(watch->watch_list, NULL);
	spin_unlock(&wlist->lock);

	/* We now own the reference on watch that used to belong to wlist. */

	n.watch.type = WATCH_TYPE_META;
	n.watch.subtype = WATCH_META_REMOVAL_NOTIFICATION;
	n.watch.info = watch->info_id | watch_sizeof(n.watch);
	n.id = id;
	if (id != 0)
		n.watch.info = watch->info_id | watch_sizeof(n);

	wqueue = rcu_dereference(watch->queue);

	if (lock_wqueue(wqueue)) {
		post_one_notification(wqueue, &n.watch);

		if (!hlist_unhashed(&watch->queue_node)) {
			hlist_del_init_rcu(&watch->queue_node);
			put_watch(watch);
		}

		unlock_wqueue(wqueue);
	}

	if (wlist->release_watch) {
		void (*release_watch)(struct watch *);

		release_watch = wlist->release_watch;
		rcu_read_unlock();
		(*release_watch)(watch);
		rcu_read_lock();
	}
	put_watch(watch);

	if (all && !hlist_empty(&wlist->watchers))
		goto again;
out:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(remove_watch_from_object);

/*
 * Remove all the watches that are contributory to a queue.  This has the
 * potential to race with removal of the watches by the destruction of the
 * objects being watched or with the distribution of notifications.
 */
void watch_queue_clear(struct watch_queue *wqueue)
{
	struct watch_list *wlist;
	struct watch *watch;
	bool release;

	rcu_read_lock();
	spin_lock_bh(&wqueue->lock);

	/*
	 * This pipe can be freed by callers like free_pipe_info().
	 * Removing this reference also prevents new notifications.
	 */
	wqueue->pipe = NULL;

	while (!hlist_empty(&wqueue->watches)) {
		watch = hlist_entry(wqueue->watches.first, struct watch, queue_node);
		hlist_del_init_rcu(&watch->queue_node);
		/* We now own a ref on the watch. */
		spin_unlock_bh(&wqueue->lock);

		/* We can't do the next bit under the queue lock as we need to
		 * get the list lock - which would cause a deadlock if someone
		 * was removing from the opposite direction at the same time or
		 * posting a notification.
		 */
		wlist = rcu_dereference(watch->watch_list);
		if (wlist) {
			void (*release_watch)(struct watch *);

			spin_lock(&wlist->lock);

			release = !hlist_unhashed(&watch->list_node);
			if (release) {
				hlist_del_init_rcu(&watch->list_node);
				rcu_assign_pointer(watch->watch_list, NULL);

				/* We now own a second ref on the watch. */
			}

			release_watch = wlist->release_watch;
			spin_unlock(&wlist->lock);

			if (release) {
				if (release_watch) {
					rcu_read_unlock();
					/* This might need to call dput(), so
					 * we have to drop all the locks.
					 */
					(*release_watch)(watch);
					rcu_read_lock();
				}
				put_watch(watch);
			}
		}

		put_watch(watch);
		spin_lock_bh(&wqueue->lock);
	}

	spin_unlock_bh(&wqueue->lock);
	rcu_read_unlock();
}

/**
 * get_watch_queue - Get a watch queue from its file descriptor.
 * @fd: The fd to query.
 */
struct watch_queue *get_watch_queue(int fd)
{
	struct pipe_inode_info *pipe;
	struct watch_queue *wqueue = ERR_PTR(-EINVAL);
	CLASS(fd, f)(fd);

	if (!fd_empty(f)) {
		pipe = get_pipe_info(fd_file(f), false);
		if (pipe && pipe->watch_queue) {
			wqueue = pipe->watch_queue;
			kref_get(&wqueue->usage);
		}
	}

	return wqueue;
}
EXPORT_SYMBOL(get_watch_queue);

/*
 * Initialise a watch queue
 */
int watch_queue_init(struct pipe_inode_info *pipe)
{
	struct watch_queue *wqueue;

	wqueue = kzalloc(sizeof(*wqueue), GFP_KERNEL);
	if (!wqueue)
		return -ENOMEM;

	wqueue->pipe = pipe;
	kref_init(&wqueue->usage);
	spin_lock_init(&wqueue->lock);
	INIT_HLIST_HEAD(&wqueue->watches);

	pipe->watch_queue = wqueue;
	return 0;
}
