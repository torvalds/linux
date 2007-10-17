/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>

MODULE_ALIAS_MISCDEV(FUSE_MINOR);

static struct kmem_cache *fuse_req_cachep;

static struct fuse_conn *fuse_get_conn(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	return file->private_data;
}

static void fuse_request_init(struct fuse_req *req)
{
	memset(req, 0, sizeof(*req));
	INIT_LIST_HEAD(&req->list);
	INIT_LIST_HEAD(&req->intr_entry);
	init_waitqueue_head(&req->waitq);
	atomic_set(&req->count, 1);
}

struct fuse_req *fuse_request_alloc(void)
{
	struct fuse_req *req = kmem_cache_alloc(fuse_req_cachep, GFP_KERNEL);
	if (req)
		fuse_request_init(req);
	return req;
}

void fuse_request_free(struct fuse_req *req)
{
	kmem_cache_free(fuse_req_cachep, req);
}

static void block_sigs(sigset_t *oldset)
{
	sigset_t mask;

	siginitsetinv(&mask, sigmask(SIGKILL));
	sigprocmask(SIG_BLOCK, &mask, oldset);
}

static void restore_sigs(sigset_t *oldset)
{
	sigprocmask(SIG_SETMASK, oldset, NULL);
}

static void __fuse_get_request(struct fuse_req *req)
{
	atomic_inc(&req->count);
}

/* Must be called with > 1 refcount */
static void __fuse_put_request(struct fuse_req *req)
{
	BUG_ON(atomic_read(&req->count) < 2);
	atomic_dec(&req->count);
}

static void fuse_req_init_context(struct fuse_req *req)
{
	req->in.h.uid = current->fsuid;
	req->in.h.gid = current->fsgid;
	req->in.h.pid = current->pid;
}

struct fuse_req *fuse_get_req(struct fuse_conn *fc)
{
	struct fuse_req *req;
	sigset_t oldset;
	int intr;
	int err;

	atomic_inc(&fc->num_waiting);
	block_sigs(&oldset);
	intr = wait_event_interruptible(fc->blocked_waitq, !fc->blocked);
	restore_sigs(&oldset);
	err = -EINTR;
	if (intr)
		goto out;

	err = -ENOTCONN;
	if (!fc->connected)
		goto out;

	req = fuse_request_alloc();
	err = -ENOMEM;
	if (!req)
		goto out;

	fuse_req_init_context(req);
	req->waiting = 1;
	return req;

 out:
	atomic_dec(&fc->num_waiting);
	return ERR_PTR(err);
}

/*
 * Return request in fuse_file->reserved_req.  However that may
 * currently be in use.  If that is the case, wait for it to become
 * available.
 */
static struct fuse_req *get_reserved_req(struct fuse_conn *fc,
					 struct file *file)
{
	struct fuse_req *req = NULL;
	struct fuse_file *ff = file->private_data;

	do {
		wait_event(fc->reserved_req_waitq, ff->reserved_req);
		spin_lock(&fc->lock);
		if (ff->reserved_req) {
			req = ff->reserved_req;
			ff->reserved_req = NULL;
			get_file(file);
			req->stolen_file = file;
		}
		spin_unlock(&fc->lock);
	} while (!req);

	return req;
}

/*
 * Put stolen request back into fuse_file->reserved_req
 */
static void put_reserved_req(struct fuse_conn *fc, struct fuse_req *req)
{
	struct file *file = req->stolen_file;
	struct fuse_file *ff = file->private_data;

	spin_lock(&fc->lock);
	fuse_request_init(req);
	BUG_ON(ff->reserved_req);
	ff->reserved_req = req;
	wake_up_all(&fc->reserved_req_waitq);
	spin_unlock(&fc->lock);
	fput(file);
}

/*
 * Gets a requests for a file operation, always succeeds
 *
 * This is used for sending the FLUSH request, which must get to
 * userspace, due to POSIX locks which may need to be unlocked.
 *
 * If allocation fails due to OOM, use the reserved request in
 * fuse_file.
 *
 * This is very unlikely to deadlock accidentally, since the
 * filesystem should not have it's own file open.  If deadlock is
 * intentional, it can still be broken by "aborting" the filesystem.
 */
