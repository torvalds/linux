// SPDX-License-Identifier: GPL-2.0
/*
 * Support for async notification of waitid
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/compat.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "cancel.h"
#include "waitid.h"
#include "../kernel/exit.h"

static void io_waitid_cb(struct io_kiocb *req, struct io_tw_state *ts);

#define IO_WAITID_CANCEL_FLAG	BIT(31)
#define IO_WAITID_REF_MASK	GENMASK(30, 0)

struct io_waitid {
	struct file *file;
	int which;
	pid_t upid;
	int options;
	atomic_t refs;
	struct wait_queue_head *head;
	struct siginfo __user *infop;
	struct waitid_info info;
};

static void io_waitid_free(struct io_kiocb *req)
{
	struct io_waitid_async *iwa = req->async_data;

	put_pid(iwa->wo.wo_pid);
	kfree(req->async_data);
	req->async_data = NULL;
	req->flags &= ~REQ_F_ASYNC_DATA;
}

#ifdef CONFIG_COMPAT
static bool io_waitid_compat_copy_si(struct io_waitid *iw, int signo)
{
	struct compat_siginfo __user *infop;
	bool ret;

	infop = (struct compat_siginfo __user *) iw->infop;

	if (!user_write_access_begin(infop, sizeof(*infop)))
		return false;

	unsafe_put_user(signo, &infop->si_signo, Efault);
	unsafe_put_user(0, &infop->si_errno, Efault);
	unsafe_put_user(iw->info.cause, &infop->si_code, Efault);
	unsafe_put_user(iw->info.pid, &infop->si_pid, Efault);
	unsafe_put_user(iw->info.uid, &infop->si_uid, Efault);
	unsafe_put_user(iw->info.status, &infop->si_status, Efault);
	ret = true;
done:
	user_write_access_end();
	return ret;
Efault:
	ret = false;
	goto done;
}
#endif

static bool io_waitid_copy_si(struct io_kiocb *req, int signo)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	bool ret;

	if (!iw->infop)
		return true;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		return io_waitid_compat_copy_si(iw, signo);
#endif

	if (!user_write_access_begin(iw->infop, sizeof(*iw->infop)))
		return false;

	unsafe_put_user(signo, &iw->infop->si_signo, Efault);
	unsafe_put_user(0, &iw->infop->si_errno, Efault);
	unsafe_put_user(iw->info.cause, &iw->infop->si_code, Efault);
	unsafe_put_user(iw->info.pid, &iw->infop->si_pid, Efault);
	unsafe_put_user(iw->info.uid, &iw->infop->si_uid, Efault);
	unsafe_put_user(iw->info.status, &iw->infop->si_status, Efault);
	ret = true;
done:
	user_write_access_end();
	return ret;
Efault:
	ret = false;
	goto done;
}

static int io_waitid_finish(struct io_kiocb *req, int ret)
{
	int signo = 0;

	if (ret > 0) {
		signo = SIGCHLD;
		ret = 0;
	}

	if (!io_waitid_copy_si(req, signo))
		ret = -EFAULT;
	io_waitid_free(req);
	return ret;
}

static void io_waitid_complete(struct io_kiocb *req, int ret)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	struct io_tw_state ts = {};

	/* anyone completing better be holding a reference */
	WARN_ON_ONCE(!(atomic_read(&iw->refs) & IO_WAITID_REF_MASK));

	lockdep_assert_held(&req->ctx->uring_lock);

	hlist_del_init(&req->hash_node);

	ret = io_waitid_finish(req, ret);
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	io_req_task_complete(req, &ts);
}

static bool __io_waitid_cancel(struct io_ring_ctx *ctx, struct io_kiocb *req)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	struct io_waitid_async *iwa = req->async_data;

	/*
	 * Mark us canceled regardless of ownership. This will prevent a
	 * potential retry from a spurious wakeup.
	 */
	atomic_or(IO_WAITID_CANCEL_FLAG, &iw->refs);

	/* claim ownership */
	if (atomic_fetch_inc(&iw->refs) & IO_WAITID_REF_MASK)
		return false;

	spin_lock_irq(&iw->head->lock);
	list_del_init(&iwa->wo.child_wait.entry);
	spin_unlock_irq(&iw->head->lock);
	io_waitid_complete(req, -ECANCELED);
	return true;
}

int io_waitid_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
		     unsigned int issue_flags)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	int nr = 0;

	if (cd->flags & (IORING_ASYNC_CANCEL_FD|IORING_ASYNC_CANCEL_FD_FIXED))
		return -ENOENT;

	io_ring_submit_lock(ctx, issue_flags);
	hlist_for_each_entry_safe(req, tmp, &ctx->waitid_list, hash_node) {
		if (req->cqe.user_data != cd->data &&
		    !(cd->flags & IORING_ASYNC_CANCEL_ANY))
			continue;
		if (__io_waitid_cancel(ctx, req))
			nr++;
		if (!(cd->flags & IORING_ASYNC_CANCEL_ALL))
			break;
	}
	io_ring_submit_unlock(ctx, issue_flags);

	if (nr)
		return nr;

	return -ENOENT;
}

