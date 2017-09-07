/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.c
 *
 * File open, close, extend, truncate
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/splice.h>
#include <linux/mount.h>
#include <linux/writeback.h>
#include <linux/falloc.h>
#include <linux/quotaops.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "sysfile.h"
#include "inode.h"
#include "ioctl.h"
#include "journal.h"
#include "locks.h"
#include "mmap.h"
#include "suballoc.h"
#include "super.h"
#include "xattr.h"
#include "acl.h"
#include "quota.h"
#include "refcounttree.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

static int ocfs2_init_file_private(struct inode *inode, struct file *file)
{
	struct ocfs2_file_private *fp;

	fp = kzalloc(sizeof(struct ocfs2_file_private), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->fp_file = file;
	mutex_init(&fp->fp_mutex);
	ocfs2_file_lock_res_init(&fp->fp_flock, fp);
	file->private_data = fp;

	return 0;
}

static void ocfs2_free_file_private(struct inode *inode, struct file *file)
{
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (fp) {
		ocfs2_simple_drop_lockres(osb, &fp->fp_flock);
		ocfs2_lock_res_free(&fp->fp_flock);
		kfree(fp);
		file->private_data = NULL;
	}
}

static int ocfs2_file_open(struct inode *inode, struct file *file)
{
	int status;
	int mode = file->f_flags;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	trace_ocfs2_file_open(inode, file, file->f_path.dentry,
			      (unsigned long long)OCFS2_I(inode)->ip_blkno,
			      file->f_path.dentry->d_name.len,
			      file->f_path.dentry->d_name.name, mode);

	if (file->f_mode & FMODE_WRITE) {
		status = dquot_initialize(inode);
		if (status)
			goto leave;
	}

	spin_lock(&oi->ip_lock);

	/* Check that the inode hasn't been wiped from disk by another
	 * node. If it hasn't then we're safe as long as we hold the
	 * spin lock until our increment of open count. */
	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_DELETED) {
		spin_unlock(&oi->ip_lock);

		status = -ENOENT;
		goto leave;
	}

	if (mode & O_DIRECT)
		oi->ip_flags |= OCFS2_INODE_OPEN_DIRECT;

	oi->ip_open_count++;
	spin_unlock(&oi->ip_lock);

	status = ocfs2_init_file_private(inode, file);
	if (status) {
		/*
		 * We want to set open count back if we're failing the
		 * open.
		 */
		spin_lock(&oi->ip_lock);
		oi->ip_open_count--;
		spin_unlock(&oi->ip_lock);
	}

leave:
	return status;
}

static int ocfs2_file_release(struct inode *inode, struct file *file)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	spin_lock(&oi->ip_lock);
	if (!--oi->ip_open_count)
		oi->ip_flags &= ~OCFS2_INODE_OPEN_DIRECT;

	trace_ocfs2_file_release(inode, file, file->f_path.dentry,
				 oi->ip_blkno,
				 file->f_path.dentry->d_name.len,
				 file->f_path.dentry->d_name.name,
				 oi->ip_open_count);
	spin_unlock(&oi->ip_lock);

	ocfs2_free_file_private(inode, file);

	return 0;
}

static int ocfs2_dir_open(struct inode *inode, struct file *file)
{
	return ocfs2_init_file_private(inode, file);
}

static int ocfs2_dir_release(struct inode *inode, struct file *file)
{
	ocfs2_free_file_private(inode, file);
	return 0;
}

static int ocfs2_sync_file(struct file *file, loff_t start, loff_t end,
			   int datasync)
{
	int err = 0;
	struct inode *inode = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	journal_t *journal = osb->journal->j_journal;
	int ret;
	tid_t commit_tid;
	bool needs_barrier = false;

	trace_ocfs2_sync_file(inode, file, file->f_path.dentry,
			      OCFS2_I(inode)->ip_blkno,
			      file->f_path.dentry->d_name.len,
			      file->f_path.dentry->d_name.name,
			      (unsigned long long)datasync);

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;

	commit_tid = datasync ? oi->i_datasync_tid : oi->i_sync_tid;
	if (journal->j_flags & JBD2_BARRIER &&
	    !jbd2_trans_will_send_data_barrier(journal, commit_tid))
		needs_barrier = true;
	err = jbd2_complete_transaction(journal, commit_tid);
	if (needs_barrier) {
		ret = blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
		if (!err)
			err = ret;
	}

	if (err)
		mlog_errno(err);

	return (err < 0) ? -EIO : 0;
}

int ocfs2_should_update_atime(struct inode *inode,
			      struct vfsmount *vfsmnt)
{
	struct timespec now;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return 0;

	if ((inode->i_flags & S_NOATIME) ||
	    ((inode->i_sb->s_flags & MS_NODIRATIME) && S_ISDIR(inode->i_mode)))
		return 0;

	/*
	 * We can be called with no vfsmnt structure - NFSD will
	 * sometimes do this.
	 *
	 * Note that our action here is different than touch_atime() -
	 * if we can't tell whether this is a noatime mount, then we
	 * don't know whether to trust the value of s_atime_quantum.
	 */
	if (vfsmnt == NULL)
		return 0;

	if ((vfsmnt->mnt_flags & MNT_NOATIME) ||
	    ((vfsmnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode)))
		return 0;

	if (vfsmnt->mnt_flags & MNT_RELATIME) {
		if ((timespec_compare(&inode->i_atime, &inode->i_mtime) <= 0) ||
		    (timespec_compare(&inode->i_atime, &inode->i_ctime) <= 0))
			return 1;

		return 0;
	}

	now = current_time(inode);
	if ((now.tv_sec - inode->i_atime.tv_sec <= osb->s_atime_quantum))
		return 0;
	else
		return 1;
}

int ocfs2_update_inode_atime(struct inode *inode,
			     struct buffer_head *bh)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *) bh->b_data;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * Don't use ocfs2_mark_inode_dirty() here as we don't always
	 * have i_mutex to guard against concurrent changes to other
	 * inode fields.
	 */
	inode->i_atime = current_time(inode);
	di->i_atime = cpu_to_le64(inode->i_atime.tv_sec);
	di->i_atime_nsec = cpu_to_le32(inode->i_atime.tv_nsec);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);
	ocfs2_journal_dirty(handle, bh);

out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	return ret;
}

int ocfs2_set_inode_size(handle_t *handle,
				struct inode *inode,
				struct buffer_head *fe_bh,
				u64 new_i_size)
{
	int status;

	i_size_write(inode, new_i_size);
	inode->i_blocks = ocfs2_inode_sector_count(inode);
	inode->i_ctime = inode->i_mtime = current_time(inode);

	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	return status;
}

int ocfs2_simple_size_update(struct inode *inode,
			     struct buffer_head *di_bh,
			     u64 new_i_size)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle = NULL;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_set_inode_size(handle, inode, di_bh,
				   new_i_size);
	if (ret < 0)
		mlog_errno(ret);

	ocfs2_update_inode_fsync_trans(handle, inode, 0);
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_cow_file_pos(struct inode *inode,
			      struct buffer_head *fe_bh,
			      u64 offset)
{
	int status;
	u32 phys, cpos = offset >> OCFS2_SB(inode->i_sb)->s_clustersize_bits;
	unsigned int num_clusters = 0;
	unsigned int ext_flags = 0;

	/*
	 * If the new offset is aligned to the range of the cluster, there is
	 * no space for ocfs2_zero_range_for_truncate to fill, so no need to
	 * CoW either.
	 */
	if ((offset & (OCFS2_SB(inode->i_sb)->s_clustersize - 1)) == 0)
		return 0;

	status = ocfs2_get_clusters(inode, cpos, &phys,
				    &num_clusters, &ext_flags);
	if (status) {
		mlog_errno(status);
		goto out;
	}

	if (!(ext_flags & OCFS2_EXT_REFCOUNTED))
		goto out;

	return ocfs2_refcount_cow(inode, fe_bh, cpos, 1, cpos+1);

out:
	return status;
}

