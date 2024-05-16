// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/io_uring.h>
#include <linux/splice.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "splice.h"

struct io_splice {
	struct file			*file_out;
	loff_t				off_out;
	loff_t				off_in;
	u64				len;
	int				splice_fd_in;
	unsigned int			flags;
};

static int __io_splice_prep(struct io_kiocb *req,
			    const struct io_uring_sqe *sqe)
{
	struct io_splice *sp = io_kiocb_to_cmd(req, struct io_splice);
	unsigned int valid_flags = SPLICE_F_FD_IN_FIXED | SPLICE_F_ALL;

	sp->len = READ_ONCE(sqe->len);
	sp->flags = READ_ONCE(sqe->splice_flags);
	if (unlikely(sp->flags & ~valid_flags))
		return -EINVAL;
	sp->splice_fd_in = READ_ONCE(sqe->splice_fd_in);
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_tee_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	if (READ_ONCE(sqe->splice_off_in) || READ_ONCE(sqe->off))
		return -EINVAL;
	return __io_splice_prep(req, sqe);
}

int io_tee(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_splice *sp = io_kiocb_to_cmd(req, struct io_splice);
	struct file *out = sp->file_out;
	unsigned int flags = sp->flags & ~SPLICE_F_FD_IN_FIXED;
	struct file *in;
	ssize_t ret = 0;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	if (sp->flags & SPLICE_F_FD_IN_FIXED)
		in = io_file_get_fixed(req, sp->splice_fd_in, issue_flags);
	else
		in = io_file_get_normal(req, sp->splice_fd_in);
	if (!in) {
		ret = -EBADF;
		goto done;
	}

	if (sp->len)
		ret = do_tee(in, out, sp->len, flags);

	if (!(sp->flags & SPLICE_F_FD_IN_FIXED))
		fput(in);
done:
	if (ret != sp->len)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

int io_splice_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_splice *sp = io_kiocb_to_cmd(req, struct io_splice);

	sp->off_in = READ_ONCE(sqe->splice_off_in);
	sp->off_out = READ_ONCE(sqe->off);
	return __io_splice_prep(req, sqe);
}

int io_splice(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_splice *sp = io_kiocb_to_cmd(req, struct io_splice);
	struct file *out = sp->file_out;
	unsigned int flags = sp->flags & ~SPLICE_F_FD_IN_FIXED;
	loff_t *poff_in, *poff_out;
	struct file *in;
	ssize_t ret = 0;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	if (sp->flags & SPLICE_F_FD_IN_FIXED)
		in = io_file_get_fixed(req, sp->splice_fd_in, issue_flags);
	else
		in = io_file_get_normal(req, sp->splice_fd_in);
	if (!in) {
		ret = -EBADF;
		goto done;
	}

	poff_in = (sp->off_in == -1) ? NULL : &sp->off_in;
	poff_out = (sp->off_out == -1) ? NULL : &sp->off_out;

	if (sp->len)
		ret = do_splice(in, poff_in, out, poff_out, sp->len, flags);

	if (!(sp->flags & SPLICE_F_FD_IN_FIXED))
		fput(in);
done:
	if (ret != sp->len)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}