struct fuse_req *fuse_get_req_nofail(struct fuse_conn *fc, struct file *file)
{
	struct fuse_req *req;

	atomic_inc(&fc->num_waiting);
	wait_event(fc->blocked_waitq, !fc->blocked);
	req = fuse_request_alloc();
	if (!req)
		req = get_reserved_req(fc, file);

	fuse_req_init_context(req);
	req->waiting = 1;
	return req;
}

void fuse_put_request(struct fuse_conn *fc, struct fuse_req *req)
{
	if (atomic_dec_and_test(&req->count)) {
		if (req->waiting)
			atomic_dec(&fc->num_waiting);

		if (req->stolen_file)
			put_reserved_req(fc, req);
		else
			fuse_request_free(req);
	}
}

/*
 * This function is called when a request is finished.  Either a reply
 * has arrived or it was aborted (and not yet sent) or some error
 * occurred during communication with userspace, or the device file
 * was closed.  The requester thread is woken up (if still waiting),
 * the 'end' callback is called if given, else the reference to the
 * request is released
 *
 * Called with fc->lock, unlocks it
 */
static void request_end(struct fuse_conn *fc, struct fuse_req *req)
	__releases(fc->lock)
{
	void (*end) (struct fuse_conn *, struct fuse_req *) = req->end;
	req->end = NULL;
	list_del(&req->list);
	list_del(&req->intr_entry);
	req->state = FUSE_REQ_FINISHED;
	if (req->background) {
		if (fc->num_background == FUSE_MAX_BACKGROUND) {
			fc->blocked = 0;
			wake_up_all(&fc->blocked_waitq);
		}
		if (fc->num_background == FUSE_CONGESTION_THRESHOLD) {
			clear_bdi_congested(&fc->bdi, READ);
			clear_bdi_congested(&fc->bdi, WRITE);
		}
		fc->num_background--;
	}
	spin_unlock(&fc->lock);
	wake_up(&req->waitq);
	if (end)
		end(fc, req);
	else
		fuse_put_request(fc, req);
}

static void wait_answer_interruptible(struct fuse_conn *fc,
				      struct fuse_req *req)
{
	if (signal_pending(current))
		return;

	spin_unlock(&fc->lock);
	wait_event_interruptible(req->waitq, req->state == FUSE_REQ_FINISHED);
	spin_lock(&fc->lock);
}

static void queue_interrupt(struct fuse_conn *fc, struct fuse_req *req)
{
	list_add_tail(&req->intr_entry, &fc->interrupts);
	wake_up(&fc->waitq);
	kill_fasync(&fc->fasync, SIGIO, POLL_IN);
}

/* Called with fc->lock held.  Releases, and then reacquires it. */
static void request_wait_answer(struct fuse_conn *fc, struct fuse_req *req)
{
	if (!fc->no_interrupt) {
		/* Any signal may interrupt this */
		wait_answer_interruptible(fc, req);

		if (req->aborted)
			goto aborted;
		if (req->state == FUSE_REQ_FINISHED)
			return;

		req->interrupted = 1;
		if (req->state == FUSE_REQ_SENT)
			queue_interrupt(fc, req);
	}

	if (!req->force) {
		sigset_t oldset;

		/* Only fatal signals may interrupt this */
		block_sigs(&oldset);
		wait_answer_interruptible(fc, req);
		restore_sigs(&oldset);

		if (req->aborted)
			goto aborted;
		if (req->state == FUSE_REQ_FINISHED)
			return;

		/* Request is not yet in userspace, bail out */
		if (req->state == FUSE_REQ_PENDING) {
			list_del(&req->list);
			__fuse_put_request(req);
			req->out.h.error = -EINTR;
			return;
		}
	}

	/*
	 * Either request is already in userspace, or it was forced.
	 * Wait it out.
	 */
	spin_unlock(&fc->lock);
	wait_event(req->waitq, req->state == FUSE_REQ_FINISHED);
	spin_lock(&fc->lock);

	if (!req->aborted)
		return;

 aborted:
	BUG_ON(req->state != FUSE_REQ_FINISHED);
	if (req->locked) {
		/* This is uninterruptible sleep, because data is
		   being copied to/from the buffers of req.  During
		   locked state, there mustn't be any filesystem
		   operation (e.g. page fault), since that could lead
		   to deadlock */
		spin_unlock(&fc->lock);
		wait_event(req->waitq, !req->locked);
		spin_lock(&fc->lock);
	}
}

