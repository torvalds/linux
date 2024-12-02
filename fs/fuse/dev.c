/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/pipe_fs_i.h>
#include <linux/swap.h>
#include <linux/splice.h>
#include <linux/sched.h>

#include <trace/hooks/fuse.h>

MODULE_ALIAS_MISCDEV(FUSE_MINOR);
MODULE_ALIAS("devname:fuse");

/* Ordinary requests have even IDs, while interrupts IDs are odd */
#define FUSE_INT_REQ_BIT (1ULL << 0)
#define FUSE_REQ_ID_STEP (1ULL << 1)

static struct kmem_cache *fuse_req_cachep;

static struct fuse_dev *fuse_get_dev(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	return READ_ONCE(file->private_data);
}

static void fuse_request_init(struct fuse_mount *fm, struct fuse_req *req)
{
	INIT_LIST_HEAD(&req->list);
	INIT_LIST_HEAD(&req->intr_entry);
	init_waitqueue_head(&req->waitq);
	refcount_set(&req->count, 1);
	__set_bit(FR_PENDING, &req->flags);
	req->fm = fm;
}

static struct fuse_req *fuse_request_alloc(struct fuse_mount *fm, gfp_t flags)
{
	struct fuse_req *req = kmem_cache_zalloc(fuse_req_cachep, flags);
	if (req)
		fuse_request_init(fm, req);

	return req;
}

static void fuse_request_free(struct fuse_req *req)
{
	kmem_cache_free(fuse_req_cachep, req);
}

static void __fuse_get_request(struct fuse_req *req)
{
	refcount_inc(&req->count);
}

/* Must be called with > 1 refcount */
static void __fuse_put_request(struct fuse_req *req)
{
	refcount_dec(&req->count);
}

void fuse_set_initialized(struct fuse_conn *fc)
{
	/* Make sure stores before this are seen on another CPU */
	smp_wmb();
	fc->initialized = 1;
}

static bool fuse_block_alloc(struct fuse_conn *fc, bool for_background)
{
	return !fc->initialized || (for_background && fc->blocked);
}

static void fuse_drop_waiting(struct fuse_conn *fc)
{
	/*
	 * lockess check of fc->connected is okay, because atomic_dec_and_test()
	 * provides a memory barrier matched with the one in fuse_wait_aborted()
	 * to ensure no wake-up is missed.
	 */
	if (atomic_dec_and_test(&fc->num_waiting) &&
	    !READ_ONCE(fc->connected)) {
		/* wake up aborters */
		wake_up_all(&fc->blocked_waitq);
	}
}

static void fuse_put_request(struct fuse_req *req);

static struct fuse_req *fuse_get_req(struct fuse_mount *fm, bool for_background)
{
	struct fuse_conn *fc = fm->fc;
	struct fuse_req *req;
	int err;
	atomic_inc(&fc->num_waiting);

	if (fuse_block_alloc(fc, for_background)) {
		err = -EINTR;
		if (wait_event_killable_exclusive(fc->blocked_waitq,
				!fuse_block_alloc(fc, for_background)))
			goto out;
	}
	/* Matches smp_wmb() in fuse_set_initialized() */
	smp_rmb();

	err = -ENOTCONN;
	if (!fc->connected)
		goto out;

	err = -ECONNREFUSED;
	if (fc->conn_error)
		goto out;

	req = fuse_request_alloc(fm, GFP_KERNEL);
	err = -ENOMEM;
	if (!req) {
		if (for_background)
			wake_up(&fc->blocked_waitq);
		goto out;
	}

	req->in.h.uid = from_kuid(fc->user_ns, current_fsuid());
	req->in.h.gid = from_kgid(fc->user_ns, current_fsgid());
	req->in.h.pid = pid_nr_ns(task_pid(current), fc->pid_ns);

	__set_bit(FR_WAITING, &req->flags);
	if (for_background)
		__set_bit(FR_BACKGROUND, &req->flags);

	if (unlikely(req->in.h.uid == ((uid_t)-1) ||
		     req->in.h.gid == ((gid_t)-1))) {
		fuse_put_request(req);
		return ERR_PTR(-EOVERFLOW);
	}
	return req;

 out:
	fuse_drop_waiting(fc);
	return ERR_PTR(err);
}

static void fuse_put_request(struct fuse_req *req)
{
	struct fuse_conn *fc = req->fm->fc;

	if (refcount_dec_and_test(&req->count)) {
		if (test_bit(FR_BACKGROUND, &req->flags)) {
			/*
			 * We get here in the unlikely case that a background
			 * request was allocated but not sent
			 */
			spin_lock(&fc->bg_lock);
			if (!fc->blocked)
				wake_up(&fc->blocked_waitq);
			spin_unlock(&fc->bg_lock);
		}

		if (test_bit(FR_WAITING, &req->flags)) {
			__clear_bit(FR_WAITING, &req->flags);
			fuse_drop_waiting(fc);
		}

		fuse_request_free(req);
	}
}

unsigned int fuse_len_args(unsigned int numargs, struct fuse_arg *args)
{
	unsigned nbytes = 0;
	unsigned i;

	for (i = 0; i < numargs; i++)
		nbytes += args[i].size;

	return nbytes;
}
EXPORT_SYMBOL_GPL(fuse_len_args);

u64 fuse_get_unique(struct fuse_iqueue *fiq)
{
	fiq->reqctr += FUSE_REQ_ID_STEP;
	return fiq->reqctr;
}
EXPORT_SYMBOL_GPL(fuse_get_unique);

static unsigned int fuse_req_hash(u64 unique)
{
	return hash_long(unique & ~FUSE_INT_REQ_BIT, FUSE_PQ_HASH_BITS);
}

/**
 * A new request is available, wake fiq->waitq
 */
static void fuse_dev_wake_and_unlock(struct fuse_iqueue *fiq, bool sync)
__releases(fiq->lock)
{
	if (sync)
		wake_up_sync(&fiq->waitq);
	else
		wake_up(&fiq->waitq);
	kill_fasync(&fiq->fasync, SIGIO, POLL_IN);
	spin_unlock(&fiq->lock);
}

const struct fuse_iqueue_ops fuse_dev_fiq_ops = {
	.wake_forget_and_unlock		= fuse_dev_wake_and_unlock,
	.wake_interrupt_and_unlock	= fuse_dev_wake_and_unlock,
	.wake_pending_and_unlock	= fuse_dev_wake_and_unlock,
};
EXPORT_SYMBOL_GPL(fuse_dev_fiq_ops);

static void queue_request_and_unlock(struct fuse_iqueue *fiq,
				     struct fuse_req *req, bool sync)
__releases(fiq->lock)
{
	req->in.h.len = sizeof(struct fuse_in_header) +
		fuse_len_args(req->args->in_numargs,
			      (struct fuse_arg *) req->args->in_args);
	list_add_tail(&req->list, &fiq->pending);
	trace_android_vh_queue_request_and_unlock(&fiq->waitq, sync);
	fiq->ops->wake_pending_and_unlock(fiq, sync);
}

void fuse_queue_forget(struct fuse_conn *fc, struct fuse_forget_link *forget,
		       u64 nodeid, u64 nlookup)
{
	struct fuse_iqueue *fiq = &fc->iq;

	if (nodeid == 0) {
		kfree(forget);
		return;
	}

	forget->forget_one.nodeid = nodeid;
	forget->forget_one.nlookup = nlookup;

	spin_lock(&fiq->lock);
	if (fiq->connected) {
		fiq->forget_list_tail->next = forget;
		fiq->forget_list_tail = forget;
		fiq->ops->wake_forget_and_unlock(fiq, false);
	} else {
		kfree(forget);
		spin_unlock(&fiq->lock);
	}
}

static void flush_bg_queue(struct fuse_conn *fc)
{
	struct fuse_iqueue *fiq = &fc->iq;

	while (fc->active_background < fc->max_background &&
	       !list_empty(&fc->bg_queue)) {
		struct fuse_req *req;

		req = list_first_entry(&fc->bg_queue, struct fuse_req, list);
		list_del(&req->list);
		fc->active_background++;
		spin_lock(&fiq->lock);
		req->in.h.unique = fuse_get_unique(fiq);
		queue_request_and_unlock(fiq, req, false);
	}
}

/*
 * This function is called when a request is finished.  Either a reply
 * has arrived or it was aborted (and not yet sent) or some error
 * occurred during communication with userspace, or the device file
 * was closed.  The requester thread is woken up (if still waiting),
 * the 'end' callback is called if given, else the reference to the
 * request is released
 */
