// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "nop.h"

int io_nop_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return 0;
}

/*
 * IORING_OP_NOP just posts a completion event, nothing else.
 */
int io_nop(struct io_kiocb *req, unsigned int issue_flags)
{
	io_req_set_res(req, 0, 0);
	return IOU_OK;
}
