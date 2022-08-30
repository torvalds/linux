// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/nospec.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "tctx.h"
#include "poll.h"
#include "timeout.h"
#include "cancel.h"

struct io_cancel {
	struct file			*file;
	u64				addr;
	u32				flags;
	s32				fd;
};

#define CANCEL_FLAGS	(IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_FD | \
			 IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_FD_FIXED)

static bool io_cancel_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_cancel_data *cd = data;

	if (req->ctx != cd->ctx)
		return false;
	if (cd->flags & IORING_ASYNC_CANCEL_ANY) {
		;
	} else if (cd->flags & IORING_ASYNC_CANCEL_FD) {
		if (req->file != cd->file)
			return false;
	} else {
		if (req->cqe.user_data != cd->data)
			return false;
	}
	if (cd->flags & (IORING_ASYNC_CANCEL_ALL|IORING_ASYNC_CANCEL_ANY)) {
		if (cd->seq == req->work.cancel_seq)
			return false;
		req->work.cancel_seq = cd->seq;
	}
	return true;
}

static int io_async_cancel_one(struct io_uring_task *tctx,
			       struct io_cancel_data *cd)
{
	enum io_wq_cancel cancel_ret;
	int ret = 0;
	bool all;

	if (!tctx || !tctx->io_wq)
		return -ENOENT;

	all = cd->flags & (IORING_ASYNC_CANCEL_ALL|IORING_ASYNC_CANCEL_ANY);
	cancel_ret = io_wq_cancel_cb(tctx->io_wq, io_cancel_cb, cd, all);
	switch (cancel_ret) {
	case IO_WQ_CANCEL_OK:
		ret = 0;
		break;
	case IO_WQ_CANCEL_RUNNING:
		ret = -EALREADY;
		break;
	case IO_WQ_CANCEL_NOTFOUND:
		ret = -ENOENT;
		break;
	}

	return ret;
}

int io_try_cancel(struct io_uring_task *tctx, struct io_cancel_data *cd,
		  unsigned issue_flags)
{
	struct io_ring_ctx *ctx = cd->ctx;
	int ret;

	WARN_ON_ONCE(!io_wq_current_is_worker() && tctx != current->io_uring);

	ret = io_async_cancel_one(tctx, cd);
	/*
	 * Fall-through even for -EALREADY, as we may have poll armed
	 * that need unarming.
	 */
	if (!ret)
		return 0;

	ret = io_poll_cancel(ctx, cd, issue_flags);
	if (ret != -ENOENT)
		return ret;

	spin_lock(&ctx->completion_lock);
	if (!(cd->flags & IORING_ASYNC_CANCEL_FD))
		ret = io_timeout_cancel(ctx, cd);
	spin_unlock(&ctx->completion_lock);
	return ret;
}

int io_async_cancel_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_cancel *cancel = io_kiocb_to_cmd(req, struct io_cancel);

	if (unlikely(req->flags & REQ_F_BUFFER_SELECT))
		return -EINVAL;
	if (sqe->off || sqe->len || sqe->splice_fd_in)
		return -EINVAL;

	cancel->addr = READ_ONCE(sqe->addr);
	cancel->flags = READ_ONCE(sqe->cancel_flags);
	if (cancel->flags & ~CANCEL_FLAGS)
		return -EINVAL;
	if (cancel->flags & IORING_ASYNC_CANCEL_FD) {
		if (cancel->flags & IORING_ASYNC_CANCEL_ANY)
			return -EINVAL;
		cancel->fd = READ_ONCE(sqe->fd);
	}

	return 0;
}

static int __io_async_cancel(struct io_cancel_data *cd,
			     struct io_uring_task *tctx,
			     unsigned int issue_flags)
{
	bool all = cd->flags & (IORING_ASYNC_CANCEL_ALL|IORING_ASYNC_CANCEL_ANY);
	struct io_ring_ctx *ctx = cd->ctx;
	struct io_tctx_node *node;
	int ret, nr = 0;

	do {
		ret = io_try_cancel(tctx, cd, issue_flags);
		if (ret == -ENOENT)
			break;
		if (!all)
			return ret;
		nr++;
	} while (1);

	/* slow path, try all io-wq's */
	io_ring_submit_lock(ctx, issue_flags);
	ret = -ENOENT;
	list_for_each_entry(node, &ctx->tctx_list, ctx_node) {
		struct io_uring_task *tctx = node->task->io_uring;

		ret = io_async_cancel_one(tctx, cd);
		if (ret != -ENOENT) {
			if (!all)
				break;
			nr++;
		}
	}
	io_ring_submit_unlock(ctx, issue_flags);
	return all ? nr : ret;
}

