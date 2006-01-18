/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/gfs2_ioctl.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "jdata.h"
#include "lm.h"
#include "log.h"
#include "meta_io.h"
#include "ops_file.h"
#include "ops_vm.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

/* "bad" is for NFS support */
struct filldir_bad_entry {
	char *fbe_name;
	unsigned int fbe_length;
	uint64_t fbe_offset;
	struct gfs2_inum fbe_inum;
	unsigned int fbe_type;
};

struct filldir_bad {
	struct gfs2_sbd *fdb_sbd;

	struct filldir_bad_entry *fdb_entry;
	unsigned int fdb_entry_num;
	unsigned int fdb_entry_off;

	char *fdb_name;
	unsigned int fdb_name_size;
	unsigned int fdb_name_off;
};

/* For regular, non-NFS */
struct filldir_reg {
	struct gfs2_sbd *fdr_sbd;
	int fdr_prefetch;

	filldir_t fdr_filldir;
	void *fdr_opaque;
};

typedef ssize_t(*do_rw_t) (struct file *file,
		   char __user *buf,
		   size_t size, loff_t *offset,
		   unsigned int num_gh, struct gfs2_holder *ghs);

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
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_holder i_gh;
	loff_t error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	if (origin == 2) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (!error) {
			error = remote_llseek(file, offset, origin);
			gfs2_glock_dq_uninit(&i_gh);
		}
	} else
		error = remote_llseek(file, offset, origin);

	return error;
}

static inline unsigned int vma2state(struct vm_area_struct *vma)
{
	if ((vma->vm_flags & (VM_MAYWRITE | VM_MAYSHARE)) ==
	    (VM_MAYWRITE | VM_MAYSHARE))
		return LM_ST_EXCLUSIVE;
	return LM_ST_SHARED;
}

static ssize_t walk_vm_hard(struct file *file, const char __user *buf, size_t size,
		    loff_t *offset, do_rw_t operation)
{
	struct gfs2_holder *ghs;
	unsigned int num_gh = 0;
	ssize_t count;
	struct super_block *sb = file->f_dentry->d_inode->i_sb;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start = (unsigned long)buf;
	unsigned long end = start + size;
	int dumping = (current->flags & PF_DUMPCORE);
	unsigned int x = 0;

	for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
		if (end <= vma->vm_start)
			break;
		if (vma->vm_file &&
		    vma->vm_file->f_dentry->d_inode->i_sb == sb) {
			num_gh++;
		}
	}

	ghs = kcalloc((num_gh + 1), sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!ghs) {
		if (!dumping)
			up_read(&mm->mmap_sem);
		return -ENOMEM;
	}

	for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
		if (end <= vma->vm_start)
			break;
		if (vma->vm_file) {
			struct inode *inode = vma->vm_file->f_dentry->d_inode;
			if (inode->i_sb == sb)
				gfs2_holder_init(get_v2ip(inode)->i_gl,
						 vma2state(vma), 0, &ghs[x++]);
		}
	}

	if (!dumping)
		up_read(&mm->mmap_sem);

	gfs2_assert(get_v2sdp(sb), x == num_gh);

	count = operation(file, buf, size, offset, num_gh, ghs);

	while (num_gh--)
		gfs2_holder_uninit(&ghs[num_gh]);
	kfree(ghs);

	return count;
}