static int ocfs2_orphan_for_truncate(struct ocfs2_super *osb,
				     struct inode *inode,
				     struct buffer_head *fe_bh,
				     u64 new_i_size)
{
	int status;
	handle_t *handle;
	struct ocfs2_dinode *di;
	u64 cluster_bytes;

	/*
	 * We need to CoW the cluster contains the offset if it is reflinked
	 * since we will call ocfs2_zero_range_for_truncate later which will
	 * write "0" from offset to the end of the cluster.
	 */
	status = ocfs2_cow_file_pos(inode, fe_bh, new_i_size);
	if (status) {
		mlog_errno(status);
		return status;
	}

	/* TODO: This needs to actually orphan the inode in this
	 * transaction. */

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_journal_access_di(handle, INODE_CACHE(inode), fe_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_commit;
	}

	/*
	 * Do this before setting i_size.
	 */
	cluster_bytes = ocfs2_align_bytes_to_clusters(inode->i_sb, new_i_size);
	status = ocfs2_zero_range_for_truncate(inode, handle, new_i_size,
					       cluster_bytes);
	if (status) {
		mlog_errno(status);
		goto out_commit;
	}

	i_size_write(inode, new_i_size);
	inode->i_ctime = inode->i_mtime = current_time(inode);

	di = (struct ocfs2_dinode *) fe_bh->b_data;
	di->i_size = cpu_to_le64(new_i_size);
	di->i_ctime = di->i_mtime = cpu_to_le64(inode->i_ctime.tv_sec);
	di->i_ctime_nsec = di->i_mtime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

	ocfs2_journal_dirty(handle, fe_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	return status;
}

int ocfs2_truncate_file(struct inode *inode,
			       struct buffer_head *di_bh,
			       u64 new_i_size)
{
	int status = 0;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	/* We trust di_bh because it comes from ocfs2_inode_lock(), which
	 * already validated it */
	fe = (struct ocfs2_dinode *) di_bh->b_data;

	trace_ocfs2_truncate_file((unsigned long long)OCFS2_I(inode)->ip_blkno,
				  (unsigned long long)le64_to_cpu(fe->i_size),
				  (unsigned long long)new_i_size);

	mlog_bug_on_msg(le64_to_cpu(fe->i_size) != i_size_read(inode),
			"Inode %llu, inode i_size = %lld != di "
			"i_size = %llu, i_flags = 0x%x\n",
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			i_size_read(inode),
			(unsigned long long)le64_to_cpu(fe->i_size),
			le32_to_cpu(fe->i_flags));

	if (new_i_size > le64_to_cpu(fe->i_size)) {
		trace_ocfs2_truncate_file_error(
			(unsigned long long)le64_to_cpu(fe->i_size),
			(unsigned long long)new_i_size);
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	ocfs2_resv_discard(&osb->osb_la_resmap,
			   &OCFS2_I(inode)->ip_la_data_resv);

	/*
	 * The inode lock forced other nodes to sync and drop their
	 * pages, which (correctly) happens even if we have a truncate
	 * without allocation change - ocfs2 cluster sizes can be much
	 * greater than page size, so we have to truncate them
	 * anyway.
	 */
	unmap_mapping_range(inode->i_mapping, new_i_size + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(inode->i_mapping, new_i_size);

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		status = ocfs2_truncate_inline(inode, di_bh, new_i_size,
					       i_size_read(inode), 1);
		if (status)
			mlog_errno(status);

		goto bail_unlock_sem;
	}

	/* alright, we're going to need to do a full blown alloc size
	 * change. Orphan the inode so that recovery can complete the
	 * truncate if necessary. This does the task of marking
	 * i_size. */
	status = ocfs2_orphan_for_truncate(osb, inode, di_bh, new_i_size);
	if (status < 0) {
		mlog_errno(status);
		goto bail_unlock_sem;
	}

	status = ocfs2_commit_truncate(osb, inode, di_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail_unlock_sem;
	}

	/* TODO: orphan dir cleanup here. */
bail_unlock_sem:
	up_write(&OCFS2_I(inode)->ip_alloc_sem);

bail:
	if (!status && OCFS2_I(inode)->ip_clusters == 0)
		status = ocfs2_try_remove_refcount_tree(inode, di_bh);

	return status;
}

/*
 * extend file allocation only here.
 * we'll update all the disk stuff, and oip->alloc_size
 *
 * expect stuff to be locked, a transaction started and enough data /
 * metadata reservations in the contexts.
 *
 * Will return -EAGAIN, and a reason if a restart is needed.
 * If passed in, *reason will always be set, even in error.
 */
int ocfs2_add_inode_data(struct ocfs2_super *osb,
			 struct inode *inode,
			 u32 *logical_offset,
			 u32 clusters_to_add,
			 int mark_unwritten,
			 struct buffer_head *fe_bh,
			 handle_t *handle,
			 struct ocfs2_alloc_context *data_ac,
			 struct ocfs2_alloc_context *meta_ac,
			 enum ocfs2_alloc_restarted *reason_ret)
{
	int ret;
	struct ocfs2_extent_tree et;

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), fe_bh);
	ret = ocfs2_add_clusters_in_btree(handle, &et, logical_offset,
					  clusters_to_add, mark_unwritten,
					  data_ac, meta_ac, reason_ret);

	return ret;
}

static int __ocfs2_extend_allocation(struct inode *inode, u32 logical_start,
				     u32 clusters_to_add, int mark_unwritten)
{
	int status = 0;
	int restart_func = 0;
	int credits;
	u32 prev_clusters;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *fe = NULL;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why = RESTART_NONE;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_extent_tree et;
	int did_quota = 0;

	/*
	 * Unwritten extent only exists for file systems which
	 * support holes.
	 */
	BUG_ON(mark_unwritten && !ocfs2_sparse_alloc(osb));

	status = ocfs2_read_inode_block(inode, &bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}
	fe = (struct ocfs2_dinode *) bh->b_data;

restart_all:
	BUG_ON(le32_to_cpu(fe->i_clusters) != OCFS2_I(inode)->ip_clusters);

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), bh);
	status = ocfs2_lock_allocators(inode, &et, clusters_to_add, 0,
				       &data_ac, &meta_ac);
	if (status) {
		mlog_errno(status);
		goto leave;
	}

	credits = ocfs2_calc_extend_credits(osb->sb, &fe->id2.i_list);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

restarted_transaction:
	trace_ocfs2_extend_allocation(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		(unsigned long long)i_size_read(inode),
		le32_to_cpu(fe->i_clusters), clusters_to_add,
		why, restart_func);

	status = dquot_alloc_space_nodirty(inode,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_add));
	if (status)
		goto leave;
	did_quota = 1;

	/* reserve a write to the file entry early on - that we if we
	 * run out of credits in the allocation path, we can still
	 * update i_size. */
	status = ocfs2_journal_access_di(handle, INODE_CACHE(inode), bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	prev_clusters = OCFS2_I(inode)->ip_clusters;

	status = ocfs2_add_inode_data(osb,
				      inode,
				      &logical_start,
				      clusters_to_add,
				      mark_unwritten,
				      bh,
				      handle,
				      data_ac,
				      meta_ac,
				      &why);
	if ((status < 0) && (status != -EAGAIN)) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}
	ocfs2_update_inode_fsync_trans(handle, inode, 1);
	ocfs2_journal_dirty(handle, bh);

	spin_lock(&OCFS2_I(inode)->ip_lock);
	clusters_to_add -= (OCFS2_I(inode)->ip_clusters - prev_clusters);
	spin_unlock(&OCFS2_I(inode)->ip_lock);
	/* Release unused quota reservation */
	dquot_free_space(inode,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_add));
	did_quota = 0;

	if (why != RESTART_NONE && clusters_to_add) {
		if (why == RESTART_META) {
			restart_func = 1;
			status = 0;
		} else {
			BUG_ON(why != RESTART_TRANS);

			status = ocfs2_allocate_extend_trans(handle, 1);
			if (status < 0) {
				/* handle still has to be committed at
				 * this point. */
				status = -ENOMEM;
				mlog_errno(status);
				goto leave;
			}
			goto restarted_transaction;
		}
	}

	trace_ocfs2_extend_allocation_end(OCFS2_I(inode)->ip_blkno,
	     le32_to_cpu(fe->i_clusters),
	     (unsigned long long)le64_to_cpu(fe->i_size),
	     OCFS2_I(inode)->ip_clusters,
	     (unsigned long long)i_size_read(inode));

