// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "nop.h"

struct io_nop {
	/* NOTE: kiocb has the file as the first member, so don't do it here */
	struct file     *file;
	int             result;
};

int io_nop_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	unsigned int flags;
	struct io_nop *nop = io_kiocb_to_cmd(req, struct io_nop);

	flags = READ_ONCE(sqe->nop_flags);
	if (flags & ~IORING_NOP_INJECT_RESULT)
		return -EINVAL;

	if (flags & IORING_NOP_INJECT_RESULT)
		nop->result = READ_ONCE(sqe->len);
	else
		nop->result = 0;
	return 0;
}

int io_nop(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_nop *nop = io_kiocb_to_cmd(req, struct io_nop);

	if (nop->result < 0)
		req_set_fail(req);
	io_req_set_res(req, nop->result, 0);
	return IOU_OK;
}