/**
 * walk_vm - Walk the vmas associated with a buffer for read or write.
 *    If any of them are gfs2, pass the gfs2 inode down to the read/write
 *    worker function so that locks can be acquired in the correct order.
 * @file: The file to read/write from/to
 * @buf: The buffer to copy to/from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @operation: The read or write worker function
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t walk_vm(struct file *file, const char __user *buf, size_t size,
	       loff_t *offset, do_rw_t operation)
{
	struct gfs2_holder gh;

	if (current->mm) {
		struct super_block *sb = file->f_dentry->d_inode->i_sb;
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		unsigned long start = (unsigned long)buf;
		unsigned long end = start + size;
		int dumping = (current->flags & PF_DUMPCORE);

		if (!dumping)
			down_read(&mm->mmap_sem);

		for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
			if (end <= vma->vm_start)
				break;
			if (vma->vm_file &&
			    vma->vm_file->f_dentry->d_inode->i_sb == sb)
				goto do_locks;
		}

		if (!dumping)
			up_read(&mm->mmap_sem);
	}

	return operation(file, buf, size, offset, 0, &gh);

do_locks:
	return walk_vm_hard(file, buf, size, offset, operation);
}

static ssize_t do_jdata_read(struct file *file, char __user *buf, size_t size,
			     loff_t *offset)
{
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	ssize_t count = 0;

	if (*offset < 0)
		return -EINVAL;
	if (!access_ok(VERIFY_WRITE, buf, size))
		return -EFAULT;

	if (!(file->f_flags & O_LARGEFILE)) {
		if (*offset >= MAX_NON_LFS)
			return -EFBIG;
		if (*offset + size > MAX_NON_LFS)
			size = MAX_NON_LFS - *offset;
	}

	count = gfs2_jdata_read(ip, buf, *offset, size, gfs2_copy2user);

	if (count > 0)
		*offset += count;

	return count;
}

/**
 * do_read_direct - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @ghs: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, errno on failure
 */

static ssize_t do_read_direct(struct file *file, char __user *buf, size_t size,
			      loff_t *offset, unsigned int num_gh,
			      struct gfs2_holder *ghs)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs2_inode *ip = get_v2ip(inode);
	unsigned int state = LM_ST_DEFERRED;
	int flags = 0;
	unsigned int x;
	ssize_t count = 0;
	int error;

	for (x = 0; x < num_gh; x++)
		if (ghs[x].gh_gl == ip->i_gl) {
			state = LM_ST_SHARED;
			flags |= GL_LOCAL_EXCL;
			break;
		}

	gfs2_holder_init(ip->i_gl, state, flags, &ghs[num_gh]);

	error = gfs2_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	error = -EINVAL;
	if (gfs2_is_jdata(ip))
		goto out_gunlock;

	if (gfs2_is_stuffed(ip)) {
		size_t mask = bdev_hardsect_size(inode->i_sb->s_bdev) - 1;

		if (((*offset) & mask) || (((unsigned long)buf) & mask))
			goto out_gunlock;

		count = do_jdata_read(file, buf, size & ~mask, offset);
	} else
		count = generic_file_read(file, buf, size, offset);

	error = 0;

 out_gunlock:
	gfs2_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs2_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * do_read_buf - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @ghs: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, errno on failure
 */

static ssize_t do_read_buf(struct file *file, char __user *buf, size_t size,
			   loff_t *offset, unsigned int num_gh,
			   struct gfs2_holder *ghs)
{
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	ssize_t count = 0;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &ghs[num_gh]);

	error = gfs2_glock_nq_m_atime(num_gh + 1, ghs);
	if (error)
		goto out;

	if (gfs2_is_jdata(ip))
		count = do_jdata_read(file, buf, size, offset);
	else
		count = generic_file_read(file, buf, size, offset);

	gfs2_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs2_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * gfs2_read - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, errno on failure
 */

static ssize_t gfs2_read(struct file *file, char __user *buf, size_t size,
			 loff_t *offset)
{
	atomic_inc(&get_v2sdp(file->f_mapping->host->i_sb)->sd_ops_file);

	if (file->f_flags & O_DIRECT)
		return walk_vm(file, buf, size, offset, do_read_direct);
	else
		return walk_vm(file, buf, size, offset, do_read_buf);
}

/**
 * grope_mapping - feel up a mapping that needs to be written
 * @buf: the start of the memory to be written
 * @size: the size of the memory to be written
 *
 * We do this after acquiring the locks on the mapping,
 * but before starting the write transaction.  We need to make
 * sure that we don't cause recursive transactions if blocks
 * need to be allocated to the file backing the mapping.
 *
 * Returns: errno
 */

static int grope_mapping(const char __user *buf, size_t size)
{
	const char __user *stop = buf + size;
	char c;

	while (buf < stop) {
		if (copy_from_user(&c, buf, 1))
			return -EFAULT;
		buf += PAGE_CACHE_SIZE;
		buf = (const char __user *)PAGE_ALIGN((unsigned long)buf);
	}

	return 0;
}