void fuse_request_end(struct fuse_req *req)
{
	struct fuse_mount *fm = req->fm;
	struct fuse_conn *fc = fm->fc;
	struct fuse_iqueue *fiq = &fc->iq;

	if (test_and_set_bit(FR_FINISHED, &req->flags))
		goto put_request;

	/*
	 * test_and_set_bit() implies smp_mb() between bit
	 * changing and below FR_INTERRUPTED check. Pairs with
	 * smp_mb() from queue_interrupt().
	 */
	if (test_bit(FR_INTERRUPTED, &req->flags)) {
		spin_lock(&fiq->lock);
		list_del_init(&req->intr_entry);
		spin_unlock(&fiq->lock);
	}
	WARN_ON(test_bit(FR_PENDING, &req->flags));
	WARN_ON(test_bit(FR_SENT, &req->flags));
	if (test_bit(FR_BACKGROUND, &req->flags)) {
		spin_lock(&fc->bg_lock);
		clear_bit(FR_BACKGROUND, &req->flags);
		if (fc->num_background == fc->max_background) {
			fc->blocked = 0;
			wake_up(&fc->blocked_waitq);
		} else if (!fc->blocked) {
			/*
			 * Wake up next waiter, if any.  It's okay to use
			 * waitqueue_active(), as we've already synced up
			 * fc->blocked with waiters with the wake_up() call
			 * above.
			 */
			if (waitqueue_active(&fc->blocked_waitq))
				wake_up(&fc->blocked_waitq);
		}

		fc->num_background--;
		fc->active_background--;
		flush_bg_queue(fc);
		spin_unlock(&fc->bg_lock);
	} else {
		/* Wake up waiter sleeping in request_wait_answer() */
		wake_up(&req->waitq);
		trace_android_vh_fuse_request_end(current);
	}

	if (test_bit(FR_ASYNC, &req->flags))
		req->args->end(fm, req->args, req->out.h.error);
put_request:
	fuse_put_request(req);
}
EXPORT_SYMBOL_GPL(fuse_request_end);

static int queue_interrupt(struct fuse_req *req)
{
	struct fuse_iqueue *fiq = &req->fm->fc->iq;

	spin_lock(&fiq->lock);
	/* Check for we've sent request to interrupt this req */
	if (unlikely(!test_bit(FR_INTERRUPTED, &req->flags))) {
		spin_unlock(&fiq->lock);
		return -EINVAL;
	}

	if (list_empty(&req->intr_entry)) {
		list_add_tail(&req->intr_entry, &fiq->interrupts);
		/*
		 * Pairs with smp_mb() implied by test_and_set_bit()
		 * from fuse_request_end().
		 */
		smp_mb();
		if (test_bit(FR_FINISHED, &req->flags)) {
			list_del_init(&req->intr_entry);
			spin_unlock(&fiq->lock);
			return 0;
		}
		fiq->ops->wake_interrupt_and_unlock(fiq, false);
	} else {
		spin_unlock(&fiq->lock);
	}
	return 0;
}

static void request_wait_answer(struct fuse_req *req)
{
	struct fuse_conn *fc = req->fm->fc;
	struct fuse_iqueue *fiq = &fc->iq;
	int err;

	if (!fc->no_interrupt) {
		/* Any signal may interrupt this */
		err = wait_event_interruptible(req->waitq,
					test_bit(FR_FINISHED, &req->flags));
		if (!err)
			return;

		set_bit(FR_INTERRUPTED, &req->flags);
		/* matches barrier in fuse_dev_do_read() */
		smp_mb__after_atomic();
		if (test_bit(FR_SENT, &req->flags))
			queue_interrupt(req);
	}

	if (!test_bit(FR_FORCE, &req->flags)) {
		/* Only fatal signals may interrupt this */
		err = wait_event_killable(req->waitq,
					test_bit(FR_FINISHED, &req->flags));
		if (!err)
			return;

		spin_lock(&fiq->lock);
		/* Request is not yet in userspace, bail out */
		if (test_bit(FR_PENDING, &req->flags)) {
			list_del(&req->list);
			spin_unlock(&fiq->lock);
			__fuse_put_request(req);
			req->out.h.error = -EINTR;
			return;
		}
		spin_unlock(&fiq->lock);
	}

	/*
	 * Either request is already in userspace, or it was forced.
	 * Wait it out.
	 */
	wait_event(req->waitq, test_bit(FR_FINISHED, &req->flags));
}

static void __fuse_request_send(struct fuse_req *req)
{
	struct fuse_iqueue *fiq = &req->fm->fc->iq;

	BUG_ON(test_bit(FR_BACKGROUND, &req->flags));
	spin_lock(&fiq->lock);
	if (!fiq->connected) {
		spin_unlock(&fiq->lock);
		req->out.h.error = -ENOTCONN;
	} else {
		req->in.h.unique = fuse_get_unique(fiq);
		/* acquire extra reference, since request is still needed
		   after fuse_request_end() */
		__fuse_get_request(req);
		queue_request_and_unlock(fiq, req, true);

		request_wait_answer(req);
		/* Pairs with smp_wmb() in fuse_request_end() */
		smp_rmb();
	}
}

static void fuse_adjust_compat(struct fuse_conn *fc, struct fuse_args *args)
{
	if (fc->minor < 4 && args->opcode == FUSE_STATFS)
		args->out_args[0].size = FUSE_COMPAT_STATFS_SIZE;

	if (fc->minor < 9) {
		switch (args->opcode) {
		case FUSE_LOOKUP:
		case FUSE_CREATE:
		case FUSE_MKNOD:
		case FUSE_MKDIR:
		case FUSE_SYMLINK:
		case FUSE_LINK:
			args->out_args[0].size = FUSE_COMPAT_ENTRY_OUT_SIZE;
			break;
		case FUSE_GETATTR:
		case FUSE_SETATTR:
			args->out_args[0].size = FUSE_COMPAT_ATTR_OUT_SIZE;
			break;
		}
	}
	if (fc->minor < 12) {
		switch (args->opcode) {
		case FUSE_CREATE:
			args->in_args[0].size = sizeof(struct fuse_open_in);
			break;
		case FUSE_MKNOD:
			args->in_args[0].size = FUSE_COMPAT_MKNOD_IN_SIZE;
			break;
		}
	}
}

static void fuse_force_creds(struct fuse_req *req)
{
	struct fuse_conn *fc = req->fm->fc;

	req->in.h.uid = from_kuid_munged(fc->user_ns, current_fsuid());
	req->in.h.gid = from_kgid_munged(fc->user_ns, current_fsgid());
	req->in.h.pid = pid_nr_ns(task_pid(current), fc->pid_ns);
}

static void fuse_args_to_req(struct fuse_req *req, struct fuse_args *args)
{
	req->in.h.opcode = args->opcode;
	req->in.h.nodeid = args->nodeid;
	req->in.h.padding = args->error_in;
	req->args = args;
	if (args->is_ext)
		req->in.h.total_extlen = args->in_args[args->ext_idx].size / 8;
	if (args->end)
		__set_bit(FR_ASYNC, &req->flags);
}

ssize_t fuse_simple_request(struct fuse_mount *fm, struct fuse_args *args)
{
	struct fuse_conn *fc = fm->fc;
	struct fuse_req *req;
	ssize_t ret;

	if (args->force) {
		atomic_inc(&fc->num_waiting);
		req = fuse_request_alloc(fm, GFP_KERNEL | __GFP_NOFAIL);

		if (!args->nocreds)
			fuse_force_creds(req);

		__set_bit(FR_WAITING, &req->flags);
		__set_bit(FR_FORCE, &req->flags);
	} else {
		WARN_ON(args->nocreds);
		req = fuse_get_req(fm, false);
		if (IS_ERR(req))
			return PTR_ERR(req);
	}

	/* Needs to be done after fuse_get_req() so that fc->minor is valid */
	fuse_adjust_compat(fc, args);
	fuse_args_to_req(req, args);

	if (!args->noreply)
		__set_bit(FR_ISREPLY, &req->flags);
	__fuse_request_send(req);
	ret = req->out.h.error;
	if (!ret && args->out_argvar) {
		BUG_ON(args->out_numargs == 0);
		ret = args->out_args[args->out_numargs - 1].size;
	}
	fuse_put_request(req);

	return ret;
}

