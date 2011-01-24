/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/ext2_fs.h>
#include <linux/falloc.h>
#include <linux/swap.h>
#include <linux/crc32.h>
#include <linux/writeback.h>
#include <asm/uaccess.h>
#include <linux/dlm.h>
#include <linux/dlm_plock.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

/**
 * gfs2_llseek - seek to a location in a file
 * @file: the file
 * @offset: the offset
 * @origin: Where to seek from (SEEK_SET, SEEK_CUR, or SEEK_END)
 *
 * SEEK_END requires the glock for the file because it references the
 * file's size.
 *
 * Returns: The new offset, or errno
 */

static loff_t gfs2_llseek(struct file *file, loff_t offset, int origin)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_holder i_gh;
	loff_t error;

	if (origin == 2) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (!error) {
			error = generic_file_llseek_unlocked(file, offset, origin);
			gfs2_glock_dq_uninit(&i_gh);
		}
	} else
		error = generic_file_llseek_unlocked(file, offset, origin);

	return error;
}

/**
 * gfs2_readdir - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: errno
 */

static int gfs2_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *dir = file->f_mapping->host;
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	u64 offset = file->f_pos;
	int error;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
	error = gfs2_glock_nq(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		return error;
	}

	error = gfs2_dir_read(dir, &offset, dirent, filldir);

	gfs2_glock_dq_uninit(&d_gh);

	file->f_pos = offset;

	return error;
}

/**
 * fsflags_cvt
 * @table: A table of 32 u32 flags
 * @val: a 32 bit value to convert
 *
 * This function can be used to convert between fsflags values and
 * GFS2's own flags values.
 *
 * Returns: the converted flags
 */
static u32 fsflags_cvt(const u32 *table, u32 val)
{
	u32 res = 0;
	while(val) {
		if (val & 1)
			res |= *table;
		table++;
		val >>= 1;
	}
	return res;
}

static const u32 fsflags_to_gfs2[32] = {
	[3] = GFS2_DIF_SYNC,
	[4] = GFS2_DIF_IMMUTABLE,
	[5] = GFS2_DIF_APPENDONLY,
	[7] = GFS2_DIF_NOATIME,
	[12] = GFS2_DIF_EXHASH,
	[14] = GFS2_DIF_INHERIT_JDATA,
};

static const u32 gfs2_to_fsflags[32] = {
	[gfs2fl_Sync] = FS_SYNC_FL,
	[gfs2fl_Immutable] = FS_IMMUTABLE_FL,
	[gfs2fl_AppendOnly] = FS_APPEND_FL,
	[gfs2fl_NoAtime] = FS_NOATIME_FL,
	[gfs2fl_ExHash] = FS_INDEX_FL,
	[gfs2fl_InheritJdata] = FS_JOURNAL_DATA_FL,
};

static int gfs2_get_flags(struct file *filp, u32 __user *ptr)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int error;
	u32 fsflags;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	error = gfs2_glock_nq(&gh);
	if (error)
		return error;

	fsflags = fsflags_cvt(gfs2_to_fsflags, ip->i_diskflags);
	if (!S_ISDIR(inode->i_mode) && ip->i_diskflags & GFS2_DIF_JDATA)
		fsflags |= FS_JOURNAL_DATA_FL;
	if (put_user(fsflags, ptr))
		error = -EFAULT;

	gfs2_glock_dq(&gh);
	gfs2_holder_uninit(&gh);
	return error;
}

void gfs2_set_inode_flags(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned int flags = inode->i_flags;

	flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (ip->i_diskflags & GFS2_DIF_IMMUTABLE)
		flags |= S_IMMUTABLE;
	if (ip->i_diskflags & GFS2_DIF_APPENDONLY)
		flags |= S_APPEND;
	if (ip->i_diskflags & GFS2_DIF_NOATIME)
		flags |= S_NOATIME;
	if (ip->i_diskflags & GFS2_DIF_SYNC)
		flags |= S_SYNC;
	inode->i_flags = flags;
}