static unsigned len_args(unsigned numargs, struct fuse_arg *args)
{
	unsigned nbytes = 0;
	unsigned i;

	for (i = 0; i < numargs; i++)
		nbytes += args[i].size;

	return nbytes;
}

static u64 fuse_get_unique(struct fuse_conn *fc)
 {
 	fc->reqctr++;
 	/* zero is special */
 	if (fc->reqctr == 0)
 		fc->reqctr = 1;

	return fc->reqctr;
}

static void queue_request(struct fuse_conn *fc, struct fuse_req *req)
{
	req->in.h.unique = fuse_get_unique(fc);
	req->in.h.len = sizeof(struct fuse_in_header) +
		len_args(req->in.numargs, (struct fuse_arg *) req->in.args);
	list_add_tail(&req->list, &fc->pending);
	req->state = FUSE_REQ_PENDING;
	if (!req->waiting) {
		req->waiting = 1;
		atomic_inc(&fc->num_waiting);
	}
	wake_up(&fc->waitq);
	kill_fasync(&fc->fasync, SIGIO, POLL_IN);
}

void request_send(struct fuse_conn *fc, struct fuse_req *req)
{
	req->isreply = 1;
	spin_lock(&fc->lock);
	if (!fc->connected)
		req->out.h.error = -ENOTCONN;
	else if (fc->conn_error)
		req->out.h.error = -ECONNREFUSED;
	else {
		queue_request(fc, req);
		/* acquire extra reference, since request is still needed
		   after request_end() */
		__fuse_get_request(req);

		request_wait_answer(fc, req);
	}
	spin_unlock(&fc->lock);
}

static void request_send_nowait(struct fuse_conn *fc, struct fuse_req *req)
{
	spin_lock(&fc->lock);
	if (fc->connected) {
		req->background = 1;
		fc->num_background++;
		if (fc->num_background == FUSE_MAX_BACKGROUND)
			fc->blocked = 1;
		if (fc->num_background == FUSE_CONGESTION_THRESHOLD) {
			set_bdi_congested(&fc->bdi, READ);
			set_bdi_congested(&fc->bdi, WRITE);
		}

		queue_request(fc, req);
		spin_unlock(&fc->lock);
	} else {
		req->out.h.error = -ENOTCONN;
		request_end(fc, req);
	}
}

void request_send_noreply(struct fuse_conn *fc, struct fuse_req *req)
{
	req->isreply = 0;
	request_send_nowait(fc, req);
}

void request_send_background(struct fuse_conn *fc, struct fuse_req *req)
{
	req->isreply = 1;
	request_send_nowait(fc, req);
}

/*
 * Lock the request.  Up to the next unlock_request() there mustn't be
 * anything that could cause a page-fault.  If the request was already
 * aborted bail out.
 */
static int lock_request(struct fuse_conn *fc, struct fuse_req *req)
{
	int err = 0;
	if (req) {
		spin_lock(&fc->lock);
		if (req->aborted)
			err = -ENOENT;
		else
			req->locked = 1;
		spin_unlock(&fc->lock);
	}
	return err;
}

/*
 * Unlock request.  If it was aborted during being locked, the
 * requester thread is currently waiting for it to be unlocked, so
 * wake it up.
 */
static void unlock_request(struct fuse_conn *fc, struct fuse_req *req)
{
	if (req) {
		spin_lock(&fc->lock);
		req->locked = 0;
		if (req->aborted)
			wake_up(&req->waitq);
		spin_unlock(&fc->lock);
	}
}

struct fuse_copy_state {
	struct fuse_conn *fc;
	int write;
	struct fuse_req *req;
	const struct iovec *iov;
	unsigned long nr_segs;
	unsigned long seglen;
	unsigned long addr;
	struct page *pg;
	void *mapaddr;
	void *buf;
	unsigned len;
};