leave:
	if (status < 0 && did_quota)
		dquot_free_space(inode,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_add));
	if (handle) {
		ocfs2_commit_trans(osb, handle);
		handle = NULL;
	}
	if (data_ac) {
		ocfs2_free_alloc_context(data_ac);
		data_ac = NULL;
	}
	if (meta_ac) {
		ocfs2_free_alloc_context(meta_ac);
		meta_ac = NULL;
	}
	if ((!status) && restart_func) {
		restart_func = 0;
		goto restart_all;
	}
	brelse(bh);
	bh = NULL;

	return status;
}

/*
 * While a write will already be ordering the data, a truncate will not.
 * Thus, we need to explicitly order the zeroed pages.
 */
static handle_t *ocfs2_zero_start_ordered_transaction(struct inode *inode,
						struct buffer_head *di_bh)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle = NULL;
	int ret = 0;

	if (!ocfs2_should_order_data(inode))
		goto out;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_jbd2_file_inode(handle, inode);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret)
		mlog_errno(ret);
	ocfs2_update_inode_fsync_trans(handle, inode, 1);

out:
	if (ret) {
		if (!IS_ERR(handle))
			ocfs2_commit_trans(osb, handle);
		handle = ERR_PTR(ret);
	}
	return handle;
}

/* Some parts of this taken from generic_cont_expand, which turned out
 * to be too fragile to do exactly what we need without us having to
 * worry about recursive locking in ->write_begin() and ->write_end(). */
static int ocfs2_write_zero_page(struct inode *inode, u64 abs_from,
				 u64 abs_to, struct buffer_head *di_bh)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	unsigned long index = abs_from >> PAGE_SHIFT;
	handle_t *handle;
	int ret = 0;
	unsigned zero_from, zero_to, block_start, block_end;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	BUG_ON(abs_from >= abs_to);
	BUG_ON(abs_to > (((u64)index + 1) << PAGE_SHIFT));
	BUG_ON(abs_from & (inode->i_blkbits - 1));

	handle = ocfs2_zero_start_ordered_transaction(inode, di_bh);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	page = find_or_create_page(mapping, index, GFP_NOFS);
	if (!page) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out_commit_trans;
	}

	/* Get the offsets within the page that we want to zero */
	zero_from = abs_from & (PAGE_SIZE - 1);
	zero_to = abs_to & (PAGE_SIZE - 1);
	if (!zero_to)
		zero_to = PAGE_SIZE;

	trace_ocfs2_write_zero_page(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)abs_from,
			(unsigned long long)abs_to,
			index, zero_from, zero_to);

	/* We know that zero_from is block aligned */
	for (block_start = zero_from; block_start < zero_to;
	     block_start = block_end) {
		block_end = block_start + i_blocksize(inode);

		/*
		 * block_start is block-aligned.  Bump it by one to force
		 * __block_write_begin and block_commit_write to zero the
		 * whole block.
		 */
		ret = __block_write_begin(page, block_start + 1, 0,
					  ocfs2_get_block);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_unlock;
		}


		/* must not update i_size! */
		ret = block_commit_write(page, block_start + 1,
					 block_start + 1);
		if (ret < 0)
			mlog_errno(ret);
		else
			ret = 0;
	}

	/*
	 * fs-writeback will release the dirty pages without page lock
	 * whose offset are over inode size, the release happens at
	 * block_write_full_page().
	 */
	i_size_write(inode, abs_to);
	inode->i_blocks = ocfs2_inode_sector_count(inode);
	di->i_size = cpu_to_le64((u64)i_size_read(inode));
	inode->i_mtime = inode->i_ctime = current_time(inode);
	di->i_mtime = di->i_ctime = cpu_to_le64(inode->i_mtime.tv_sec);
	di->i_ctime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);
	di->i_mtime_nsec = di->i_ctime_nsec;
	if (handle) {
		ocfs2_journal_dirty(handle, di_bh);
		ocfs2_update_inode_fsync_trans(handle, inode, 1);
	}

out_unlock:
	unlock_page(page);
	put_page(page);
out_commit_trans:
	if (handle)
		ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	return ret;
}

/*
 * Find the next range to zero.  We do this in terms of bytes because
 * that's what ocfs2_zero_extend() wants, and it is dealing with the
 * pagecache.  We may return multiple extents.
 *
 * zero_start and zero_end are ocfs2_zero_extend()s current idea of what
 * needs to be zeroed.  range_start and range_end return the next zeroing
 * range.  A subsequent call should pass the previous range_end as its
 * zero_start.  If range_end is 0, there's nothing to do.
 *
 * Unwritten extents are skipped over.  Refcounted extents are CoWd.
 */
static int ocfs2_zero_extend_get_range(struct inode *inode,
				       struct buffer_head *di_bh,
				       u64 zero_start, u64 zero_end,
				       u64 *range_start, u64 *range_end)
{
	int rc = 0, needs_cow = 0;
	u32 p_cpos, zero_clusters = 0;
	u32 zero_cpos =
		zero_start >> OCFS2_SB(inode->i_sb)->s_clustersize_bits;
	u32 last_cpos = ocfs2_clusters_for_bytes(inode->i_sb, zero_end);
	unsigned int num_clusters = 0;
	unsigned int ext_flags = 0;

	while (zero_cpos < last_cpos) {
		rc = ocfs2_get_clusters(inode, zero_cpos, &p_cpos,
					&num_clusters, &ext_flags);
		if (rc) {
			mlog_errno(rc);
			goto out;
		}

		if (p_cpos && !(ext_flags & OCFS2_EXT_UNWRITTEN)) {
			zero_clusters = num_clusters;
			if (ext_flags & OCFS2_EXT_REFCOUNTED)
				needs_cow = 1;
			break;
		}

		zero_cpos += num_clusters;
	}
	if (!zero_clusters) {
		*range_end = 0;
		goto out;
	}

	while ((zero_cpos + zero_clusters) < last_cpos) {
		rc = ocfs2_get_clusters(inode, zero_cpos + zero_clusters,
					&p_cpos, &num_clusters,
					&ext_flags);
		if (rc) {
			mlog_errno(rc);
			goto out;
		}

		if (!p_cpos || (ext_flags & OCFS2_EXT_UNWRITTEN))
			break;
		if (ext_flags & OCFS2_EXT_REFCOUNTED)
			needs_cow = 1;
		zero_clusters += num_clusters;
	}
	if ((zero_cpos + zero_clusters) > last_cpos)
		zero_clusters = last_cpos - zero_cpos;

	if (needs_cow) {
		rc = ocfs2_refcount_cow(inode, di_bh, zero_cpos,
					zero_clusters, UINT_MAX);
		if (rc) {
			mlog_errno(rc);
			goto out;
		}
	}

	*range_start = ocfs2_clusters_to_bytes(inode->i_sb, zero_cpos);
	*range_end = ocfs2_clusters_to_bytes(inode->i_sb,
					     zero_cpos + zero_clusters);

out:
	return rc;
}

/*
 * Zero one range returned from ocfs2_zero_extend_get_range().  The caller
 * has made sure that the entire range needs zeroing.
 */
static int ocfs2_zero_extend_range(struct inode *inode, u64 range_start,
				   u64 range_end, struct buffer_head *di_bh)
{
	int rc = 0;
	u64 next_pos;
	u64 zero_pos = range_start;

	trace_ocfs2_zero_extend_range(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)range_start,
			(unsigned long long)range_end);
	BUG_ON(range_start >= range_end);

	while (zero_pos < range_end) {
		next_pos = (zero_pos & PAGE_MASK) + PAGE_SIZE;
		if (next_pos > range_end)
			next_pos = range_end;
		rc = ocfs2_write_zero_page(inode, zero_pos, next_pos, di_bh);
		if (rc < 0) {
			mlog_errno(rc);
			break;
		}
		zero_pos = next_pos;

		/*
		 * Very large extends have the potential to lock up
		 * the cpu for extended periods of time.
		 */
		cond_resched();
	}

	return rc;
}