/* Flags that can be set by user space */
#define GFS2_FLAGS_USER_SET (GFS2_DIF_JDATA|			\
			     GFS2_DIF_IMMUTABLE|		\
			     GFS2_DIF_APPENDONLY|		\
			     GFS2_DIF_NOATIME|			\
			     GFS2_DIF_SYNC|			\
			     GFS2_DIF_SYSTEM|			\
			     GFS2_DIF_INHERIT_JDATA)

/**
 * gfs2_set_flags - set flags on an inode
 * @inode: The inode
 * @flags: The flags to set
 * @mask: Indicates which flags are valid
 *
 */
static int do_gfs2_set_flags(struct file *filp, u32 reqflags, u32 mask)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *bh;
	struct gfs2_holder gh;
	int error;
	u32 new_flags, flags;

	error = mnt_want_write(filp->f_path.mnt);
	if (error)
		return error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		goto out_drop_write;

	error = -EACCES;
	if (!is_owner_or_cap(inode))
		goto out;

	error = 0;
	flags = ip->i_diskflags;
	new_flags = (flags & ~mask) | (reqflags & mask);
	if ((new_flags ^ flags) == 0)
		goto out;

	error = -EINVAL;
	if ((new_flags ^ flags) & ~GFS2_FLAGS_USER_SET)
		goto out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) && (new_flags & GFS2_DIF_IMMUTABLE))
		goto out;
	if (IS_APPEND(inode) && (new_flags & GFS2_DIF_APPENDONLY))
		goto out;
	if (((new_flags ^ flags) & GFS2_DIF_IMMUTABLE) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		goto out;
	if (!IS_IMMUTABLE(inode)) {
		error = gfs2_permission(inode, MAY_WRITE, 0);
		if (error)
			goto out;
	}
	if ((flags ^ new_flags) & GFS2_DIF_JDATA) {
		if (flags & GFS2_DIF_JDATA)
			gfs2_log_flush(sdp, ip->i_gl);
		error = filemap_fdatawrite(inode->i_mapping);
		if (error)
			goto out;
		error = filemap_fdatawait(inode->i_mapping);
		if (error)
			goto out;
	}
	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		goto out;
	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out_trans_end;
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	ip->i_diskflags = new_flags;
	gfs2_dinode_out(ip, bh->b_data);
	brelse(bh);
	gfs2_set_inode_flags(inode);
	gfs2_set_aops(inode);
out_trans_end:
	gfs2_trans_end(sdp);
out:
	gfs2_glock_dq_uninit(&gh);
out_drop_write:
	mnt_drop_write(filp->f_path.mnt);
	return error;
}

static int gfs2_set_flags(struct file *filp, u32 __user *ptr)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	u32 fsflags, gfsflags;

	if (get_user(fsflags, ptr))
		return -EFAULT;

	gfsflags = fsflags_cvt(fsflags_to_gfs2, fsflags);
	if (!S_ISDIR(inode->i_mode)) {
		if (gfsflags & GFS2_DIF_INHERIT_JDATA)
			gfsflags ^= (GFS2_DIF_JDATA | GFS2_DIF_INHERIT_JDATA);
		return do_gfs2_set_flags(filp, gfsflags, ~0);
	}
	return do_gfs2_set_flags(filp, gfsflags, ~GFS2_DIF_JDATA);
}

static long gfs2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
	case FS_IOC_GETFLAGS:
		return gfs2_get_flags(filp, (u32 __user *)arg);
	case FS_IOC_SETFLAGS:
		return gfs2_set_flags(filp, (u32 __user *)arg);
	}
	return -ENOTTY;
}

/**
 * gfs2_allocate_page_backing - Use bmap to allocate blocks
 * @page: The (locked) page to allocate backing for
 *
 * We try to allocate all the blocks required for the page in
 * one go. This might fail for various reasons, so we keep
 * trying until all the blocks to back this page are allocated.
 * If some of the blocks are already allocated, thats ok too.
 */