static void fuse_copy_init(struct fuse_copy_state *cs, struct fuse_conn *fc,
			   int write, struct fuse_req *req,
			   const struct iovec *iov, unsigned long nr_segs)
{
	memset(cs, 0, sizeof(*cs));
	cs->fc = fc;
	cs->write = write;
	cs->req = req;
	cs->iov = iov;
	cs->nr_segs = nr_segs;
}

/* Unmap and put previous page of userspace buffer */
static void fuse_copy_finish(struct fuse_copy_state *cs)
{
	if (cs->mapaddr) {
		kunmap_atomic(cs->mapaddr, KM_USER0);
		if (cs->write) {
			flush_dcache_page(cs->pg);
			set_page_dirty_lock(cs->pg);
		}
		put_page(cs->pg);
		cs->mapaddr = NULL;
	}
}

/*
 * Get another pagefull of userspace buffer, and map it to kernel
 * address space, and lock request
 */
static int fuse_copy_fill(struct fuse_copy_state *cs)
{
	unsigned long offset;
	int err;

	unlock_request(cs->fc, cs->req);
	fuse_copy_finish(cs);
	if (!cs->seglen) {
		BUG_ON(!cs->nr_segs);
		cs->seglen = cs->iov[0].iov_len;
		cs->addr = (unsigned long) cs->iov[0].iov_base;
		cs->iov ++;
		cs->nr_segs --;
	}
	down_read(&current->mm->mmap_sem);
	err = get_user_pages(current, current->mm, cs->addr, 1, cs->write, 0,
			     &cs->pg, NULL);
	up_read(&current->mm->mmap_sem);
	if (err < 0)
		return err;
	BUG_ON(err != 1);
	offset = cs->addr % PAGE_SIZE;
	cs->mapaddr = kmap_atomic(cs->pg, KM_USER0);
	cs->buf = cs->mapaddr + offset;
	cs->len = min(PAGE_SIZE - offset, cs->seglen);
	cs->seglen -= cs->len;
	cs->addr += cs->len;

	return lock_request(cs->fc, cs->req);
}

/* Do as much copy to/from userspace buffer as we can */
static int fuse_copy_do(struct fuse_copy_state *cs, void **val, unsigned *size)
{
	unsigned ncpy = min(*size, cs->len);
	if (val) {
		if (cs->write)
			memcpy(cs->buf, *val, ncpy);
		else
			memcpy(*val, cs->buf, ncpy);
		*val += ncpy;
	}
	*size -= ncpy;
	cs->len -= ncpy;
	cs->buf += ncpy;
	return ncpy;
}

/*
 * Copy a page in the request to/from the userspace buffer.  Must be
 * done atomically
 */
static int fuse_copy_page(struct fuse_copy_state *cs, struct page *page,
			  unsigned offset, unsigned count, int zeroing)
{
	if (page && zeroing && count < PAGE_SIZE) {
		void *mapaddr = kmap_atomic(page, KM_USER1);
		memset(mapaddr, 0, PAGE_SIZE);
		kunmap_atomic(mapaddr, KM_USER1);
	}
	while (count) {
		int err;
		if (!cs->len && (err = fuse_copy_fill(cs)))
			return err;
		if (page) {
			void *mapaddr = kmap_atomic(page, KM_USER1);
			void *buf = mapaddr + offset;
			offset += fuse_copy_do(cs, &buf, &count);
			kunmap_atomic(mapaddr, KM_USER1);
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
	unsigned offset = req->page_offset;
	unsigned count = min(nbytes, (unsigned) PAGE_SIZE - offset);

	for (i = 0; i < req->num_pages && (nbytes || zeroing); i++) {
		struct page *page = req->pages[i];
		int err = fuse_copy_page(cs, page, offset, count, zeroing);
		if (err)
			return err;

		nbytes -= count;
		count = min(nbytes, (unsigned) PAGE_SIZE);
		offset = 0;
	}
	return 0;
}

/* Copy a single argument in the request to/from userspace buffer */
static int fuse_copy_one(struct fuse_copy_state *cs, void *val, unsigned size)
{
	while (size) {
		int err;
		if (!cs->len && (err = fuse_copy_fill(cs)))
			return err;
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

static int request_pending(struct fuse_conn *fc)
{
	return !list_empty(&fc->pending) || !list_empty(&fc->interrupts);
}

/* Wait until a request is available on the pending list */
static void request_wait(struct fuse_conn *fc)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(&fc->waitq, &wait);
	while (fc->connected && !request_pending(fc)) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current))
			break;

		spin_unlock(&fc->lock);
		schedule();
		spin_lock(&fc->lock);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&fc->waitq, &wait);
}

