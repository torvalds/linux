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
#include "waitid.h"
#include "futex.h"
#include "cancel.h"

struct io_cancel {
	struct file			*file;
	u64				addr;
	u32				flags;
	s32				fd;
	u8				opcode;
};

#define CANCEL_FLAGS	(IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_FD | \
			 IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_FD_FIXED | \
			 IORING_ASYNC_CANCEL_USERDATA | IORING_ASYNC_CANCEL_OP)

/*
 * Returns true if the request matches the criteria outlined by 'cd'.
 */
bool io_cancel_req_match(struct io_kiocb *req, struct io_cancel_data *cd)
{
	bool match_user_data = cd->flags & IORING_ASYNC_CANCEL_USERDATA;

	if (req->ctx != cd->ctx)
		return false;

	if (!(cd->flags & (IORING_ASYNC_CANCEL_FD | IORING_ASYNC_CANCEL_OP)))
		match_user_data = true;

	if (cd->flags & IORING_ASYNC_CANCEL_ANY)
		goto check_seq;
	if (cd->flags & IORING_ASYNC_CANCEL_FD) {
		if (req->file != cd->file)
			return false;
	}
	if (cd->flags & IORING_ASYNC_CANCEL_OP) {
		if (req->opcode != cd->opcode)
			return false;
	}
	if (match_user_data && req->cqe.user_data != cd->data)
		return false;
	if (cd->flags & IORING_ASYNC_CANCEL_ALL) {
check_seq:
		if (io_cancel_match_sequence(req, cd->seq))
			return false;
	}

	return true;
}

static bool io_cancel_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_cancel_data *cd = data;

	return io_cancel_req_match(req, cd);
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

	ret = io_waitid_cancel(ctx, cd, issue_flags);
	if (ret != -ENOENT)
		return ret;

	ret = io_futex_cancel(ctx, cd, issue_flags);
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
	if (sqe->off || sqe->splice_fd_in)
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
	if (cancel->flags & IORING_ASYNC_CANCEL_OP) {
		if (cancel->flags & IORING_ASYNC_CANCEL_ANY)
			return -EINVAL;
		cancel->opcode = READ_ONCE(sqe->len);
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
		ret = io_async_cancel_one(node->task->io_uring, cd);
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
		.opcode	= cancel->opcode,
		.seq	= atomic_inc_return(&req->ctx->cancel_seq),
	};
	struct io_uring_task *tctx = req->tctx;
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

static int __io_sync_cancel(struct io_uring_task *tctx,
			    struct io_cancel_data *cd, int fd)
{
	struct io_ring_ctx *ctx = cd->ctx;

	/* fixed must be grabbed every time since we drop the uring_lock */
	if ((cd->flags & IORING_ASYNC_CANCEL_FD) &&
	    (cd->flags & IORING_ASYNC_CANCEL_FD_FIXED)) {
		struct io_rsrc_node *node;

		node = io_rsrc_node_lookup(&ctx->file_table.data, fd);
		if (unlikely(!node))
			return -EBADF;
		cd->file = io_slot_file(node);
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
	struct file *file = NULL;
	DEFINE_WAIT(wait);
	int ret, i;

	if (copy_from_user(&sc, arg, sizeof(sc)))
		return -EFAULT;
	if (sc.flags & ~CANCEL_FLAGS)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(sc.pad); i++)
		if (sc.pad[i])
			return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(sc.pad2); i++)
		if (sc.pad2[i])
			return -EINVAL;

	cd.data = sc.addr;
	cd.flags = sc.flags;
	cd.opcode = sc.opcode;

	/* we can grab a normal file descriptor upfront */
	if ((cd.flags & IORING_ASYNC_CANCEL_FD) &&
	   !(cd.flags & IORING_ASYNC_CANCEL_FD_FIXED)) {
		file = fget(sc.fd);
		if (!file)
			return -EBADF;
		cd.file = file;
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

		mutex_unlock(&ctx->uring_lock);
		if (ret != -EALREADY)
			break;

		ret = io_run_task_work_sig(ctx);
		if (ret < 0)
			break;
		ret = schedule_hrtimeout(&timeout, HRTIMER_MODE_ABS);
		if (!ret) {
			ret = -ETIME;
			break;
		}
		mutex_lock(&ctx->uring_lock);
	} while (1);

	finish_wait(&ctx->cq_wait, &wait);
	mutex_lock(&ctx->uring_lock);

	if (ret == -ENOENT || ret > 0)
		ret = 0;
out:
	if (file)
		fput(file);
	return ret;
}

bool io_cancel_remove_all(struct io_ring_ctx *ctx, struct io_uring_task *tctx,
			  struct hlist_head *list, bool cancel_all,
			  bool (*cancel)(struct io_kiocb *))
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool found = false;

	lockdep_assert_held(&ctx->uring_lock);

	hlist_for_each_entry_safe(req, tmp, list, hash_node) {
		if (!io_match_task_safe(req, tctx, cancel_all))
			continue;
		hlist_del_init(&req->hash_node);
		if (cancel(req))
			found = true;
	}

	return found;
}

int io_cancel_remove(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
		     unsigned int issue_flags, struct hlist_head *list,
		     bool (*cancel)(struct io_kiocb *))
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	int nr = 0;

	io_ring_submit_lock(ctx, issue_flags);
	hlist_for_each_entry_safe(req, tmp, list, hash_node) {
		if (!io_cancel_req_match(req, cd))
			continue;
		if (cancel(req))
			nr++;
		if (!(cd->flags & IORING_ASYNC_CANCEL_ALL))
			break;
	}
	io_ring_submit_unlock(ctx, issue_flags);
	return nr ?: -ENOENT;
}