static int gfs2_allocate_page_backing(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head bh;
	unsigned long size = PAGE_CACHE_SIZE;
	u64 lblock = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);

	do {
		bh.b_state = 0;
		bh.b_size = size;
		gfs2_block_map(inode, lblock, &bh, 1);
		if (!buffer_mapped(&bh))
			return -EIO;
		size -= bh.b_size;
		lblock += (bh.b_size >> inode->i_blkbits);
	} while(size > 0);
	return 0;
}

/**
 * gfs2_page_mkwrite - Make a shared, mmap()ed, page writable
 * @vma: The virtual memory area
 * @page: The page which is about to become writable
 *
 * When the page becomes writable, we need to ensure that we have
 * blocks allocated on disk to back that page.
 */

static int gfs2_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	unsigned long last_index;
	u64 pos = page->index << PAGE_CACHE_SHIFT;
	unsigned int data_blocks, ind_blocks, rblocks;
	struct gfs2_holder gh;
	struct gfs2_alloc *al;
	int ret;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	ret = gfs2_glock_nq(&gh);
	if (ret)
		goto out;

	set_bit(GLF_DIRTY, &ip->i_gl->gl_flags);
	set_bit(GIF_SW_PAGED, &ip->i_flags);

	if (!gfs2_write_alloc_required(ip, pos, PAGE_CACHE_SIZE))
		goto out_unlock;
	ret = -ENOMEM;
	al = gfs2_alloc_get(ip);
	if (al == NULL)
		goto out_unlock;

	ret = gfs2_quota_lock_check(ip);
	if (ret)
		goto out_alloc_put;
	gfs2_write_calc_reserv(ip, PAGE_CACHE_SIZE, &data_blocks, &ind_blocks);
	al->al_requested = data_blocks + ind_blocks;
	ret = gfs2_inplace_reserve(ip);
	if (ret)
		goto out_quota_unlock;

	rblocks = RES_DINODE + ind_blocks;
	if (gfs2_is_jdata(ip))
		rblocks += data_blocks ? data_blocks : 1;
	if (ind_blocks || data_blocks) {
		rblocks += RES_STATFS + RES_QUOTA;
		rblocks += gfs2_rg_blocks(al);
	}
	ret = gfs2_trans_begin(sdp, rblocks, 0);
	if (ret)
		goto out_trans_fail;

	lock_page(page);
	ret = -EINVAL;
	last_index = ip->i_inode.i_size >> PAGE_CACHE_SHIFT;
	if (page->index > last_index)
		goto out_unlock_page;
	ret = 0;
	if (!PageUptodate(page) || page->mapping != ip->i_inode.i_mapping)
		goto out_unlock_page;
	if (gfs2_is_stuffed(ip)) {
		ret = gfs2_unstuff_dinode(ip, page);
		if (ret)
			goto out_unlock_page;
	}
	ret = gfs2_allocate_page_backing(page);

out_unlock_page:
	unlock_page(page);
	gfs2_trans_end(sdp);
out_trans_fail:
	gfs2_inplace_release(ip);
out_quota_unlock:
	gfs2_quota_unlock(ip);
out_alloc_put:
	gfs2_alloc_put(ip);
out_unlock:
	gfs2_glock_dq(&gh);
out:
	gfs2_holder_uninit(&gh);
	if (ret == -ENOMEM)
		ret = VM_FAULT_OOM;
	else if (ret)
		ret = VM_FAULT_SIGBUS;
	return ret;
}

static const struct vm_operations_struct gfs2_vm_ops = {
	.fault = filemap_fault,
	.page_mkwrite = gfs2_page_mkwrite,
};

/**
 * gfs2_mmap -
 * @file: The file to map
 * @vma: The VMA which described the mapping
 *
 * There is no need to get a lock here unless we should be updating
 * atime. We ignore any locking errors since the only consequence is
 * a missed atime update (which will just be deferred until later).
 *
 * Returns: 0
 */

static int gfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);

	if (!(file->f_flags & O_NOATIME)) {
		struct gfs2_holder i_gh;
		int error;

		gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
		error = gfs2_glock_nq(&i_gh);
		file_accessed(file);
		if (error == 0)
			gfs2_glock_dq_uninit(&i_gh);
	}
	vma->vm_ops = &gfs2_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR;

	return 0;
}

