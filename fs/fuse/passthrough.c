// SPDX-License-Identifier: GPL-2.0

#include "fuse_i.h"

#include <linux/fuse.h>
#include <linux/idr.h>
#include <linux/uio.h>

#define PASSTHROUGH_IOCB_MASK                                                  \
	(IOCB_APPEND | IOCB_DSYNC | IOCB_HIPRI | IOCB_NOWAIT | IOCB_SYNC)

struct fuse_aio_req {
	struct kiocb iocb;
	struct kiocb *iocb_fuse;
};

static void fuse_copyattr(struct file *dst_file, struct file *src_file)
{
	struct inode *dst = file_inode(dst_file);
	struct inode *src = file_inode(src_file);

	i_size_write(dst, i_size_read(src));
}

static void fuse_aio_cleanup_handler(struct fuse_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	if (iocb->ki_flags & IOCB_WRITE) {
		__sb_writers_acquired(file_inode(iocb->ki_filp)->i_sb,
				      SB_FREEZE_WRITE);
		file_end_write(iocb->ki_filp);
		fuse_copyattr(iocb_fuse->ki_filp, iocb->ki_filp);
	}

	iocb_fuse->ki_pos = iocb->ki_pos;
	kfree(aio_req);
}

static void fuse_aio_rw_complete(struct kiocb *iocb, long res, long res2)
{
	struct fuse_aio_req *aio_req =
		container_of(iocb, struct fuse_aio_req, iocb);
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	fuse_aio_cleanup_handler(aio_req);
	iocb_fuse->ki_complete(iocb_fuse, res, res2);
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb_fuse,
				   struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct file *passthrough_filp = ff->passthrough.filp;

	if (!iov_iter_count(iter))
		return 0;

	old_cred = override_creds(ff->passthrough.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		ret = vfs_iter_read(passthrough_filp, iter, &iocb_fuse->ki_pos,
				    iocb_to_rw_flags(iocb_fuse->ki_flags,
						     PASSTHROUGH_IOCB_MASK));
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req) {
			ret = -ENOMEM;
			goto out;
		}

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, passthrough_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_read_iter(passthrough_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
out:
	revert_creds(old_cred);

	return ret;
}

ssize_t fuse_passthrough_write_iter(struct kiocb *iocb_fuse,
				    struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct inode *fuse_inode = file_inode(fuse_filp);
	struct file *passthrough_filp = ff->passthrough.filp;
	struct inode *passthrough_inode = file_inode(passthrough_filp);

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(fuse_inode);

	old_cred = override_creds(ff->passthrough.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		file_start_write(passthrough_filp);
		ret = vfs_iter_write(passthrough_filp, iter, &iocb_fuse->ki_pos,
				     iocb_to_rw_flags(iocb_fuse->ki_flags,
						      PASSTHROUGH_IOCB_MASK));
		file_end_write(passthrough_filp);
		if (ret > 0)
			fuse_copyattr(fuse_filp, passthrough_filp);
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req) {
			ret = -ENOMEM;
			goto out;
		}

		file_start_write(passthrough_filp);
		__sb_writers_release(passthrough_inode->i_sb, SB_FREEZE_WRITE);

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, passthrough_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_write_iter(passthrough_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
out:
	revert_creds(old_cred);
	inode_unlock(fuse_inode);

	return ret;
}

ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	const struct cred *old_cred;
	struct fuse_file *ff = file->private_data;
	struct inode *fuse_inode = file_inode(file);
	struct file *passthrough_filp = ff->passthrough.filp;
	struct inode *passthrough_inode = file_inode(passthrough_filp);

	if (!passthrough_filp->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(passthrough_filp);

	old_cred = override_creds(ff->passthrough.cred);
	ret = call_mmap(vma->vm_file, vma);
	revert_creds(old_cred);

	if (ret)
		fput(passthrough_filp);
	else
		fput(file);

	if (file->f_flags & O_NOATIME)
		return ret;

	if ((!timespec64_equal(&fuse_inode->i_mtime,
			       &passthrough_inode->i_mtime) ||
	     !timespec64_equal(&fuse_inode->i_ctime,
			       &passthrough_inode->i_ctime))) {
		fuse_inode->i_mtime = passthrough_inode->i_mtime;
		fuse_inode->i_ctime = passthrough_inode->i_ctime;
	}
	touch_atime(&file->f_path);

	return ret;
}

int fuse_passthrough_open(struct fuse_dev *fud,
			  struct fuse_passthrough_out *pto)
{
	int res;
	struct file *passthrough_filp;
	struct fuse_conn *fc = fud->fc;
	struct inode *passthrough_inode;
	struct super_block *passthrough_sb;
	struct fuse_passthrough *passthrough;

	if (!fc->passthrough)
		return -EPERM;

	/* This field is reserved for future implementation */
	if (pto->len != 0)
		return -EINVAL;

	passthrough_filp = fget(pto->fd);
	if (!passthrough_filp) {
		pr_err("FUSE: invalid file descriptor for passthrough.\n");
		return -EBADF;
	}

	if (!passthrough_filp->f_op->read_iter ||
	    !passthrough_filp->f_op->write_iter) {
		pr_err("FUSE: passthrough file misses file operations.\n");
		res = -EBADF;
		goto err_free_file;
	}

	passthrough_inode = file_inode(passthrough_filp);
	passthrough_sb = passthrough_inode->i_sb;
	if (passthrough_sb->s_stack_depth >= FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("FUSE: fs stacking depth exceeded for passthrough\n");
		res = -EINVAL;
		goto err_free_file;
	}

	passthrough = kmalloc(sizeof(struct fuse_passthrough), GFP_KERNEL);
	if (!passthrough) {
		res = -ENOMEM;
		goto err_free_file;
	}

	passthrough->filp = passthrough_filp;
	passthrough->cred = prepare_creds();

	idr_preload(GFP_KERNEL);
	spin_lock(&fc->passthrough_req_lock);
	res = idr_alloc(&fc->passthrough_req, passthrough, 1, 0, GFP_ATOMIC);
	spin_unlock(&fc->passthrough_req_lock);
	idr_preload_end();

	if (res > 0)
		return res;

	fuse_passthrough_release(passthrough);
	kfree(passthrough);

err_free_file:
	fput(passthrough_filp);

	return res;
}

int fuse_passthrough_setup(struct fuse_conn *fc, struct fuse_file *ff,
			   struct fuse_open_out *openarg)
{
	struct fuse_passthrough *passthrough;
	int passthrough_fh = openarg->passthrough_fh;

	if (!fc->passthrough)
		return -EPERM;

	/* Default case, passthrough is not requested */
	if (passthrough_fh <= 0)
		return -EINVAL;

	spin_lock(&fc->passthrough_req_lock);
	passthrough = idr_remove(&fc->passthrough_req, passthrough_fh);
	spin_unlock(&fc->passthrough_req_lock);

	if (!passthrough)
		return -EINVAL;

	ff->passthrough = *passthrough;
	kfree(passthrough);

	return 0;
}

void fuse_passthrough_release(struct fuse_passthrough *passthrough)
{
	if (passthrough->filp) {
		fput(passthrough->filp);
		passthrough->filp = NULL;
	}
	if (passthrough->cred) {
		put_cred(passthrough->cred);
		passthrough->cred = NULL;
	}
}
