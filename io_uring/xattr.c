// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/io_uring.h>
#include <linux/xattr.h>

#include <uapi/linux/io_uring.h>

#include "../fs/internal.h"

#include "io_uring.h"
#include "xattr.h"

struct io_xattr {
	struct file			*file;
	struct xattr_ctx		ctx;
	struct filename			*filename;
};

void io_xattr_cleanup(struct io_kiocb *req)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);

	if (ix->filename)
		putname(ix->filename);

	kfree(ix->ctx.kname);
	kvfree(ix->ctx.kvalue);
}

static void io_xattr_finish(struct io_kiocb *req, int ret)
{
	req->flags &= ~REQ_F_NEED_CLEANUP;

	io_xattr_cleanup(req);
	io_req_set_res(req, ret, 0);
}

static int __io_getxattr_prep(struct io_kiocb *req,
			      const struct io_uring_sqe *sqe)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	const char __user *name;
	int ret;

	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	ix->filename = NULL;
	ix->ctx.kvalue = NULL;
	name = u64_to_user_ptr(READ_ONCE(sqe->addr));
	ix->ctx.cvalue = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	ix->ctx.size = READ_ONCE(sqe->len);
	ix->ctx.flags = READ_ONCE(sqe->xattr_flags);

	if (ix->ctx.flags)
		return -EINVAL;

	ix->ctx.kname = kmalloc(sizeof(*ix->ctx.kname), GFP_KERNEL);
	if (!ix->ctx.kname)
		return -ENOMEM;

	ret = strncpy_from_user(ix->ctx.kname->name, name,
				sizeof(ix->ctx.kname->name));
	if (!ret || ret == sizeof(ix->ctx.kname->name))
		ret = -ERANGE;
	if (ret < 0) {
		kfree(ix->ctx.kname);
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

int io_fgetxattr_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_getxattr_prep(req, sqe);
}

int io_getxattr_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	const char __user *path;
	int ret;

	ret = __io_getxattr_prep(req, sqe);
	if (ret)
		return ret;

	path = u64_to_user_ptr(READ_ONCE(sqe->addr3));

	ix->filename = getname_flags(path, LOOKUP_FOLLOW, NULL);
	if (IS_ERR(ix->filename)) {
		ret = PTR_ERR(ix->filename);
		ix->filename = NULL;
	}

	return ret;
}

int io_fgetxattr(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

	ret = do_getxattr(mnt_idmap(req->file->f_path.mnt),
			req->file->f_path.dentry,
			&ix->ctx);

	io_xattr_finish(req, ret);
	return IOU_OK;
}

int io_getxattr(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	unsigned int lookup_flags = LOOKUP_FOLLOW;
	struct path path;
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

retry:
	ret = filename_lookup(AT_FDCWD, ix->filename, lookup_flags, &path, NULL);
	if (!ret) {
		ret = do_getxattr(mnt_idmap(path.mnt), path.dentry, &ix->ctx);

		path_put(&path);
		if (retry_estale(ret, lookup_flags)) {
			lookup_flags |= LOOKUP_REVAL;
			goto retry;
		}
	}

	io_xattr_finish(req, ret);
	return IOU_OK;
}

static int __io_setxattr_prep(struct io_kiocb *req,
			const struct io_uring_sqe *sqe)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	const char __user *name;
	int ret;

	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	ix->filename = NULL;
	name = u64_to_user_ptr(READ_ONCE(sqe->addr));
	ix->ctx.cvalue = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	ix->ctx.kvalue = NULL;
	ix->ctx.size = READ_ONCE(sqe->len);
	ix->ctx.flags = READ_ONCE(sqe->xattr_flags);

	ix->ctx.kname = kmalloc(sizeof(*ix->ctx.kname), GFP_KERNEL);
	if (!ix->ctx.kname)
		return -ENOMEM;

	ret = setxattr_copy(name, &ix->ctx);
	if (ret) {
		kfree(ix->ctx.kname);
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

int io_setxattr_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	const char __user *path;
	int ret;

	ret = __io_setxattr_prep(req, sqe);
	if (ret)
		return ret;

	path = u64_to_user_ptr(READ_ONCE(sqe->addr3));

	ix->filename = getname_flags(path, LOOKUP_FOLLOW, NULL);
	if (IS_ERR(ix->filename)) {
		ret = PTR_ERR(ix->filename);
		ix->filename = NULL;
	}

	return ret;
}

int io_fsetxattr_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_setxattr_prep(req, sqe);
}

static int __io_setxattr(struct io_kiocb *req, unsigned int issue_flags,
			const struct path *path)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	int ret;

	ret = mnt_want_write(path->mnt);
	if (!ret) {
		ret = do_setxattr(mnt_idmap(path->mnt), path->dentry, &ix->ctx);
		mnt_drop_write(path->mnt);
	}

	return ret;
}

int io_fsetxattr(struct io_kiocb *req, unsigned int issue_flags)
{
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

	ret = __io_setxattr(req, issue_flags, &req->file->f_path);
	io_xattr_finish(req, ret);
	return IOU_OK;
}

int io_setxattr(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_xattr *ix = io_kiocb_to_cmd(req, struct io_xattr);
	unsigned int lookup_flags = LOOKUP_FOLLOW;
	struct path path;
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

retry:
	ret = filename_lookup(AT_FDCWD, ix->filename, lookup_flags, &path, NULL);
	if (!ret) {
		ret = __io_setxattr(req, issue_flags, &path);
		path_put(&path);
		if (retry_estale(ret, lookup_flags)) {
			lookup_flags |= LOOKUP_REVAL;
			goto retry;
		}
	}

	io_xattr_finish(req, ret);
	return IOU_OK;
}