/**
 * gfs2_open - open a file
 * @inode: the inode to open
 * @file: the struct file for this opening
 *
 * Returns: errno
 */

static int gfs2_open(struct inode *inode, struct file *file)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder i_gh;
	struct gfs2_file *fp;
	int error;

	fp = kzalloc(sizeof(struct gfs2_file), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	mutex_init(&fp->f_fl_mutex);

	gfs2_assert_warn(GFS2_SB(inode), !file->private_data);
	file->private_data = fp;

	if (S_ISREG(ip->i_inode.i_mode)) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (error)
			goto fail;

		if (!(file->f_flags & O_LARGEFILE) &&
		    i_size_read(inode) > MAX_NON_LFS) {
			error = -EOVERFLOW;
			goto fail_gunlock;
		}

		gfs2_glock_dq_uninit(&i_gh);
	}

	return 0;

fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);
fail:
	file->private_data = NULL;
	kfree(fp);
	return error;
}

/**
 * gfs2_close - called to close a struct file
 * @inode: the inode the struct file belongs to
 * @file: the struct file being closed
 *
 * Returns: errno
 */

static int gfs2_close(struct inode *inode, struct file *file)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_file *fp;

	fp = file->private_data;
	file->private_data = NULL;

	if (gfs2_assert_warn(sdp, fp))
		return -EIO;

	kfree(fp);

	return 0;
}

/**
 * gfs2_fsync - sync the dirty data for a file (across the cluster)
 * @file: the file that points to the dentry (we ignore this)
 * @dentry: the dentry that points to the inode to sync
 *
 * The VFS will flush "normal" data for us. We only need to worry
 * about metadata here. For journaled data, we just do a log flush
 * as we can't avoid it. Otherwise we can just bale out if datasync
 * is set. For stuffed inodes we must flush the log in order to
 * ensure that all data is on disk.
 *
 * The call to write_inode_now() is there to write back metadata and
 * the inode itself. It does also try and write the data, but thats
 * (hopefully) a no-op due to the VFS having already called filemap_fdatawrite()
 * for us.
 *
 * Returns: errno
 */

static int gfs2_fsync(struct file *file, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	int sync_state = inode->i_state & (I_DIRTY_SYNC|I_DIRTY_DATASYNC);
	int ret = 0;

	if (gfs2_is_jdata(GFS2_I(inode))) {
		gfs2_log_flush(GFS2_SB(inode), GFS2_I(inode)->i_gl);
		return 0;
	}

	if (sync_state != 0) {
		if (!datasync)
			ret = write_inode_now(inode, 0);

		if (gfs2_is_stuffed(GFS2_I(inode)))
			gfs2_log_flush(GFS2_SB(inode), GFS2_I(inode)->i_gl);
	}

	return ret;
}

/**
 * gfs2_file_aio_write - Perform a write to a file
 * @iocb: The io context
 * @iov: The data to write
 * @nr_segs: Number of @iov segments
 * @pos: The file position
 *
 * We have to do a lock/unlock here to refresh the inode size for
 * O_APPEND writes, otherwise we can land up writing at the wrong
 * offset. There is still a race, but provided the app is using its
 * own file locking, this will make O_APPEND work as expected.
 *
 */

static ssize_t gfs2_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
				   unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;

	if (file->f_flags & O_APPEND) {
		struct dentry *dentry = file->f_dentry;
		struct gfs2_inode *ip = GFS2_I(dentry->d_inode);
		struct gfs2_holder gh;
		int ret;

		ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
		if (ret)
			return ret;
		gfs2_glock_dq_uninit(&gh);
	}

	return generic_file_aio_write(iocb, iov, nr_segs, pos);
}

static void empty_write_end(struct page *page, unsigned from,
			   unsigned to)
{
	struct gfs2_inode *ip = GFS2_I(page->mapping->host);

	page_zero_new_buffers(page, from, to);
	flush_dcache_page(page);
	mark_page_accessed(page);

	if (!gfs2_is_writeback(ip))
		gfs2_page_add_databufs(ip, page, from, to);

	block_commit_write(page, from, to);
}