/**
 * do_write_direct_alloc - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t do_write_direct_alloc(struct file *file, const char __user *buf, size_t size,
				     loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = NULL;
	struct iovec local_iov = { .iov_base = buf, .iov_len = size };
	struct buffer_head *dibh;
	unsigned int data_blocks, ind_blocks;
	ssize_t count;
	int error;

	gfs2_write_calc_reserv(ip, size, &data_blocks, &ind_blocks);

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = gfs2_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (error)
		goto fail_gunlock_q;

	al->al_requested = data_blocks + ind_blocks;

	error = gfs2_inplace_reserve(ip);
	if (error)
		goto fail_gunlock_q;

	error = gfs2_trans_begin(sdp,
				 al->al_rgd->rd_ri.ri_length + ind_blocks +
				 RES_DINODE + RES_STATFS + RES_QUOTA, 0);
	if (error)
		goto fail_ipres;

	if ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)) {
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		ip->i_di.di_mode &= (ip->i_di.di_mode & S_IXGRP) ?
			(~(S_ISUID | S_ISGID)) : (~S_ISUID);

		gfs2_trans_add_bh(ip->i_gl, dibh);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	if (gfs2_is_stuffed(ip)) {
		error = gfs2_unstuff_dinode(ip, gfs2_unstuffer_sync, NULL);
		if (error)
			goto fail_end_trans;
	}

	count = generic_file_write_nolock(file, &local_iov, 1, offset);
	if (count < 0) {
		error = count;
		goto fail_end_trans;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto fail_end_trans;

	if (ip->i_di.di_size < inode->i_size)
		ip->i_di.di_size = inode->i_size;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	gfs2_trans_end(sdp);

	if (file->f_flags & O_SYNC)
		gfs2_log_flush_glock(ip->i_gl);

	gfs2_inplace_release(ip);
	gfs2_quota_unlock(ip);
	gfs2_alloc_put(ip);

	if (file->f_mapping->nrpages) {
		error = filemap_fdatawrite(file->f_mapping);
		if (!error)
			error = filemap_fdatawait(file->f_mapping);
	}
	if (error)
		return error;

	return count;

 fail_end_trans:
	gfs2_trans_end(sdp);

 fail_ipres:
	gfs2_inplace_release(ip);

 fail_gunlock_q:
	gfs2_quota_unlock(ip);

 fail:
	gfs2_alloc_put(ip);

	return error;
}

/**
 * do_write_direct - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @gh: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t do_write_direct(struct file *file, const char __user *buf, size_t size,
			       loff_t *offset, unsigned int num_gh,
			       struct gfs2_holder *ghs)
{
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_file *fp = get_v2fp(file);
	unsigned int state = LM_ST_DEFERRED;
	int alloc_required;
	unsigned int x;
	size_t s;
	ssize_t count = 0;
	int error;

	if (test_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags))
		state = LM_ST_EXCLUSIVE;
	else
		for (x = 0; x < num_gh; x++)
			if (ghs[x].gh_gl == ip->i_gl) {
				state = LM_ST_EXCLUSIVE;
				break;
			}

 restart:
	gfs2_holder_init(ip->i_gl, state, 0, &ghs[num_gh]);

	error = gfs2_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	error = -EINVAL;
	if (gfs2_is_jdata(ip))
		goto out_gunlock;

	if (num_gh) {
		error = grope_mapping(buf, size);
		if (error)
			goto out_gunlock;
	}

	if (file->f_flags & O_APPEND)
		*offset = ip->i_di.di_size;

	if (!(file->f_flags & O_LARGEFILE)) {
		error = -EFBIG;
		if (*offset >= MAX_NON_LFS)
			goto out_gunlock;
		if (*offset + size > MAX_NON_LFS)
			size = MAX_NON_LFS - *offset;
	}

	if (gfs2_is_stuffed(ip) ||
	    *offset + size > ip->i_di.di_size ||
	    ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)))
		alloc_required = 1;
	else {
		error = gfs2_write_alloc_required(ip, *offset, size,
						 &alloc_required);
		if (error)
			goto out_gunlock;
	}

	if (alloc_required && state != LM_ST_EXCLUSIVE) {
		gfs2_glock_dq_m(num_gh + 1, ghs);
		gfs2_holder_uninit(&ghs[num_gh]);
		state = LM_ST_EXCLUSIVE;
		goto restart;
	}

	if (alloc_required) {
		set_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags);

		/* split large writes into smaller atomic transactions */
		while (size) {
			s = gfs2_tune_get(sdp, gt_max_atomic_write);
			if (s > size)
				s = size;

			error = do_write_direct_alloc(file, buf, s, offset);
			if (error < 0)
				goto out_gunlock;

			buf += error;
			size -= error;
			count += error;
		}
	} else {
		struct iovec local_iov = { .iov_base = buf, .iov_len = size };
		struct gfs2_holder t_gh;

		clear_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags);

		error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED,
					   GL_NEVER_RECURSE, &t_gh);
		if (error)
			goto out_gunlock;

		count = generic_file_write_nolock(file, &local_iov, 1, offset);

		gfs2_glock_dq_uninit(&t_gh);
	}

	error = 0;

 out_gunlock:
	gfs2_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs2_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * do_do_write_buf - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t do_do_write_buf(struct file *file, const char __user *buf, size_t size,
			       loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = NULL;
	struct buffer_head *dibh;
	unsigned int data_blocks, ind_blocks;
	int alloc_required, journaled;
	ssize_t count;
	int error;

	journaled = gfs2_is_jdata(ip);

	gfs2_write_calc_reserv(ip, size, &data_blocks, &ind_blocks);

	error = gfs2_write_alloc_required(ip, *offset, size, &alloc_required);
	if (error)
		return error;

	if (alloc_required) {
		al = gfs2_alloc_get(ip);

		error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto fail;

		error = gfs2_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
		if (error)
			goto fail_gunlock_q;

		al->al_requested = data_blocks + ind_blocks;

		error = gfs2_inplace_reserve(ip);
		if (error)
			goto fail_gunlock_q;

		error = gfs2_trans_begin(sdp,
					 al->al_rgd->rd_ri.ri_length +
					 ind_blocks +
					 ((journaled) ? data_blocks : 0) +
					 RES_DINODE + RES_STATFS + RES_QUOTA,
					 0);
		if (error)
			goto fail_ipres;
	} else {
		error = gfs2_trans_begin(sdp,
					((journaled) ? data_blocks : 0) +
					RES_DINODE,
					0);
		if (error)
			goto fail_ipres;
	}

	if ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)) {
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		ip->i_di.di_mode &= (ip->i_di.di_mode & S_IXGRP) ?
					  (~(S_ISUID | S_ISGID)) : (~S_ISUID);

		gfs2_trans_add_bh(ip->i_gl, dibh);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	if (journaled) {
		count = gfs2_jdata_write(ip, buf, *offset, size,
					 gfs2_copy_from_user);
		if (count < 0) {
			error = count;
			goto fail_end_trans;
		}

		*offset += count;
	} else {
		struct iovec local_iov = { .iov_base = buf, .iov_len = size };

		count = generic_file_write_nolock(file, &local_iov, 1, offset);
		if (count < 0) {
			error = count;
			goto fail_end_trans;
		}

		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		if (ip->i_di.di_size < inode->i_size)
			ip->i_di.di_size = inode->i_size;
		ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

		gfs2_trans_add_bh(ip->i_gl, dibh);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);

	if (file->f_flags & O_SYNC || IS_SYNC(inode)) {
		gfs2_log_flush_glock(ip->i_gl);
		error = filemap_fdatawrite(file->f_mapping);
		if (error == 0)
			error = filemap_fdatawait(file->f_mapping);
		if (error)
			goto fail_ipres;
	}

	if (alloc_required) {
		gfs2_assert_warn(sdp, count != size ||
				 al->al_alloced);
		gfs2_inplace_release(ip);
		gfs2_quota_unlock(ip);
		gfs2_alloc_put(ip);
	}

	return count;

 fail_end_trans:
	gfs2_trans_end(sdp);

 fail_ipres:
	if (alloc_required)
		gfs2_inplace_release(ip);

 fail_gunlock_q:
	if (alloc_required)
		gfs2_quota_unlock(ip);

 fail:
	if (alloc_required)
		gfs2_alloc_put(ip);

	return error;
}