int ocfs2_zero_extend(struct inode *inode, struct buffer_head *di_bh,
		      loff_t zero_to_size)
{
	int ret = 0;
	u64 zero_start, range_start = 0, range_end = 0;
	struct super_block *sb = inode->i_sb;

	zero_start = ocfs2_align_bytes_to_blocks(sb, i_size_read(inode));
	trace_ocfs2_zero_extend((unsigned long long)OCFS2_I(inode)->ip_blkno,
				(unsigned long long)zero_start,
				(unsigned long long)i_size_read(inode));
	while (zero_start < zero_to_size) {
		ret = ocfs2_zero_extend_get_range(inode, di_bh, zero_start,
						  zero_to_size,
						  &range_start,
						  &range_end);
		if (ret) {
			mlog_errno(ret);
			break;
		}
		if (!range_end)
			break;
		/* Trim the ends */
		if (range_start < zero_start)
			range_start = zero_start;
		if (range_end > zero_to_size)
			range_end = zero_to_size;

		ret = ocfs2_zero_extend_range(inode, range_start,
					      range_end, di_bh);
		if (ret) {
			mlog_errno(ret);
			break;
		}
		zero_start = range_end;
	}

	return ret;
}

int ocfs2_extend_no_holes(struct inode *inode, struct buffer_head *di_bh,
			  u64 new_i_size, u64 zero_to)
{
	int ret;
	u32 clusters_to_add;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	/*
	 * Only quota files call this without a bh, and they can't be
	 * refcounted.
	 */
	BUG_ON(!di_bh && ocfs2_is_refcount_inode(inode));
	BUG_ON(!di_bh && !(oi->ip_flags & OCFS2_INODE_SYSTEM_FILE));

	clusters_to_add = ocfs2_clusters_for_bytes(inode->i_sb, new_i_size);
	if (clusters_to_add < oi->ip_clusters)
		clusters_to_add = 0;
	else
		clusters_to_add -= oi->ip_clusters;

	if (clusters_to_add) {
		ret = __ocfs2_extend_allocation(inode, oi->ip_clusters,
						clusters_to_add, 0);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Call this even if we don't add any clusters to the tree. We
	 * still need to zero the area between the old i_size and the
	 * new i_size.
	 */
	ret = ocfs2_zero_extend(inode, di_bh, zero_to);
	if (ret < 0)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_extend_file(struct inode *inode,
			     struct buffer_head *di_bh,
			     u64 new_i_size)
{
	int ret = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	BUG_ON(!di_bh);

	/* setattr sometimes calls us like this. */
	if (new_i_size == 0)
		goto out;

	if (i_size_read(inode) == new_i_size)
		goto out;
	BUG_ON(new_i_size < i_size_read(inode));

	/*
	 * The alloc sem blocks people in read/write from reading our
	 * allocation until we're done changing it. We depend on
	 * i_mutex to block other extend/truncate calls while we're
	 * here.  We even have to hold it for sparse files because there
	 * might be some tail zeroing.
	 */
	down_write(&oi->ip_alloc_sem);

	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		/*
		 * We can optimize small extends by keeping the inodes
		 * inline data.
		 */
		if (ocfs2_size_fits_inline_data(di_bh, new_i_size)) {
			up_write(&oi->ip_alloc_sem);
			goto out_update_size;
		}

		ret = ocfs2_convert_inline_data_to_extents(inode, di_bh);
		if (ret) {
			up_write(&oi->ip_alloc_sem);
			mlog_errno(ret);
			goto out;
		}
	}

	if (ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)))
		ret = ocfs2_zero_extend(inode, di_bh, new_i_size);
	else
		ret = ocfs2_extend_no_holes(inode, di_bh, new_i_size,
					    new_i_size);

	up_write(&oi->ip_alloc_sem);

	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

out_update_size:
	ret = ocfs2_simple_size_update(inode, di_bh, new_i_size);
	if (ret < 0)
		mlog_errno(ret);

out:
	return ret;
}

int ocfs2_setattr(struct dentry *dentry, struct iattr *attr)
{
	int status = 0, size_change;
	int inode_locked = 0;
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct buffer_head *bh = NULL;
	handle_t *handle = NULL;
	struct dquot *transfer_to[MAXQUOTAS] = { };
	int qtype;
	int had_lock;
	struct ocfs2_lock_holder oh;

	trace_ocfs2_setattr(inode, dentry,
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    dentry->d_name.len, dentry->d_name.name,
			    attr->ia_valid, attr->ia_mode,
			    from_kuid(&init_user_ns, attr->ia_uid),
			    from_kgid(&init_user_ns, attr->ia_gid));

	/* ensuring we don't even attempt to truncate a symlink */
	if (S_ISLNK(inode->i_mode))
		attr->ia_valid &= ~ATTR_SIZE;

#define OCFS2_VALID_ATTRS (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_SIZE \
			   | ATTR_GID | ATTR_UID | ATTR_MODE)
	if (!(attr->ia_valid & OCFS2_VALID_ATTRS))
		return 0;

	status = setattr_prepare(dentry, attr);
	if (status)
		return status;

	if (is_quota_modification(inode, attr)) {
		status = dquot_initialize(inode);
		if (status)
			return status;
	}
	size_change = S_ISREG(inode->i_mode) && attr->ia_valid & ATTR_SIZE;
	if (size_change) {
		status = ocfs2_rw_lock(inode, 1);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	had_lock = ocfs2_inode_lock_tracker(inode, &bh, 1, &oh);
	if (had_lock < 0) {
		status = had_lock;
		goto bail_unlock_rw;
	} else if (had_lock) {
		/*
		 * As far as we know, ocfs2_setattr() could only be the first
		 * VFS entry point in the call chain of recursive cluster
		 * locking issue.
		 *
		 * For instance:
		 * chmod_common()
		 *  notify_change()
		 *   ocfs2_setattr()
		 *    posix_acl_chmod()
		 *     ocfs2_iop_get_acl()
		 *
		 * But, we're not 100% sure if it's always true, because the
		 * ordering of the VFS entry points in the call chain is out
		 * of our control. So, we'd better dump the stack here to
		 * catch the other cases of recursive locking.
		 */
		mlog(ML_ERROR, "Another case of recursive locking:\n");
		dump_stack();
	}
	inode_locked = 1;

	if (size_change) {
		status = inode_newsize_ok(inode, attr->ia_size);
		if (status)
			goto bail_unlock;

		inode_dio_wait(inode);

		if (i_size_read(inode) >= attr->ia_size) {
			if (ocfs2_should_order_data(inode)) {
				status = ocfs2_begin_ordered_truncate(inode,
								      attr->ia_size);
				if (status)
					goto bail_unlock;
			}
			status = ocfs2_truncate_file(inode, bh, attr->ia_size);
		} else
			status = ocfs2_extend_file(inode, bh, attr->ia_size);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			status = -ENOSPC;
			goto bail_unlock;
		}
	}

	if ((attr->ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, inode->i_uid)) ||
	    (attr->ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, inode->i_gid))) {
		/*
		 * Gather pointers to quota structures so that allocation /
		 * freeing of quota structures happens here and not inside
		 * dquot_transfer() where we have problems with lock ordering
		 */
		if (attr->ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, inode->i_uid)
		    && OCFS2_HAS_RO_COMPAT_FEATURE(sb,
		    OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
			transfer_to[USRQUOTA] = dqget(sb, make_kqid_uid(attr->ia_uid));
			if (IS_ERR(transfer_to[USRQUOTA])) {
				status = PTR_ERR(transfer_to[USRQUOTA]);
				goto bail_unlock;
			}
		}
		if (attr->ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, inode->i_gid)
		    && OCFS2_HAS_RO_COMPAT_FEATURE(sb,
		    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
			transfer_to[GRPQUOTA] = dqget(sb, make_kqid_gid(attr->ia_gid));
			if (IS_ERR(transfer_to[GRPQUOTA])) {
				status = PTR_ERR(transfer_to[GRPQUOTA]);
				goto bail_unlock;
			}
		}
		handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS +
					   2 * ocfs2_quota_trans_credits(sb));
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_errno(status);
			goto bail_unlock;
		}
		status = __dquot_transfer(inode, transfer_to);
		if (status < 0)
			goto bail_commit;
	} else {
		handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_errno(status);
			goto bail_unlock;
		}
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	status = ocfs2_mark_inode_dirty(handle, inode, bh);
	if (status < 0)
		mlog_errno(status);

bail_commit:
	ocfs2_commit_trans(osb, handle);
