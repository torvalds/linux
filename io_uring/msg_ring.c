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
	struct file			*src_file;
	u64 user_data;
	u32 len;
	u32 cmd;
	u32 src_fd;
	u32 dst_fd;
	u32 flags;
};

static void io_double_unlock_ctx(struct io_ring_ctx *octx)
{
	mutex_unlock(&octx->uring_lock);
}

static int io_double_lock_ctx(struct io_ring_ctx *octx,
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
	mutex_lock(&octx->uring_lock);
	return 0;
}

void io_msg_ring_cleanup(struct io_kiocb *req)
{
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);

	if (WARN_ON_ONCE(!msg->src_file))
		return;

	fput(msg->src_file);
	msg->src_file = NULL;
}

static int io_msg_ring_data(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *target_ctx = req->file->private_data;
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	int ret;

	if (msg->src_fd || msg->dst_fd || msg->flags)
		return -EINVAL;
	if (target_ctx->flags & IORING_SETUP_R_DISABLED)
		return -EBADFD;

	ret = -EOVERFLOW;
	if (target_ctx->flags & IORING_SETUP_IOPOLL) {
		if (unlikely(io_double_lock_ctx(target_ctx, issue_flags)))
			return -EAGAIN;
		if (io_post_aux_cqe(target_ctx, msg->user_data, msg->len, 0, true))
			ret = 0;
		io_double_unlock_ctx(target_ctx);
	} else {
		if (io_post_aux_cqe(target_ctx, msg->user_data, msg->len, 0, true))
			ret = 0;
	}

	return ret;
}

static struct file *io_msg_grab_file(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file = NULL;
	unsigned long file_ptr;
	int idx = msg->src_fd;

	io_ring_submit_lock(ctx, issue_flags);
	if (likely(idx < ctx->nr_user_files)) {
		idx = array_index_nospec(idx, ctx->nr_user_files);
		file_ptr = io_fixed_file_slot(&ctx->file_table, idx)->file_ptr;
		file = (struct file *) (file_ptr & FFS_MASK);
		if (file)
			get_file(file);
	}
	io_ring_submit_unlock(ctx, issue_flags);
	return file;
}

static int io_msg_install_complete(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *target_ctx = req->file->private_data;
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	struct file *src_file = msg->src_file;
	int ret;

	if (unlikely(io_double_lock_ctx(target_ctx, issue_flags)))
		return -EAGAIN;

	ret = __io_fixed_fd_install(target_ctx, src_file, msg->dst_fd);
	if (ret < 0)
		goto out_unlock;

	msg->src_file = NULL;
	req->flags &= ~REQ_F_NEED_CLEANUP;

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
	io_double_unlock_ctx(target_ctx);
	return ret;
}

static int io_msg_send_fd(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *target_ctx = req->file->private_data;
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);
	struct io_ring_ctx *ctx = req->ctx;
	struct file *src_file = msg->src_file;

	if (target_ctx == ctx)
		return -EINVAL;
	if (!src_file) {
		src_file = io_msg_grab_file(req, issue_flags);
		if (!src_file)
			return -EBADF;
		msg->src_file = src_file;
		req->flags |= REQ_F_NEED_CLEANUP;
	}
	return io_msg_install_complete(req, issue_flags);
}

int io_msg_ring_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_msg *msg = io_kiocb_to_cmd(req, struct io_msg);

	if (unlikely(sqe->buf_index || sqe->personality))
		return -EINVAL;

	msg->src_file = NULL;
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
		ret = io_msg_ring_data(req, issue_flags);
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