int io_async_cancel(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_cancel *cancel = io_kiocb_to_cmd(req, struct io_cancel);
	struct io_cancel_data cd = {
		.ctx	= req->ctx,
		.data	= cancel->addr,
		.flags	= cancel->flags,
		.seq	= atomic_inc_return(&req->ctx->cancel_seq),
	};
	struct io_uring_task *tctx = req->task->io_uring;
	int ret;

	if (cd.flags & IORING_ASYNC_CANCEL_FD) {
		if (req->flags & REQ_F_FIXED_FILE ||
		    cd.flags & IORING_ASYNC_CANCEL_FD_FIXED) {
			req->flags |= REQ_F_FIXED_FILE;
			req->file = io_file_get_fixed(req, cancel->fd,
							issue_flags);
		} else {
			req->file = io_file_get_normal(req, cancel->fd);
		}
		if (!req->file) {
			ret = -EBADF;
			goto done;
		}
		cd.file = req->file;
	}

	ret = __io_async_cancel(&cd, tctx, issue_flags);
done:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

void init_hash_table(struct io_hash_table *table, unsigned size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		spin_lock_init(&table->hbs[i].lock);
		INIT_HLIST_HEAD(&table->hbs[i].list);
	}
}

static int __io_sync_cancel(struct io_uring_task *tctx,
			    struct io_cancel_data *cd, int fd)
{
	struct io_ring_ctx *ctx = cd->ctx;

	/* fixed must be grabbed every time since we drop the uring_lock */
	if ((cd->flags & IORING_ASYNC_CANCEL_FD) &&
	    (cd->flags & IORING_ASYNC_CANCEL_FD_FIXED)) {
		unsigned long file_ptr;

		if (unlikely(fd >= ctx->nr_user_files))
			return -EBADF;
		fd = array_index_nospec(fd, ctx->nr_user_files);
		file_ptr = io_fixed_file_slot(&ctx->file_table, fd)->file_ptr;
		cd->file = (struct file *) (file_ptr & FFS_MASK);
		if (!cd->file)
			return -EBADF;
	}

	return __io_async_cancel(cd, tctx, 0);
}

int io_sync_cancel(struct io_ring_ctx *ctx, void __user *arg)
	__must_hold(&ctx->uring_lock)
{
	struct io_cancel_data cd = {
		.ctx	= ctx,
		.seq	= atomic_inc_return(&ctx->cancel_seq),
	};
	ktime_t timeout = KTIME_MAX;
	struct io_uring_sync_cancel_reg sc;
	struct fd f = { };
	DEFINE_WAIT(wait);
	int ret;

	if (copy_from_user(&sc, arg, sizeof(sc)))
		return -EFAULT;
	if (sc.flags & ~CANCEL_FLAGS)
		return -EINVAL;
	if (sc.pad[0] || sc.pad[1] || sc.pad[2] || sc.pad[3])
		return -EINVAL;

	cd.data = sc.addr;
	cd.flags = sc.flags;

	/* we can grab a normal file descriptor upfront */
	if ((cd.flags & IORING_ASYNC_CANCEL_FD) &&
	   !(cd.flags & IORING_ASYNC_CANCEL_FD_FIXED)) {
		f = fdget(sc.fd);
		if (!f.file)
			return -EBADF;
		cd.file = f.file;
	}

	ret = __io_sync_cancel(current->io_uring, &cd, sc.fd);

	/* found something, done! */
	if (ret != -EALREADY)
		goto out;

	if (sc.timeout.tv_sec != -1UL || sc.timeout.tv_nsec != -1UL) {
		struct timespec64 ts = {
			.tv_sec		= sc.timeout.tv_sec,
			.tv_nsec	= sc.timeout.tv_nsec
		};

		timeout = ktime_add_ns(timespec64_to_ktime(ts), ktime_get_ns());
	}

	/*
	 * Keep looking until we get -ENOENT. we'll get woken everytime
	 * every time a request completes and will retry the cancelation.
	 */
	do {
		cd.seq = atomic_inc_return(&ctx->cancel_seq);

		prepare_to_wait(&ctx->cq_wait, &wait, TASK_INTERRUPTIBLE);

		ret = __io_sync_cancel(current->io_uring, &cd, sc.fd);

		if (ret != -EALREADY)
			break;

		mutex_unlock(&ctx->uring_lock);
		ret = io_run_task_work_sig();
		if (ret < 0) {
			mutex_lock(&ctx->uring_lock);
			break;
		}
		ret = schedule_hrtimeout(&timeout, HRTIMER_MODE_ABS);
		mutex_lock(&ctx->uring_lock);
		if (!ret) {
			ret = -ETIME;
			break;
		}
	} while (1);

	finish_wait(&ctx->cq_wait, &wait);

	if (ret == -ENOENT || ret > 0)
		ret = 0;
out:
	fdput(f);
	return ret;
}
