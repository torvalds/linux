// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "rsrc.h"
#include "nop.h"

struct io_nop {
	/* NOTE: kiocb has the file as the first member, so don't do it here */
	struct file     *file;
	int             result;
	int		fd;
	unsigned int	flags;
};

#define NOP_FLAGS	(IORING_NOP_INJECT_RESULT | IORING_NOP_FIXED_FILE | \
			 IORING_NOP_FIXED_BUFFER | IORING_NOP_FILE)

int io_nop_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_nop *nop = io_kiocb_to_cmd(req, struct io_nop);

	nop->flags = READ_ONCE(sqe->nop_flags);
	if (nop->flags & ~NOP_FLAGS)
		return -EINVAL;

	if (nop->flags & IORING_NOP_INJECT_RESULT)
		nop->result = READ_ONCE(sqe->len);
	else
		nop->result = 0;
	if (nop->flags & IORING_NOP_FILE)
		nop->fd = READ_ONCE(sqe->fd);
	else
		nop->fd = -1;
	if (nop->flags & IORING_NOP_FIXED_BUFFER)
		req->buf_index = READ_ONCE(sqe->buf_index);
	return 0;
}

int io_nop(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_nop *nop = io_kiocb_to_cmd(req, struct io_nop);
	int ret = nop->result;

	if (nop->flags & IORING_NOP_FILE) {
		if (nop->flags & IORING_NOP_FIXED_FILE) {
			req->file = io_file_get_fixed(req, nop->fd, issue_flags);
			req->flags |= REQ_F_FIXED_FILE;
		} else {
			req->file = io_file_get_normal(req, nop->fd);
		}
		if (!req->file) {
			ret = -EBADF;
			goto done;
		}
	}
	if (nop->flags & IORING_NOP_FIXED_BUFFER) {
		if (!io_find_buf_node(req, issue_flags))
			ret = -EFAULT;
	}
done:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, nop->result, 0);
	return IOU_OK;
}