/*
 * Transfer an interrupt request to userspace
 *
 * Unlike other requests this is assembled on demand, without a need
 * to allocate a separate fuse_req structure.
 *
 * Called with fc->lock held, releases it
 */
static int fuse_read_interrupt(struct fuse_conn *fc, struct fuse_req *req,
			       const struct iovec *iov, unsigned long nr_segs)
	__releases(fc->lock)
{
	struct fuse_copy_state cs;
	struct fuse_in_header ih;
	struct fuse_interrupt_in arg;
	unsigned reqsize = sizeof(ih) + sizeof(arg);
	int err;

	list_del_init(&req->intr_entry);
	req->intr_unique = fuse_get_unique(fc);
	memset(&ih, 0, sizeof(ih));
	memset(&arg, 0, sizeof(arg));
	ih.len = reqsize;
	ih.opcode = FUSE_INTERRUPT;
	ih.unique = req->intr_unique;
	arg.unique = req->in.h.unique;

	spin_unlock(&fc->lock);
	if (iov_length(iov, nr_segs) < reqsize)
		return -EINVAL;

	fuse_copy_init(&cs, fc, 1, NULL, iov, nr_segs);
	err = fuse_copy_one(&cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(&cs, &arg, sizeof(arg));
	fuse_copy_finish(&cs);

	return err ? err : reqsize;
}

/*
 * Read a single request into the userspace filesystem's buffer.  This
 * function waits until a request is available, then removes it from
 * the pending list and copies request data to userspace buffer.  If
 * no reply is needed (FORGET) or request has been aborted or there
 * was an error during the copying then it's finished by calling
 * request_end().  Otherwise add it to the processing list, and set
 * the 'sent' flag.
 */
static ssize_t fuse_dev_read(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t pos)
{
	int err;
	struct fuse_req *req;
	struct fuse_in *in;
	struct fuse_copy_state cs;
	unsigned reqsize;
	struct file *file = iocb->ki_filp;
	struct fuse_conn *fc = fuse_get_conn(file);
	if (!fc)
		return -EPERM;

 restart:
	spin_lock(&fc->lock);
	err = -EAGAIN;
	if ((file->f_flags & O_NONBLOCK) && fc->connected &&
	    !request_pending(fc))
		goto err_unlock;

	request_wait(fc);
	err = -ENODEV;
	if (!fc->connected)
		goto err_unlock;
	err = -ERESTARTSYS;
	if (!request_pending(fc))
		goto err_unlock;

	if (!list_empty(&fc->interrupts)) {
		req = list_entry(fc->interrupts.next, struct fuse_req,
				 intr_entry);
		return fuse_read_interrupt(fc, req, iov, nr_segs);
	}

	req = list_entry(fc->pending.next, struct fuse_req, list);
	req->state = FUSE_REQ_READING;
	list_move(&req->list, &fc->io);

	in = &req->in;
	reqsize = in->h.len;
	/* If request is too large, reply with an error and restart the read */
	if (iov_length(iov, nr_segs) < reqsize) {
		req->out.h.error = -EIO;
		/* SETXATTR is special, since it may contain too large data */
		if (in->h.opcode == FUSE_SETXATTR)
			req->out.h.error = -E2BIG;
		request_end(fc, req);
		goto restart;
	}
	spin_unlock(&fc->lock);
	fuse_copy_init(&cs, fc, 1, req, iov, nr_segs);
	err = fuse_copy_one(&cs, &in->h, sizeof(in->h));
	if (!err)
		err = fuse_copy_args(&cs, in->numargs, in->argpages,
				     (struct fuse_arg *) in->args, 0);
	fuse_copy_finish(&cs);
	spin_lock(&fc->lock);
	req->locked = 0;
	if (!err && req->aborted)
		err = -ENOENT;
	if (err) {
		if (!req->aborted)
			req->out.h.error = -EIO;
		request_end(fc, req);
		return err;
	}
	if (!req->isreply)
		request_end(fc, req);
	else {
		req->state = FUSE_REQ_SENT;
		list_move_tail(&req->list, &fc->processing);
		if (req->interrupted)
			queue_interrupt(fc, req);
		spin_unlock(&fc->lock);
	}
	return reqsize;

 err_unlock:
	spin_unlock(&fc->lock);
	return err;
}

/* Look up request on processing list by unique ID */
static struct fuse_req *request_find(struct fuse_conn *fc, u64 unique)
{
	struct list_head *entry;

	list_for_each(entry, &fc->processing) {
		struct fuse_req *req;
		req = list_entry(entry, struct fuse_req, list);
		if (req->in.h.unique == unique || req->intr_unique == unique)
			return req;
	}
	return NULL;
}

static int copy_out_args(struct fuse_copy_state *cs, struct fuse_out *out,
			 unsigned nbytes)
{
	unsigned reqsize = sizeof(struct fuse_out_header);

	if (out->h.error)
		return nbytes != reqsize ? -EINVAL : 0;

	reqsize += len_args(out->numargs, out->args);

	if (reqsize < nbytes || (reqsize > nbytes && !out->argvar))
		return -EINVAL;
	else if (reqsize > nbytes) {
		struct fuse_arg *lastarg = &out->args[out->numargs-1];
		unsigned diffsize = reqsize - nbytes;
		if (diffsize > lastarg->size)
			return -EINVAL;
		lastarg->size -= diffsize;
	}
	return fuse_copy_args(cs, out->numargs, out->argpages, out->args,
			      out->page_zeroing);
}

/*
 * Write a single reply to a request.  First the header is copied from
 * the write buffer.  The request is then searched on the processing
 * list by the unique ID found in the header.  If found, then remove
 * it from the list and copy the rest of the buffer to the request.
 * The request is finished by calling request_end()
 */
static ssize_t fuse_dev_write(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos)
{
	int err;
	unsigned nbytes = iov_length(iov, nr_segs);
	struct fuse_req *req;
	struct fuse_out_header oh;
	struct fuse_copy_state cs;
	struct fuse_conn *fc = fuse_get_conn(iocb->ki_filp);
	if (!fc)
		return -EPERM;

	fuse_copy_init(&cs, fc, 0, NULL, iov, nr_segs);
	if (nbytes < sizeof(struct fuse_out_header))
		return -EINVAL;

	err = fuse_copy_one(&cs, &oh, sizeof(oh));
	if (err)
		goto err_finish;
	err = -EINVAL;
	if (!oh.unique || oh.error <= -1000 || oh.error > 0 ||
	    oh.len != nbytes)
		goto err_finish;

	spin_lock(&fc->lock);
	err = -ENOENT;
	if (!fc->connected)
		goto err_unlock;

	req = request_find(fc, oh.unique);
	if (!req)
		goto err_unlock;

	if (req->aborted) {
		spin_unlock(&fc->lock);
		fuse_copy_finish(&cs);
		spin_lock(&fc->lock);
		request_end(fc, req);
		return -ENOENT;
	}
	/* Is it an interrupt reply? */
	if (req->intr_unique == oh.unique) {
		err = -EINVAL;
		if (nbytes != sizeof(struct fuse_out_header))
			goto err_unlock;

		if (oh.error == -ENOSYS)
			fc->no_interrupt = 1;
		else if (oh.error == -EAGAIN)
			queue_interrupt(fc, req);

		spin_unlock(&fc->lock);
		fuse_copy_finish(&cs);
		return nbytes;
	}

	req->state = FUSE_REQ_WRITING;
	list_move(&req->list, &fc->io);
	req->out.h = oh;
	req->locked = 1;
	cs.req = req;
	spin_unlock(&fc->lock);

	err = copy_out_args(&cs, &req->out, nbytes);
	fuse_copy_finish(&cs);

	spin_lock(&fc->lock);
	req->locked = 0;
	if (!err) {
		if (req->aborted)
			err = -ENOENT;
	} else if (!req->aborted)
		req->out.h.error = -EIO;
	request_end(fc, req);

	return err ? err : nbytes;

 err_unlock:
	spin_unlock(&fc->lock);
 err_finish:
	fuse_copy_finish(&cs);
	return err;
}

static unsigned fuse_dev_poll(struct file *file, poll_table *wait)
{
	unsigned mask = POLLOUT | POLLWRNORM;
	struct fuse_conn *fc = fuse_get_conn(file);
	if (!fc)
		return POLLERR;

	poll_wait(file, &fc->waitq, wait);

	spin_lock(&fc->lock);
	if (!fc->connected)
		mask = POLLERR;
	else if (request_pending(fc))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock(&fc->lock);

	return mask;
}

/*
 * Abort all requests on the given list (pending or processing)
 *
 * This function releases and reacquires fc->lock
 */
static void end_requests(struct fuse_conn *fc, struct list_head *head)
{
	while (!list_empty(head)) {
		struct fuse_req *req;
		req = list_entry(head->next, struct fuse_req, list);
		req->out.h.error = -ECONNABORTED;
		request_end(fc, req);
		spin_lock(&fc->lock);
	}
}

/*
 * Abort requests under I/O
 *
 * The requests are set to aborted and finished, and the request
 * waiter is woken up.  This will make request_wait_answer() wait
 * until the request is unlocked and then return.
 *
 * If the request is asynchronous, then the end function needs to be
 * called after waiting for the request to be unlocked (if it was
 * locked).
 */
static void end_io_requests(struct fuse_conn *fc)
{
	while (!list_empty(&fc->io)) {
		struct fuse_req *req =
			list_entry(fc->io.next, struct fuse_req, list);
		void (*end) (struct fuse_conn *, struct fuse_req *) = req->end;

		req->aborted = 1;
		req->out.h.error = -ECONNABORTED;
		req->state = FUSE_REQ_FINISHED;
		list_del_init(&req->list);
		wake_up(&req->waitq);
		if (end) {
			req->end = NULL;
			/* The end function will consume this reference */
			__fuse_get_request(req);
			spin_unlock(&fc->lock);
			wait_event(req->waitq, !req->locked);
			end(fc, req);
			spin_lock(&fc->lock);
		}
	}
}

/*
 * Abort all requests.
 *
 * Emergency exit in case of a malicious or accidental deadlock, or
 * just a hung filesystem.
 *
 * The same effect is usually achievable through killing the
 * filesystem daemon and all users of the filesystem.  The exception
 * is the combination of an asynchronous request and the tricky
 * deadlock (see Documentation/filesystems/fuse.txt).
 *
 * During the aborting, progression of requests from the pending and
 * processing lists onto the io list, and progression of new requests
 * onto the pending list is prevented by req->connected being false.
 *
 * Progression of requests under I/O to the processing list is
 * prevented by the req->aborted flag being true for these requests.
 * For this reason requests on the io list must be aborted first.
 */
void fuse_abort_conn(struct fuse_conn *fc)
{
	spin_lock(&fc->lock);
	if (fc->connected) {
		fc->connected = 0;
		fc->blocked = 0;
		end_io_requests(fc);
		end_requests(fc, &fc->pending);
		end_requests(fc, &fc->processing);
		wake_up_all(&fc->waitq);
		wake_up_all(&fc->blocked_waitq);
		kill_fasync(&fc->fasync, SIGIO, POLL_IN);
	}
	spin_unlock(&fc->lock);
}

static int fuse_dev_release(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(file);
	if (fc) {
		spin_lock(&fc->lock);
		fc->connected = 0;
		end_requests(fc, &fc->pending);
		end_requests(fc, &fc->processing);
		spin_unlock(&fc->lock);
		fasync_helper(-1, file, 0, &fc->fasync);
		fuse_conn_put(fc);
	}

	return 0;
}

static int fuse_dev_fasync(int fd, struct file *file, int on)
{
	struct fuse_conn *fc = fuse_get_conn(file);
	if (!fc)
		return -EPERM;

	/* No locking - fasync_helper does its own locking */
	return fasync_helper(fd, file, on, &fc->fasync);
}

const struct file_operations fuse_dev_operations = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= do_sync_read,
	.aio_read	= fuse_dev_read,
	.write		= do_sync_write,
	.aio_write	= fuse_dev_write,
	.poll		= fuse_dev_poll,
	.release	= fuse_dev_release,
	.fasync		= fuse_dev_fasync,
};

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