/**
 * do_write_buf - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @gh: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t do_write_buf(struct file *file, const char __user *buf, size_t size,
			    loff_t *offset, unsigned int num_gh,
			    struct gfs2_holder *ghs)
{
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	size_t s;
	ssize_t count = 0;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[num_gh]);

	error = gfs2_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	if (num_gh) {
		error = grope_mapping(buf, size);
		if (error)
			goto out_gunlock;
	}

	if (file->f_flags & O_APPEND)
		*offset = ip->i_di.di_size;

	if (!(file->f_flags & O_LARGEFILE)) {
		error = -EFBIG;
		if (*offset >= MAX_NON_LFS)
			goto out_gunlock;
		if (*offset + size > MAX_NON_LFS)
			size = MAX_NON_LFS - *offset;
	}

	/* split large writes into smaller atomic transactions */
	while (size) {
		s = gfs2_tune_get(sdp, gt_max_atomic_write);
		if (s > size)
			s = size;

		error = do_do_write_buf(file, buf, s, offset);
		if (error < 0)
			goto out_gunlock;

		buf += error;
		size -= error;
		count += error;
	}

	error = 0;

 out_gunlock:
	gfs2_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs2_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * gfs2_write - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, errno on failure
 */

static ssize_t gfs2_write(struct file *file, const char __user *buf,
			  size_t size, loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	ssize_t count;

	atomic_inc(&get_v2sdp(inode->i_sb)->sd_ops_file);

	if (*offset < 0)
		return -EINVAL;
	if (!access_ok(VERIFY_READ, buf, size))
		return -EFAULT;

	mutex_lock(&inode->i_mutex);
	if (file->f_flags & O_DIRECT)
		count = walk_vm(file, buf, size, offset,
				do_write_direct);
	else
		count = walk_vm(file, buf, size, offset, do_write_buf);
	mutex_unlock(&inode->i_mutex);

	return count;
}