static bool fuse_request_queue_background(struct fuse_req *req)
{
	struct fuse_mount *fm = req->fm;
	struct fuse_conn *fc = fm->fc;
	bool queued = false;

	WARN_ON(!test_bit(FR_BACKGROUND, &req->flags));
	if (!test_bit(FR_WAITING, &req->flags)) {
		__set_bit(FR_WAITING, &req->flags);
		atomic_inc(&fc->num_waiting);
	}
	__set_bit(FR_ISREPLY, &req->flags);
	spin_lock(&fc->bg_lock);
	if (likely(fc->connected)) {
		fc->num_background++;
		if (fc->num_background == fc->max_background)
			fc->blocked = 1;
		list_add_tail(&req->list, &fc->bg_queue);
		flush_bg_queue(fc);
		queued = true;
	}
	spin_unlock(&fc->bg_lock);

	return queued;
}

int fuse_simple_background(struct fuse_mount *fm, struct fuse_args *args,
			    gfp_t gfp_flags)
{
	struct fuse_req *req;

	if (args->force) {
		WARN_ON(!args->nocreds);
		req = fuse_request_alloc(fm, gfp_flags);
		if (!req)
			return -ENOMEM;
		__set_bit(FR_BACKGROUND, &req->flags);
	} else {
		WARN_ON(args->nocreds);
		req = fuse_get_req(fm, true);
		if (IS_ERR(req))
			return PTR_ERR(req);
	}

	fuse_args_to_req(req, args);

	if (!fuse_request_queue_background(req)) {
		fuse_put_request(req);
		return -ENOTCONN;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fuse_simple_background);

static int fuse_simple_notify_reply(struct fuse_mount *fm,
				    struct fuse_args *args, u64 unique)
{
	struct fuse_req *req;
	struct fuse_iqueue *fiq = &fm->fc->iq;
	int err = 0;

	req = fuse_get_req(fm, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	__clear_bit(FR_ISREPLY, &req->flags);
	req->in.h.unique = unique;

	fuse_args_to_req(req, args);

	spin_lock(&fiq->lock);
	if (fiq->connected) {
		queue_request_and_unlock(fiq, req, false);
	} else {
		err = -ENODEV;
		spin_unlock(&fiq->lock);
		fuse_put_request(req);
	}

	return err;
}

/*
 * Lock the request.  Up to the next unlock_request() there mustn't be
 * anything that could cause a page-fault.  If the request was already
 * aborted bail out.
 */
static int lock_request(struct fuse_req *req)
{
	int err = 0;
	if (req) {
		spin_lock(&req->waitq.lock);
		if (test_bit(FR_ABORTED, &req->flags))
			err = -ENOENT;
		else
			set_bit(FR_LOCKED, &req->flags);
		spin_unlock(&req->waitq.lock);
	}
	return err;
}

/*
 * Unlock request.  If it was aborted while locked, caller is responsible
 * for unlocking and ending the request.
 */
static int unlock_request(struct fuse_req *req)
{
	int err = 0;
	if (req) {
		spin_lock(&req->waitq.lock);
		if (test_bit(FR_ABORTED, &req->flags))
			err = -ENOENT;
		else
			clear_bit(FR_LOCKED, &req->flags);
		spin_unlock(&req->waitq.lock);
	}
	return err;
}

struct fuse_copy_state {
	int write;
	struct fuse_req *req;
	struct iov_iter *iter;
	struct pipe_buffer *pipebufs;
	struct pipe_buffer *currbuf;
	struct pipe_inode_info *pipe;
	unsigned long nr_segs;
	struct page *pg;
	unsigned len;
	unsigned offset;
	unsigned move_pages:1;
};

static void fuse_copy_init(struct fuse_copy_state *cs, int write,
			   struct iov_iter *iter)
{
	memset(cs, 0, sizeof(*cs));
	cs->write = write;
	cs->iter = iter;
}

/* Unmap and put previous page of userspace buffer */
static void fuse_copy_finish(struct fuse_copy_state *cs)
{
	if (cs->currbuf) {
		struct pipe_buffer *buf = cs->currbuf;

		if (cs->write)
			buf->len = PAGE_SIZE - cs->len;
		cs->currbuf = NULL;
	} else if (cs->pg) {
		if (cs->write) {
			flush_dcache_page(cs->pg);
			set_page_dirty_lock(cs->pg);
		}
		put_page(cs->pg);
	}
	cs->pg = NULL;
}

/*
 * Get another pagefull of userspace buffer, and map it to kernel
 * address space, and lock request
 */
static int fuse_copy_fill(struct fuse_copy_state *cs)
{
	struct page *page;
	int err;

	err = unlock_request(cs->req);
	if (err)
		return err;

	fuse_copy_finish(cs);
	if (cs->pipebufs) {
		struct pipe_buffer *buf = cs->pipebufs;

		if (!cs->write) {
			err = pipe_buf_confirm(cs->pipe, buf);
			if (err)
				return err;

			BUG_ON(!cs->nr_segs);
			cs->currbuf = buf;
			cs->pg = buf->page;
			cs->offset = buf->offset;
			cs->len = buf->len;
			cs->pipebufs++;
			cs->nr_segs--;
		} else {
			if (cs->nr_segs >= cs->pipe->max_usage)
				return -EIO;

			page = alloc_page(GFP_HIGHUSER);
			if (!page)
				return -ENOMEM;

			buf->page = page;
			buf->offset = 0;
			buf->len = 0;

			cs->currbuf = buf;
			cs->pg = page;
			cs->offset = 0;
			cs->len = PAGE_SIZE;
			cs->pipebufs++;
			cs->nr_segs++;
		}
	} else {
		size_t off;
		err = iov_iter_get_pages2(cs->iter, &page, PAGE_SIZE, 1, &off);
		if (err < 0)
			return err;
		BUG_ON(!err);
		cs->len = err;
		cs->offset = off;
		cs->pg = page;
	}

	return lock_request(cs->req);
}

/* Do as much copy to/from userspace buffer as we can */
static int fuse_copy_do(struct fuse_copy_state *cs, void **val, unsigned *size)
{
	unsigned ncpy = min(*size, cs->len);
	if (val) {
		void *pgaddr = kmap_local_page(cs->pg);
		void *buf = pgaddr + cs->offset;

		if (cs->write)
			memcpy(buf, *val, ncpy);
		else
			memcpy(*val, buf, ncpy);

		kunmap_local(pgaddr);
		*val += ncpy;
	}
	*size -= ncpy;
	cs->len -= ncpy;
	cs->offset += ncpy;
	return ncpy;
}

static int fuse_check_page(struct page *page)
{
	if (page_mapcount(page) ||
	    page->mapping != NULL ||
	    (page->flags & PAGE_FLAGS_CHECK_AT_PREP &
	     ~(1 << PG_locked |
	       1 << PG_referenced |
	       1 << PG_uptodate |
	       1 << PG_lru |
	       1 << PG_active |
	       1 << PG_workingset |
	       1 << PG_reclaim |
	       1 << PG_waiters |
	       LRU_GEN_MASK | LRU_REFS_MASK))) {
		dump_page(page, "fuse: trying to steal weird page");
		return 1;
	}
	return 0;
}

static int fuse_try_move_page(struct fuse_copy_state *cs, struct page **pagep)
{
	int err;
	struct page *oldpage = *pagep;
	struct page *newpage;
	struct pipe_buffer *buf = cs->pipebufs;

	get_page(oldpage);
	err = unlock_request(cs->req);
	if (err)
		goto out_put_old;

	fuse_copy_finish(cs);

	err = pipe_buf_confirm(cs->pipe, buf);
	if (err)
		goto out_put_old;

	BUG_ON(!cs->nr_segs);
	cs->currbuf = buf;
	cs->len = buf->len;
	cs->pipebufs++;
	cs->nr_segs--;

	if (cs->len != PAGE_SIZE)
		goto out_fallback;

	if (!pipe_buf_try_steal(cs->pipe, buf))
		goto out_fallback;

	newpage = buf->page;

	if (!PageUptodate(newpage))
		SetPageUptodate(newpage);

	ClearPageMappedToDisk(newpage);

	if (fuse_check_page(newpage) != 0)
		goto out_fallback_unlock;

	/*
	 * This is a new and locked page, it shouldn't be mapped or
	 * have any special flags on it
	 */
	if (WARN_ON(page_mapped(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(page_has_private(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(PageDirty(oldpage) || PageWriteback(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(PageMlocked(oldpage)))
		goto out_fallback_unlock;

	replace_page_cache_page(oldpage, newpage);

	get_page(newpage);

	if (!(buf->flags & PIPE_BUF_FLAG_LRU))
		lru_cache_add(newpage);

	/*
	 * Release while we have extra ref on stolen page.  Otherwise
	 * anon_pipe_buf_release() might think the page can be reused.
	 */
	pipe_buf_release(cs->pipe, buf);

	err = 0;
	spin_lock(&cs->req->waitq.lock);
	if (test_bit(FR_ABORTED, &cs->req->flags))
		err = -ENOENT;
	else
		*pagep = newpage;
	spin_unlock(&cs->req->waitq.lock);

	if (err) {
		unlock_page(newpage);
		put_page(newpage);
		goto out_put_old;
	}

	unlock_page(oldpage);
	/* Drop ref for ap->pages[] array */
	put_page(oldpage);
	cs->len = 0;

	err = 0;
out_put_old:
	/* Drop ref obtained in this function */
	put_page(oldpage);
	return err;

out_fallback_unlock:
	unlock_page(newpage);
out_fallback:
	cs->pg = buf->page;
	cs->offset = buf->offset;

	err = lock_request(cs->req);
	if (!err)
		err = 1;

	goto out_put_old;
}

static int fuse_ref_page(struct fuse_copy_state *cs, struct page *page,
			 unsigned offset, unsigned count)
{
	struct pipe_buffer *buf;
	int err;

	if (cs->nr_segs >= cs->pipe->max_usage)
		return -EIO;

	get_page(page);
	err = unlock_request(cs->req);
	if (err) {
		put_page(page);
		return err;
	}

	fuse_copy_finish(cs);

	buf = cs->pipebufs;
	buf->page = page;
	buf->offset = offset;
	buf->len = count;

	cs->pipebufs++;
	cs->nr_segs++;
	cs->len = 0;

	return 0;
}

/*
 * Copy a page in the request to/from the userspace buffer.  Must be
 * done atomically
 */
static int fuse_copy_page(struct fuse_copy_state *cs, struct page **pagep,
			  unsigned offset, unsigned count, int zeroing)
{
	int err;
	struct page *page = *pagep;

	if (page && zeroing && count < PAGE_SIZE)
		clear_highpage(page);

	while (count) {
		if (cs->write && cs->pipebufs && page) {
			/*
			 * Can't control lifetime of pipe buffers, so always
			 * copy user pages.
			 */
			if (cs->req->args->user_pages) {
				err = fuse_copy_fill(cs);
				if (err)
					return err;
			} else {
				return fuse_ref_page(cs, page, offset, count);
			}
		} else if (!cs->len) {
			if (cs->move_pages && page &&
			    offset == 0 && count == PAGE_SIZE) {
				err = fuse_try_move_page(cs, pagep);
				if (err <= 0)
					return err;
			} else {
				err = fuse_copy_fill(cs);
				if (err)
					return err;
			}
		}
		if (page) {
			void *mapaddr = kmap_local_page(page);
			void *buf = mapaddr + offset;
			offset += fuse_copy_do(cs, &buf, &count);
			kunmap_local(mapaddr);
		} else
			offset += fuse_copy_do(cs, NULL, &count);
	}
	if (page && !cs->write)
		flush_dcache_page(page);
	return 0;
}

/* Copy pages in the request to/from userspace buffer */
static int fuse_copy_pages(struct fuse_copy_state *cs, unsigned nbytes,
			   int zeroing)
{
	unsigned i;
	struct fuse_req *req = cs->req;
	struct fuse_args_pages *ap = container_of(req->args, typeof(*ap), args);


	for (i = 0; i < ap->num_pages && (nbytes || zeroing); i++) {
		int err;
		unsigned int offset = ap->descs[i].offset;
		unsigned int count = min(nbytes, ap->descs[i].length);

		err = fuse_copy_page(cs, &ap->pages[i], offset, count, zeroing);
		if (err)
			return err;

		nbytes -= count;
	}
	return 0;
}

/* Copy a single argument in the request to/from userspace buffer */
static int fuse_copy_one(struct fuse_copy_state *cs, void *val, unsigned size)
{
	while (size) {
		if (!cs->len) {
			int err = fuse_copy_fill(cs);
			if (err)
				return err;
		}
		fuse_copy_do(cs, &val, &size);
	}
	return 0;
}

/* Copy request arguments to/from userspace buffer */
static int fuse_copy_args(struct fuse_copy_state *cs, unsigned numargs,
			  unsigned argpages, struct fuse_arg *args,
			  int zeroing)
{
	int err = 0;
	unsigned i;

	for (i = 0; !err && i < numargs; i++)  {
		struct fuse_arg *arg = &args[i];
		if (i == numargs - 1 && argpages)
			err = fuse_copy_pages(cs, arg->size, zeroing);
		else
			err = fuse_copy_one(cs, arg->value, arg->size);
	}
	return err;
}

static int forget_pending(struct fuse_iqueue *fiq)
{
	return fiq->forget_list_head.next != NULL;
}

static int request_pending(struct fuse_iqueue *fiq)
{
	return !list_empty(&fiq->pending) || !list_empty(&fiq->interrupts) ||
		forget_pending(fiq);
}

/*
 * Transfer an interrupt request to userspace
 *
 * Unlike other requests this is assembled on demand, without a need
 * to allocate a separate fuse_req structure.
 *
 * Called with fiq->lock held, releases it
 */
static int fuse_read_interrupt(struct fuse_iqueue *fiq,
			       struct fuse_copy_state *cs,
			       size_t nbytes, struct fuse_req *req)
__releases(fiq->lock)
{
	struct fuse_in_header ih;
	struct fuse_interrupt_in arg;
	unsigned reqsize = sizeof(ih) + sizeof(arg);
	int err;

	list_del_init(&req->intr_entry);
	memset(&ih, 0, sizeof(ih));
	memset(&arg, 0, sizeof(arg));
	ih.len = reqsize;
	ih.opcode = FUSE_INTERRUPT;
	ih.unique = (req->in.h.unique | FUSE_INT_REQ_BIT);
	arg.unique = req->in.h.unique;

	spin_unlock(&fiq->lock);
	if (nbytes < reqsize)
		return -EINVAL;

	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));
	fuse_copy_finish(cs);

	return err ? err : reqsize;
}

struct fuse_forget_link *fuse_dequeue_forget(struct fuse_iqueue *fiq,
					     unsigned int max,
					     unsigned int *countp)
{
	struct fuse_forget_link *head = fiq->forget_list_head.next;
	struct fuse_forget_link **newhead = &head;
	unsigned count;

	for (count = 0; *newhead != NULL && count < max; count++)
		newhead = &(*newhead)->next;

	fiq->forget_list_head.next = *newhead;
	*newhead = NULL;
	if (fiq->forget_list_head.next == NULL)
		fiq->forget_list_tail = &fiq->forget_list_head;

	if (countp != NULL)
		*countp = count;

	return head;
}
EXPORT_SYMBOL(fuse_dequeue_forget);

static int fuse_read_single_forget(struct fuse_iqueue *fiq,
				   struct fuse_copy_state *cs,
				   size_t nbytes)
__releases(fiq->lock)
{
	int err;
	struct fuse_forget_link *forget = fuse_dequeue_forget(fiq, 1, NULL);
	struct fuse_forget_in arg = {
		.nlookup = forget->forget_one.nlookup,
	};
	struct fuse_in_header ih = {
		.opcode = FUSE_FORGET,
		.nodeid = forget->forget_one.nodeid,
		.unique = fuse_get_unique(fiq),
		.len = sizeof(ih) + sizeof(arg),
	};

	spin_unlock(&fiq->lock);
	kfree(forget);
	if (nbytes < ih.len)
		return -EINVAL;

	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));
	fuse_copy_finish(cs);

	if (err)
		return err;

	return ih.len;
}

static int fuse_read_batch_forget(struct fuse_iqueue *fiq,
				   struct fuse_copy_state *cs, size_t nbytes)
__releases(fiq->lock)
{
	int err;
	unsigned max_forgets;
	unsigned count;
	struct fuse_forget_link *head;
	struct fuse_batch_forget_in arg = { .count = 0 };
	struct fuse_in_header ih = {
		.opcode = FUSE_BATCH_FORGET,
		.unique = fuse_get_unique(fiq),
		.len = sizeof(ih) + sizeof(arg),
	};

	if (nbytes < ih.len) {
		spin_unlock(&fiq->lock);
		return -EINVAL;
	}

	max_forgets = (nbytes - ih.len) / sizeof(struct fuse_forget_one);
	head = fuse_dequeue_forget(fiq, max_forgets, &count);
	spin_unlock(&fiq->lock);

	arg.count = count;
	ih.len += count * sizeof(struct fuse_forget_one);
	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));

	while (head) {
		struct fuse_forget_link *forget = head;

		if (!err) {
			err = fuse_copy_one(cs, &forget->forget_one,
					    sizeof(forget->forget_one));
		}
		head = forget->next;
		kfree(forget);
	}

	fuse_copy_finish(cs);

	if (err)
		return err;

	return ih.len;
}

static int fuse_read_forget(struct fuse_conn *fc, struct fuse_iqueue *fiq,
			    struct fuse_copy_state *cs,
			    size_t nbytes)
__releases(fiq->lock)
{
	if (fc->minor < 16 || fiq->forget_list_head.next->next == NULL)
		return fuse_read_single_forget(fiq, cs, nbytes);
	else
		return fuse_read_batch_forget(fiq, cs, nbytes);
}

/*
 * Read a single request into the userspace filesystem's buffer.  This
 * function waits until a request is available, then removes it from
 * the pending list and copies request data to userspace buffer.  If
 * no reply is needed (FORGET) or request has been aborted or there
 * was an error during the copying then it's finished by calling
 * fuse_request_end().  Otherwise add it to the processing list, and set
 * the 'sent' flag.
 */
static ssize_t fuse_dev_do_read(struct fuse_dev *fud, struct file *file,
				struct fuse_copy_state *cs, size_t nbytes)
{
	ssize_t err;
	struct fuse_conn *fc = fud->fc;
	struct fuse_iqueue *fiq = &fc->iq;
	struct fuse_pqueue *fpq = &fud->pq;
	struct fuse_req *req;
	struct fuse_args *args;
	unsigned reqsize;
	unsigned int hash;

	/*
	 * Require sane minimum read buffer - that has capacity for fixed part
	 * of any request header + negotiated max_write room for data.
	 *
	 * Historically libfuse reserves 4K for fixed header room, but e.g.
	 * GlusterFS reserves only 80 bytes
	 *
	 *	= `sizeof(fuse_in_header) + sizeof(fuse_write_in)`
	 *
	 * which is the absolute minimum any sane filesystem should be using
	 * for header room.
	 */
	if (nbytes < max_t(size_t, FUSE_MIN_READ_BUFFER,
			   sizeof(struct fuse_in_header) +
			   sizeof(struct fuse_write_in) +
			   fc->max_write))
		return -EINVAL;

 restart:
	for (;;) {
		spin_lock(&fiq->lock);
		if (!fiq->connected || request_pending(fiq))
			break;
		spin_unlock(&fiq->lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		err = wait_event_interruptible_exclusive(fiq->waitq,
				!fiq->connected || request_pending(fiq));
		if (err)
			return err;
	}

	if (!fiq->connected) {
		err = fc->aborted ? -ECONNABORTED : -ENODEV;
		goto err_unlock;
	}

	if (!list_empty(&fiq->interrupts)) {
		req = list_entry(fiq->interrupts.next, struct fuse_req,
				 intr_entry);
		return fuse_read_interrupt(fiq, cs, nbytes, req);
	}

	if (forget_pending(fiq)) {
		if (list_empty(&fiq->pending) || fiq->forget_batch-- > 0)
			return fuse_read_forget(fc, fiq, cs, nbytes);

		if (fiq->forget_batch <= -8)
			fiq->forget_batch = 16;
	}

	req = list_entry(fiq->pending.next, struct fuse_req, list);
	clear_bit(FR_PENDING, &req->flags);
	list_del_init(&req->list);
	spin_unlock(&fiq->lock);

	args = req->args;
	reqsize = req->in.h.len;

	/* If request is too large, reply with an error and restart the read */
	if (nbytes < reqsize) {
		req->out.h.error = -EIO;
		/* SETXATTR is special, since it may contain too large data */
		if (args->opcode == FUSE_SETXATTR)
			req->out.h.error = -E2BIG;
		fuse_request_end(req);
		goto restart;
	}
	spin_lock(&fpq->lock);
	/*
	 *  Must not put request on fpq->io queue after having been shut down by
	 *  fuse_abort_conn()
	 */
	if (!fpq->connected) {
		req->out.h.error = err = -ECONNABORTED;
		goto out_end;

	}
	list_add(&req->list, &fpq->io);
	spin_unlock(&fpq->lock);
	cs->req = req;
	err = fuse_copy_one(cs, &req->in.h, sizeof(req->in.h));
	if (!err)
		err = fuse_copy_args(cs, args->in_numargs, args->in_pages,
				     (struct fuse_arg *) args->in_args, 0);
	fuse_copy_finish(cs);
	spin_lock(&fpq->lock);
	clear_bit(FR_LOCKED, &req->flags);
	if (!fpq->connected) {
		err = fc->aborted ? -ECONNABORTED : -ENODEV;
		goto out_end;
	}
	if (err) {
		req->out.h.error = -EIO;
		goto out_end;
	}
	if (!test_bit(FR_ISREPLY, &req->flags)) {
		err = reqsize;
		goto out_end;
	}
	hash = fuse_req_hash(req->in.h.unique);
	list_move_tail(&req->list, &fpq->processing[hash]);
	__fuse_get_request(req);
	set_bit(FR_SENT, &req->flags);
	spin_unlock(&fpq->lock);
	/* matches barrier in request_wait_answer() */
	smp_mb__after_atomic();
	if (test_bit(FR_INTERRUPTED, &req->flags))
		queue_interrupt(req);
	fuse_put_request(req);

	return reqsize;

out_end:
	if (!test_bit(FR_PRIVATE, &req->flags))
		list_del_init(&req->list);
	spin_unlock(&fpq->lock);
	fuse_request_end(req);
	return err;

 err_unlock:
	spin_unlock(&fiq->lock);
	return err;
}

static int fuse_dev_open(struct inode *inode, struct file *file)
{
	/*
	 * The fuse device's file's private_data is used to hold
	 * the fuse_conn(ection) when it is mounted, and is used to
	 * keep track of whether the file has been mounted already.
	 */
	file->private_data = NULL;
	return 0;
}

static ssize_t fuse_dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_copy_state cs;
	struct file *file = iocb->ki_filp;
	struct fuse_dev *fud = fuse_get_dev(file);

	if (!fud)
		return -EPERM;

	if (!user_backed_iter(to))
		return -EINVAL;

	fuse_copy_init(&cs, 1, to);

	return fuse_dev_do_read(fud, file, &cs, iov_iter_count(to));
}

static ssize_t fuse_dev_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe,
				    size_t len, unsigned int flags)
{
	int total, ret;
	int page_nr = 0;
	struct pipe_buffer *bufs;
	struct fuse_copy_state cs;
	struct fuse_dev *fud = fuse_get_dev(in);

	if (!fud)
		return -EPERM;

	bufs = kvmalloc_array(pipe->max_usage, sizeof(struct pipe_buffer),
			      GFP_KERNEL);
	if (!bufs)
		return -ENOMEM;

	fuse_copy_init(&cs, 1, NULL);
	cs.pipebufs = bufs;
	cs.pipe = pipe;
	ret = fuse_dev_do_read(fud, in, &cs, len);
	if (ret < 0)
		goto out;

	if (pipe_occupancy(pipe->head, pipe->tail) + cs.nr_segs > pipe->max_usage) {
		ret = -EIO;
		goto out;
	}

	for (ret = total = 0; page_nr < cs.nr_segs; total += ret) {
		/*
		 * Need to be careful about this.  Having buf->ops in module
		 * code can Oops if the buffer persists after module unload.
		 */
		bufs[page_nr].ops = &nosteal_pipe_buf_ops;
		bufs[page_nr].flags = 0;
		ret = add_to_pipe(pipe, &bufs[page_nr++]);
		if (unlikely(ret < 0))
			break;
	}
	if (total)
		ret = total;
out:
	for (; page_nr < cs.nr_segs; page_nr++)
		put_page(bufs[page_nr].page);

	kvfree(bufs);
	return ret;
}

static int fuse_notify_poll(struct fuse_conn *fc, unsigned int size,
			    struct fuse_copy_state *cs)
{
	struct fuse_notify_poll_wakeup_out outarg;
	int err = -EINVAL;

	if (size != sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	fuse_copy_finish(cs);
	return fuse_notify_poll_wakeup(fc, &outarg);

err:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_inval_inode(struct fuse_conn *fc, unsigned int size,
				   struct fuse_copy_state *cs)
{
	struct fuse_notify_inval_inode_out outarg;
	int err = -EINVAL;

	if (size != sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;
	fuse_copy_finish(cs);

	down_read(&fc->killsb);
	err = fuse_reverse_inval_inode(fc, outarg.ino,
				       outarg.off, outarg.len);
	up_read(&fc->killsb);
	return err;

err:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_inval_entry(struct fuse_conn *fc, unsigned int size,
				   struct fuse_copy_state *cs)
{
	struct fuse_notify_inval_entry_out outarg;
	int err = -ENOMEM;
	char *buf;
	struct qstr name;

	buf = kzalloc(FUSE_NAME_MAX + 1, GFP_KERNEL);
	if (!buf)
		goto err;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	err = -ENAMETOOLONG;
	if (outarg.namelen > FUSE_NAME_MAX)
		goto err;

	err = -EINVAL;
	if (size != sizeof(outarg) + outarg.namelen + 1)
		goto err;

	name.name = buf;
	name.len = outarg.namelen;
	err = fuse_copy_one(cs, buf, outarg.namelen + 1);
	if (err)
		goto err;
	fuse_copy_finish(cs);
	buf[outarg.namelen] = 0;

	down_read(&fc->killsb);
	err = fuse_reverse_inval_entry(fc, outarg.parent, 0, &name, outarg.flags);
	up_read(&fc->killsb);
	kfree(buf);
	return err;

err:
	kfree(buf);
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_delete(struct fuse_conn *fc, unsigned int size,
			      struct fuse_copy_state *cs)
{
	struct fuse_notify_delete_out outarg;
	int err = -ENOMEM;
	char *buf;
	struct qstr name;

	buf = kzalloc(FUSE_NAME_MAX + 1, GFP_KERNEL);
	if (!buf)
		goto err;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	err = -ENAMETOOLONG;
	if (outarg.namelen > FUSE_NAME_MAX)
		goto err;

	err = -EINVAL;
	if (size != sizeof(outarg) + outarg.namelen + 1)
		goto err;

	name.name = buf;
	name.len = outarg.namelen;
	err = fuse_copy_one(cs, buf, outarg.namelen + 1);
	if (err)
		goto err;
	fuse_copy_finish(cs);
	buf[outarg.namelen] = 0;

	down_read(&fc->killsb);
	err = fuse_reverse_inval_entry(fc, outarg.parent, outarg.child, &name, 0);
	up_read(&fc->killsb);
	kfree(buf);
	return err;

err:
	kfree(buf);
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_store(struct fuse_conn *fc, unsigned int size,
			     struct fuse_copy_state *cs)
{
	struct fuse_notify_store_out outarg;
	struct inode *inode;
	struct address_space *mapping;
	u64 nodeid;
	int err;
	pgoff_t index;
	unsigned int offset;
	unsigned int num;
	loff_t file_size;
	loff_t end;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto out_finish;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto out_finish;

	err = -EINVAL;
	if (size - sizeof(outarg) != outarg.size)
		goto out_finish;

	nodeid = outarg.nodeid;

	down_read(&fc->killsb);

	err = -ENOENT;
	inode = fuse_ilookup(fc, nodeid,  NULL);
	if (!inode)
		goto out_up_killsb;

	mapping = inode->i_mapping;
	index = outarg.offset >> PAGE_SHIFT;
	offset = outarg.offset & ~PAGE_MASK;
	file_size = i_size_read(inode);
	end = outarg.offset + outarg.size;
	if (end > file_size) {
		file_size = end;
		fuse_write_update_attr(inode, file_size, outarg.size);
	}

	num = outarg.size;
	while (num) {
		struct page *page;
		unsigned int this_num;

		err = -ENOMEM;
		page = find_or_create_page(mapping, index,
					   mapping_gfp_mask(mapping));
		if (!page)
			goto out_iput;

		this_num = min_t(unsigned, num, PAGE_SIZE - offset);
		err = fuse_copy_page(cs, &page, offset, this_num, 0);
		if (!PageUptodate(page) && !err && offset == 0 &&
		    (this_num == PAGE_SIZE || file_size == end)) {
			zero_user_segment(page, this_num, PAGE_SIZE);
			SetPageUptodate(page);
		}
		unlock_page(page);
		put_page(page);

		if (err)
			goto out_iput;

		num -= this_num;
		offset = 0;
		index++;
	}

	err = 0;

out_iput:
	iput(inode);
out_up_killsb:
	up_read(&fc->killsb);
out_finish:
	fuse_copy_finish(cs);
	return err;
}

struct fuse_retrieve_args {
	struct fuse_args_pages ap;
	struct fuse_notify_retrieve_in inarg;
};

static void fuse_retrieve_end(struct fuse_mount *fm, struct fuse_args *args,
			      int error)
{
	struct fuse_retrieve_args *ra =
		container_of(args, typeof(*ra), ap.args);

	release_pages(ra->ap.pages, ra->ap.num_pages);
	kfree(ra);
}

static int fuse_retrieve(struct fuse_mount *fm, struct inode *inode,
			 struct fuse_notify_retrieve_out *outarg)
{
	int err;
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	loff_t file_size;
	unsigned int num;
	unsigned int offset;
	size_t total_len = 0;
	unsigned int num_pages;
	struct fuse_conn *fc = fm->fc;
	struct fuse_retrieve_args *ra;
	size_t args_size = sizeof(*ra);
	struct fuse_args_pages *ap;
	struct fuse_args *args;

	offset = outarg->offset & ~PAGE_MASK;
	file_size = i_size_read(inode);

	num = min(outarg->size, fc->max_write);
	if (outarg->offset > file_size)
		num = 0;
	else if (outarg->offset + num > file_size)
		num = file_size - outarg->offset;

	num_pages = (num + offset + PAGE_SIZE - 1) >> PAGE_SHIFT;
	num_pages = min(num_pages, fc->max_pages);

	args_size += num_pages * (sizeof(ap->pages[0]) + sizeof(ap->descs[0]));

	ra = kzalloc(args_size, GFP_KERNEL);
	if (!ra)
		return -ENOMEM;

	ap = &ra->ap;
	ap->pages = (void *) (ra + 1);
	ap->descs = (void *) (ap->pages + num_pages);

	args = &ap->args;
	args->nodeid = outarg->nodeid;
	args->opcode = FUSE_NOTIFY_REPLY;
	args->in_numargs = 2;
	args->in_pages = true;
	args->end = fuse_retrieve_end;

	index = outarg->offset >> PAGE_SHIFT;

	while (num && ap->num_pages < num_pages) {
		struct page *page;
		unsigned int this_num;

		page = find_get_page(mapping, index);
		if (!page)
			break;

		this_num = min_t(unsigned, num, PAGE_SIZE - offset);
		ap->pages[ap->num_pages] = page;
		ap->descs[ap->num_pages].offset = offset;
		ap->descs[ap->num_pages].length = this_num;
		ap->num_pages++;

		offset = 0;
		num -= this_num;
		total_len += this_num;
		index++;
	}
	ra->inarg.offset = outarg->offset;
	ra->inarg.size = total_len;
	args->in_args[0].size = sizeof(ra->inarg);
	args->in_args[0].value = &ra->inarg;
	args->in_args[1].size = total_len;

	err = fuse_simple_notify_reply(fm, args, outarg->notify_unique);
	if (err)
		fuse_retrieve_end(fm, args, err);

	return err;
}

static int fuse_notify_retrieve(struct fuse_conn *fc, unsigned int size,
				struct fuse_copy_state *cs)
{
	struct fuse_notify_retrieve_out outarg;
	struct fuse_mount *fm;
	struct inode *inode;
	u64 nodeid;
	int err;

	err = -EINVAL;
	if (size != sizeof(outarg))
		goto copy_finish;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto copy_finish;

	fuse_copy_finish(cs);

	down_read(&fc->killsb);
	err = -ENOENT;
	nodeid = outarg.nodeid;

	inode = fuse_ilookup(fc, nodeid, &fm);
	if (inode) {
		err = fuse_retrieve(fm, inode, &outarg);
		iput(inode);
	}
	up_read(&fc->killsb);

	return err;

copy_finish:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify(struct fuse_conn *fc, enum fuse_notify_code code,
		       unsigned int size, struct fuse_copy_state *cs)
{
	/* Don't try to move pages (yet) */
	cs->move_pages = 0;

	switch (code) {
	case FUSE_NOTIFY_POLL:
		return fuse_notify_poll(fc, size, cs);

	case FUSE_NOTIFY_INVAL_INODE:
		return fuse_notify_inval_inode(fc, size, cs);

	case FUSE_NOTIFY_INVAL_ENTRY:
		return fuse_notify_inval_entry(fc, size, cs);

	case FUSE_NOTIFY_STORE:
		return fuse_notify_store(fc, size, cs);

	case FUSE_NOTIFY_RETRIEVE:
		return fuse_notify_retrieve(fc, size, cs);

	case FUSE_NOTIFY_DELETE:
		return fuse_notify_delete(fc, size, cs);

	default:
		fuse_copy_finish(cs);
		return -EINVAL;
	}
}

/* Look up request on processing list by unique ID */
static struct fuse_req *request_find(struct fuse_pqueue *fpq, u64 unique)
{
	unsigned int hash = fuse_req_hash(unique);
	struct fuse_req *req;

	list_for_each_entry(req, &fpq->processing[hash], list) {
		if (req->in.h.unique == unique)
			return req;
	}
	return NULL;
}

static int copy_out_args(struct fuse_copy_state *cs, struct fuse_args *args,
			 unsigned nbytes)
{
	unsigned reqsize = sizeof(struct fuse_out_header);

	reqsize += fuse_len_args(args->out_numargs, args->out_args);

	if (reqsize < nbytes || (reqsize > nbytes && !args->out_argvar))
		return -EINVAL;
	else if (reqsize > nbytes) {
		struct fuse_arg *lastarg = &args->out_args[args->out_numargs-1];
		unsigned diffsize = reqsize - nbytes;

		if (diffsize > lastarg->size)
			return -EINVAL;
		lastarg->size -= diffsize;
	}
	return fuse_copy_args(cs, args->out_numargs, args->out_pages,
			      args->out_args, args->page_zeroing);
}

/*
 * Write a single reply to a request.  First the header is copied from
 * the write buffer.  The request is then searched on the processing
 * list by the unique ID found in the header.  If found, then remove
 * it from the list and copy the rest of the buffer to the request.
 * The request is finished by calling fuse_request_end().
 */
static ssize_t fuse_dev_do_write(struct fuse_dev *fud,
				 struct fuse_copy_state *cs, size_t nbytes)
{
	int err;
	struct fuse_conn *fc = fud->fc;
	struct fuse_pqueue *fpq = &fud->pq;
	struct fuse_req *req;
	struct fuse_out_header oh;

	err = -EINVAL;
	if (nbytes < sizeof(struct fuse_out_header))
		goto out;

	err = fuse_copy_one(cs, &oh, sizeof(oh));
	if (err)
		goto copy_finish;

	err = -EINVAL;
	if (oh.len != nbytes)
		goto copy_finish;

	/*
	 * Zero oh.unique indicates unsolicited notification message
	 * and error contains notification code.
	 */
	if (!oh.unique) {
		err = fuse_notify(fc, oh.error, nbytes - sizeof(oh), cs);
		goto out;
	}

	err = -EINVAL;
	if (oh.error <= -512 || oh.error > 0)
		goto copy_finish;

	spin_lock(&fpq->lock);
	req = NULL;
	if (fpq->connected)
		req = request_find(fpq, oh.unique & ~FUSE_INT_REQ_BIT);

	err = -ENOENT;
	if (!req) {
		spin_unlock(&fpq->lock);
		goto copy_finish;
	}

	/* Is it an interrupt reply ID? */
	if (oh.unique & FUSE_INT_REQ_BIT) {
		__fuse_get_request(req);
		spin_unlock(&fpq->lock);

		err = 0;
		if (nbytes != sizeof(struct fuse_out_header))
			err = -EINVAL;
		else if (oh.error == -ENOSYS)
			fc->no_interrupt = 1;
		else if (oh.error == -EAGAIN)
			err = queue_interrupt(req);

		fuse_put_request(req);

		goto copy_finish;
	}

	clear_bit(FR_SENT, &req->flags);
	list_move(&req->list, &fpq->io);
	req->out.h = oh;
	set_bit(FR_LOCKED, &req->flags);
	spin_unlock(&fpq->lock);
	cs->req = req;
	if (!req->args->page_replace)
		cs->move_pages = 0;

	if (oh.error)
		err = nbytes != sizeof(oh) ? -EINVAL : 0;
	else
		err = copy_out_args(cs, req->args, nbytes);
	fuse_copy_finish(cs);

	if (!err && req->in.h.opcode == FUSE_CANONICAL_PATH && !oh.error) {
		char *path = (char *)req->args->out_args[0].value;

		path[req->args->out_args[0].size - 1] = 0;
		req->out.h.error =
			kern_path(path, 0, req->args->canonical_path);
	}

	if (!err && (req->in.h.opcode == FUSE_LOOKUP ||
		     req->in.h.opcode == (FUSE_LOOKUP | FUSE_POSTFILTER)) &&
		req->args->out_args[1].size == sizeof(struct fuse_entry_bpf_out)) {
		struct fuse_entry_bpf_out *febo = (struct fuse_entry_bpf_out *)
				req->args->out_args[1].value;
		struct fuse_entry_bpf *feb = container_of(febo, struct fuse_entry_bpf, out);

		if (febo->backing_action == FUSE_ACTION_REPLACE)
			feb->backing_file = fget(febo->backing_fd);
		if (febo->bpf_action == FUSE_ACTION_REPLACE)
			feb->bpf_file = fget(febo->bpf_fd);
	}

	spin_lock(&fpq->lock);
	clear_bit(FR_LOCKED, &req->flags);
	if (!fpq->connected)
		err = -ENOENT;
	else if (err)
		req->out.h.error = -EIO;
	if (!test_bit(FR_PRIVATE, &req->flags))
		list_del_init(&req->list);
	spin_unlock(&fpq->lock);

	fuse_request_end(req);
out:
	return err ? err : nbytes;

copy_finish:
	fuse_copy_finish(cs);
	goto out;
}

static ssize_t fuse_dev_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct fuse_copy_state cs;
	struct fuse_dev *fud = fuse_get_dev(iocb->ki_filp);

	if (!fud)
		return -EPERM;

	if (!user_backed_iter(from))
		return -EINVAL;

	fuse_copy_init(&cs, 0, from);

	return fuse_dev_do_write(fud, &cs, iov_iter_count(from));
}

static ssize_t fuse_dev_splice_write(struct pipe_inode_info *pipe,
				     struct file *out, loff_t *ppos,
				     size_t len, unsigned int flags)
{
	unsigned int head, tail, mask, count;
	unsigned nbuf;
	unsigned idx;
	struct pipe_buffer *bufs;
	struct fuse_copy_state cs;
	struct fuse_dev *fud;
	size_t rem;
	ssize_t ret;

	fud = fuse_get_dev(out);
	if (!fud)
		return -EPERM;

	pipe_lock(pipe);

	head = pipe->head;
	tail = pipe->tail;
	mask = pipe->ring_size - 1;
	count = head - tail;

	bufs = kvmalloc_array(count, sizeof(struct pipe_buffer), GFP_KERNEL);
	if (!bufs) {
		pipe_unlock(pipe);
		return -ENOMEM;
	}

	nbuf = 0;
	rem = 0;
	for (idx = tail; idx != head && rem < len; idx++)
		rem += pipe->bufs[idx & mask].len;

	ret = -EINVAL;
	if (rem < len)
		goto out_free;

	rem = len;
	while (rem) {
		struct pipe_buffer *ibuf;
		struct pipe_buffer *obuf;

		if (WARN_ON(nbuf >= count || tail == head))
			goto out_free;

		ibuf = &pipe->bufs[tail & mask];
		obuf = &bufs[nbuf];

		if (rem >= ibuf->len) {
			*obuf = *ibuf;
			ibuf->ops = NULL;
			tail++;
			pipe->tail = tail;
		} else {
			if (!pipe_buf_get(pipe, ibuf))
				goto out_free;

			*obuf = *ibuf;
			obuf->flags &= ~PIPE_BUF_FLAG_GIFT;
			obuf->len = rem;
			ibuf->offset += obuf->len;
			ibuf->len -= obuf->len;
		}
		nbuf++;
		rem -= obuf->len;
	}
	pipe_unlock(pipe);

	fuse_copy_init(&cs, 0, NULL);
	cs.pipebufs = bufs;
	cs.nr_segs = nbuf;
	cs.pipe = pipe;

	if (flags & SPLICE_F_MOVE)
		cs.move_pages = 1;

	ret = fuse_dev_do_write(fud, &cs, len);

	pipe_lock(pipe);
out_free:
	for (idx = 0; idx < nbuf; idx++) {
		struct pipe_buffer *buf = &bufs[idx];

		if (buf->ops)
			pipe_buf_release(pipe, buf);
	}
	pipe_unlock(pipe);

	kvfree(bufs);
	return ret;
}

static __poll_t fuse_dev_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;
	struct fuse_iqueue *fiq;
	struct fuse_dev *fud = fuse_get_dev(file);

	if (!fud)
		return EPOLLERR;

	fiq = &fud->fc->iq;
	poll_wait(file, &fiq->waitq, wait);

	spin_lock(&fiq->lock);
	if (!fiq->connected)
		mask = EPOLLERR;
	else if (request_pending(fiq))
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock(&fiq->lock);

	return mask;
}

/* Abort all requests on the given list (pending or processing) */
static void end_requests(struct list_head *head)
{
	while (!list_empty(head)) {
		struct fuse_req *req;
		req = list_entry(head->next, struct fuse_req, list);
		req->out.h.error = -ECONNABORTED;
		clear_bit(FR_SENT, &req->flags);
		list_del_init(&req->list);
		fuse_request_end(req);
	}
}

static void end_polls(struct fuse_conn *fc)
{
	struct rb_node *p;

	p = rb_first(&fc->polled_files);

	while (p) {
		struct fuse_file *ff;
		ff = rb_entry(p, struct fuse_file, polled_node);
		wake_up_interruptible_all(&ff->poll_wait);

		p = rb_next(p);
	}
}

/*
 * Abort all requests.
 *
 * Emergency exit in case of a malicious or accidental deadlock, or just a hung
 * filesystem.
 *
 * The same effect is usually achievable through killing the filesystem daemon
 * and all users of the filesystem.  The exception is the combination of an
 * asynchronous request and the tricky deadlock (see
 * Documentation/filesystems/fuse.rst).
 *
 * Aborting requests under I/O goes as follows: 1: Separate out unlocked
 * requests, they should be finished off immediately.  Locked requests will be
 * finished after unlock; see unlock_request(). 2: Finish off the unlocked
 * requests.  It is possible that some request will finish before we can.  This
 * is OK, the request will in that case be removed from the list before we touch
 * it.
 */
void fuse_abort_conn(struct fuse_conn *fc)
{
	struct fuse_iqueue *fiq = &fc->iq;

	spin_lock(&fc->lock);
	if (fc->connected) {
		struct fuse_dev *fud;
		struct fuse_req *req, *next;
		LIST_HEAD(to_end);
		unsigned int i;

		/* Background queuing checks fc->connected under bg_lock */
		spin_lock(&fc->bg_lock);
		fc->connected = 0;
		spin_unlock(&fc->bg_lock);

		fuse_set_initialized(fc);
		list_for_each_entry(fud, &fc->devices, entry) {
			struct fuse_pqueue *fpq = &fud->pq;

			spin_lock(&fpq->lock);
			fpq->connected = 0;
			list_for_each_entry_safe(req, next, &fpq->io, list) {
				req->out.h.error = -ECONNABORTED;
				spin_lock(&req->waitq.lock);
				set_bit(FR_ABORTED, &req->flags);
				if (!test_bit(FR_LOCKED, &req->flags)) {
					set_bit(FR_PRIVATE, &req->flags);
					__fuse_get_request(req);
					list_move(&req->list, &to_end);
				}
				spin_unlock(&req->waitq.lock);
			}
			for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
				list_splice_tail_init(&fpq->processing[i],
						      &to_end);
			spin_unlock(&fpq->lock);
		}
		spin_lock(&fc->bg_lock);
		fc->blocked = 0;
		fc->max_background = UINT_MAX;
		flush_bg_queue(fc);
		spin_unlock(&fc->bg_lock);

		spin_lock(&fiq->lock);
		fiq->connected = 0;
		list_for_each_entry(req, &fiq->pending, list)
			clear_bit(FR_PENDING, &req->flags);
		list_splice_tail_init(&fiq->pending, &to_end);
		while (forget_pending(fiq))
			kfree(fuse_dequeue_forget(fiq, 1, NULL));
		wake_up_all(&fiq->waitq);
		spin_unlock(&fiq->lock);
		kill_fasync(&fiq->fasync, SIGIO, POLL_IN);
		end_polls(fc);
		wake_up_all(&fc->blocked_waitq);
		spin_unlock(&fc->lock);

		end_requests(&to_end);
	} else {
		spin_unlock(&fc->lock);
	}
}
EXPORT_SYMBOL_GPL(fuse_abort_conn);

void fuse_wait_aborted(struct fuse_conn *fc)
{
	/* matches implicit memory barrier in fuse_drop_waiting() */
	smp_mb();
	wait_event(fc->blocked_waitq, atomic_read(&fc->num_waiting) == 0);
}

int fuse_dev_release(struct inode *inode, struct file *file)
{
	struct fuse_dev *fud = fuse_get_dev(file);

	if (fud) {
		struct fuse_conn *fc = fud->fc;
		struct fuse_pqueue *fpq = &fud->pq;
		LIST_HEAD(to_end);
		unsigned int i;

		spin_lock(&fpq->lock);
		WARN_ON(!list_empty(&fpq->io));
		for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
			list_splice_init(&fpq->processing[i], &to_end);
		spin_unlock(&fpq->lock);

		end_requests(&to_end);

		/* Are we the last open device? */
		if (atomic_dec_and_test(&fc->dev_count)) {
			WARN_ON(fc->iq.fasync != NULL);
			fuse_abort_conn(fc);
		}
		fuse_dev_free(fud);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fuse_dev_release);

static int fuse_dev_fasync(int fd, struct file *file, int on)
{
	struct fuse_dev *fud = fuse_get_dev(file);

	if (!fud)
		return -EPERM;

	/* No locking - fasync_helper does its own locking */
	return fasync_helper(fd, file, on, &fud->fc->iq.fasync);
}

static int fuse_device_clone(struct fuse_conn *fc, struct file *new)
{
	struct fuse_dev *fud;

	if (new->private_data)
		return -EINVAL;

	fud = fuse_dev_alloc_install(fc);
	if (!fud)
		return -ENOMEM;

	new->private_data = fud;
	atomic_inc(&fc->dev_count);

	return 0;
}

static long fuse_dev_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int res;
	int oldfd;
	struct fuse_dev *fud = NULL;

	switch (cmd) {
	case FUSE_DEV_IOC_CLONE:
		res = -EFAULT;
		if (!get_user(oldfd, (__u32 __user *)arg)) {
			struct file *old = fget(oldfd);

			res = -EINVAL;
			if (old) {
				/*
				 * Check against file->f_op because CUSE
				 * uses the same ioctl handler.
				 */
				if (old->f_op == file->f_op &&
				    old->f_cred->user_ns ==
					    file->f_cred->user_ns)
					fud = fuse_get_dev(old);

				if (fud) {
					mutex_lock(&fuse_mutex);
					res = fuse_device_clone(fud->fc, file);
					mutex_unlock(&fuse_mutex);
				}
				fput(old);
			}
		}
		break;
	case FUSE_DEV_IOC_PASSTHROUGH_OPEN:
		res = -EFAULT;
		if (!get_user(oldfd, (__u32 __user *)arg)) {
			res = -EINVAL;
			fud = fuse_get_dev(file);
			if (fud)
				res = fuse_passthrough_open(fud, oldfd);
		}
		break;
	default:
		res = -ENOTTY;
		break;
	}
	return res;
}

const struct file_operations fuse_dev_operations = {
	.owner		= THIS_MODULE,
	.open		= fuse_dev_open,
	.llseek		= no_llseek,
	.read_iter	= fuse_dev_read,
	.splice_read	= fuse_dev_splice_read,
	.write_iter	= fuse_dev_write,
	.splice_write	= fuse_dev_splice_write,
	.poll		= fuse_dev_poll,
	.release	= fuse_dev_release,
	.fasync		= fuse_dev_fasync,
	.unlocked_ioctl = fuse_dev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};
EXPORT_SYMBOL_GPL(fuse_dev_operations);

static struct miscdevice fuse_miscdevice = {
	.minor = FUSE_MINOR,
	.name  = "fuse",
	.fops = &fuse_dev_operations,
};

int __init fuse_dev_init(void)
{
	int err = -ENOMEM;
	fuse_req_cachep = kmem_cache_create("fuse_request",
					    sizeof(struct fuse_req),
					    0, 0, NULL);
	if (!fuse_req_cachep)
		goto out;

	err = misc_register(&fuse_miscdevice);
	if (err)
		goto out_cache_clean;

	return 0;

 out_cache_clean:
	kmem_cache_destroy(fuse_req_cachep);
 out:
	return err;
}

void fuse_dev_cleanup(void)
{
	misc_deregister(&fuse_miscdevice);
	kmem_cache_destroy(fuse_req_cachep);
}
