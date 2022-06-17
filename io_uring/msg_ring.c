// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring_types.h"
#include "io_uring.h"
#include "msg_ring.h"

struct io_msg {
	struct file			*file;
	u64 user_data;
	u32 len;
};

int io_msg_ring_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_msg *msg = io_kiocb_to_cmd(req);

	if (unlikely(sqe->addr || sqe->rw_flags || sqe->splice_fd_in ||
		     sqe->buf_index || sqe->personality))
		return -EINVAL;

	msg->user_data = READ_ONCE(sqe->off);
	msg->len = READ_ONCE(sqe->len);
	return 0;
}

int io_msg_ring(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_msg *msg = io_kiocb_to_cmd(req);
	struct io_ring_ctx *target_ctx;
	int ret;

	ret = -EBADFD;
	if (!io_is_uring_fops(req->file))
		goto done;

	ret = -EOVERFLOW;
	target_ctx = req->file->private_data;
	if (io_post_aux_cqe(target_ctx, msg->user_data, msg->len, 0))
		ret = 0;

done:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	/* put file to avoid an attempt to IOPOLL the req */
	io_put_file(req->file);
	req->file = NULL;
	return IOU_OK;
}
