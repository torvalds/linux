// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/io_uring.h>
#include <linux/security.h>
#include <linux/nospec.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "rsrc.h"
#include "uring_cmd.h"

static void io_uring_cmd_work(struct io_kiocb *req, bool *locked)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);

	ioucmd->task_work_cb(ioucmd);
}

void io_uring_cmd_complete_in_task(struct io_uring_cmd *ioucmd,
			void (*task_work_cb)(struct io_uring_cmd *))
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	ioucmd->task_work_cb = task_work_cb;
	req->io_task_work.func = io_uring_cmd_work;
	io_req_task_work_add(req);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_complete_in_task);

static inline void io_req_set_cqe32_extra(struct io_kiocb *req,
					  u64 extra1, u64 extra2)
{
	req->extra1 = extra1;
	req->extra2 = extra2;
	req->flags |= REQ_F_CQE32_INIT;
}

/*
 * Called by consumers of io_uring_cmd, if they originally returned
 * -EIOCBQUEUED upon receiving the command.
 */
void io_uring_cmd_done(struct io_uring_cmd *ioucmd, ssize_t ret, ssize_t res2)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	if (ret < 0)
		req_set_fail(req);

	io_req_set_res(req, ret, 0);
	if (req->ctx->flags & IORING_SETUP_CQE32)
		io_req_set_cqe32_extra(req, res2, 0);
	if (req->ctx->flags & IORING_SETUP_IOPOLL)
		/* order with io_iopoll_req_issued() checking ->iopoll_complete */
		smp_store_release(&req->iopoll_completed, 1);
	else
		__io_req_complete(req, 0);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_done);

int io_uring_cmd_prep_async(struct io_kiocb *req)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	size_t cmd_size;

	BUILD_BUG_ON(uring_cmd_pdu_size(0) != 16);
	BUILD_BUG_ON(uring_cmd_pdu_size(1) != 80);

	cmd_size = uring_cmd_pdu_size(req->ctx->flags & IORING_SETUP_SQE128);

	memcpy(req->async_data, ioucmd->cmd, cmd_size);
	return 0;
}

int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);

	if (sqe->__pad1)
		return -EINVAL;

	ioucmd->flags = READ_ONCE(sqe->uring_cmd_flags);
	if (ioucmd->flags & ~IORING_URING_CMD_FIXED)
		return -EINVAL;

	if (ioucmd->flags & IORING_URING_CMD_FIXED) {
		struct io_ring_ctx *ctx = req->ctx;
		u16 index;

		req->buf_index = READ_ONCE(sqe->buf_index);
		if (unlikely(req->buf_index >= ctx->nr_user_bufs))
			return -EFAULT;
		index = array_index_nospec(req->buf_index, ctx->nr_user_bufs);
		req->imu = ctx->user_bufs[index];
		io_req_set_rsrc_node(req, ctx, 0);
	}
	ioucmd->cmd = sqe->cmd;
	ioucmd->cmd_op = READ_ONCE(sqe->cmd_op);
	return 0;
}

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file = req->file;
	int ret;

	if (!req->file->f_op->uring_cmd)
		return -EOPNOTSUPP;

	ret = security_uring_cmd(ioucmd);
	if (ret)
		return ret;

	if (ctx->flags & IORING_SETUP_SQE128)
		issue_flags |= IO_URING_F_SQE128;
	if (ctx->flags & IORING_SETUP_CQE32)
		issue_flags |= IO_URING_F_CQE32;
	if (ctx->flags & IORING_SETUP_IOPOLL) {
		issue_flags |= IO_URING_F_IOPOLL;
		req->iopoll_completed = 0;
		WRITE_ONCE(ioucmd->cookie, NULL);
	}

	if (req_has_async_data(req))
		ioucmd->cmd = req->async_data;

	ret = file->f_op->uring_cmd(ioucmd, issue_flags);
	if (ret == -EAGAIN) {
		if (!req_has_async_data(req)) {
			if (io_alloc_async_data(req))
				return -ENOMEM;
			io_uring_cmd_prep_async(req);
		}
		return -EAGAIN;
	}

	if (ret != -EIOCBQUEUED) {
		if (ret < 0)
			req_set_fail(req);
		io_req_set_res(req, ret, 0);
		return ret;
	}

	return IOU_ISSUE_SKIP_COMPLETE;
}

int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter, void *ioucmd)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	return io_import_fixed(rw, iter, req->imu, ubuf, len);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_import_fixed);