static int write_empty_blocks(struct page *page, unsigned from, unsigned to)
{
	unsigned start, end, next;
	struct buffer_head *bh, *head;
	int error;

	if (!page_has_buffers(page)) {
		error = __block_write_begin(page, from, to - from, gfs2_block_map);
		if (unlikely(error))
			return error;

		empty_write_end(page, from, to);
		return 0;
	}

	bh = head = page_buffers(page);
	next = end = 0;
	while (next < from) {
		next += bh->b_size;
		bh = bh->b_this_page;
	}
	start = next;
	do {
		next += bh->b_size;
		if (buffer_mapped(bh)) {
			if (end) {
				error = __block_write_begin(page, start, end - start,
							    gfs2_block_map);
				if (unlikely(error))
					return error;
				empty_write_end(page, start, end);
				end = 0;
			}
			start = next;
		}
		else
			end = next;
		bh = bh->b_this_page;
	} while (next < to);

	if (end) {
		error = __block_write_begin(page, start, end - start, gfs2_block_map);
		if (unlikely(error))
			return error;
		empty_write_end(page, start, end);
	}

	return 0;
}

static int fallocate_chunk(struct inode *inode, loff_t offset, loff_t len,
			   int mode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct buffer_head *dibh;
	int error;
	u64 start = offset >> PAGE_CACHE_SHIFT;
	unsigned int start_offset = offset & ~PAGE_CACHE_MASK;
	u64 end = (offset + len - 1) >> PAGE_CACHE_SHIFT;
	pgoff_t curr;
	struct page *page;
	unsigned int end_offset = (offset + len) & ~PAGE_CACHE_MASK;
	unsigned int from, to;

	if (!end_offset)
		end_offset = PAGE_CACHE_SIZE;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (unlikely(error))
		goto out;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);

	if (gfs2_is_stuffed(ip)) {
		error = gfs2_unstuff_dinode(ip, NULL);
		if (unlikely(error))
			goto out;
	}

	curr = start;
	offset = start << PAGE_CACHE_SHIFT;
	from = start_offset;
	to = PAGE_CACHE_SIZE;
	while (curr <= end) {
		page = grab_cache_page_write_begin(inode->i_mapping, curr,
						   AOP_FLAG_NOFS);
		if (unlikely(!page)) {
			error = -ENOMEM;
			goto out;
		}

		if (curr == end)
			to = end_offset;
		error = write_empty_blocks(page, from, to);
		if (!error && offset + to > inode->i_size &&
		    !(mode & FALLOC_FL_KEEP_SIZE)) {
			i_size_write(inode, offset + to);
		}
		unlock_page(page);
		page_cache_release(page);
		if (error)
			goto out;
		curr++;
		offset += PAGE_CACHE_SIZE;
		from = 0;
	}

	gfs2_dinode_out(ip, dibh->b_data);
	mark_inode_dirty(inode);

	brelse(dibh);

out:
	return error;
}

static void calc_max_reserv(struct gfs2_inode *ip, loff_t max, loff_t *len,
			    unsigned int *data_blocks, unsigned int *ind_blocks)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	unsigned int max_blocks = ip->i_alloc->al_rgd->rd_free_clone;
	unsigned int tmp, max_data = max_blocks - 3 * (sdp->sd_max_height - 1);

	for (tmp = max_data; tmp > sdp->sd_diptrs;) {
		tmp = DIV_ROUND_UP(tmp, sdp->sd_inptrs);
		max_data -= tmp;
	}
	/* This calculation isn't the exact reverse of gfs2_write_calc_reserve,
	   so it might end up with fewer data blocks */
	if (max_data <= *data_blocks)
		return;
	*data_blocks = max_data;
	*ind_blocks = max_blocks - max_data;
	*len = ((loff_t)max_data - 3) << sdp->sd_sb.sb_bsize_shift;
	if (*len > max) {
		*len = max;
		gfs2_write_calc_reserv(ip, max, data_blocks, ind_blocks);
	}
}