/**
 * filldir_reg_func - Report a directory entry to the caller of gfs2_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int filldir_reg_func(void *opaque, const char *name, unsigned int length,
			    uint64_t offset, struct gfs2_inum *inum,
			    unsigned int type)
{
	struct filldir_reg *fdr = (struct filldir_reg *)opaque;
	struct gfs2_sbd *sdp = fdr->fdr_sbd;
	int error;

	error = fdr->fdr_filldir(fdr->fdr_opaque, name, length, offset,
				 inum->no_formal_ino, type);
	if (error)
		return 1;

	if (fdr->fdr_prefetch && !(length == 1 && *name == '.')) {
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_reg - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: errno
 */

static int readdir_reg(struct file *file, void *dirent, filldir_t filldir)
{
	struct gfs2_inode *dip = get_v2ip(file->f_mapping->host);
	struct filldir_reg fdr;
	struct gfs2_holder d_gh;
	uint64_t offset = file->f_pos;
	int error;

	fdr.fdr_sbd = dip->i_sbd;
	fdr.fdr_prefetch = 1;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs2_glock_nq_atime(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		return error;
	}

	error = gfs2_dir_read(dip, &offset, &fdr, filldir_reg_func);

	gfs2_glock_dq_uninit(&d_gh);

	file->f_pos = offset;

	return error;
}

/**
 * filldir_bad_func - Report a directory entry to the caller of gfs2_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * For supporting NFS.
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int filldir_bad_func(void *opaque, const char *name, unsigned int length,
			    uint64_t offset, struct gfs2_inum *inum,
			    unsigned int type)
{
	struct filldir_bad *fdb = (struct filldir_bad *)opaque;
	struct gfs2_sbd *sdp = fdb->fdb_sbd;
	struct filldir_bad_entry *fbe;

	if (fdb->fdb_entry_off == fdb->fdb_entry_num ||
	    fdb->fdb_name_off + length > fdb->fdb_name_size)
		return 1;

	fbe = &fdb->fdb_entry[fdb->fdb_entry_off];
	fbe->fbe_name = fdb->fdb_name + fdb->fdb_name_off;
	memcpy(fbe->fbe_name, name, length);
	fbe->fbe_length = length;
	fbe->fbe_offset = offset;
	fbe->fbe_inum = *inum;
	fbe->fbe_type = type;

	fdb->fdb_entry_off++;
	fdb->fdb_name_off += length;

	if (!(length == 1 && *name == '.')) {
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs2_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs2_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_bad - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * For supporting NFS.
 *
 * Returns: errno
 */

