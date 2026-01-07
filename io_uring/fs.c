// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "../fs/internal.h"

#include "io_uring.h"
#include "fs.h"

struct io_rename {
	struct file			*file;
	int				old_dfd;
	int				new_dfd;
	struct delayed_filename		oldpath;
	struct delayed_filename		newpath;
	int				flags;
};

struct io_unlink {
	struct file			*file;
	int				dfd;
	int				flags;
	struct delayed_filename		filename;
};

struct io_mkdir {
	struct file			*file;
	int				dfd;
	umode_t				mode;
	struct delayed_filename		filename;
};

struct io_link {
	struct file			*file;
	int				old_dfd;
	int				new_dfd;
	struct delayed_filename		oldpath;
	struct delayed_filename		newpath;
	int				flags;
};

int io_renameat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_rename *ren = io_kiocb_to_cmd(req, struct io_rename);
	const char __user *oldf, *newf;
	int err;

	if (sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	ren->old_dfd = READ_ONCE(sqe->fd);
	oldf = u64_to_user_ptr(READ_ONCE(sqe->addr));
	newf = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	ren->new_dfd = READ_ONCE(sqe->len);
	ren->flags = READ_ONCE(sqe->rename_flags);

	err = delayed_getname(&ren->oldpath, oldf);
	if (unlikely(err))
		return err;

	err = delayed_getname(&ren->newpath, newf);
	if (unlikely(err)) {
		dismiss_delayed_filename(&ren->oldpath);
		return err;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_renameat(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_rename *ren = io_kiocb_to_cmd(req, struct io_rename);
	CLASS(filename_complete_delayed, old)(&ren->oldpath);
	CLASS(filename_complete_delayed, new)(&ren->newpath);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	ret = filename_renameat2(ren->old_dfd, old,
				 ren->new_dfd, new, ren->flags);

	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

void io_renameat_cleanup(struct io_kiocb *req)
{
	struct io_rename *ren = io_kiocb_to_cmd(req, struct io_rename);

	dismiss_delayed_filename(&ren->oldpath);
	dismiss_delayed_filename(&ren->newpath);
}

int io_unlinkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_unlink *un = io_kiocb_to_cmd(req, struct io_unlink);
	const char __user *fname;
	int err;

	if (sqe->off || sqe->len || sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	un->dfd = READ_ONCE(sqe->fd);

	un->flags = READ_ONCE(sqe->unlink_flags);
	if (un->flags & ~AT_REMOVEDIR)
		return -EINVAL;

	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	err = delayed_getname(&un->filename, fname);
	if (unlikely(err))
		return err;

	req->flags |= REQ_F_NEED_CLEANUP;
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_unlinkat(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_unlink *un = io_kiocb_to_cmd(req, struct io_unlink);
	CLASS(filename_complete_delayed, name)(&un->filename);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	if (un->flags & AT_REMOVEDIR)
		ret = filename_rmdir(un->dfd, name);
	else
		ret = filename_unlinkat(un->dfd, name);

	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

void io_unlinkat_cleanup(struct io_kiocb *req)
{
	struct io_unlink *ul = io_kiocb_to_cmd(req, struct io_unlink);

	dismiss_delayed_filename(&ul->filename);
}

int io_mkdirat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_mkdir *mkd = io_kiocb_to_cmd(req, struct io_mkdir);
	const char __user *fname;
	int err;

	if (sqe->off || sqe->rw_flags || sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	mkd->dfd = READ_ONCE(sqe->fd);
	mkd->mode = READ_ONCE(sqe->len);

	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	err = delayed_getname(&mkd->filename, fname);
	if (unlikely(err))
		return err;

	req->flags |= REQ_F_NEED_CLEANUP;
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_mkdirat(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_mkdir *mkd = io_kiocb_to_cmd(req, struct io_mkdir);
	CLASS(filename_complete_delayed, name)(&mkd->filename);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	ret = filename_mkdirat(mkd->dfd, name, mkd->mode);

	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

void io_mkdirat_cleanup(struct io_kiocb *req)
{
	struct io_mkdir *md = io_kiocb_to_cmd(req, struct io_mkdir);

	dismiss_delayed_filename(&md->filename);
}

int io_symlinkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_link *sl = io_kiocb_to_cmd(req, struct io_link);
	const char __user *oldpath, *newpath;
	int err;

	if (sqe->len || sqe->rw_flags || sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	sl->new_dfd = READ_ONCE(sqe->fd);
	oldpath = u64_to_user_ptr(READ_ONCE(sqe->addr));
	newpath = u64_to_user_ptr(READ_ONCE(sqe->addr2));

	err = delayed_getname(&sl->oldpath, oldpath);
	if (unlikely(err))
		return err;

	err = delayed_getname(&sl->newpath, newpath);
	if (unlikely(err)) {
		dismiss_delayed_filename(&sl->oldpath);
		return err;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_symlinkat(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_link *sl = io_kiocb_to_cmd(req, struct io_link);
	CLASS(filename_complete_delayed, old)(&sl->oldpath);
	CLASS(filename_complete_delayed, new)(&sl->newpath);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	ret = filename_symlinkat(old, sl->new_dfd, new);

	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

int io_linkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_link *lnk = io_kiocb_to_cmd(req, struct io_link);
	const char __user *oldf, *newf;
	int err;

	if (sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	lnk->old_dfd = READ_ONCE(sqe->fd);
	lnk->new_dfd = READ_ONCE(sqe->len);
	oldf = u64_to_user_ptr(READ_ONCE(sqe->addr));
	newf = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	lnk->flags = READ_ONCE(sqe->hardlink_flags);

	err = delayed_getname_uflags(&lnk->oldpath, oldf, lnk->flags);
	if (unlikely(err))
		return err;

	err = delayed_getname(&lnk->newpath, newf);
	if (unlikely(err)) {
		dismiss_delayed_filename(&lnk->oldpath);
		return err;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	req->flags |= REQ_F_FORCE_ASYNC;
	return 0;
}

int io_linkat(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_link *lnk = io_kiocb_to_cmd(req, struct io_link);
	CLASS(filename_complete_delayed, old)(&lnk->oldpath);
	CLASS(filename_complete_delayed, new)(&lnk->newpath);
	int ret;

	WARN_ON_ONCE(issue_flags & IO_URING_F_NONBLOCK);

	ret = filename_linkat(lnk->old_dfd, old, lnk->new_dfd, new, lnk->flags);

	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

void io_link_cleanup(struct io_kiocb *req)
{
	struct io_link *sl = io_kiocb_to_cmd(req, struct io_link);

	dismiss_delayed_filename(&sl->oldpath);
	dismiss_delayed_filename(&sl->newpath);
}