static long gfs2_fallocate(struct file *file, int mode, loff_t offset,
			   loff_t len)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned int data_blocks = 0, ind_blocks = 0, rblocks;
	loff_t bytes, max_bytes;
	struct gfs2_alloc *al;
	int error;
	loff_t next = (offset + len - 1) >> sdp->sd_sb.sb_bsize_shift;
	next = (next + 1) << sdp->sd_sb.sb_bsize_shift;

	/* We only support the FALLOC_FL_KEEP_SIZE mode */
	if (mode & ~FALLOC_FL_KEEP_SIZE)
		return -EOPNOTSUPP;

	offset = (offset >> sdp->sd_sb.sb_bsize_shift) <<
		 sdp->sd_sb.sb_bsize_shift;

	len = next - offset;
	bytes = sdp->sd_max_rg_data * sdp->sd_sb.sb_bsize / 2;
	if (!bytes)
		bytes = UINT_MAX;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ip->i_gh);
	error = gfs2_glock_nq(&ip->i_gh);
	if (unlikely(error))
		goto out_uninit;

	if (!gfs2_write_alloc_required(ip, offset, len))
		goto out_unlock;

	while (len > 0) {
		if (len < bytes)
			bytes = len;
		al = gfs2_alloc_get(ip);
		if (!al) {
			error = -ENOMEM;
			goto out_unlock;
		}

		error = gfs2_quota_lock_check(ip);
		if (error)
			goto out_alloc_put;

retry:
		gfs2_write_calc_reserv(ip, bytes, &data_blocks, &ind_blocks);

		al->al_requested = data_blocks + ind_blocks;
		error = gfs2_inplace_reserve(ip);
		if (error) {
			if (error == -ENOSPC && bytes > sdp->sd_sb.sb_bsize) {
				bytes >>= 1;
				goto retry;
			}
			goto out_qunlock;
		}
		max_bytes = bytes;
		calc_max_reserv(ip, len, &max_bytes, &data_blocks, &ind_blocks);
		al->al_requested = data_blocks + ind_blocks;

		rblocks = RES_DINODE + ind_blocks + RES_STATFS + RES_QUOTA +
			  RES_RG_HDR + gfs2_rg_blocks(al);
		if (gfs2_is_jdata(ip))
			rblocks += data_blocks ? data_blocks : 1;

		error = gfs2_trans_begin(sdp, rblocks,
					 PAGE_CACHE_SIZE/sdp->sd_sb.sb_bsize);
		if (error)
			goto out_trans_fail;

		error = fallocate_chunk(inode, offset, max_bytes, mode);
		gfs2_trans_end(sdp);

		if (error)
			goto out_trans_fail;

		len -= max_bytes;
		offset += max_bytes;
		gfs2_inplace_release(ip);
		gfs2_quota_unlock(ip);
		gfs2_alloc_put(ip);
	}
	goto out_unlock;

out_trans_fail:
	gfs2_inplace_release(ip);
out_qunlock:
	gfs2_quota_unlock(ip);
out_alloc_put:
	gfs2_alloc_put(ip);
out_unlock:
	gfs2_glock_dq(&ip->i_gh);
out_uninit:
	gfs2_holder_uninit(&ip->i_gh);
	return error;
}

#ifdef CONFIG_GFS2_FS_LOCKING_DLM

/**
 * gfs2_setlease - acquire/release a file lease
 * @file: the file pointer
 * @arg: lease type
 * @fl: file lock
 *
 * We don't currently have a way to enforce a lease across the whole
 * cluster; until we do, disable leases (by just returning -EINVAL),
 * unless the administrator has requested purely local locking.
 *
 * Locking: called under lock_flocks
 *
 * Returns: errno
 */

static int gfs2_setlease(struct file *file, long arg, struct file_lock **fl)
{
	return -EINVAL;
}