static int readdir_bad(struct file *file, void *dirent, filldir_t filldir)
{
	struct gfs2_inode *dip = get_v2ip(file->f_mapping->host);
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct filldir_reg fdr;
	unsigned int entries, size;
	struct filldir_bad *fdb;
	struct gfs2_holder d_gh;
	uint64_t offset = file->f_pos;
	unsigned int x;
	struct filldir_bad_entry *fbe;
	int error;

	entries = gfs2_tune_get(sdp, gt_entries_per_readdir);
	size = sizeof(struct filldir_bad) +
	    entries * (sizeof(struct filldir_bad_entry) + GFS2_FAST_NAME_SIZE);

	fdb = kzalloc(size, GFP_KERNEL);
	if (!fdb)
		return -ENOMEM;

	fdb->fdb_sbd = sdp;
	fdb->fdb_entry = (struct filldir_bad_entry *)(fdb + 1);
	fdb->fdb_entry_num = entries;
	fdb->fdb_name = ((char *)fdb) + sizeof(struct filldir_bad) +
		entries * sizeof(struct filldir_bad_entry);
	fdb->fdb_name_size = entries * GFS2_FAST_NAME_SIZE;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs2_glock_nq_atime(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		goto out;
	}

	error = gfs2_dir_read(dip, &offset, fdb, filldir_bad_func);

	gfs2_glock_dq_uninit(&d_gh);

	fdr.fdr_sbd = sdp;
	fdr.fdr_prefetch = 0;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	for (x = 0; x < fdb->fdb_entry_off; x++) {
		fbe = &fdb->fdb_entry[x];

		error = filldir_reg_func(&fdr,
					 fbe->fbe_name, fbe->fbe_length,
					 fbe->fbe_offset,
					 &fbe->fbe_inum, fbe->fbe_type);
		if (error) {
			file->f_pos = fbe->fbe_offset;
			error = 0;
			goto out;
		}
	}

	file->f_pos = offset;

 out:
	kfree(fdb);

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
	int error;

	atomic_inc(&get_v2sdp(file->f_mapping->host->i_sb)->sd_ops_file);

	if (strcmp(current->comm, "nfsd") != 0)
		error = readdir_reg(file, dirent, filldir);
	else
		error = readdir_bad(file, dirent, filldir);

	return error;
}

static int gfs2_ioctl_flags(struct gfs2_inode *ip, unsigned int cmd, unsigned long arg)
{
	unsigned int lmode = (cmd == GFS2_IOCTL_SETFLAGS) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	struct buffer_head *dibh;
	struct gfs2_holder i_gh;
	int error;
	__u32 flags = 0, change;

	if (cmd == GFS2_IOCTL_SETFLAGS) {
		error = get_user(flags, (__u32 __user *)arg);
		if (error)
			return -EFAULT;
	}

	error = gfs2_glock_nq_init(ip->i_gl, lmode, 0, &i_gh);
	if (error)
		return error;

	if (cmd == GFS2_IOCTL_SETFLAGS) {
		change = flags ^ ip->i_di.di_flags;
		error = -EPERM;
		if (change & (GFS2_DIF_IMMUTABLE|GFS2_DIF_APPENDONLY)) {
			if (!capable(CAP_LINUX_IMMUTABLE))
				goto out;
		}
		error = -EINVAL;
		if (flags & (GFS2_DIF_JDATA|GFS2_DIF_DIRECTIO)) {
			if (!S_ISREG(ip->i_di.di_mode))
				goto out;
			/* FIXME: Would be nice not to require the following test */
			if ((flags & GFS2_DIF_JDATA) && ip->i_di.di_size)
				goto out;
		}
		if (flags & (GFS2_DIF_INHERIT_JDATA|GFS2_DIF_INHERIT_DIRECTIO)) {
			if (!S_ISDIR(ip->i_di.di_mode))
				goto out;
		}

		error = gfs2_trans_begin(ip->i_sbd, RES_DINODE, 0);
		if (error)
			goto out;

		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto out_trans_end;

		ip->i_di.di_flags = flags;

		gfs2_trans_add_bh(ip->i_gl, dibh);
        	gfs2_dinode_out(&ip->i_di, dibh->b_data);

        	brelse(dibh);

out_trans_end:
		gfs2_trans_end(ip->i_sbd);
	} else {
		flags = ip->i_di.di_flags;
	}
out:
	gfs2_glock_dq_uninit(&i_gh);
	if (cmd == GFS2_IOCTL_GETFLAGS) {
		if (put_user(flags, (__u32 __user *)arg))
			return -EFAULT;
	}
	return error;
}

