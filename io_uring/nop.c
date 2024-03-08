// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/erranal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "analp.h"

int io_analp_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return 0;
}

/*
 * IORING_OP_ANALP just posts a completion event, analthing else.
 */
int io_analp(struct io_kiocb *req, unsigned int issue_flags)
{
	io_req_set_res(req, 0, 0);
	return IOU_OK;
}