/**
 * gfs2_lock - acquire/release a posix lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(file->f_mapping->host);
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;

	if (!(fl->fl_flags & FL_POSIX))
		return -ENOLCK;
	if (__mandatory_lock(&ip->i_inode) && fl->fl_type != F_UNLCK)
		return -ENOLCK;

	if (cmd == F_CANCELLK) {
		/* Hack: */
		cmd = F_SETLK;
		fl->fl_type = F_UNLCK;
	}
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;
	if (IS_GETLK(cmd))
		return dlm_posix_get(ls->ls_dlm, ip->i_no_addr, file, fl);
	else if (fl->fl_type == F_UNLCK)
		return dlm_posix_unlock(ls->ls_dlm, ip->i_no_addr, file, fl);
	else
		return dlm_posix_lock(ls->ls_dlm, ip->i_no_addr, file, cmd, fl);
}

static int do_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;
	struct gfs2_inode *ip = GFS2_I(file->f_path.dentry->d_inode);
	struct gfs2_glock *gl;
	unsigned int state;
	int flags;
	int error = 0;

	state = (fl->fl_type == F_WRLCK) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	flags = (IS_SETLKW(cmd) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE;

	mutex_lock(&fp->f_fl_mutex);

	gl = fl_gh->gh_gl;
	if (gl) {
		if (fl_gh->gh_state == state)
			goto out;
		flock_lock_file_wait(file,
				     &(struct file_lock){.fl_type = F_UNLCK});
		gfs2_glock_dq_wait(fl_gh);
		gfs2_holder_reinit(state, flags, fl_gh);
	} else {
		error = gfs2_glock_get(GFS2_SB(&ip->i_inode), ip->i_no_addr,
				       &gfs2_flock_glops, CREATE, &gl);
		if (error)
			goto out;
		gfs2_holder_init(gl, state, flags, fl_gh);
		gfs2_glock_put(gl);
	}
	error = gfs2_glock_nq(fl_gh);
	if (error) {
		gfs2_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED)
			error = -EAGAIN;
	} else {
		error = flock_lock_file_wait(file, fl);
		gfs2_assert_warn(GFS2_SB(&ip->i_inode), !error);
	}

out:
	mutex_unlock(&fp->f_fl_mutex);
	return error;
}

static void do_unflock(struct file *file, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;

	mutex_lock(&fp->f_fl_mutex);
	flock_lock_file_wait(file, fl);
	if (fl_gh->gh_gl)
		gfs2_glock_dq_uninit(fl_gh);
	mutex_unlock(&fp->f_fl_mutex);
}

/**
 * gfs2_flock - acquire/release a flock lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_flock(struct file *file, int cmd, struct file_lock *fl)
{
	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;
	if (fl->fl_type & LOCK_MAND)
		return -EOPNOTSUPP;

	if (fl->fl_type == F_UNLCK) {
		do_unflock(file, fl);
		return 0;
	} else {
		return do_flock(file, cmd, fl);
	}
}

const struct file_operations gfs2_file_fops = {
	.llseek		= gfs2_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= gfs2_file_aio_write,
	.unlocked_ioctl	= gfs2_ioctl,
	.mmap		= gfs2_mmap,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.lock		= gfs2_lock,
	.flock		= gfs2_flock,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.setlease	= gfs2_setlease,
	.fallocate	= gfs2_fallocate,
};

const struct file_operations gfs2_dir_fops = {
	.readdir	= gfs2_readdir,
	.unlocked_ioctl	= gfs2_ioctl,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.lock		= gfs2_lock,
	.flock		= gfs2_flock,
	.llseek		= default_llseek,
};

#endif /* CONFIG_GFS2_FS_LOCKING_DLM */

const struct file_operations gfs2_file_fops_nolock = {
	.llseek		= gfs2_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= gfs2_file_aio_write,
	.unlocked_ioctl	= gfs2_ioctl,
	.mmap		= gfs2_mmap,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.setlease	= generic_setlease,
	.fallocate	= gfs2_fallocate,
};

const struct file_operations gfs2_dir_fops_nolock = {
	.readdir	= gfs2_readdir,
	.unlocked_ioctl	= gfs2_ioctl,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.llseek		= default_llseek,
};

