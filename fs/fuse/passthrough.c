// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE passthrough to backing file.
 *
 * Copyright (c) 2023 CTERA Networks.
 */

#include "fuse_i.h"

#include <linux/file.h>
#include <linux/backing-file.h>
#include <linux/splice.h>

static inline struct file *fuse_file_passthrough(struct fuse_file *ff)
{
	return ff->passthrough;
}
static void fuse_file_accessed(struct file *file)
{
	struct inode *inode = file_inode(file);

	fuse_invalidate_atime(inode);
}

static void fuse_passthrough_end_write(struct kiocb *iocb, ssize_t ret)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	fuse_write_update_attr(inode, iocb->ki_pos, ret);
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	size_t count = iov_iter_count(iter);
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.accessed = fuse_file_accessed,
	};


	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu\n", __func__,
		 backing_file, iocb->ki_pos, count);

	if (!count)
		return 0;

	if (!backing_file)
		return fuse_ext_map_read_iter(iocb, iter);

	return backing_file_read_iter(backing_file, iter, iocb, iocb->ki_flags, &ctx);
}

ssize_t fuse_passthrough_write_iter(struct kiocb *iocb,
				    struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	size_t count = iov_iter_count(iter);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.end_write = fuse_passthrough_end_write,
	};

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu\n", __func__,
		 backing_file, iocb->ki_pos, count);

	if (!count)
		return 0;

	guard(rwsem_write)(&inode->i_rwsem);

	if (!backing_file)
		return fuse_ext_map_write_iter(iocb, iter);

	ret = backing_file_write_iter(backing_file, iter, iocb, iocb->ki_flags,
				      &ctx);

	return ret;
}

ssize_t fuse_passthrough_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_inode_info *pipe,
				     size_t len, unsigned int flags)
{
	struct fuse_file *ff = in->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.accessed = fuse_file_accessed,
	};
	struct kiocb iocb;
	ssize_t ret;

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu, flags=0x%x\n", __func__,
		 backing_file, *ppos, len, flags);

	if (!backing_file)
		return copy_splice_read(in, ppos, pipe, len, flags);

	init_sync_kiocb(&iocb, in);
	iocb.ki_pos = *ppos;
	ret = backing_file_splice_read(backing_file, &iocb, pipe, len, flags, &ctx);
	*ppos = iocb.ki_pos;

	return ret;
}

ssize_t fuse_passthrough_splice_write(struct pipe_inode_info *pipe,
				      struct file *out, loff_t *ppos,
				      size_t len, unsigned int flags)
{
	struct fuse_file *ff = out->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct inode *inode = file_inode(out);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.end_write = fuse_passthrough_end_write,
	};
	struct kiocb iocb;

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu, flags=0x%x\n", __func__,
		 backing_file, *ppos, len, flags);

	if (!backing_file)
		return iter_file_splice_write(pipe, out, ppos, len, flags);

	inode_lock(inode);
	init_sync_kiocb(&iocb, out);
	iocb.ki_pos = *ppos;
	ret = backing_file_splice_write(pipe, backing_file, &iocb, len, flags, &ctx);
	*ppos = iocb.ki_pos;
	inode_unlock(inode);

	return ret;
}

ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.accessed = fuse_file_accessed,
	};

	pr_debug("%s: backing_file=0x%p, start=%lu, end=%lu\n", __func__,
		 backing_file, vma->vm_start, vma->vm_end);

	if (!backing_file)
		return fuse_ext_map_mmap(file, vma);

	return backing_file_mmap(backing_file, vma, &ctx);
}

int fuse_passthrough_fadvise(struct file *file, loff_t offset, loff_t len, int advice)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	const struct cred *old_cred;
	int ret;

	if (!backing_file)
		return generic_fadvise(file, offset, len, advice);

	old_cred = override_creds(ff->cred);
	ret = vfs_fadvise(backing_file, offset, len, advice);
	revert_creds(old_cred);

	return ret;
}

/*
 * Setup passthrough to a backing file.
 */
int fuse_passthrough_open(struct file *file, struct fuse_backing *fb)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file;

	if (fb->type == FUSE_BACKING_EXTMAP) {
		if (fuse_backing_is_dax(fb) != !!IS_DAX(file_inode(file)))
			return fuse_EIO("dax mode mismatch");
		return 0;
	}

	if (fb->type != FUSE_BACKING_PATH)
		return fuse_EIO("invalid backing type");

	if (IS_DAX(file_inode(file)))
		return fuse_EIO("dax mode on backing file not supported");

	/* Allocate backing file per fuse file to store fuse path */
	backing_file = backing_file_open(file, file->f_flags, &fb->path, fb->cred);
	if (IS_ERR(backing_file))
		return fuse_EIO("failed to open backing file (%ld)", PTR_ERR(backing_file));

	ff->passthrough = backing_file;
	ff->cred = get_cred(fb->cred);

	pr_debug("%s: backing_id=%llu, fb=0x%p, backing_file=0x%p\n", __func__,
		 fb->backing_id, fb, ff->passthrough);

	return 0;
}

void fuse_passthrough_release(struct fuse_file *ff, struct fuse_backing *fb)
{
	struct file *backing_file = fuse_file_passthrough(ff);

	pr_debug("%s: fb=0x%p, backing_file=0x%p\n", __func__,
		 fb, backing_file);

	if (!backing_file)
		return;

	fput(backing_file);
	ff->passthrough = NULL;
	put_cred(ff->cred);
	ff->cred = NULL;
}