/**
 * gfs2_ioctl - do an ioctl on a file
 * @inode: the inode
 * @file: the file pointer
 * @cmd: the ioctl command
 * @arg: the argument
 *
 * Returns: errno
 */

static int gfs2_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		      unsigned long arg)
{
	struct gfs2_inode *ip = get_v2ip(inode);

	atomic_inc(&ip->i_sbd->sd_ops_file);

	switch (cmd) {
	case GFS2_IOCTL_SETFLAGS:
	case GFS2_IOCTL_GETFLAGS:
		return gfs2_ioctl_flags(ip, cmd, arg);

	default:
		return -ENOTTY;
	}
}

/**
 * gfs2_mmap -
 * @file: The file to map
 * @vma: The VMA which described the mapping
 *
 * Returns: 0 or error code
 */

static int gfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_holder i_gh;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs2_glock_nq_atime(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		return error;
	}

	if (gfs2_is_jdata(ip)) {
		if (vma->vm_flags & VM_MAYSHARE)
			error = -EOPNOTSUPP;
		else
			vma->vm_ops = &gfs2_vm_ops_private;
	} else {
		/* This is VM_MAYWRITE instead of VM_WRITE because a call
		   to mprotect() can turn on VM_WRITE later. */

		if ((vma->vm_flags & (VM_MAYSHARE | VM_MAYWRITE)) ==
		    (VM_MAYSHARE | VM_MAYWRITE))
			vma->vm_ops = &gfs2_vm_ops_sharewrite;
		else
			vma->vm_ops = &gfs2_vm_ops_private;
	}

	gfs2_glock_dq_uninit(&i_gh);

	return error;
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
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_holder i_gh;
	struct gfs2_file *fp;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	fp = kzalloc(sizeof(struct gfs2_file), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	init_MUTEX(&fp->f_fl_mutex);

	fp->f_inode = ip;
	fp->f_vfile = file;

	gfs2_assert_warn(ip->i_sbd, !get_v2fp(file));
	set_v2fp(file, fp);

	if (S_ISREG(ip->i_di.di_mode)) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (error)
			goto fail;

		if (!(file->f_flags & O_LARGEFILE) &&
		    ip->i_di.di_size > MAX_NON_LFS) {
			error = -EFBIG;
			goto fail_gunlock;
		}

		/* Listen to the Direct I/O flag */

		if (ip->i_di.di_flags & GFS2_DIF_DIRECTIO)
			file->f_flags |= O_DIRECT;

		/* Don't let the user open O_DIRECT on a jdata file */

		if ((file->f_flags & O_DIRECT) && gfs2_is_jdata(ip)) {
			error = -EINVAL;
			goto fail_gunlock;
		}

		gfs2_glock_dq_uninit(&i_gh);
	}

	return 0;

 fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);

 fail:
	set_v2fp(file, NULL);
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
	struct gfs2_sbd *sdp = get_v2sdp(inode->i_sb);
	struct gfs2_file *fp;

	atomic_inc(&sdp->sd_ops_file);

	fp = get_v2fp(file);
	set_v2fp(file, NULL);

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
 * Returns: errno
 */

static int gfs2_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct gfs2_inode *ip = get_v2ip(dentry->d_inode);

	atomic_inc(&ip->i_sbd->sd_ops_file);
	gfs2_log_flush_glock(ip->i_gl);

	return 0;
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
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct lm_lockname name =
		{ .ln_number = ip->i_num.no_addr,
		  .ln_type = LM_TYPE_PLOCK };

	atomic_inc(&sdp->sd_ops_file);

	if (!(fl->fl_flags & FL_POSIX))
		return -ENOLCK;
	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks) {
		if (IS_GETLK(cmd)) {
			struct file_lock *tmp;
			lock_kernel();
			tmp = posix_test_lock(file, fl);
			fl->fl_type = F_UNLCK;
			if (tmp)
				memcpy(fl, tmp, sizeof(struct file_lock));
			unlock_kernel();
			return 0;
		} else {
			int error;
			lock_kernel();
			error = posix_lock_file_wait(file, fl);
			unlock_kernel();
			return error;
		}
	}

	if (IS_GETLK(cmd))
		return gfs2_lm_plock_get(sdp, &name, file, fl);
	else if (fl->fl_type == F_UNLCK)
		return gfs2_lm_punlock(sdp, &name, file, fl);
	else
		return gfs2_lm_plock(sdp, &name, file, cmd, fl);
}

