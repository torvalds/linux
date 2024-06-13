// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "../fs/internal.h"

#include "io_uring.h"
#include "statx.h"

struct io_statx {
	struct file			*file;
	int				dfd;
	unsigned int			mask;
	unsigned int			flags;
	struct filename			*filename;
	struct statx __user		*buffer;
};

int io_statx_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_statx *sx = io_kiocb_to_cmd(req, struct io_statx);
	const char __user *path;

	if (sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (req->flags & REQ_F_FIXED_FILE)
		return -EBADF;

	sx->dfd = READ_ONCE(sqe->fd);
	sx->mask = READ_ONCE(sqe->len);
	path = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sx->buffer = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	sx->flags = READ_ONCE(sqe->statx_flags);

	sx->filename = getname_flags(path,
				     getname_statx_lookup_flags(sx->flags),
				     NULL);

	if (IS_ERR(sx->filename)) {
		int ret = PTR_ERR(sx->filename);

		sx->filename = NULL;
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

int io_statx(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_statx *sx = io_kiocb_to_cmd(req, struct io_statx);
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

	ret = do_statx(sx->dfd, sx->filename, sx->flags, sx->mask, sx->buffer);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

void io_statx_cleanup(struct io_kiocb *req)
{
	struct io_statx *sx = io_kiocb_to_cmd(req, struct io_statx);

	if (sx->filename)
		putname(sx->filename);
}