bool io_waitid_remove_all(struct io_ring_ctx *ctx, struct io_uring_task *tctx,
			  bool cancel_all)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool found = false;

	lockdep_assert_held(&ctx->uring_lock);

	hlist_for_each_entry_safe(req, tmp, &ctx->waitid_list, hash_node) {
		if (!io_match_task_safe(req, tctx, cancel_all))
			continue;
		hlist_del_init(&req->hash_node);
		__io_waitid_cancel(ctx, req);
		found = true;
	}

	return found;
}

static inline bool io_waitid_drop_issue_ref(struct io_kiocb *req)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	struct io_waitid_async *iwa = req->async_data;

	if (!atomic_sub_return(1, &iw->refs))
		return false;

	/*
	 * Wakeup triggered, racing with us. It was prevented from
	 * completing because of that, queue up the tw to do that.
	 */
	req->io_task_work.func = io_waitid_cb;
	io_req_task_work_add(req);
	remove_wait_queue(iw->head, &iwa->wo.child_wait);
	return true;
}

static void io_waitid_cb(struct io_kiocb *req, struct io_tw_state *ts)
{
	struct io_waitid_async *iwa = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	io_tw_lock(ctx, ts);

	ret = __do_wait(&iwa->wo);

	/*
	 * If we get -ERESTARTSYS here, we need to re-arm and check again
	 * to ensure we get another callback. If the retry works, then we can
	 * just remove ourselves from the waitqueue again and finish the
	 * request.
	 */
	if (unlikely(ret == -ERESTARTSYS)) {
		struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);

		/* Don't retry if cancel found it meanwhile */
		ret = -ECANCELED;
		if (!(atomic_read(&iw->refs) & IO_WAITID_CANCEL_FLAG)) {
			iw->head = &current->signal->wait_chldexit;
			add_wait_queue(iw->head, &iwa->wo.child_wait);
			ret = __do_wait(&iwa->wo);
			if (ret == -ERESTARTSYS) {
				/* retry armed, drop our ref */
				io_waitid_drop_issue_ref(req);
				return;
			}

			remove_wait_queue(iw->head, &iwa->wo.child_wait);
		}
	}

	io_waitid_complete(req, ret);
}

static int io_waitid_wait(struct wait_queue_entry *wait, unsigned mode,
			  int sync, void *key)
{
	struct wait_opts *wo = container_of(wait, struct wait_opts, child_wait);
	struct io_waitid_async *iwa = container_of(wo, struct io_waitid_async, wo);
	struct io_kiocb *req = iwa->req;
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	struct task_struct *p = key;

	if (!pid_child_should_wake(wo, p))
		return 0;

	/* cancel is in progress */
	if (atomic_fetch_inc(&iw->refs) & IO_WAITID_REF_MASK)
		return 1;

	req->io_task_work.func = io_waitid_cb;
	io_req_task_work_add(req);
	list_del_init(&wait->entry);
	return 1;
}

int io_waitid_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);

	if (sqe->addr || sqe->buf_index || sqe->addr3 || sqe->waitid_flags)
		return -EINVAL;

	iw->which = READ_ONCE(sqe->len);
	iw->upid = READ_ONCE(sqe->fd);
	iw->options = READ_ONCE(sqe->file_index);
	iw->infop = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	return 0;
}

int io_waitid(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_waitid *iw = io_kiocb_to_cmd(req, struct io_waitid);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_waitid_async *iwa;
	int ret;

	if (io_alloc_async_data(req))
		return -ENOMEM;

	iwa = req->async_data;
	iwa->req = req;

	ret = kernel_waitid_prepare(&iwa->wo, iw->which, iw->upid, &iw->info,
					iw->options, NULL);
	if (ret)
		goto done;

	/*
	 * Mark the request as busy upfront, in case we're racing with the
	 * wakeup. If we are, then we'll notice when we drop this initial
	 * reference again after arming.
	 */
	atomic_set(&iw->refs, 1);

	/*
	 * Cancel must hold the ctx lock, so there's no risk of cancelation
	 * finding us until a) we remain on the list, and b) the lock is
	 * dropped. We only need to worry about racing with the wakeup
	 * callback.
	 */
	io_ring_submit_lock(ctx, issue_flags);
	hlist_add_head(&req->hash_node, &ctx->waitid_list);

	init_waitqueue_func_entry(&iwa->wo.child_wait, io_waitid_wait);
	iwa->wo.child_wait.private = req->tctx->task;
	iw->head = &current->signal->wait_chldexit;
	add_wait_queue(iw->head, &iwa->wo.child_wait);

	ret = __do_wait(&iwa->wo);
	if (ret == -ERESTARTSYS) {
		/*
		 * Nobody else grabbed a reference, it'll complete when we get
		 * a waitqueue callback, or if someone cancels it.
		 */
		if (!io_waitid_drop_issue_ref(req)) {
			io_ring_submit_unlock(ctx, issue_flags);
			return IOU_ISSUE_SKIP_COMPLETE;
		}

		/*
		 * Wakeup triggered, racing with us. It was prevented from
		 * completing because of that, queue up the tw to do that.
		 */
		io_ring_submit_unlock(ctx, issue_flags);
		return IOU_ISSUE_SKIP_COMPLETE;
	}

	hlist_del_init(&req->hash_node);
	remove_wait_queue(iw->head, &iwa->wo.child_wait);
	ret = io_waitid_finish(req, ret);

	io_ring_submit_unlock(ctx, issue_flags);
done:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}