/**
 * gfs2_sendfile - Send bytes to a file or socket
 * @in_file: The file to read from
 * @out_file: The file to write to
 * @count: The amount of data
 * @offset: The beginning file offset
 *
 * Outputs: offset - updated according to number of bytes read
 *
 * Returns: The number of bytes sent, errno on failure
 */

static ssize_t gfs2_sendfile(struct file *in_file, loff_t *offset, size_t count,
			     read_actor_t actor, void *target)
{
	struct gfs2_inode *ip = get_v2ip(in_file->f_mapping->host);
	struct gfs2_holder gh;
	ssize_t retval;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);

	retval = gfs2_glock_nq_atime(&gh);
	if (retval)
		goto out;

	if (gfs2_is_jdata(ip))
		retval = -EOPNOTSUPP;
	else
		retval = generic_file_sendfile(in_file, offset, count, actor,
					       target);

	gfs2_glock_dq(&gh);

 out:
	gfs2_holder_uninit(&gh);

	return retval;
}

static int do_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_file *fp = get_v2fp(file);
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;
	struct gfs2_inode *ip = fp->f_inode;
	struct gfs2_glock *gl;
	unsigned int state;
	int flags;
	int error = 0;

	state = (fl->fl_type == F_WRLCK) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	flags = ((IS_SETLKW(cmd)) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE;

	down(&fp->f_fl_mutex);

	gl = fl_gh->gh_gl;
	if (gl) {
		if (fl_gh->gh_state == state)
			goto out;
		gfs2_glock_hold(gl);
		flock_lock_file_wait(file,
				     &(struct file_lock){.fl_type = F_UNLCK});		
		gfs2_glock_dq_uninit(fl_gh);
	} else {
		error = gfs2_glock_get(ip->i_sbd,
				      ip->i_num.no_addr, &gfs2_flock_glops,
				      CREATE, &gl);
		if (error)
			goto out;
	}

	gfs2_holder_init(gl, state, flags, fl_gh);
	gfs2_glock_put(gl);

	error = gfs2_glock_nq(fl_gh);
	if (error) {
		gfs2_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED)
			error = -EAGAIN;
	} else {
		error = flock_lock_file_wait(file, fl);
		gfs2_assert_warn(ip->i_sbd, !error);
	}

 out:
	up(&fp->f_fl_mutex);

	return error;
}

static void do_unflock(struct file *file, struct file_lock *fl)
{
	struct gfs2_file *fp = get_v2fp(file);
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;

	down(&fp->f_fl_mutex);
	flock_lock_file_wait(file, fl);
	if (fl_gh->gh_gl)
		gfs2_glock_dq_uninit(fl_gh);
	up(&fp->f_fl_mutex);
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
	struct gfs2_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;
	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks)
		return flock_lock_file_wait(file, fl);

	if (fl->fl_type == F_UNLCK) {
		do_unflock(file, fl);
		return 0;
	} else
		return do_flock(file, cmd, fl);
}

struct file_operations gfs2_file_fops = {
	.llseek = gfs2_llseek,
	.read = gfs2_read,
	.write = gfs2_write,
	.ioctl = gfs2_ioctl,
	.mmap = gfs2_mmap,
	.open = gfs2_open,
	.release = gfs2_close,
	.fsync = gfs2_fsync,
	.lock = gfs2_lock,
	.sendfile = gfs2_sendfile,
	.flock = gfs2_flock,
};

struct file_operations gfs2_dir_fops = {
	.readdir = gfs2_readdir,
	.ioctl = gfs2_ioctl,
	.open = gfs2_open,
	.release = gfs2_close,
	.fsync = gfs2_fsync,
	.lock = gfs2_lock,
	.flock = gfs2_flock,
};