bail_unlock:
	if (status && inode_locked) {
		ocfs2_inode_unlock_tracker(inode, 1, &oh, had_lock);
		inode_locked = 0;
	}
bail_unlock_rw:
	if (size_change)
		ocfs2_rw_unlock(inode, 1);
bail:

	/* Release quota pointers in case we acquired them */
	for (qtype = 0; qtype < OCFS2_MAXQUOTAS; qtype++)
		dqput(transfer_to[qtype]);

	if (!status && attr->ia_valid & ATTR_MODE) {
		status = ocfs2_acl_chmod(inode, bh);
		if (status < 0)
			mlog_errno(status);
	}
	if (inode_locked)
		ocfs2_inode_unlock_tracker(inode, 1, &oh, had_lock);

	brelse(bh);
	return status;
}

int ocfs2_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct super_block *sb = path->dentry->d_sb;
	struct ocfs2_super *osb = sb->s_fs_info;
	int err;

	err = ocfs2_inode_revalidate(path->dentry);
	if (err) {
		if (err != -ENOENT)
			mlog_errno(err);
		goto bail;
	}

	generic_fillattr(inode, stat);
	/*
	 * If there is inline data in the inode, the inode will normally not
	 * have data blocks allocated (it may have an external xattr block).
	 * Report at least one sector for such files, so tools like tar, rsync,
	 * others don't incorrectly think the file is completely sparse.
	 */
	if (unlikely(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL))
		stat->blocks += (stat->size + 511)>>9;

	/* We set the blksize from the cluster size for performance */
	stat->blksize = osb->s_clustersize;

bail:
	return err;
}

int ocfs2_permission(struct inode *inode, int mask)
{
	int ret, had_lock;
	struct ocfs2_lock_holder oh;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	had_lock = ocfs2_inode_lock_tracker(inode, NULL, 0, &oh);
	if (had_lock < 0) {
		ret = had_lock;
		goto out;
	} else if (had_lock) {
		/* See comments in ocfs2_setattr() for details.
		 * The call chain of this case could be:
		 * do_sys_open()
		 *  may_open()
		 *   inode_permission()
		 *    ocfs2_permission()
		 *     ocfs2_iop_get_acl()
		 */
		mlog(ML_ERROR, "Another case of recursive locking:\n");
		dump_stack();
	}

	ret = generic_permission(inode, mask);

	ocfs2_inode_unlock_tracker(inode, 0, &oh, had_lock);
out:
	return ret;
}

static int __ocfs2_write_remove_suid(struct inode *inode,
				     struct buffer_head *bh)
{
	int ret;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di;

	trace_ocfs2_write_remove_suid(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			inode->i_mode);

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_trans;
	}

	inode->i_mode &= ~S_ISUID;
	if ((inode->i_mode & S_ISGID) && (inode->i_mode & S_IXGRP))
		inode->i_mode &= ~S_ISGID;

	di = (struct ocfs2_dinode *) bh->b_data;
	di->i_mode = cpu_to_le16(inode->i_mode);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

	ocfs2_journal_dirty(handle, bh);

