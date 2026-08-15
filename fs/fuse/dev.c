// SPDX-License-Identifier: GPL-2.0
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
*/

#include "dev.h"
#include "args.h"
#include "dev_uring_i.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/pipe_fs_i.h>
#include <linux/swap.h>
#include <linux/splice.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include "fuse_trace.h"

MODULE_ALIAS_MISCDEV(FUSE_MINOR);
MODULE_ALIAS("devname:fuse");

static DECLARE_WAIT_QUEUE_HEAD(fuse_dev_waitq);

static struct kmem_cache *fuse_req_cachep;

static void fuse_request_init(struct fuse_chan *fch, struct fuse_req *req)
{
	INIT_LIST_HEAD(&req->list);
	INIT_LIST_HEAD(&req->intr_entry);
	init_waitqueue_head(&req->waitq);
	refcount_set(&req->count, 1);
	__set_bit(FR_PENDING, &req->flags);
	req->chan = fch;
	req->create_time = jiffies;
}

static struct fuse_req *fuse_request_alloc(struct fuse_chan *fch, gfp_t flags)
{
	struct fuse_req *req = kmem_cache_zalloc(fuse_req_cachep, flags);
	if (req)
		fuse_request_init(fch, req);

	return req;
}

static void fuse_request_free(struct fuse_req *req)
{
	WARN_ON(!list_empty(&req->intr_entry));
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

void fuse_chan_set_initialized(struct fuse_chan *fch, struct fuse_chan_param *param)
{
	if (param) {
		fch->minor = param->minor;
		fch->max_write = param->max_write;
		fch->max_pages = param->max_pages;
	}

	/* Make sure stores before this are seen on another CPU */
	smp_wmb();
	fch->initialized = 1;
	wake_up_all(&fch->blocked_waitq);
}

static bool fuse_block_alloc(struct fuse_chan *fch, bool for_background)
{
	return !fch->initialized || (for_background && fch->blocked) ||
	       (fch->io_uring && fch->connected && !fuse_uring_ready(fch));
}

static void fuse_drop_waiting(struct fuse_chan *fch)
{
	/*
	 * lockess check of fch->connected is okay, because atomic_dec_and_test()
	 * provides a memory barrier matched with the one in fuse_chan_wait_aborted()
	 * to ensure no wake-up is missed.
	 */
	if (atomic_dec_and_test(&fch->num_waiting) &&
	    !READ_ONCE(fch->connected)) {
		/* wake up aborters */
		wake_up_all(&fch->blocked_waitq);
	}
}

static void fuse_put_request(struct fuse_req *req);

static struct fuse_req *fuse_get_req(struct fuse_chan *fch, bool for_background)
{
	struct fuse_req *req;
	int err;

	atomic_inc(&fch->num_waiting);

	if (fuse_block_alloc(fch, for_background)) {
		err = -EINTR;
		if (wait_event_state_exclusive(fch->blocked_waitq,
				!fuse_block_alloc(fch, for_background),
				(TASK_KILLABLE | TASK_FREEZABLE)))
			goto out;
	}

	/* Matches smp_wmb() in fuse_chan_set_initialized() */
	smp_rmb();

	err = -ENOTCONN;
	if (!fch->connected)
		goto out;

	req = fuse_request_alloc(fch, GFP_KERNEL);
	err = -ENOMEM;
	if (!req) {
		if (for_background)
			wake_up(&fch->blocked_waitq);
		goto out;
	}

	__set_bit(FR_WAITING, &req->flags);
	if (for_background)
		__set_bit(FR_BACKGROUND, &req->flags);

	return req;

 out:
	fuse_drop_waiting(fch);
	return ERR_PTR(err);
}

static void fuse_put_request(struct fuse_req *req)
{
	struct fuse_chan *fch = req->chan;

	if (refcount_dec_and_test(&req->count)) {
		if (test_bit(FR_BACKGROUND, &req->flags)) {
			/*
			 * We get here in the unlikely case that a background
			 * request was allocated but not sent
			 */
			spin_lock(&fch->bg_lock);
			if (!fch->blocked)
				wake_up(&fch->blocked_waitq);
			spin_unlock(&fch->bg_lock);
		}

		if (test_bit(FR_WAITING, &req->flags)) {
			__clear_bit(FR_WAITING, &req->flags);
			fuse_drop_waiting(fch);
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

static u64 fuse_get_unique_locked(struct fuse_iqueue *fiq)
{
	fiq->reqctr += FUSE_REQ_ID_STEP;
	return fiq->reqctr;
}

u64 fuse_get_unique(struct fuse_iqueue *fiq)
{
	u64 ret;

	spin_lock(&fiq->lock);
	ret = fuse_get_unique_locked(fiq);
	spin_unlock(&fiq->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(fuse_get_unique);

unsigned int fuse_req_hash(u64 unique)
{
	return hash_long(unique & ~FUSE_INT_REQ_BIT, FUSE_PQ_HASH_BITS);
}
EXPORT_SYMBOL_GPL(fuse_req_hash);

/*
 * A new request is available, wake fiq->waitq
 */
static void fuse_dev_wake_and_unlock(struct fuse_iqueue *fiq)
__releases(fiq->lock)
{
	wake_up(&fiq->waitq);
	kill_fasync(&fiq->fasync, SIGIO, POLL_IN);
	spin_unlock(&fiq->lock);
}

struct fuse_forget_link *fuse_alloc_forget(void)
{
	return kzalloc_obj(struct fuse_forget_link, GFP_KERNEL_ACCOUNT);
}

void fuse_dev_queue_forget(struct fuse_iqueue *fiq,
			   struct fuse_forget_link *forget)
{
	spin_lock(&fiq->lock);
	if (fiq->connected) {
		fiq->forget_list_tail->next = forget;
		fiq->forget_list_tail = forget;
		fuse_dev_wake_and_unlock(fiq);
	} else {
		kfree(forget);
		spin_unlock(&fiq->lock);
	}
}

void fuse_dev_queue_interrupt(struct fuse_iqueue *fiq, struct fuse_req *req)
{
	spin_lock(&fiq->lock);
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
		} else  {
			fuse_dev_wake_and_unlock(fiq);
		}
	} else {
		spin_unlock(&fiq->lock);
	}
}

static inline void fuse_request_assign_unique_locked(struct fuse_iqueue *fiq,
						     struct fuse_req *req)
{
	if (req->in.h.opcode != FUSE_NOTIFY_REPLY)
		req->in.h.unique = fuse_get_unique_locked(fiq);

	/* tracepoint captures in.h.unique and in.h.len */
	trace_fuse_request_send(req);
}

inline void fuse_request_assign_unique(struct fuse_iqueue *fiq,
				       struct fuse_req *req)
{
	if (req->in.h.opcode != FUSE_NOTIFY_REPLY)
		req->in.h.unique = fuse_get_unique(fiq);

	/* tracepoint captures in.h.unique and in.h.len */
	trace_fuse_request_send(req);
}
EXPORT_SYMBOL_GPL(fuse_request_assign_unique);

static void fuse_dev_queue_req(struct fuse_iqueue *fiq, struct fuse_req *req)
{
	spin_lock(&fiq->lock);
	if (fiq->connected) {
		fuse_request_assign_unique_locked(fiq, req);
		list_add_tail(&req->list, &fiq->pending);
		fuse_dev_wake_and_unlock(fiq);
	} else {
		spin_unlock(&fiq->lock);
		req->out.h.error = -ENOTCONN;
		clear_bit(FR_PENDING, &req->flags);
		fuse_request_end(req);
	}
}

static const struct fuse_iqueue_ops fuse_dev_fiq_ops = {
	.send_forget	= fuse_dev_queue_forget,
	.send_interrupt	= fuse_dev_queue_interrupt,
	.send_req	= fuse_dev_queue_req,
};

void fuse_iqueue_init(struct fuse_iqueue *fiq, const struct fuse_iqueue_ops *ops, void *priv)
{
	spin_lock_init(&fiq->lock);
	init_waitqueue_head(&fiq->waitq);
	INIT_LIST_HEAD(&fiq->pending);
	INIT_LIST_HEAD(&fiq->interrupts);
	fiq->forget_list_tail = &fiq->forget_list_head;
	fiq->connected = 1;
	fiq->ops = ops;
	fiq->priv = priv;
}
EXPORT_SYMBOL_GPL(fuse_iqueue_init);

void fuse_chan_release(struct fuse_chan *fch)
{
	struct fuse_iqueue *fiq = &fch->iq;

	if (fiq->ops->release)
		fiq->ops->release(fiq);

	if (fch->timeout.req_timeout)
		cancel_delayed_work_sync(&fch->timeout.work);
}

void fuse_chan_free(struct fuse_chan *fch)
{
	WARN_ON(!list_empty(&fch->devices));
	kfree(fch->pq_prealloc);
	kfree(fch);
}
EXPORT_SYMBOL_GPL(fuse_chan_free);

struct fuse_chan *fuse_chan_new(void)
{
	struct fuse_chan *fch = kzalloc_obj(struct fuse_chan);
	if (!fch)
		return NULL;

	spin_lock_init(&fch->lock);
	INIT_LIST_HEAD(&fch->devices);
	spin_lock_init(&fch->bg_lock);
	INIT_LIST_HEAD(&fch->bg_queue);
	init_waitqueue_head(&fch->blocked_waitq);
	atomic_set(&fch->num_waiting, 0);
	fch->max_background = FUSE_DEFAULT_MAX_BACKGROUND;
	fch->initialized = 0;
	fch->blocked = 0;
	fch->connected = 1;
	fch->timeout.req_timeout = 0;

	return fch;
}
EXPORT_SYMBOL_GPL(fuse_chan_new);

struct list_head *fuse_pqueue_alloc(void)
{
	struct list_head *pq = kzalloc_objs(struct list_head, FUSE_PQ_HASH_SIZE);

	if (pq) {
		for (int i = 0; i < FUSE_PQ_HASH_SIZE; i++)
			INIT_LIST_HEAD(&pq[i]);
	}
	return pq;
}

struct fuse_chan *fuse_dev_chan_new(void)
{
	struct fuse_chan *fch __free(kfree) = fuse_chan_new();
	if (!fch)
		return NULL;

	fch->pq_prealloc = fuse_pqueue_alloc();
	if (!fch->pq_prealloc)
		return NULL;

	fuse_iqueue_init(&fch->iq, &fuse_dev_fiq_ops, NULL);

	return no_free_ptr(fch);
}
EXPORT_SYMBOL_GPL(fuse_dev_chan_new);

unsigned int fuse_chan_num_background(struct fuse_chan *fch)
{
	return READ_ONCE(fch->num_background);
}

unsigned int fuse_chan_max_background(struct fuse_chan *fch)
{
	return READ_ONCE(fch->max_background);
}

void fuse_chan_max_background_set(struct fuse_chan *fch, unsigned int val)
{
	spin_lock(&fch->bg_lock);
	fch->max_background = val;
	fch->blocked = fch->num_background >= fch->max_background;
	if (!fch->blocked)
		wake_up(&fch->blocked_waitq);
	spin_unlock(&fch->bg_lock);
}

unsigned int fuse_chan_num_waiting(struct fuse_chan *fch)
{
	return atomic_read(&fch->num_waiting);
}

void fuse_chan_set_fc(struct fuse_chan *fch, struct fuse_conn *fc)
{
	fch->conn = fc;
}

void fuse_chan_io_uring_enable(struct fuse_chan *fch)
{
	fch->io_uring = 1;
}

void fuse_pqueue_init(struct fuse_pqueue *fpq)
{
	spin_lock_init(&fpq->lock);
	INIT_LIST_HEAD(&fpq->io);
	fpq->connected = 1;
	fpq->processing = NULL;
}

static struct fuse_dev *fuse_dev_alloc_no_pq(void)
{
	struct fuse_dev *fud;

	fud = kzalloc_obj(struct fuse_dev);
	if (!fud)
		return NULL;

	refcount_set(&fud->ref, 1);
	fuse_pqueue_init(&fud->pq);

	return fud;
}

struct fuse_dev *fuse_dev_alloc(void)
{
	struct fuse_dev *fud __free(kfree) = fuse_dev_alloc_no_pq();
	if (!fud)
		return NULL;

	fud->pq.processing = fuse_pqueue_alloc();
	if (!fud->pq.processing)
		return NULL;

	return no_free_ptr(fud);
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc);

/*
 * Installs @fch into @fud, return true on success.  "Consumes" @pq in either case.
 */
static bool fuse_dev_install_with_pq(struct fuse_dev *fud, struct fuse_chan *fch,
				     struct list_head *pq)
{
	struct fuse_chan *old_fch;

	guard(spinlock)(&fch->lock);
	/*
	 * Pairs with:
	 *  - xchg() in fuse_dev_release()
	 *  - smp_load_acquire() in fuse_dev_fc_get()
	 */
	old_fch = cmpxchg(&fud->chan, NULL, fch);
	if (old_fch) {
		/*
		 * failed to set fud->chan because
		 *  - it was already set to a different fc
		 *  - it was set to disconneted
		 */
		kfree(pq);
		return false;
	}
	if (pq) {
		WARN_ON(fud->pq.processing);
		fud->pq.processing = pq;
	}
	list_add_tail(&fud->entry, &fch->devices);
	fuse_conn_get(fch->conn);
	wake_up_all(&fuse_dev_waitq);
	return true;
}

void fuse_dev_install(struct fuse_dev *fud, struct fuse_chan *fch)
{
	struct list_head *pq = fch->pq_prealloc;

	fch->pq_prealloc = NULL;
	if (!fuse_dev_install_with_pq(fud, fch, pq)) {
		/* Channel is not usable without a dev */
		fuse_chan_abort(fch, false);
	}
}
EXPORT_SYMBOL_GPL(fuse_dev_install);

struct fuse_dev *fuse_dev_alloc_install(struct fuse_chan *fch)
{
	struct fuse_dev *fud;

	fud = fuse_dev_alloc_no_pq();
	if (!fud)
		return NULL;

	fuse_dev_install(fud, fch);
	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc_install);

void fuse_dev_put(struct fuse_dev *fud)
{
	struct fuse_chan *fch;

	if (!refcount_dec_and_test(&fud->ref))
		return;

	fch = fuse_dev_chan_get(fud);
	if (fch && fch != FUSE_DEV_CHAN_DISCONNECTED) {
		/* This is the virtiofs case (fuse_dev_release() not called) */
		spin_lock(&fch->lock);
		list_del(&fud->entry);
		spin_unlock(&fch->lock);

		fuse_conn_put(fch->conn);
	}
	kfree(fud->pq.processing);
	kfree(fud);
}
EXPORT_SYMBOL_GPL(fuse_dev_put);

bool fuse_dev_is_installed(struct fuse_dev *fud)
{
	struct fuse_chan *fch = fuse_dev_chan_get(fud);

	return fch != NULL && fch != FUSE_DEV_CHAN_DISCONNECTED;
}

/*
 * Checks if @fc matches the one installed in @fud
 */
bool fuse_dev_verify(struct fuse_dev *fud, struct fuse_chan *fch)
{
	return fuse_dev_chan_get(fud) == fch;
}

bool fuse_dev_is_sync_init(struct fuse_dev *fud)
{
	return fud->sync_init;
}

struct fuse_dev *fuse_dev_grab(struct file *file)
{
	struct fuse_dev *fud = fuse_file_to_fud(file);

	refcount_inc(&fud->ref);
	return fud;
}

static void fuse_send_one(struct fuse_iqueue *fiq, struct fuse_req *req)
{
	req->in.h.len = sizeof(struct fuse_in_header) +
		fuse_len_args(req->args->in_numargs,
			      (struct fuse_arg *) req->args->in_args);
	fiq->ops->send_req(fiq, req);
}

void fuse_chan_queue_forget(struct fuse_chan *fch, struct fuse_forget_link *forget,
			    u64 nodeid, u64 nlookup)
{
	struct fuse_iqueue *fiq = &fch->iq;

	forget->forget_one.nodeid = nodeid;
	forget->forget_one.nlookup = nlookup;

	fiq->ops->send_forget(fiq, forget);
}

static void flush_bg_queue(struct fuse_chan *fch)
{
	struct fuse_iqueue *fiq = &fch->iq;

	while (fch->active_background < fch->max_background &&
	       !list_empty(&fch->bg_queue)) {
		struct fuse_req *req;

		req = list_first_entry(&fch->bg_queue, struct fuse_req, list);
		list_del(&req->list);
		fch->active_background++;
		fuse_send_one(fiq, req);
	}
}

void fuse_request_bg_finish(struct fuse_chan *fch, struct fuse_req *req)
{
	lockdep_assert_held(&fch->bg_lock);

	clear_bit(FR_BACKGROUND, &req->flags);
	if (fch->num_background == fch->max_background) {
		fch->blocked = 0;
		wake_up(&fch->blocked_waitq);
	} else if (!fch->blocked) {
		/*
		 * Wake up next waiter, if any.  It's okay to use
		 * waitqueue_active(), as we've already synced up
		 * fch->blocked with waiters with the wake_up() call
		 * above.
		 */
		if (waitqueue_active(&fch->blocked_waitq))
			wake_up(&fch->blocked_waitq);
	}

	fch->num_background--;
	fch->active_background--;
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
	struct fuse_chan *fch = req->chan;
	struct fuse_iqueue *fiq = &fch->iq;

	if (test_and_set_bit(FR_FINISHED, &req->flags))
		goto put_request;

	trace_fuse_request_end(req);
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
		spin_lock(&fch->bg_lock);
		fuse_request_bg_finish(fch, req);
		flush_bg_queue(fch);
		spin_unlock(&fch->bg_lock);
	} else {
		/* Wake up waiter sleeping in request_wait_answer() */
		wake_up(&req->waitq);
	}

	if (test_bit(FR_ASYNC, &req->flags))
		req->args->end(req->args, req->out.h.error);
put_request:
	fuse_put_request(req);
}
EXPORT_SYMBOL_GPL(fuse_request_end);

static int queue_interrupt(struct fuse_req *req)
{
	struct fuse_iqueue *fiq = &req->chan->iq;

	/* Check for we've sent request to interrupt this req */
	if (unlikely(!test_bit(FR_INTERRUPTED, &req->flags)))
		return -EINVAL;

	fiq->ops->send_interrupt(fiq, req);

	return 0;
}

bool fuse_remove_pending_req(struct fuse_req *req, spinlock_t *lock)
{
	spin_lock(lock);
	if (test_bit(FR_PENDING, &req->flags)) {
		/*
		 * FR_PENDING does not get cleared as the request will end
		 * up in destruction anyway.
		 */
		list_del(&req->list);
		spin_unlock(lock);
		__fuse_put_request(req);
		req->out.h.error = -EINTR;
		return true;
	}
	spin_unlock(lock);
	return false;
}

static void request_wait_answer(struct fuse_req *req)
{
	struct fuse_chan *fch = req->chan;
	struct fuse_iqueue *fiq = &fch->iq;
	int err;

	if (!fch->no_interrupt) {
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
		bool removed;

		/* Only fatal signals may interrupt this */
		err = wait_event_killable(req->waitq,
					test_bit(FR_FINISHED, &req->flags));
		if (!err)
			return;

		if (req->args->abort_on_kill) {
			fuse_chan_abort(fch, false);
			return;
		}

		if (test_bit(FR_URING, &req->flags))
			removed = fuse_uring_remove_pending_req(req);
		else
			removed = fuse_remove_pending_req(req, &fiq->lock);
		if (removed)
			return;
	}

	/*
	 * Either request is already in userspace, or it was forced.
	 * Wait it out.
	 */
	wait_event(req->waitq, test_bit(FR_FINISHED, &req->flags));
}

static void __fuse_request_send(struct fuse_req *req)
{
	struct fuse_iqueue *fiq = &req->chan->iq;

	BUG_ON(test_bit(FR_BACKGROUND, &req->flags));

	/* acquire extra reference, since request is still needed after
	   fuse_request_end() */
	__fuse_get_request(req);
	fuse_send_one(fiq, req);

	request_wait_answer(req);
	/* Pairs with smp_wmb() in fuse_request_end() */
	smp_rmb();
}

static void fuse_adjust_compat(struct fuse_chan *fch, struct fuse_args *args)
{
	if (fch->minor < 4 && args->opcode == FUSE_STATFS)
		args->out_args[0].size = FUSE_COMPAT_STATFS_SIZE;

	if (fch->minor < 9) {
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
	if (fch->minor < 12) {
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

static void fuse_args_to_req(struct fuse_req *req, struct fuse_args *args)
{
	req->in.h.opcode = args->opcode;
	req->in.h.nodeid = args->nodeid;
	req->in.h.uid = args->uid;
	req->in.h.gid = args->gid;
	req->in.h.pid = args->pid;
	req->args = args;
	if (args->is_ext)
		req->in.h.total_extlen = args->in_args[args->ext_idx].size / 8;
	if (args->end)
		__set_bit(FR_ASYNC, &req->flags);
}

ssize_t fuse_chan_send(struct fuse_chan *fch, struct fuse_args *args)
{
	struct fuse_req *req;
	ssize_t ret;

	if (args->force) {
		atomic_inc(&fch->num_waiting);
		req = fuse_request_alloc(fch, GFP_KERNEL | __GFP_NOFAIL);

		__set_bit(FR_WAITING, &req->flags);
		if (!args->abort_on_kill)
			__set_bit(FR_FORCE, &req->flags);
	} else {
		req = fuse_get_req(fch, false);
		if (IS_ERR(req))
			return PTR_ERR(req);
	}

	/* Needs to be done after fuse_get_req() so that fch->minor is valid */
	fuse_adjust_compat(fch, args);
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

#ifdef CONFIG_FUSE_IO_URING
static bool fuse_request_queue_background_uring(struct fuse_req *req)
{
	struct fuse_iqueue *fiq = &req->chan->iq;

	req->in.h.len = sizeof(struct fuse_in_header) +
		fuse_len_args(req->args->in_numargs,
			      (struct fuse_arg *) req->args->in_args);
	fuse_request_assign_unique(fiq, req);

	return fuse_uring_queue_bq_req(req);
}
#endif

/*
 * @return true if queued
 */
static int fuse_request_queue_background(struct fuse_req *req)
{
	struct fuse_chan *fch = req->chan;
	bool queued = false;

	WARN_ON(!test_bit(FR_BACKGROUND, &req->flags));
	if (!test_bit(FR_WAITING, &req->flags)) {
		__set_bit(FR_WAITING, &req->flags);
		atomic_inc(&fch->num_waiting);
	}
	__set_bit(FR_ISREPLY, &req->flags);

#ifdef CONFIG_FUSE_IO_URING
	if (fuse_uring_ready(fch))
		return fuse_request_queue_background_uring(req);
#endif

	spin_lock(&fch->bg_lock);
	if (likely(fch->connected)) {
		fch->num_background++;
		if (fch->num_background == fch->max_background)
			fch->blocked = 1;
		list_add_tail(&req->list, &fch->bg_queue);
		flush_bg_queue(fch);
		queued = true;
	}
	spin_unlock(&fch->bg_lock);

	return queued;
}

int fuse_chan_send_bg(struct fuse_chan *fch, struct fuse_args *args, gfp_t gfp_flags)
{
	struct fuse_req *req;

	if (args->force) {
		req = fuse_request_alloc(fch, gfp_flags);
		if (!req)
			return -ENOMEM;
		__set_bit(FR_BACKGROUND, &req->flags);
	} else {
		req = fuse_get_req(fch, true);
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

int fuse_chan_send_notify_reply(struct fuse_chan *fch, struct fuse_args *args, u64 unique)
{
	struct fuse_req *req;
	struct fuse_iqueue *fiq = &fch->iq;

	req = fuse_get_req(fch, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	__clear_bit(FR_ISREPLY, &req->flags);
	req->in.h.unique = unique;

	fuse_args_to_req(req, args);

	fuse_send_one(fiq, req);

	return 0;
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

void fuse_copy_init(struct fuse_copy_state *cs, bool write,
		    struct iov_iter *iter)
{
	memset(cs, 0, sizeof(*cs));
	cs->write = write;
	cs->iter = iter;
}

/* Unmap and put previous page of userspace buffer */
void fuse_copy_finish(struct fuse_copy_state *cs)
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
	if (cs->is_uring)
		cs->ring.copied_sz += ncpy;

	return ncpy;
}

static int fuse_check_folio(struct folio *folio)
{
	if (folio_mapped(folio) ||
	    folio->mapping != NULL ||
	    (folio->flags.f & PAGE_FLAGS_CHECK_AT_PREP &
	     ~(1 << PG_locked |
	       1 << PG_referenced |
	       1 << PG_lru |
	       1 << PG_active |
	       1 << PG_workingset |
	       1 << PG_reclaim |
	       1 << PG_waiters |
	       LRU_GEN_MASK | LRU_REFS_MASK))) {
		dump_page(&folio->page, "fuse: trying to steal weird page");
		return 1;
	}
	return 0;
}

/*
 * Attempt to steal a page from the splice() pipe and move it into the
 * pagecache. If successful, the pointer in @pagep will be updated. The
 * folio that was originally in @pagep will lose a reference and the new
 * folio returned in @pagep will carry a reference.
 */
static int fuse_try_move_folio(struct fuse_copy_state *cs, struct folio **foliop)
{
	int err;
	struct folio *oldfolio = *foliop;
	struct folio *newfolio;
	struct pipe_buffer *buf = cs->pipebufs;

	folio_get(oldfolio);
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

	if (cs->len != folio_size(oldfolio))
		goto out_fallback;

	if (!pipe_buf_try_steal(cs->pipe, buf))
		goto out_fallback;

	newfolio = page_folio(buf->page);

	folio_clear_uptodate(newfolio);
	folio_clear_mappedtodisk(newfolio);

	if (folio_test_large(newfolio))
		goto out_fallback_unlock;

	if (fuse_check_folio(newfolio) != 0)
		goto out_fallback_unlock;

	/*
	 * This is a new and locked page, it shouldn't be mapped or
	 * have any special flags on it
	 */
	if (WARN_ON(folio_mapped(oldfolio)))
		goto out_fallback_unlock;
	if (WARN_ON(folio_has_private(oldfolio)))
		goto out_fallback_unlock;
	if (WARN_ON(folio_test_dirty(oldfolio) ||
				folio_test_writeback(oldfolio)))
		goto out_fallback_unlock;
	if (WARN_ON(folio_test_mlocked(oldfolio)))
		goto out_fallback_unlock;

	err = lock_request(cs->req);
	if (err)
		goto out_fallback_unlock;

	replace_page_cache_folio(oldfolio, newfolio);

	folio_get(newfolio);

	if (!(buf->flags & PIPE_BUF_FLAG_LRU))
		folio_add_lru(newfolio);

	/*
	 * Release while we have extra ref on stolen page.  Otherwise
	 * anon_pipe_buf_release() might think the page can be reused.
	 */
	pipe_buf_release(cs->pipe, buf);

	*foliop = newfolio;
	folio_unlock(oldfolio);
	/* Drop ref for ap->pages[] array */
	folio_put(oldfolio);
	cs->len = 0;

	err = 0;
out_put_old:
	/* Drop ref obtained in this function */
	folio_put(oldfolio);
	return err;

out_fallback_unlock:
	folio_unlock(newfolio);
out_fallback:
	cs->pg = buf->page;
	cs->offset = buf->offset;

	err = lock_request(cs->req);
	if (!err)
		err = 1;

	goto out_put_old;
}

static int fuse_ref_folio(struct fuse_copy_state *cs, struct folio *folio,
			  unsigned offset, unsigned count)
{
	struct pipe_buffer *buf;
	int err;

	if (cs->nr_segs >= cs->pipe->max_usage)
		return -EIO;

	folio_get(folio);
	err = unlock_request(cs->req);
	if (err) {
		folio_put(folio);
		return err;
	}

	fuse_copy_finish(cs);

	buf = cs->pipebufs;
	buf->page = &folio->page;
	buf->offset = offset;
	buf->len = count;

	cs->pipebufs++;
	cs->nr_segs++;
	cs->len = 0;

	return lock_request(cs->req);
}

/*
 * Copy a folio in the request to/from the userspace buffer.  Must be
 * done atomically
 */
int fuse_copy_folio(struct fuse_copy_state *cs, struct folio **foliop,
		    unsigned offset, unsigned count, int zeroing)
{
	int err;
	struct folio *folio = *foliop;
	size_t size;

	if (folio) {
		size = folio_size(folio);
		if (zeroing && count < size)
			folio_zero_range(folio, 0, size);
	}

	while (count) {
		if (cs->write && cs->pipebufs && folio) {
			/*
			 * Can't control lifetime of pipe buffers, so always
			 * copy user pages.
			 */
			if (cs->req->args->user_pages) {
				err = fuse_copy_fill(cs);
				if (err)
					return err;
			} else {
				return fuse_ref_folio(cs, folio, offset, count);
			}
		} else if (!cs->len) {
			if (cs->move_folios && folio &&
			    offset == 0 && count == size) {
				err = fuse_try_move_folio(cs, foliop);
				if (err <= 0)
					return err;
			} else {
				err = fuse_copy_fill(cs);
				if (err)
					return err;
			}
		}
		if (folio) {
			void *mapaddr = kmap_local_folio(folio, offset);
			void *buf = mapaddr;
			unsigned int copy = count;
			unsigned int bytes_copied;

			if (folio_test_highmem(folio) && count > PAGE_SIZE - offset_in_page(offset))
				copy = PAGE_SIZE - offset_in_page(offset);

			bytes_copied = fuse_copy_do(cs, &buf, &copy);
			kunmap_local(mapaddr);
			offset += bytes_copied;
			count -= bytes_copied;
		} else
			offset += fuse_copy_do(cs, NULL, &count);
	}
	if (folio && !cs->write)
		flush_dcache_folio(folio);
	return 0;
}

/* Copy folios in the request to/from userspace buffer */
static int fuse_copy_folios(struct fuse_copy_state *cs, unsigned nbytes,
			    int zeroing)
{
	unsigned i;
	struct fuse_req *req = cs->req;
	struct fuse_args_pages *ap = container_of(req->args, typeof(*ap), args);

	for (i = 0; i < ap->num_folios && (nbytes || zeroing); i++) {
		int err;
		unsigned int offset = ap->descs[i].offset;
		unsigned int count = min(nbytes, ap->descs[i].length);

		err = fuse_copy_folio(cs, &ap->folios[i], offset, count, zeroing);
		if (err)
			return err;

		nbytes -= count;
	}
	return 0;
}

/* Copy a single argument in the request to/from userspace buffer */
int fuse_copy_one(struct fuse_copy_state *cs, void *val, unsigned size)
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
int fuse_copy_args(struct fuse_copy_state *cs, unsigned numargs,
		   unsigned argpages, struct fuse_arg *args,
		   int zeroing)
{
	int err = 0;
	unsigned i;

	for (i = 0; !err && i < numargs; i++)  {
		struct fuse_arg *arg = &args[i];
		if (i == numargs - 1 && argpages)
			err = fuse_copy_folios(cs, arg->size, zeroing);
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
static int fuse_read_interrupt(struct fuse_iqueue *fiq, struct fuse_copy_state *cs)
__releases(fiq->lock)
{
	struct fuse_req *req = list_first_entry(&fiq->interrupts, struct fuse_req, intr_entry);
	struct fuse_interrupt_in arg = {
		.unique = req->in.h.unique,
	};
	struct fuse_in_header ih = {
		.opcode = FUSE_INTERRUPT,
		.unique = (req->in.h.unique | FUSE_INT_REQ_BIT),
		.len = sizeof(ih) + sizeof(arg),
	};
	int err;

	list_del_init(&req->intr_entry);
	spin_unlock(&fiq->lock);

	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));
	fuse_copy_finish(cs);

	return err ? err : ih.len;
}

static struct fuse_forget_link *fuse_dequeue_forget(struct fuse_iqueue *fiq,
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

static int fuse_read_single_forget(struct fuse_iqueue *fiq,
				   struct fuse_copy_state *cs)
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
		.unique = fuse_get_unique_locked(fiq),
		.len = sizeof(ih) + sizeof(arg),
	};

	spin_unlock(&fiq->lock);
	kfree(forget);

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
		.unique = fuse_get_unique_locked(fiq),
		.len = sizeof(ih) + sizeof(arg),
	};

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

static int fuse_read_forget(struct fuse_chan *fch, struct fuse_iqueue *fiq,
			    struct fuse_copy_state *cs,
			    size_t nbytes)
__releases(fiq->lock)
{
	if (fch->minor < 16 || fiq->forget_list_head.next->next == NULL)
		return fuse_read_single_forget(fiq, cs);
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
	struct fuse_chan *fch = fud->chan;
	struct fuse_iqueue *fiq = &fch->iq;
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
			   fch->max_write))
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
		err = fch->abort_with_err ? -ECONNABORTED : -ENODEV;
		goto err_unlock;
	}

	if (!list_empty(&fiq->interrupts))
		return fuse_read_interrupt(fiq, cs);

	if (forget_pending(fiq)) {
		if (list_empty(&fiq->pending) || fiq->forget_batch-- > 0)
			return fuse_read_forget(fch, fiq, cs, nbytes);

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
	 *  fuse_chan_abort()
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
		err = fch->abort_with_err ? -ECONNABORTED : -ENODEV;
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
	trace_fuse_request_sent(req);
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
	struct fuse_dev *fud = fuse_dev_alloc_no_pq();

	if (!fud)
		return -ENOMEM;

	file->private_data = fud;
	return 0;
}

struct fuse_dev *fuse_get_dev(struct file *file)
{
	struct fuse_dev *fud = fuse_file_to_fud(file);
	int err;

	if (unlikely(!fuse_dev_chan_get(fud))) {
		/* only block waiting for mount if sync init was requested */
		if (!fud->sync_init)
			return ERR_PTR(-EPERM);

		err = wait_event_interruptible(fuse_dev_waitq, fuse_dev_chan_get(fud) != NULL);
		if (err)
			return ERR_PTR(err);
	}

	return fud;
}

static ssize_t fuse_dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_copy_state cs;
	struct file *file = iocb->ki_filp;
	struct fuse_dev *fud = fuse_get_dev(file);

	if (IS_ERR(fud))
		return PTR_ERR(fud);

	if (!user_backed_iter(to))
		return -EINVAL;

	fuse_copy_init(&cs, true, to);

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

	if (IS_ERR(fud))
		return PTR_ERR(fud);

	bufs = kvmalloc_objs(struct pipe_buffer, pipe->max_usage);
	if (!bufs)
		return -ENOMEM;

	fuse_copy_init(&cs, true, NULL);
	cs.pipebufs = bufs;
	cs.pipe = pipe;
	ret = fuse_dev_do_read(fud, in, &cs, len);
	if (ret < 0)
		goto out;

	if (pipe_buf_usage(pipe) + cs.nr_segs > pipe->max_usage) {
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

/*
 * Resending all processing queue requests.
 *
 * During a FUSE daemon panics and failover, it is possible for some inflight
 * requests to be lost and never returned. As a result, applications awaiting
 * replies would become stuck forever. To address this, we can use notification
 * to trigger resending of these pending requests to the FUSE daemon, ensuring
 * they are properly processed again.
 *
 * Please note that this strategy is applicable only to idempotent requests or
 * if the FUSE daemon takes careful measures to avoid processing duplicated
 * non-idempotent requests.
 */
void fuse_chan_resend(struct fuse_chan *fch)
{
	struct fuse_dev *fud;
	struct fuse_req *req, *next;
	struct fuse_iqueue *fiq = &fch->iq;
	LIST_HEAD(to_queue);
	unsigned int i;

	spin_lock(&fch->lock);
	if (!fch->connected) {
		spin_unlock(&fch->lock);
		return;
	}

	list_for_each_entry(fud, &fch->devices, entry) {
		struct fuse_pqueue *fpq = &fud->pq;

		spin_lock(&fpq->lock);
		for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
			list_splice_tail_init(&fpq->processing[i], &to_queue);
		spin_unlock(&fpq->lock);
	}
	spin_unlock(&fch->lock);

	list_for_each_entry_safe(req, next, &to_queue, list) {
		set_bit(FR_PENDING, &req->flags);
		clear_bit(FR_SENT, &req->flags);
		/* mark the request as resend request */
		req->in.h.unique |= FUSE_UNIQUE_RESEND;
	}

	spin_lock(&fiq->lock);
	if (!fiq->connected) {
		spin_unlock(&fiq->lock);
		list_for_each_entry(req, &to_queue, list)
			clear_bit(FR_PENDING, &req->flags);
		fuse_dev_end_requests(&to_queue);
		return;
	}
	/*
	 * Remove interrupt entries for resent requests to prevent stale
	 * intr_entry on fiq->interrupts after the request is re-queued.
	 */
	list_for_each_entry(req, &to_queue, list) {
		if (test_bit(FR_INTERRUPTED, &req->flags))
			list_del_init(&req->intr_entry);
	}
	/* iq and pq requests are both oldest to newest */
	list_splice(&to_queue, &fiq->pending);
	fuse_dev_wake_and_unlock(fiq);
}

/* Look up request on processing list by unique ID */
struct fuse_req *fuse_request_find(struct fuse_pqueue *fpq, u64 unique)
{
	unsigned int hash = fuse_req_hash(unique);
	struct fuse_req *req;

	list_for_each_entry(req, &fpq->processing[hash], list) {
		if (req->in.h.unique == unique)
			return req;
	}
	return NULL;
}

int fuse_copy_out_args(struct fuse_copy_state *cs, struct fuse_args *args,
		       unsigned nbytes)
{

	unsigned int reqsize = 0;

	/*
	 * Uring has all headers separated from args - args is payload only
	 */
	if (!cs->is_uring)
		reqsize = sizeof(struct fuse_out_header);

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
	struct fuse_chan *fch = fud->chan;
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
		/*
		 * Only allow notifications during while the connection is in an
		 * initialized and connected state
		 */
		err = -EINVAL;
		if (!fch->initialized || !fch->connected)
			goto copy_finish;

		/* Don't try to move folios (yet) */
		cs->move_folios = false;

		err = fuse_notify(fch->conn, oh.error, nbytes - sizeof(oh), cs);
		goto copy_finish;
	}

	err = -EINVAL;
	if (oh.error <= -512 || oh.error > 0)
		goto copy_finish;

	spin_lock(&fpq->lock);
	req = NULL;
	if (fpq->connected)
		req = fuse_request_find(fpq, oh.unique & ~FUSE_INT_REQ_BIT);

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
			fch->no_interrupt = 1;
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
		cs->move_folios = false;

	if (oh.error)
		err = nbytes != sizeof(oh) ? -EINVAL : 0;
	else
		err = fuse_copy_out_args(cs, req->args, nbytes);
	fuse_copy_finish(cs);

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
	struct fuse_dev *fud = __fuse_get_dev(iocb->ki_filp);

	if (!fud)
		return -EPERM;

	if (!user_backed_iter(from))
		return -EINVAL;

	fuse_copy_init(&cs, false, from);

	return fuse_dev_do_write(fud, &cs, iov_iter_count(from));
}

static ssize_t fuse_dev_splice_write(struct pipe_inode_info *pipe,
				     struct file *out, loff_t *ppos,
				     size_t len, unsigned int flags)
{
	unsigned int head, tail, count;
	unsigned nbuf;
	unsigned idx;
	struct pipe_buffer *bufs;
	struct fuse_copy_state cs;
	struct fuse_dev *fud = __fuse_get_dev(out);
	size_t rem;
	ssize_t ret;

	if (!fud)
		return -EPERM;

	pipe_lock(pipe);

	head = pipe->head;
	tail = pipe->tail;
	count = pipe_occupancy(head, tail);

	bufs = kvmalloc_objs(struct pipe_buffer, count);
	if (!bufs) {
		pipe_unlock(pipe);
		return -ENOMEM;
	}

	nbuf = 0;
	rem = 0;
	for (idx = tail; !pipe_empty(head, idx) && rem < len; idx++)
		rem += pipe_buf(pipe, idx)->len;

	ret = -EINVAL;
	if (rem < len)
		goto out_free;

	rem = len;
	while (rem) {
		struct pipe_buffer *ibuf;
		struct pipe_buffer *obuf;

		if (WARN_ON(nbuf >= count || pipe_empty(head, tail)))
			goto out_free;

		ibuf = pipe_buf(pipe, tail);
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

	fuse_copy_init(&cs, false, NULL);
	cs.pipebufs = bufs;
	cs.nr_segs = nbuf;
	cs.pipe = pipe;

	if (flags & SPLICE_F_MOVE)
		cs.move_folios = true;

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

	if (IS_ERR(fud))
		return EPOLLERR;

	fiq = &fud->chan->iq;
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
void fuse_dev_end_requests(struct list_head *head)
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

/*
 * Abort all requests.
 *
 * Emergency exit in case of a malicious or accidental deadlock, or just a hung
 * filesystem.
 *
 * The same effect is usually achievable through killing the filesystem daemon
 * and all users of the filesystem.  The exception is the combination of an
 * asynchronous request and the tricky deadlock (see
 * Documentation/filesystems/fuse/fuse.rst).
 *
 * Aborting requests under I/O goes as follows: 1: Separate out unlocked
 * requests, they should be finished off immediately.  Locked requests will be
 * finished after unlock; see unlock_request(). 2: Finish off the unlocked
 * requests.  It is possible that some request will finish before we can.  This
 * is OK, the request will in that case be removed from the list before we touch
 * it.
 */
void fuse_chan_abort(struct fuse_chan *fch, bool abort_with_err)
{
	struct fuse_iqueue *fiq = &fch->iq;

	fch->abort_with_err = abort_with_err;

	spin_lock(&fch->lock);
	if (fch->connected) {
		struct fuse_dev *fud;
		struct fuse_req *req, *next;
		LIST_HEAD(to_end);
		unsigned int i;

		if (fch->timeout.req_timeout)
			cancel_delayed_work(&fch->timeout.work);

		/* Background queuing checks fch->connected under bg_lock */
		spin_lock(&fch->bg_lock);
		fch->connected = 0;
		spin_unlock(&fch->bg_lock);

		fuse_chan_set_initialized(fch, NULL);
		list_for_each_entry(fud, &fch->devices, entry) {
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
		spin_lock(&fch->bg_lock);
		fch->blocked = 0;
		fch->max_background = UINT_MAX;
		flush_bg_queue(fch);
		spin_unlock(&fch->bg_lock);

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
		fuse_end_polls(fch->conn);
		wake_up_all(&fch->blocked_waitq);
		spin_unlock(&fch->lock);

		fuse_dev_end_requests(&to_end);

		/*
		 * fch->lock must not be taken to avoid conflicts with io-uring
		 * locks
		 */
		fuse_uring_abort(fch);
	} else {
		spin_unlock(&fch->lock);
	}
}
EXPORT_SYMBOL_GPL(fuse_chan_abort);

void fuse_chan_wait_aborted(struct fuse_chan *fch)
{
	/* matches implicit memory barrier in fuse_drop_waiting() */
	smp_mb();
	wait_event(fch->blocked_waitq, fuse_chan_num_waiting(fch) == 0);

	fuse_uring_wait_stopped_queues(fch);
}

int fuse_dev_release(struct inode *inode, struct file *file)
{
	struct fuse_dev *fud = fuse_file_to_fud(file);
	/* Pairs with cmpxchg() in fuse_dev_install() */
	struct fuse_chan *fch = xchg(&fud->chan, FUSE_DEV_CHAN_DISCONNECTED);

	if (fch) {
		struct fuse_pqueue *fpq = &fud->pq;
		LIST_HEAD(to_end);
		unsigned int i;
		bool last;

		/* Make sure fuse_dev_install_with_pq() has finished */
		spin_lock(&fch->lock);
		spin_lock(&fpq->lock);
		WARN_ON(!list_empty(&fpq->io));
		for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
			list_splice_init(&fpq->processing[i], &to_end);
		spin_unlock(&fpq->lock);

		list_del(&fud->entry);
		/* Are we the last open device? */
		last = list_empty(&fch->devices);
		spin_unlock(&fch->lock);

		fuse_dev_end_requests(&to_end);

		if (last) {
			WARN_ON(fch->iq.fasync != NULL);
			fuse_chan_abort(fch, false);
		}
		fuse_conn_put(fch->conn);
	}
	fuse_dev_put(fud);
	return 0;
}
EXPORT_SYMBOL_GPL(fuse_dev_release);

static int fuse_dev_fasync(int fd, struct file *file, int on)
{
	struct fuse_dev *fud = fuse_get_dev(file);

	if (IS_ERR(fud))
		return PTR_ERR(fud);

	/* No locking - fasync_helper does its own locking */
	return fasync_helper(fd, file, on, &fud->chan->iq.fasync);
}

static long fuse_dev_ioctl_clone(struct file *file, __u32 __user *argp)
{
	int oldfd;
	struct fuse_dev *fud, *new_fud;
	struct list_head *pq;

	if (get_user(oldfd, argp))
		return -EFAULT;

	CLASS(fd, f)(oldfd);
	if (fd_empty(f))
		return -EINVAL;

	/*
	 * Check against file->f_op because CUSE
	 * uses the same ioctl handler.
	 */
	if (fd_file(f)->f_op != file->f_op)
		return -EINVAL;

	fud = fuse_get_dev(fd_file(f));
	if (IS_ERR(fud))
		return PTR_ERR(fud);

	pq = fuse_pqueue_alloc();
	if (!pq)
		return -ENOMEM;

	new_fud = fuse_file_to_fud(file);
	if (!fuse_dev_install_with_pq(new_fud, fud->chan, pq))
		return -EINVAL;

	return 0;
}

static long fuse_dev_ioctl_backing_open(struct file *file,
					struct fuse_backing_map __user *argp)
{
	struct fuse_dev *fud = fuse_get_dev(file);
	struct fuse_backing_map map;

	if (IS_ERR(fud))
		return PTR_ERR(fud);

	if (!IS_ENABLED(CONFIG_FUSE_PASSTHROUGH))
		return -EOPNOTSUPP;

	if (copy_from_user(&map, argp, sizeof(map)))
		return -EFAULT;

	return fuse_backing_open(fud->chan->conn, &map);
}

static long fuse_dev_ioctl_backing_close(struct file *file, __u32 __user *argp)
{
	struct fuse_dev *fud = fuse_get_dev(file);
	int backing_id;

	if (IS_ERR(fud))
		return PTR_ERR(fud);

	if (!IS_ENABLED(CONFIG_FUSE_PASSTHROUGH))
		return -EOPNOTSUPP;

	if (get_user(backing_id, argp))
		return -EFAULT;

	return fuse_backing_close(fud->chan->conn, backing_id);
}

static long fuse_dev_ioctl_sync_init(struct file *file)
{
	struct fuse_dev *fud = fuse_file_to_fud(file);

	if (fuse_dev_chan_get(fud))
		return -EINVAL;

	fud->sync_init = true;
	return 0;
}

static long fuse_dev_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case FUSE_DEV_IOC_CLONE:
		return fuse_dev_ioctl_clone(file, argp);

	case FUSE_DEV_IOC_BACKING_OPEN:
		return fuse_dev_ioctl_backing_open(file, argp);

	case FUSE_DEV_IOC_BACKING_CLOSE:
		return fuse_dev_ioctl_backing_close(file, argp);

	case FUSE_DEV_IOC_SYNC_INIT:
		return fuse_dev_ioctl_sync_init(file);

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_PROC_FS
static void fuse_dev_show_fdinfo(struct seq_file *seq, struct file *file)
{
	struct fuse_dev *fud = __fuse_get_dev(file);
	if (!fud)
		return;

	seq_printf(seq, "fuse_connection:\t%u\n", fuse_conn_get_id(fud->chan->conn));
}
#endif

const struct file_operations fuse_dev_operations = {
	.owner		= THIS_MODULE,
	.open		= fuse_dev_open,
	.read_iter	= fuse_dev_read,
	.splice_read	= fuse_dev_splice_read,
	.write_iter	= fuse_dev_write,
	.splice_write	= fuse_dev_splice_write,
	.poll		= fuse_dev_poll,
	.release	= fuse_dev_release,
	.fasync		= fuse_dev_fasync,
	.unlocked_ioctl = fuse_dev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
#ifdef CONFIG_FUSE_IO_URING
	.uring_cmd	= fuse_uring_cmd,
#endif
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= fuse_dev_show_fdinfo,
#endif
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
