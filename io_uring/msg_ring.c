// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/nospec.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "rsrc.h"
#include "filetable.h"
#include "msg_ring.h"

struct io_msg {
	struct file			*file;
	u64 user_data;
	u32 len;
	u32 cmd;
	u32 src_fd;
	u32 dst_fd;
	u32 flags;
};

static int io_msg_ring_data(struct io_kiocb *req)
{
	struct io_ring_ctx *target_ctx = req->file->private_data;
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);

	if (msg->src_fd || msg->dst_fd || msg->flags)
		return -EINVAL;
	if (target_ctx->flags & IORING_SETUP_R_DISABLED)
		return -EBADFD;

	if (io_post_aux_cqe(target_ctx, msg->user_data, msg->len, 0, true))
		return 0;

	return -EOVERFLOW;
}

static void io_double_unlock_ctx(struct io_ring_ctx *ctx,
				 struct io_ring_ctx *octx,
				 unsigned int issue_flags)
{
	if (issue_flags & IO_URING_F_UNLOCKED)
		mutex_unlock(&ctx->uring_lock);
	mutex_unlock(&octx->uring_lock);
}

static int io_double_lock_ctx(struct io_ring_ctx *ctx,
			      struct io_ring_ctx *octx,
			      unsigned int issue_flags)
{
	/*
	 * To ensure proper ordering between the two ctxs, we can only
	 * attempt a trylock on the target. If that fails and we already have
	 * the source ctx lock, punt to io-wq.
	 */
	if (!(issue_flags & IO_URING_F_UNLOCKED)) {
		if (!mutex_trylock(&octx->uring_lock))
			return -EAGAIN;
		return 0;
	}

	/* Always grab smallest value ctx first. We know ctx != octx. */
	if (ctx < octx) {
		mutex_lock(&ctx->uring_lock);
		mutex_lock(&octx->uring_lock);
	} else {
		mutex_lock(&octx->uring_lock);
		mutex_lock(&ctx->uring_lock);
	}

	return 0;
}

static int io_msg_send_fd(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *target_ctx = req->file->private_data;
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long file_ptr;
	struct file *src_file;
	int ret;

	if (msg->len)
		return -EINVAL;
	if (target_ctx == ctx)
		return -EINVAL;
	if (target_ctx->flags & IORING_SETUP_R_DISABLED)
		return -EBADFD;

	ret = io_double_lock_ctx(ctx, target_ctx, issue_flags);
	if (unlikely(ret))
		return ret;

	ret = -EBADF;
	if (unlikely(msg->src_fd >= ctx->nr_user_files))
		goto out_unlock;

	msg->src_fd = array_index_nospec(msg->src_fd, ctx->nr_user_files);
	file_ptr = io_fixed_file_slot(&ctx->file_table, msg->src_fd)->file_ptr;
	if (!file_ptr)
		goto out_unlock;

	src_file = (struct file *) (file_ptr & FFS_MASK);
	get_file(src_file);

	ret = __io_fixed_fd_install(target_ctx, src_file, msg->dst_fd);
	if (ret < 0) {
		fput(src_file);
		goto out_unlock;
	}

	if (msg->flags & IORING_MSG_RING_CQE_SKIP)
		goto out_unlock;

	/*
	 * If this fails, the target still received the file descriptor but
	 * wasn't notified of the fact. This means that if this request
	 * completes with -EOVERFLOW, then the sender must ensure that a
	 * later IORING_OP_MSG_RING delivers the message.
	 */
	if (!io_post_aux_cqe(target_ctx, msg->user_data, ret, 0, true))
		ret = -EOVERFLOW;
out_unlock:
	io_double_unlock_ctx(ctx, target_ctx, issue_flags);
	return ret;
}

int io_msg_ring_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);

	if (unlikely(sqe->buf_index || sqe->personality))
		return -EINVAL;

	msg->user_data = READ_ONCE(sqe->off);
	msg->len = READ_ONCE(sqe->len);
	msg->cmd = READ_ONCE(sqe->addr);
	msg->src_fd = READ_ONCE(sqe->addr3);
	msg->dst_fd = READ_ONCE(sqe->file_index);
	msg->flags = READ_ONCE(sqe->msg_ring_flags);
	if (msg->flags & ~IORING_MSG_RING_CQE_SKIP)
		return -EINVAL;

	return 0;
}

int io_msg_ring(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	int ret;

	ret = -EBADFD;
	if (!io_is_uring_fops(req->file))
		goto done;

	switch (msg->cmd) {
	case IORING_MSG_DATA:
		ret = io_msg_ring_data(req);
		break;
	case IORING_MSG_SEND_FD:
		ret = io_msg_send_fd(req, issue_flags);
		break;
	default:
		ret = -EINVAL;
		break;
	}

done:
	if (ret == -EAGAIN)
		return -EAGAIN;
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}