out_trans:
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_write_remove_suid(struct inode *inode)
{
	int ret;
	struct buffer_head *bh = NULL;

	ret = ocfs2_read_inode_block(inode, &bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret =  __ocfs2_write_remove_suid(inode, bh);
out:
	brelse(bh);
	return ret;
}

/*
 * Allocate enough extents to cover the region starting at byte offset
 * start for len bytes. Existing extents are skipped, any extents
 * added are marked as "unwritten".
 */
static int ocfs2_allocate_unwritten_extents(struct inode *inode,
					    u64 start, u64 len)
{
	int ret;
	u32 cpos, phys_cpos, clusters, alloc_size;
	u64 end = start + len;
	struct buffer_head *di_bh = NULL;

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_read_inode_block(inode, &di_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * Nothing to do if the requested reservation range
		 * fits within the inode.
		 */
		if (ocfs2_size_fits_inline_data(di_bh, end))
			goto out;

		ret = ocfs2_convert_inline_data_to_extents(inode, di_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * We consider both start and len to be inclusive.
	 */
	cpos = start >> OCFS2_SB(inode->i_sb)->s_clustersize_bits;
	clusters = ocfs2_clusters_for_bytes(inode->i_sb, start + len);
	clusters -= cpos;

	while (clusters) {
		ret = ocfs2_get_clusters(inode, cpos, &phys_cpos,
					 &alloc_size, NULL);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * Hole or existing extent len can be arbitrary, so
		 * cap it to our own allocation request.
		 */
		if (alloc_size > clusters)
			alloc_size = clusters;

		if (phys_cpos) {
			/*
			 * We already have an allocation at this
			 * region so we can safely skip it.
			 */
			goto next;
		}

		ret = __ocfs2_extend_allocation(inode, cpos, alloc_size, 1);
		if (ret) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}

next:
		cpos += alloc_size;
		clusters -= alloc_size;
	}

	ret = 0;
out:

	brelse(di_bh);
	return ret;
}

/*
 * Truncate a byte range, avoiding pages within partial clusters. This
 * preserves those pages for the zeroing code to write to.
 */
static void ocfs2_truncate_cluster_pages(struct inode *inode, u64 byte_start,
					 u64 byte_len)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	loff_t start, end;
	struct address_space *mapping = inode->i_mapping;

	start = (loff_t)ocfs2_align_bytes_to_clusters(inode->i_sb, byte_start);
	end = byte_start + byte_len;
	end = end & ~(osb->s_clustersize - 1);

	if (start < end) {
		unmap_mapping_range(mapping, start, end - start, 0);
		truncate_inode_pages_range(mapping, start, end - 1);
	}
}

static int ocfs2_zero_partial_clusters(struct inode *inode,
				       u64 start, u64 len)
{
	int ret = 0;
	u64 tmpend = 0;
	u64 end = start + len;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	unsigned int csize = osb->s_clustersize;
	handle_t *handle;

	/*
	 * The "start" and "end" values are NOT necessarily part of
	 * the range whose allocation is being deleted. Rather, this
	 * is what the user passed in with the request. We must zero
	 * partial clusters here. There's no need to worry about
	 * physical allocation - the zeroing code knows to skip holes.
	 */
	trace_ocfs2_zero_partial_clusters(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		(unsigned long long)start, (unsigned long long)end);

	/*
	 * If both edges are on a cluster boundary then there's no
	 * zeroing required as the region is part of the allocation to
	 * be truncated.
	 */
	if ((start & (csize - 1)) == 0 && (end & (csize - 1)) == 0)
		goto out;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	/*
	 * If start is on a cluster boundary and end is somewhere in another
	 * cluster, we have not COWed the cluster starting at start, unless
	 * end is also within the same cluster. So, in this case, we skip this
	 * first call to ocfs2_zero_range_for_truncate() truncate and move on
	 * to the next one.
	 */
	if ((start & (csize - 1)) != 0) {
		/*
		 * We want to get the byte offset of the end of the 1st
		 * cluster.
		 */
		tmpend = (u64)osb->s_clustersize +
			(start & ~(osb->s_clustersize - 1));
		if (tmpend > end)
			tmpend = end;

		trace_ocfs2_zero_partial_clusters_range1(
			(unsigned long long)start,
			(unsigned long long)tmpend);

		ret = ocfs2_zero_range_for_truncate(inode, handle, start,
						    tmpend);
		if (ret)
			mlog_errno(ret);
	}

	if (tmpend < end) {
		/*
		 * This may make start and end equal, but the zeroing
		 * code will skip any work in that case so there's no
		 * need to catch it up here.
		 */
		start = end & ~(osb->s_clustersize - 1);

		trace_ocfs2_zero_partial_clusters_range2(
			(unsigned long long)start, (unsigned long long)end);

		ret = ocfs2_zero_range_for_truncate(inode, handle, start, end);
		if (ret)
			mlog_errno(ret);
	}
	ocfs2_update_inode_fsync_trans(handle, inode, 1);

	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_find_rec(struct ocfs2_extent_list *el, u32 pos)
{
	int i;
	struct ocfs2_extent_rec *rec = NULL;

	for (i = le16_to_cpu(el->l_next_free_rec) - 1; i >= 0; i--) {

		rec = &el->l_recs[i];

		if (le32_to_cpu(rec->e_cpos) < pos)
			break;
	}

	return i;
}

/*
 * Helper to calculate the punching pos and length in one run, we handle the
 * following three cases in order:
 *
 * - remove the entire record
 * - remove a partial record
 * - no record needs to be removed (hole-punching completed)
*/
static void ocfs2_calc_trunc_pos(struct inode *inode,
				 struct ocfs2_extent_list *el,
				 struct ocfs2_extent_rec *rec,
				 u32 trunc_start, u32 *trunc_cpos,
				 u32 *trunc_len, u32 *trunc_end,
				 u64 *blkno, int *done)
{
	int ret = 0;
	u32 coff, range;

	range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);

	if (le32_to_cpu(rec->e_cpos) >= trunc_start) {
		/*
		 * remove an entire extent record.
		 */
		*trunc_cpos = le32_to_cpu(rec->e_cpos);
		/*
		 * Skip holes if any.
		 */
		if (range < *trunc_end)
			*trunc_end = range;
		*trunc_len = *trunc_end - le32_to_cpu(rec->e_cpos);
		*blkno = le64_to_cpu(rec->e_blkno);
		*trunc_end = le32_to_cpu(rec->e_cpos);
	} else if (range > trunc_start) {
		/*
		 * remove a partial extent record, which means we're
		 * removing the last extent record.
		 */
		*trunc_cpos = trunc_start;
		/*
		 * skip hole if any.
		 */
		if (range < *trunc_end)
			*trunc_end = range;
		*trunc_len = *trunc_end - trunc_start;
		coff = trunc_start - le32_to_cpu(rec->e_cpos);
		*blkno = le64_to_cpu(rec->e_blkno) +
				ocfs2_clusters_to_blocks(inode->i_sb, coff);
		*trunc_end = trunc_start;
	} else {
		/*
		 * It may have two following possibilities:
		 *
		 * - last record has been removed
		 * - trunc_start was within a hole
		 *
		 * both two cases mean the completion of hole punching.
		 */
		ret = 1;
	}

	*done = ret;
}

int ocfs2_remove_inode_range(struct inode *inode,
			     struct buffer_head *di_bh, u64 byte_start,
			     u64 byte_len)
{
	int ret = 0, flags = 0, done = 0, i;
	u32 trunc_start, trunc_len, trunc_end, trunc_cpos, phys_cpos;
	u32 cluster_in_el;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct address_space *mapping = inode->i_mapping;
	struct ocfs2_extent_tree et;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el = NULL;
	struct ocfs2_extent_rec *rec = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	u64 blkno, refcount_loc = le64_to_cpu(di->i_refcount_loc);

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), di_bh);
	ocfs2_init_dealloc_ctxt(&dealloc);

	trace_ocfs2_remove_inode_range(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)byte_start,
			(unsigned long long)byte_len);

	if (byte_len == 0)
		return 0;

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_truncate_inline(inode, di_bh, byte_start,
					    byte_start + byte_len, 0);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		/*
		 * There's no need to get fancy with the page cache
		 * truncate of an inline-data inode. We're talking
		 * about less than a page here, which will be cached
		 * in the dinode buffer anyway.
		 */
		unmap_mapping_range(mapping, 0, 0, 0);
		truncate_inode_pages(mapping, 0);
		goto out;
	}

	/*
	 * For reflinks, we may need to CoW 2 clusters which might be
	 * partially zero'd later, if hole's start and end offset were
	 * within one cluster(means is not exactly aligned to clustersize).
	 */

	if (ocfs2_is_refcount_inode(inode)) {
		ret = ocfs2_cow_file_pos(inode, di_bh, byte_start);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_cow_file_pos(inode, di_bh, byte_start + byte_len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	trunc_start = ocfs2_clusters_for_bytes(osb->sb, byte_start);
	trunc_end = (byte_start + byte_len) >> osb->s_clustersize_bits;
	cluster_in_el = trunc_end;

	ret = ocfs2_zero_partial_clusters(inode, byte_start, byte_len);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	path = ocfs2_new_path_from_et(&et);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	while (trunc_end > trunc_start) {

		ret = ocfs2_find_path(INODE_CACHE(inode), path,
				      cluster_in_el);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		el = path_leaf_el(path);

		i = ocfs2_find_rec(el, trunc_end);
		/*
		 * Need to go to previous extent block.
		 */
		if (i < 0) {
			if (path->p_tree_depth == 0)
				break;

			ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb,
							    path,
							    &cluster_in_el);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			/*
			 * We've reached the leftmost extent block,
			 * it's safe to leave.
			 */
			if (cluster_in_el == 0)
				break;

			/*
			 * The 'pos' searched for previous extent block is
			 * always one cluster less than actual trunc_end.
			 */
			trunc_end = cluster_in_el + 1;

			ocfs2_reinit_path(path, 1);

			continue;

		} else
			rec = &el->l_recs[i];

		ocfs2_calc_trunc_pos(inode, el, rec, trunc_start, &trunc_cpos,
				     &trunc_len, &trunc_end, &blkno, &done);
		if (done)
			break;

		flags = rec->e_flags;
		phys_cpos = ocfs2_blocks_to_clusters(inode->i_sb, blkno);

		ret = ocfs2_remove_btree_range(inode, &et, trunc_cpos,
					       phys_cpos, trunc_len, flags,
					       &dealloc, refcount_loc, false);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		cluster_in_el = trunc_end;

		ocfs2_reinit_path(path, 1);
	}

	ocfs2_truncate_cluster_pages(inode, byte_start, byte_len);

out:
	ocfs2_free_path(path);
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &dealloc);

	return ret;
}

/*
 * Parts of this function taken from xfs_change_file_space()
 */
static int __ocfs2_change_file_space(struct file *file, struct inode *inode,
				     loff_t f_pos, unsigned int cmd,
				     struct ocfs2_space_resv *sr,
				     int change_size)
{
	int ret;
	s64 llen;
	loff_t size;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *di_bh = NULL;
	handle_t *handle;
	unsigned long long max_off = inode->i_sb->s_maxbytes;

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	inode_lock(inode);

	/*
	 * This prevents concurrent writes on other nodes
	 */
	ret = ocfs2_rw_lock(inode, 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_rw_unlock;
	}

	if (inode->i_flags & (S_IMMUTABLE|S_APPEND)) {
		ret = -EPERM;
		goto out_inode_unlock;
	}

	switch (sr->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		sr->l_start += f_pos;
		break;
	case 2: /*SEEK_END*/
		sr->l_start += i_size_read(inode);
		break;
	default:
		ret = -EINVAL;
		goto out_inode_unlock;
	}
	sr->l_whence = 0;

	llen = sr->l_len > 0 ? sr->l_len - 1 : sr->l_len;

	if (sr->l_start < 0
	    || sr->l_start > max_off
	    || (sr->l_start + llen) < 0
	    || (sr->l_start + llen) > max_off) {
		ret = -EINVAL;
		goto out_inode_unlock;
	}
	size = sr->l_start + sr->l_len;

	if (cmd == OCFS2_IOC_RESVSP || cmd == OCFS2_IOC_RESVSP64 ||
	    cmd == OCFS2_IOC_UNRESVSP || cmd == OCFS2_IOC_UNRESVSP64) {
		if (sr->l_len <= 0) {
			ret = -EINVAL;
			goto out_inode_unlock;
		}
	}

	if (file && should_remove_suid(file->f_path.dentry)) {
		ret = __ocfs2_write_remove_suid(inode, di_bh);
		if (ret) {
			mlog_errno(ret);
			goto out_inode_unlock;
		}
	}

	down_write(&OCFS2_I(inode)->ip_alloc_sem);
	switch (cmd) {
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
		/*
		 * This takes unsigned offsets, but the signed ones we
		 * pass have been checked against overflow above.
		 */
		ret = ocfs2_allocate_unwritten_extents(inode, sr->l_start,
						       sr->l_len);
		break;
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
		ret = ocfs2_remove_inode_range(inode, di_bh, sr->l_start,
					       sr->l_len);
		break;
	default:
		ret = -EINVAL;
	}
	up_write(&OCFS2_I(inode)->ip_alloc_sem);
	if (ret) {
		mlog_errno(ret);
		goto out_inode_unlock;
	}

	/*
	 * We update c/mtime for these changes
	 */
	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_inode_unlock;
	}

	if (change_size && i_size_read(inode) < size)
		i_size_write(inode, size);

	inode->i_ctime = inode->i_mtime = current_time(inode);
	ret = ocfs2_mark_inode_dirty(handle, inode, di_bh);
	if (ret < 0)
		mlog_errno(ret);

	if (file && (file->f_flags & O_SYNC))
		handle->h_sync = 1;

	ocfs2_commit_trans(osb, handle);

