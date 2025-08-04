// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "../fs/internal.h"

#include "io_uring.h"
#include "truncate.h"

struct io_ftrunc {
	struct file			*file;
	loff_t				len;
};

int io_ftruncate_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ftrunc *ft = io_kiocb_to_cmd(req, struct io_ftrunc);

	if (sqe->rw_flags || sqe->addr || sqe->len || sqe->buf_index ||
	    sqe->splice_fd_in || sqe->addr3)
		return -EINVAL;

	ft->len = READ_ONCE(sqe->off);

	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_ftruncate(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ftrunc *ft = io_kiocb_to_cmd(req, struct io_ftrunc);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	ret = do_ftruncate(req->file, ft->len, 1);

	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}