out_inode_unlock:
	brelse(di_bh);
	ocfs2_inode_unlock(inode, 1);
out_rw_unlock:
	ocfs2_rw_unlock(inode, 1);

out:
	inode_unlock(inode);
	return ret;
}

int ocfs2_change_file_space(struct file *file, unsigned int cmd,
			    struct ocfs2_space_resv *sr)
{
	struct inode *inode = file_inode(file);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int ret;

	if ((cmd == OCFS2_IOC_RESVSP || cmd == OCFS2_IOC_RESVSP64) &&
	    !ocfs2_writes_unwritten_extents(osb))
		return -ENOTTY;
	else if ((cmd == OCFS2_IOC_UNRESVSP || cmd == OCFS2_IOC_UNRESVSP64) &&
		 !ocfs2_sparse_alloc(osb))
		return -ENOTTY;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;
	ret = __ocfs2_change_file_space(file, inode, file->f_pos, cmd, sr, 0);
	mnt_drop_write_file(file);
	return ret;
}

static long ocfs2_fallocate(struct file *file, int mode, loff_t offset,
			    loff_t len)
{
	struct inode *inode = file_inode(file);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_space_resv sr;
	int change_size = 1;
	int cmd = OCFS2_IOC_RESVSP64;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;
	if (!ocfs2_writes_unwritten_extents(osb))
		return -EOPNOTSUPP;

	if (mode & FALLOC_FL_KEEP_SIZE)
		change_size = 0;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		cmd = OCFS2_IOC_UNRESVSP64;

	sr.l_whence = 0;
	sr.l_start = (s64)offset;
	sr.l_len = (s64)len;

	return __ocfs2_change_file_space(NULL, inode, offset, cmd, &sr,
					 change_size);
}

int ocfs2_check_range_for_refcount(struct inode *inode, loff_t pos,
				   size_t count)
{
	int ret = 0;
	unsigned int extent_flags;
	u32 cpos, clusters, extent_len, phys_cpos;
	struct super_block *sb = inode->i_sb;

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb)) ||
	    !ocfs2_is_refcount_inode(inode) ||
	    OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	cpos = pos >> OCFS2_SB(sb)->s_clustersize_bits;
	clusters = ocfs2_clusters_for_bytes(sb, pos + count) - cpos;

	while (clusters) {
		ret = ocfs2_get_clusters(inode, cpos, &phys_cpos, &extent_len,
					 &extent_flags);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		if (phys_cpos && (extent_flags & OCFS2_EXT_REFCOUNTED)) {
			ret = 1;
			break;
		}

		if (extent_len > clusters)
			extent_len = clusters;

		clusters -= extent_len;
		cpos += extent_len;
	}
out:
	return ret;
}

static int ocfs2_is_io_unaligned(struct inode *inode, size_t count, loff_t pos)
{
	int blockmask = inode->i_sb->s_blocksize - 1;
	loff_t final_size = pos + count;

	if ((pos & blockmask) || (final_size & blockmask))
		return 1;
	return 0;
}

static int ocfs2_prepare_inode_for_refcount(struct inode *inode,
					    struct file *file,
					    loff_t pos, size_t count,
					    int *meta_level)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	u32 cpos = pos >> OCFS2_SB(inode->i_sb)->s_clustersize_bits;
	u32 clusters =
		ocfs2_clusters_for_bytes(inode->i_sb, pos + count) - cpos;

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*meta_level = 1;

	ret = ocfs2_refcount_cow(inode, di_bh, cpos, clusters, UINT_MAX);
	if (ret)
		mlog_errno(ret);
out:
	brelse(di_bh);
	return ret;
}

static int ocfs2_prepare_inode_for_write(struct file *file,
					 loff_t pos,
					 size_t count)
{
	int ret = 0, meta_level = 0;
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = d_inode(dentry);
	loff_t end;

	/*
	 * We start with a read level meta lock and only jump to an ex
	 * if we need to make modifications here.
	 */
	for(;;) {
		ret = ocfs2_inode_lock(inode, NULL, meta_level);
		if (ret < 0) {
			meta_level = -1;
			mlog_errno(ret);
			goto out;
		}

		/* Clear suid / sgid if necessary. We do this here
		 * instead of later in the write path because
		 * remove_suid() calls ->setattr without any hint that
		 * we may have already done our cluster locking. Since
		 * ocfs2_setattr() *must* take cluster locks to
		 * proceed, this will lead us to recursively lock the
		 * inode. There's also the dinode i_size state which
		 * can be lost via setattr during extending writes (we
		 * set inode->i_size at the end of a write. */
		if (should_remove_suid(dentry)) {
			if (meta_level == 0) {
				ocfs2_inode_unlock(inode, meta_level);
				meta_level = 1;
				continue;
			}

			ret = ocfs2_write_remove_suid(inode);
			if (ret < 0) {
				mlog_errno(ret);
				goto out_unlock;
			}
		}

		end = pos + count;

		ret = ocfs2_check_range_for_refcount(inode, pos, count);
		if (ret == 1) {
			ocfs2_inode_unlock(inode, meta_level);
			meta_level = -1;

			ret = ocfs2_prepare_inode_for_refcount(inode,
							       file,
							       pos,
							       count,
							       &meta_level);
		}

		if (ret < 0) {
			mlog_errno(ret);
			goto out_unlock;
		}

		break;
	}

out_unlock:
	trace_ocfs2_prepare_inode_for_write(OCFS2_I(inode)->ip_blkno,
					    pos, count);

	if (meta_level >= 0)
		ocfs2_inode_unlock(inode, meta_level);

out:
	return ret;
}

static ssize_t ocfs2_file_write_iter(struct kiocb *iocb,
				    struct iov_iter *from)
{
	int direct_io, rw_level;
	ssize_t written = 0;
	ssize_t ret;
	size_t count = iov_iter_count(from);
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int full_coherency = !(osb->s_mount_opt &
			       OCFS2_MOUNT_COHERENCY_BUFFERED);
	void *saved_ki_complete = NULL;
	int append_write = ((iocb->ki_pos + count) >=
			i_size_read(inode) ? 1 : 0);

	trace_ocfs2_file_aio_write(inode, file, file->f_path.dentry,
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		file->f_path.dentry->d_name.len,
		file->f_path.dentry->d_name.name,
		(unsigned int)from->nr_segs);	/* GRRRRR */

	if (count == 0)
		return 0;

	direct_io = iocb->ki_flags & IOCB_DIRECT ? 1 : 0;

	inode_lock(inode);

	/*
	 * Concurrent O_DIRECT writes are allowed with
	 * mount_option "coherency=buffered".
	 * For append write, we must take rw EX.
	 */
	rw_level = (!direct_io || full_coherency || append_write);

	ret = ocfs2_rw_lock(inode, rw_level);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}

	/*
	 * O_DIRECT writes with "coherency=full" need to take EX cluster
	 * inode_lock to guarantee coherency.
	 */
	if (direct_io && full_coherency) {
		/*
		 * We need to take and drop the inode lock to force
		 * other nodes to drop their caches.  Buffered I/O
		 * already does this in write_begin().
		 */
		ret = ocfs2_inode_lock(inode, NULL, 1);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		ocfs2_inode_unlock(inode, 1);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0) {
		if (ret)
			mlog_errno(ret);
		goto out;
	}
	count = ret;

	ret = ocfs2_prepare_inode_for_write(file, iocb->ki_pos, count);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	if (direct_io && !is_sync_kiocb(iocb) &&
	    ocfs2_is_io_unaligned(inode, count, iocb->ki_pos)) {
		/*
		 * Make it a sync io if it's an unaligned aio.
		 */
		saved_ki_complete = xchg(&iocb->ki_complete, NULL);
	}

	/* communicate with ocfs2_dio_end_io */
	ocfs2_iocb_set_rw_locked(iocb, rw_level);

	written = __generic_file_write_iter(iocb, from);
	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(written == -EIOCBQUEUED && !(iocb->ki_flags & IOCB_DIRECT));

	/*
	 * deep in g_f_a_w_n()->ocfs2_direct_IO we pass in a ocfs2_dio_end_io
	 * function pointer which is called when o_direct io completes so that
	 * it can unlock our rw lock.
	 * Unfortunately there are error cases which call end_io and others
	 * that don't.  so we don't have to unlock the rw_lock if either an
	 * async dio is going to do it in the future or an end_io after an
	 * error has already done it.
	 */
	if ((written == -EIOCBQUEUED) || (!ocfs2_iocb_is_rw_locked(iocb))) {
		rw_level = -1;
	}

	if (unlikely(written <= 0))
		goto out;

	if (((file->f_flags & O_DSYNC) && !direct_io) ||
	    IS_SYNC(inode)) {
		ret = filemap_fdatawrite_range(file->f_mapping,
					       iocb->ki_pos - written,
					       iocb->ki_pos - 1);
		if (ret < 0)
			written = ret;

		if (!ret) {
			ret = jbd2_journal_force_commit(osb->journal->j_journal);
			if (ret < 0)
				written = ret;
		}

		if (!ret)
			ret = filemap_fdatawait_range(file->f_mapping,
						      iocb->ki_pos - written,
						      iocb->ki_pos - 1);
	}

out:
	if (saved_ki_complete)
		xchg(&iocb->ki_complete, saved_ki_complete);

	if (rw_level != -1)
		ocfs2_rw_unlock(inode, rw_level);

out_mutex:
	inode_unlock(inode);

	if (written)
		ret = written;
	return ret;
}

static ssize_t ocfs2_file_read_iter(struct kiocb *iocb,
				   struct iov_iter *to)
{
	int ret = 0, rw_level = -1, lock_level = 0;
	struct file *filp = iocb->ki_filp;
	struct inode *inode = file_inode(filp);

	trace_ocfs2_file_aio_read(inode, filp, filp->f_path.dentry,
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			filp->f_path.dentry->d_name.len,
			filp->f_path.dentry->d_name.name,
			to->nr_segs);	/* GRRRRR */


	if (!inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto bail;
	}

	/*
	 * buffered reads protect themselves in ->readpage().  O_DIRECT reads
	 * need locks to protect pending reads from racing with truncate.
	 */
	if (iocb->ki_flags & IOCB_DIRECT) {
		ret = ocfs2_rw_lock(inode, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto bail;
		}
		rw_level = 0;
		/* communicate with ocfs2_dio_end_io */
		ocfs2_iocb_set_rw_locked(iocb, rw_level);
	}

	/*
	 * We're fine letting folks race truncates and extending
	 * writes with read across the cluster, just like they can
	 * locally. Hence no rw_lock during read.
	 *
	 * Take and drop the meta data lock to update inode fields
	 * like i_size. This allows the checks down below
	 * generic_file_aio_read() a chance of actually working.
	 */
	ret = ocfs2_inode_lock_atime(inode, filp->f_path.mnt, &lock_level);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail;
	}
	ocfs2_inode_unlock(inode, lock_level);

	ret = generic_file_read_iter(iocb, to);
	trace_generic_file_aio_read_ret(ret);

	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(ret == -EIOCBQUEUED && !(iocb->ki_flags & IOCB_DIRECT));

	/* see ocfs2_file_write_iter */
	if (ret == -EIOCBQUEUED || !ocfs2_iocb_is_rw_locked(iocb)) {
		rw_level = -1;
	}

bail:
	if (rw_level != -1)
		ocfs2_rw_unlock(inode, rw_level);

	return ret;
}

/* Refer generic_file_llseek_unlocked() */
static loff_t ocfs2_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	int ret = 0;

	inode_lock(inode);

	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_END:
		/* SEEK_END requires the OCFS2 inode lock for the file
		 * because it references the file's size.
		 */
		ret = ocfs2_inode_lock(inode, NULL, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		offset += i_size_read(inode);
		ocfs2_inode_unlock(inode, 0);
		break;
	case SEEK_CUR:
		if (offset == 0) {
			offset = file->f_pos;
			goto out;
		}
		offset += file->f_pos;
		break;
	case SEEK_DATA:
	case SEEK_HOLE:
		ret = ocfs2_seek_data_hole_offset(file, &offset, whence);
		if (ret)
			goto out;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	offset = vfs_setpos(file, offset, inode->i_sb->s_maxbytes);

out:
	inode_unlock(inode);
	if (ret)
		return ret;
	return offset;
}

static int ocfs2_file_clone_range(struct file *file_in,
				  loff_t pos_in,
				  struct file *file_out,
				  loff_t pos_out,
				  u64 len)
{
	return ocfs2_reflink_remap_range(file_in, pos_in, file_out, pos_out,
					 len, false);
}

static ssize_t ocfs2_file_dedupe_range(struct file *src_file,
				       u64 loff,
				       u64 len,
				       struct file *dst_file,
				       u64 dst_loff)
{
	int error;

	error = ocfs2_reflink_remap_range(src_file, loff, dst_file, dst_loff,
					  len, true);
	if (error)
		return error;
	return len;
}

const struct inode_operations ocfs2_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
	.permission	= ocfs2_permission,
	.listxattr	= ocfs2_listxattr,
	.fiemap		= ocfs2_fiemap,
	.get_acl	= ocfs2_iop_get_acl,
	.set_acl	= ocfs2_iop_set_acl,
};

const struct inode_operations ocfs2_special_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
	.permission	= ocfs2_permission,
	.get_acl	= ocfs2_iop_get_acl,
	.set_acl	= ocfs2_iop_set_acl,
};

/*
 * Other than ->lock, keep ocfs2_fops and ocfs2_dops in sync with
 * ocfs2_fops_no_plocks and ocfs2_dops_no_plocks!
 */
const struct file_operations ocfs2_fops = {
	.llseek		= ocfs2_file_llseek,
	.mmap		= ocfs2_mmap,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_file_release,
	.open		= ocfs2_file_open,
	.read_iter	= ocfs2_file_read_iter,
	.write_iter	= ocfs2_file_write_iter,
	.unlocked_ioctl	= ocfs2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ocfs2_compat_ioctl,
#endif
	.lock		= ocfs2_lock,
	.flock		= ocfs2_flock,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ocfs2_fallocate,
	.clone_file_range = ocfs2_file_clone_range,
	.dedupe_file_range = ocfs2_file_dedupe_range,
};

const struct file_operations ocfs2_dops = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= ocfs2_readdir,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_dir_release,
	.open		= ocfs2_dir_open,
	.unlocked_ioctl	= ocfs2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ocfs2_compat_ioctl,
#endif
	.lock		= ocfs2_lock,
	.flock		= ocfs2_flock,
};

/*
 * POSIX-lockless variants of our file_operations.
 *
 * These will be used if the underlying cluster stack does not support
 * posix file locking, if the user passes the "localflocks" mount
 * option, or if we have a local-only fs.
 *
 * ocfs2_flock is in here because all stacks handle UNIX file locks,
 * so we still want it in the case of no stack support for
 * plocks. Internally, it will do the right thing when asked to ignore
 * the cluster.
 */
const struct file_operations ocfs2_fops_no_plocks = {
	.llseek		= ocfs2_file_llseek,
	.mmap		= ocfs2_mmap,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_file_release,
	.open		= ocfs2_file_open,
	.read_iter	= ocfs2_file_read_iter,
	.write_iter	= ocfs2_file_write_iter,
	.unlocked_ioctl	= ocfs2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ocfs2_compat_ioctl,
#endif
	.flock		= ocfs2_flock,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ocfs2_fallocate,
	.clone_file_range = ocfs2_file_clone_range,
	.dedupe_file_range = ocfs2_file_dedupe_range,
};

const struct file_operations ocfs2_dops_no_plocks = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= ocfs2_readdir,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_dir_release,
	.open		= ocfs2_dir_open,
	.unlocked_ioctl	= ocfs2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ocfs2_compat_ioctl,
#endif
	.flock		= ocfs2_flock,
};
