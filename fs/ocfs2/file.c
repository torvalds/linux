// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * file.c
 *
 * File open, close, extend, truncate
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
#include "ianalde.h"
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

static int ocfs2_init_file_private(struct ianalde *ianalde, struct file *file)
{
	struct ocfs2_file_private *fp;

	fp = kzalloc(sizeof(struct ocfs2_file_private), GFP_KERNEL);
	if (!fp)
		return -EANALMEM;

	fp->fp_file = file;
	mutex_init(&fp->fp_mutex);
	ocfs2_file_lock_res_init(&fp->fp_flock, fp);
	file->private_data = fp;

	return 0;
}

static void ocfs2_free_file_private(struct ianalde *ianalde, struct file *file)
{
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (fp) {
		ocfs2_simple_drop_lockres(osb, &fp->fp_flock);
		ocfs2_lock_res_free(&fp->fp_flock);
		kfree(fp);
		file->private_data = NULL;
	}
}

static int ocfs2_file_open(struct ianalde *ianalde, struct file *file)
{
	int status;
	int mode = file->f_flags;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

	trace_ocfs2_file_open(ianalde, file, file->f_path.dentry,
			      (unsigned long long)oi->ip_blkanal,
			      file->f_path.dentry->d_name.len,
			      file->f_path.dentry->d_name.name, mode);

	if (file->f_mode & FMODE_WRITE) {
		status = dquot_initialize(ianalde);
		if (status)
			goto leave;
	}

	spin_lock(&oi->ip_lock);

	/* Check that the ianalde hasn't been wiped from disk by aanalther
	 * analde. If it hasn't then we're safe as long as we hold the
	 * spin lock until our increment of open count. */
	if (oi->ip_flags & OCFS2_IANALDE_DELETED) {
		spin_unlock(&oi->ip_lock);

		status = -EANALENT;
		goto leave;
	}

	if (mode & O_DIRECT)
		oi->ip_flags |= OCFS2_IANALDE_OPEN_DIRECT;

	oi->ip_open_count++;
	spin_unlock(&oi->ip_lock);

	status = ocfs2_init_file_private(ianalde, file);
	if (status) {
		/*
		 * We want to set open count back if we're failing the
		 * open.
		 */
		spin_lock(&oi->ip_lock);
		oi->ip_open_count--;
		spin_unlock(&oi->ip_lock);
	}

	file->f_mode |= FMODE_ANALWAIT;

leave:
	return status;
}

static int ocfs2_file_release(struct ianalde *ianalde, struct file *file)
{
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

	spin_lock(&oi->ip_lock);
	if (!--oi->ip_open_count)
		oi->ip_flags &= ~OCFS2_IANALDE_OPEN_DIRECT;

	trace_ocfs2_file_release(ianalde, file, file->f_path.dentry,
				 oi->ip_blkanal,
				 file->f_path.dentry->d_name.len,
				 file->f_path.dentry->d_name.name,
				 oi->ip_open_count);
	spin_unlock(&oi->ip_lock);

	ocfs2_free_file_private(ianalde, file);

	return 0;
}

static int ocfs2_dir_open(struct ianalde *ianalde, struct file *file)
{
	return ocfs2_init_file_private(ianalde, file);
}

static int ocfs2_dir_release(struct ianalde *ianalde, struct file *file)
{
	ocfs2_free_file_private(ianalde, file);
	return 0;
}

static int ocfs2_sync_file(struct file *file, loff_t start, loff_t end,
			   int datasync)
{
	int err = 0;
	struct ianalde *ianalde = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	journal_t *journal = osb->journal->j_journal;
	int ret;
	tid_t commit_tid;
	bool needs_barrier = false;

	trace_ocfs2_sync_file(ianalde, file, file->f_path.dentry,
			      oi->ip_blkanal,
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
		ret = blkdev_issue_flush(ianalde->i_sb->s_bdev);
		if (!err)
			err = ret;
	}

	if (err)
		mlog_erranal(err);

	return (err < 0) ? -EIO : 0;
}

int ocfs2_should_update_atime(struct ianalde *ianalde,
			      struct vfsmount *vfsmnt)
{
	struct timespec64 analw;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return 0;

	if ((ianalde->i_flags & S_ANALATIME) ||
	    ((ianalde->i_sb->s_flags & SB_ANALDIRATIME) && S_ISDIR(ianalde->i_mode)))
		return 0;

	/*
	 * We can be called with anal vfsmnt structure - NFSD will
	 * sometimes do this.
	 *
	 * Analte that our action here is different than touch_atime() -
	 * if we can't tell whether this is a analatime mount, then we
	 * don't kanalw whether to trust the value of s_atime_quantum.
	 */
	if (vfsmnt == NULL)
		return 0;

	if ((vfsmnt->mnt_flags & MNT_ANALATIME) ||
	    ((vfsmnt->mnt_flags & MNT_ANALDIRATIME) && S_ISDIR(ianalde->i_mode)))
		return 0;

	if (vfsmnt->mnt_flags & MNT_RELATIME) {
		struct timespec64 ctime = ianalde_get_ctime(ianalde);
		struct timespec64 atime = ianalde_get_atime(ianalde);
		struct timespec64 mtime = ianalde_get_mtime(ianalde);

		if ((timespec64_compare(&atime, &mtime) <= 0) ||
		    (timespec64_compare(&atime, &ctime) <= 0))
			return 1;

		return 0;
	}

	analw = current_time(ianalde);
	if ((analw.tv_sec - ianalde_get_atime_sec(ianalde) <= osb->s_atime_quantum))
		return 0;
	else
		return 1;
}

int ocfs2_update_ianalde_atime(struct ianalde *ianalde,
			     struct buffer_head *bh)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	handle_t *handle;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *) bh->b_data;

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_erranal(ret);
		goto out_commit;
	}

	/*
	 * Don't use ocfs2_mark_ianalde_dirty() here as we don't always
	 * have i_rwsem to guard against concurrent changes to other
	 * ianalde fields.
	 */
	ianalde_set_atime_to_ts(ianalde, current_time(ianalde));
	di->i_atime = cpu_to_le64(ianalde_get_atime_sec(ianalde));
	di->i_atime_nsec = cpu_to_le32(ianalde_get_atime_nsec(ianalde));
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 0);
	ocfs2_journal_dirty(handle, bh);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

int ocfs2_set_ianalde_size(handle_t *handle,
				struct ianalde *ianalde,
				struct buffer_head *fe_bh,
				u64 new_i_size)
{
	int status;

	i_size_write(ianalde, new_i_size);
	ianalde->i_blocks = ocfs2_ianalde_sector_count(ianalde);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));

	status = ocfs2_mark_ianalde_dirty(handle, ianalde, fe_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

bail:
	return status;
}

int ocfs2_simple_size_update(struct ianalde *ianalde,
			     struct buffer_head *di_bh,
			     u64 new_i_size)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	handle_t *handle = NULL;

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_set_ianalde_size(handle, ianalde, di_bh,
				   new_i_size);
	if (ret < 0)
		mlog_erranal(ret);

	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 0);
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_cow_file_pos(struct ianalde *ianalde,
			      struct buffer_head *fe_bh,
			      u64 offset)
{
	int status;
	u32 phys, cpos = offset >> OCFS2_SB(ianalde->i_sb)->s_clustersize_bits;
	unsigned int num_clusters = 0;
	unsigned int ext_flags = 0;

	/*
	 * If the new offset is aligned to the range of the cluster, there is
	 * anal space for ocfs2_zero_range_for_truncate to fill, so anal need to
	 * CoW either.
	 */
	if ((offset & (OCFS2_SB(ianalde->i_sb)->s_clustersize - 1)) == 0)
		return 0;

	status = ocfs2_get_clusters(ianalde, cpos, &phys,
				    &num_clusters, &ext_flags);
	if (status) {
		mlog_erranal(status);
		goto out;
	}

	if (!(ext_flags & OCFS2_EXT_REFCOUNTED))
		goto out;

	return ocfs2_refcount_cow(ianalde, fe_bh, cpos, 1, cpos+1);

out:
	return status;
}

static int ocfs2_orphan_for_truncate(struct ocfs2_super *osb,
				     struct ianalde *ianalde,
				     struct buffer_head *fe_bh,
				     u64 new_i_size)
{
	int status;
	handle_t *handle;
	struct ocfs2_dianalde *di;
	u64 cluster_bytes;

	/*
	 * We need to CoW the cluster contains the offset if it is reflinked
	 * since we will call ocfs2_zero_range_for_truncate later which will
	 * write "0" from offset to the end of the cluster.
	 */
	status = ocfs2_cow_file_pos(ianalde, fe_bh, new_i_size);
	if (status) {
		mlog_erranal(status);
		return status;
	}

	/* TODO: This needs to actually orphan the ianalde in this
	 * transaction. */

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_erranal(status);
		goto out;
	}

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), fe_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto out_commit;
	}

	/*
	 * Do this before setting i_size.
	 */
	cluster_bytes = ocfs2_align_bytes_to_clusters(ianalde->i_sb, new_i_size);
	status = ocfs2_zero_range_for_truncate(ianalde, handle, new_i_size,
					       cluster_bytes);
	if (status) {
		mlog_erranal(status);
		goto out_commit;
	}

	i_size_write(ianalde, new_i_size);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));

	di = (struct ocfs2_dianalde *) fe_bh->b_data;
	di->i_size = cpu_to_le64(new_i_size);
	di->i_ctime = di->i_mtime = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	di->i_ctime_nsec = di->i_mtime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 0);

	ocfs2_journal_dirty(handle, fe_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	return status;
}

int ocfs2_truncate_file(struct ianalde *ianalde,
			       struct buffer_head *di_bh,
			       u64 new_i_size)
{
	int status = 0;
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);

	/* We trust di_bh because it comes from ocfs2_ianalde_lock(), which
	 * already validated it */
	fe = (struct ocfs2_dianalde *) di_bh->b_data;

	trace_ocfs2_truncate_file((unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				  (unsigned long long)le64_to_cpu(fe->i_size),
				  (unsigned long long)new_i_size);

	mlog_bug_on_msg(le64_to_cpu(fe->i_size) != i_size_read(ianalde),
			"Ianalde %llu, ianalde i_size = %lld != di "
			"i_size = %llu, i_flags = 0x%x\n",
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			i_size_read(ianalde),
			(unsigned long long)le64_to_cpu(fe->i_size),
			le32_to_cpu(fe->i_flags));

	if (new_i_size > le64_to_cpu(fe->i_size)) {
		trace_ocfs2_truncate_file_error(
			(unsigned long long)le64_to_cpu(fe->i_size),
			(unsigned long long)new_i_size);
		status = -EINVAL;
		mlog_erranal(status);
		goto bail;
	}

	down_write(&OCFS2_I(ianalde)->ip_alloc_sem);

	ocfs2_resv_discard(&osb->osb_la_resmap,
			   &OCFS2_I(ianalde)->ip_la_data_resv);

	/*
	 * The ianalde lock forced other analdes to sync and drop their
	 * pages, which (correctly) happens even if we have a truncate
	 * without allocation change - ocfs2 cluster sizes can be much
	 * greater than page size, so we have to truncate them
	 * anyway.
	 */

	if (OCFS2_I(ianalde)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		unmap_mapping_range(ianalde->i_mapping,
				    new_i_size + PAGE_SIZE - 1, 0, 1);
		truncate_ianalde_pages(ianalde->i_mapping, new_i_size);
		status = ocfs2_truncate_inline(ianalde, di_bh, new_i_size,
					       i_size_read(ianalde), 1);
		if (status)
			mlog_erranal(status);

		goto bail_unlock_sem;
	}

	/* alright, we're going to need to do a full blown alloc size
	 * change. Orphan the ianalde so that recovery can complete the
	 * truncate if necessary. This does the task of marking
	 * i_size. */
	status = ocfs2_orphan_for_truncate(osb, ianalde, di_bh, new_i_size);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_sem;
	}

	unmap_mapping_range(ianalde->i_mapping, new_i_size + PAGE_SIZE - 1, 0, 1);
	truncate_ianalde_pages(ianalde->i_mapping, new_i_size);

	status = ocfs2_commit_truncate(osb, ianalde, di_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_sem;
	}

	/* TODO: orphan dir cleanup here. */
bail_unlock_sem:
	up_write(&OCFS2_I(ianalde)->ip_alloc_sem);

bail:
	if (!status && OCFS2_I(ianalde)->ip_clusters == 0)
		status = ocfs2_try_remove_refcount_tree(ianalde, di_bh);

	return status;
}

/*
 * extend file allocation only here.
 * we'll update all the disk stuff, and oip->alloc_size
 *
 * expect stuff to be locked, a transaction started and eanalugh data /
 * metadata reservations in the contexts.
 *
 * Will return -EAGAIN, and a reason if a restart is needed.
 * If passed in, *reason will always be set, even in error.
 */
int ocfs2_add_ianalde_data(struct ocfs2_super *osb,
			 struct ianalde *ianalde,
			 u32 *logical_offset,
			 u32 clusters_to_add,
			 int mark_unwritten,
			 struct buffer_head *fe_bh,
			 handle_t *handle,
			 struct ocfs2_alloc_context *data_ac,
			 struct ocfs2_alloc_context *meta_ac,
			 enum ocfs2_alloc_restarted *reason_ret)
{
	struct ocfs2_extent_tree et;

	ocfs2_init_dianalde_extent_tree(&et, IANALDE_CACHE(ianalde), fe_bh);
	return ocfs2_add_clusters_in_btree(handle, &et, logical_offset,
					   clusters_to_add, mark_unwritten,
					   data_ac, meta_ac, reason_ret);
}

static int ocfs2_extend_allocation(struct ianalde *ianalde, u32 logical_start,
				   u32 clusters_to_add, int mark_unwritten)
{
	int status = 0;
	int restart_func = 0;
	int credits;
	u32 prev_clusters;
	struct buffer_head *bh = NULL;
	struct ocfs2_dianalde *fe = NULL;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why = RESTART_ANALNE;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_extent_tree et;
	int did_quota = 0;

	/*
	 * Unwritten extent only exists for file systems which
	 * support holes.
	 */
	BUG_ON(mark_unwritten && !ocfs2_sparse_alloc(osb));

	status = ocfs2_read_ianalde_block(ianalde, &bh);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}
	fe = (struct ocfs2_dianalde *) bh->b_data;

restart_all:
	BUG_ON(le32_to_cpu(fe->i_clusters) != OCFS2_I(ianalde)->ip_clusters);

	ocfs2_init_dianalde_extent_tree(&et, IANALDE_CACHE(ianalde), bh);
	status = ocfs2_lock_allocators(ianalde, &et, clusters_to_add, 0,
				       &data_ac, &meta_ac);
	if (status) {
		mlog_erranal(status);
		goto leave;
	}

	credits = ocfs2_calc_extend_credits(osb->sb, &fe->id2.i_list);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto leave;
	}

restarted_transaction:
	trace_ocfs2_extend_allocation(
		(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
		(unsigned long long)i_size_read(ianalde),
		le32_to_cpu(fe->i_clusters), clusters_to_add,
		why, restart_func);

	status = dquot_alloc_space_analdirty(ianalde,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_add));
	if (status)
		goto leave;
	did_quota = 1;

	/* reserve a write to the file entry early on - that we if we
	 * run out of credits in the allocation path, we can still
	 * update i_size. */
	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	prev_clusters = OCFS2_I(ianalde)->ip_clusters;

	status = ocfs2_add_ianalde_data(osb,
				      ianalde,
				      &logical_start,
				      clusters_to_add,
				      mark_unwritten,
				      bh,
				      handle,
				      data_ac,
				      meta_ac,
				      &why);
	if ((status < 0) && (status != -EAGAIN)) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto leave;
	}
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);
	ocfs2_journal_dirty(handle, bh);

	spin_lock(&OCFS2_I(ianalde)->ip_lock);
	clusters_to_add -= (OCFS2_I(ianalde)->ip_clusters - prev_clusters);
	spin_unlock(&OCFS2_I(ianalde)->ip_lock);
	/* Release unused quota reservation */
	dquot_free_space(ianalde,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_add));
	did_quota = 0;

	if (why != RESTART_ANALNE && clusters_to_add) {
		if (why == RESTART_META) {
			restart_func = 1;
			status = 0;
		} else {
			BUG_ON(why != RESTART_TRANS);

			status = ocfs2_allocate_extend_trans(handle, 1);
			if (status < 0) {
				/* handle still has to be committed at
				 * this point. */
				status = -EANALMEM;
				mlog_erranal(status);
				goto leave;
			}
			goto restarted_transaction;
		}
	}

	trace_ocfs2_extend_allocation_end(OCFS2_I(ianalde)->ip_blkanal,
	     le32_to_cpu(fe->i_clusters),
	     (unsigned long long)le64_to_cpu(fe->i_size),
	     OCFS2_I(ianalde)->ip_clusters,
	     (unsigned long long)i_size_read(ianalde));

leave:
	if (status < 0 && did_quota)
		dquot_free_space(ianalde,
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
 * While a write will already be ordering the data, a truncate will analt.
 * Thus, we need to explicitly order the zeroed pages.
 */
static handle_t *ocfs2_zero_start_ordered_transaction(struct ianalde *ianalde,
						      struct buffer_head *di_bh,
						      loff_t start_byte,
						      loff_t length)
{
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	handle_t *handle = NULL;
	int ret = 0;

	if (!ocfs2_should_order_data(ianalde))
		goto out;

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = -EANALMEM;
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_jbd2_ianalde_add_write(handle, ianalde, start_byte, length);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret)
		mlog_erranal(ret);
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);

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
static int ocfs2_write_zero_page(struct ianalde *ianalde, u64 abs_from,
				 u64 abs_to, struct buffer_head *di_bh)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct page *page;
	unsigned long index = abs_from >> PAGE_SHIFT;
	handle_t *handle;
	int ret = 0;
	unsigned zero_from, zero_to, block_start, block_end;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)di_bh->b_data;

	BUG_ON(abs_from >= abs_to);
	BUG_ON(abs_to > (((u64)index + 1) << PAGE_SHIFT));
	BUG_ON(abs_from & (ianalde->i_blkbits - 1));

	handle = ocfs2_zero_start_ordered_transaction(ianalde, di_bh,
						      abs_from,
						      abs_to - abs_from);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	page = find_or_create_page(mapping, index, GFP_ANALFS);
	if (!page) {
		ret = -EANALMEM;
		mlog_erranal(ret);
		goto out_commit_trans;
	}

	/* Get the offsets within the page that we want to zero */
	zero_from = abs_from & (PAGE_SIZE - 1);
	zero_to = abs_to & (PAGE_SIZE - 1);
	if (!zero_to)
		zero_to = PAGE_SIZE;

	trace_ocfs2_write_zero_page(
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			(unsigned long long)abs_from,
			(unsigned long long)abs_to,
			index, zero_from, zero_to);

	/* We kanalw that zero_from is block aligned */
	for (block_start = zero_from; block_start < zero_to;
	     block_start = block_end) {
		block_end = block_start + i_blocksize(ianalde);

		/*
		 * block_start is block-aligned.  Bump it by one to force
		 * __block_write_begin and block_commit_write to zero the
		 * whole block.
		 */
		ret = __block_write_begin(page, block_start + 1, 0,
					  ocfs2_get_block);
		if (ret < 0) {
			mlog_erranal(ret);
			goto out_unlock;
		}


		/* must analt update i_size! */
		block_commit_write(page, block_start + 1, block_start + 1);
	}

	/*
	 * fs-writeback will release the dirty pages without page lock
	 * whose offset are over ianalde size, the release happens at
	 * block_write_full_folio().
	 */
	i_size_write(ianalde, abs_to);
	ianalde->i_blocks = ocfs2_ianalde_sector_count(ianalde);
	di->i_size = cpu_to_le64((u64)i_size_read(ianalde));
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	di->i_mtime = di->i_ctime = cpu_to_le64(ianalde_get_mtime_sec(ianalde));
	di->i_ctime_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ianalde));
	di->i_mtime_nsec = di->i_ctime_nsec;
	if (handle) {
		ocfs2_journal_dirty(handle, di_bh);
		ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);
	}

out_unlock:
	unlock_page(page);
	put_page(page);
out_commit_trans:
	if (handle)
		ocfs2_commit_trans(OCFS2_SB(ianalde->i_sb), handle);
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
 * zero_start.  If range_end is 0, there's analthing to do.
 *
 * Unwritten extents are skipped over.  Refcounted extents are CoWd.
 */
static int ocfs2_zero_extend_get_range(struct ianalde *ianalde,
				       struct buffer_head *di_bh,
				       u64 zero_start, u64 zero_end,
				       u64 *range_start, u64 *range_end)
{
	int rc = 0, needs_cow = 0;
	u32 p_cpos, zero_clusters = 0;
	u32 zero_cpos =
		zero_start >> OCFS2_SB(ianalde->i_sb)->s_clustersize_bits;
	u32 last_cpos = ocfs2_clusters_for_bytes(ianalde->i_sb, zero_end);
	unsigned int num_clusters = 0;
	unsigned int ext_flags = 0;

	while (zero_cpos < last_cpos) {
		rc = ocfs2_get_clusters(ianalde, zero_cpos, &p_cpos,
					&num_clusters, &ext_flags);
		if (rc) {
			mlog_erranal(rc);
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
		rc = ocfs2_get_clusters(ianalde, zero_cpos + zero_clusters,
					&p_cpos, &num_clusters,
					&ext_flags);
		if (rc) {
			mlog_erranal(rc);
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
		rc = ocfs2_refcount_cow(ianalde, di_bh, zero_cpos,
					zero_clusters, UINT_MAX);
		if (rc) {
			mlog_erranal(rc);
			goto out;
		}
	}

	*range_start = ocfs2_clusters_to_bytes(ianalde->i_sb, zero_cpos);
	*range_end = ocfs2_clusters_to_bytes(ianalde->i_sb,
					     zero_cpos + zero_clusters);

out:
	return rc;
}

/*
 * Zero one range returned from ocfs2_zero_extend_get_range().  The caller
 * has made sure that the entire range needs zeroing.
 */
static int ocfs2_zero_extend_range(struct ianalde *ianalde, u64 range_start,
				   u64 range_end, struct buffer_head *di_bh)
{
	int rc = 0;
	u64 next_pos;
	u64 zero_pos = range_start;

	trace_ocfs2_zero_extend_range(
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			(unsigned long long)range_start,
			(unsigned long long)range_end);
	BUG_ON(range_start >= range_end);

	while (zero_pos < range_end) {
		next_pos = (zero_pos & PAGE_MASK) + PAGE_SIZE;
		if (next_pos > range_end)
			next_pos = range_end;
		rc = ocfs2_write_zero_page(ianalde, zero_pos, next_pos, di_bh);
		if (rc < 0) {
			mlog_erranal(rc);
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

int ocfs2_zero_extend(struct ianalde *ianalde, struct buffer_head *di_bh,
		      loff_t zero_to_size)
{
	int ret = 0;
	u64 zero_start, range_start = 0, range_end = 0;
	struct super_block *sb = ianalde->i_sb;

	zero_start = ocfs2_align_bytes_to_blocks(sb, i_size_read(ianalde));
	trace_ocfs2_zero_extend((unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				(unsigned long long)zero_start,
				(unsigned long long)i_size_read(ianalde));
	while (zero_start < zero_to_size) {
		ret = ocfs2_zero_extend_get_range(ianalde, di_bh, zero_start,
						  zero_to_size,
						  &range_start,
						  &range_end);
		if (ret) {
			mlog_erranal(ret);
			break;
		}
		if (!range_end)
			break;
		/* Trim the ends */
		if (range_start < zero_start)
			range_start = zero_start;
		if (range_end > zero_to_size)
			range_end = zero_to_size;

		ret = ocfs2_zero_extend_range(ianalde, range_start,
					      range_end, di_bh);
		if (ret) {
			mlog_erranal(ret);
			break;
		}
		zero_start = range_end;
	}

	return ret;
}

int ocfs2_extend_anal_holes(struct ianalde *ianalde, struct buffer_head *di_bh,
			  u64 new_i_size, u64 zero_to)
{
	int ret;
	u32 clusters_to_add;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

	/*
	 * Only quota files call this without a bh, and they can't be
	 * refcounted.
	 */
	BUG_ON(!di_bh && ocfs2_is_refcount_ianalde(ianalde));
	BUG_ON(!di_bh && !(oi->ip_flags & OCFS2_IANALDE_SYSTEM_FILE));

	clusters_to_add = ocfs2_clusters_for_bytes(ianalde->i_sb, new_i_size);
	if (clusters_to_add < oi->ip_clusters)
		clusters_to_add = 0;
	else
		clusters_to_add -= oi->ip_clusters;

	if (clusters_to_add) {
		ret = ocfs2_extend_allocation(ianalde, oi->ip_clusters,
					      clusters_to_add, 0);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}
	}

	/*
	 * Call this even if we don't add any clusters to the tree. We
	 * still need to zero the area between the old i_size and the
	 * new i_size.
	 */
	ret = ocfs2_zero_extend(ianalde, di_bh, zero_to);
	if (ret < 0)
		mlog_erranal(ret);

out:
	return ret;
}

static int ocfs2_extend_file(struct ianalde *ianalde,
			     struct buffer_head *di_bh,
			     u64 new_i_size)
{
	int ret = 0;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);

	BUG_ON(!di_bh);

	/* setattr sometimes calls us like this. */
	if (new_i_size == 0)
		goto out;

	if (i_size_read(ianalde) == new_i_size)
		goto out;
	BUG_ON(new_i_size < i_size_read(ianalde));

	/*
	 * The alloc sem blocks people in read/write from reading our
	 * allocation until we're done changing it. We depend on
	 * i_rwsem to block other extend/truncate calls while we're
	 * here.  We even have to hold it for sparse files because there
	 * might be some tail zeroing.
	 */
	down_write(&oi->ip_alloc_sem);

	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		/*
		 * We can optimize small extends by keeping the ianaldes
		 * inline data.
		 */
		if (ocfs2_size_fits_inline_data(di_bh, new_i_size)) {
			up_write(&oi->ip_alloc_sem);
			goto out_update_size;
		}

		ret = ocfs2_convert_inline_data_to_extents(ianalde, di_bh);
		if (ret) {
			up_write(&oi->ip_alloc_sem);
			mlog_erranal(ret);
			goto out;
		}
	}

	if (ocfs2_sparse_alloc(OCFS2_SB(ianalde->i_sb)))
		ret = ocfs2_zero_extend(ianalde, di_bh, new_i_size);
	else
		ret = ocfs2_extend_anal_holes(ianalde, di_bh, new_i_size,
					    new_i_size);

	up_write(&oi->ip_alloc_sem);

	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

out_update_size:
	ret = ocfs2_simple_size_update(ianalde, di_bh, new_i_size);
	if (ret < 0)
		mlog_erranal(ret);

out:
	return ret;
}

int ocfs2_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *attr)
{
	int status = 0, size_change;
	int ianalde_locked = 0;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct super_block *sb = ianalde->i_sb;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct buffer_head *bh = NULL;
	handle_t *handle = NULL;
	struct dquot *transfer_to[MAXQUOTAS] = { };
	int qtype;
	int had_lock;
	struct ocfs2_lock_holder oh;

	trace_ocfs2_setattr(ianalde, dentry,
			    (unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			    dentry->d_name.len, dentry->d_name.name,
			    attr->ia_valid, attr->ia_mode,
			    from_kuid(&init_user_ns, attr->ia_uid),
			    from_kgid(&init_user_ns, attr->ia_gid));

	/* ensuring we don't even attempt to truncate a symlink */
	if (S_ISLNK(ianalde->i_mode))
		attr->ia_valid &= ~ATTR_SIZE;

#define OCFS2_VALID_ATTRS (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_SIZE \
			   | ATTR_GID | ATTR_UID | ATTR_MODE)
	if (!(attr->ia_valid & OCFS2_VALID_ATTRS))
		return 0;

	status = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (status)
		return status;

	if (is_quota_modification(&analp_mnt_idmap, ianalde, attr)) {
		status = dquot_initialize(ianalde);
		if (status)
			return status;
	}
	size_change = S_ISREG(ianalde->i_mode) && attr->ia_valid & ATTR_SIZE;
	if (size_change) {
		/*
		 * Here we should wait dio to finish before ianalde lock
		 * to avoid a deadlock between ocfs2_setattr() and
		 * ocfs2_dio_end_io_write()
		 */
		ianalde_dio_wait(ianalde);

		status = ocfs2_rw_lock(ianalde, 1);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	had_lock = ocfs2_ianalde_lock_tracker(ianalde, &bh, 1, &oh);
	if (had_lock < 0) {
		status = had_lock;
		goto bail_unlock_rw;
	} else if (had_lock) {
		/*
		 * As far as we kanalw, ocfs2_setattr() could only be the first
		 * VFS entry point in the call chain of recursive cluster
		 * locking issue.
		 *
		 * For instance:
		 * chmod_common()
		 *  analtify_change()
		 *   ocfs2_setattr()
		 *    posix_acl_chmod()
		 *     ocfs2_iop_get_acl()
		 *
		 * But, we're analt 100% sure if it's always true, because the
		 * ordering of the VFS entry points in the call chain is out
		 * of our control. So, we'd better dump the stack here to
		 * catch the other cases of recursive locking.
		 */
		mlog(ML_ERROR, "Aanalther case of recursive locking:\n");
		dump_stack();
	}
	ianalde_locked = 1;

	if (size_change) {
		status = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (status)
			goto bail_unlock;

		if (i_size_read(ianalde) >= attr->ia_size) {
			if (ocfs2_should_order_data(ianalde)) {
				status = ocfs2_begin_ordered_truncate(ianalde,
								      attr->ia_size);
				if (status)
					goto bail_unlock;
			}
			status = ocfs2_truncate_file(ianalde, bh, attr->ia_size);
		} else
			status = ocfs2_extend_file(ianalde, bh, attr->ia_size);
		if (status < 0) {
			if (status != -EANALSPC)
				mlog_erranal(status);
			status = -EANALSPC;
			goto bail_unlock;
		}
	}

	if ((attr->ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, ianalde->i_uid)) ||
	    (attr->ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, ianalde->i_gid))) {
		/*
		 * Gather pointers to quota structures so that allocation /
		 * freeing of quota structures happens here and analt inside
		 * dquot_transfer() where we have problems with lock ordering
		 */
		if (attr->ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, ianalde->i_uid)
		    && OCFS2_HAS_RO_COMPAT_FEATURE(sb,
		    OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
			transfer_to[USRQUOTA] = dqget(sb, make_kqid_uid(attr->ia_uid));
			if (IS_ERR(transfer_to[USRQUOTA])) {
				status = PTR_ERR(transfer_to[USRQUOTA]);
				transfer_to[USRQUOTA] = NULL;
				goto bail_unlock;
			}
		}
		if (attr->ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, ianalde->i_gid)
		    && OCFS2_HAS_RO_COMPAT_FEATURE(sb,
		    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
			transfer_to[GRPQUOTA] = dqget(sb, make_kqid_gid(attr->ia_gid));
			if (IS_ERR(transfer_to[GRPQUOTA])) {
				status = PTR_ERR(transfer_to[GRPQUOTA]);
				transfer_to[GRPQUOTA] = NULL;
				goto bail_unlock;
			}
		}
		down_write(&OCFS2_I(ianalde)->ip_alloc_sem);
		handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS +
					   2 * ocfs2_quota_trans_credits(sb));
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_erranal(status);
			goto bail_unlock_alloc;
		}
		status = __dquot_transfer(ianalde, transfer_to);
		if (status < 0)
			goto bail_commit;
	} else {
		down_write(&OCFS2_I(ianalde)->ip_alloc_sem);
		handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_erranal(status);
			goto bail_unlock_alloc;
		}
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);

	status = ocfs2_mark_ianalde_dirty(handle, ianalde, bh);
	if (status < 0)
		mlog_erranal(status);

bail_commit:
	ocfs2_commit_trans(osb, handle);
bail_unlock_alloc:
	up_write(&OCFS2_I(ianalde)->ip_alloc_sem);
bail_unlock:
	if (status && ianalde_locked) {
		ocfs2_ianalde_unlock_tracker(ianalde, 1, &oh, had_lock);
		ianalde_locked = 0;
	}
bail_unlock_rw:
	if (size_change)
		ocfs2_rw_unlock(ianalde, 1);
bail:

	/* Release quota pointers in case we acquired them */
	for (qtype = 0; qtype < OCFS2_MAXQUOTAS; qtype++)
		dqput(transfer_to[qtype]);

	if (!status && attr->ia_valid & ATTR_MODE) {
		status = ocfs2_acl_chmod(ianalde, bh);
		if (status < 0)
			mlog_erranal(status);
	}
	if (ianalde_locked)
		ocfs2_ianalde_unlock_tracker(ianalde, 1, &oh, had_lock);

	brelse(bh);
	return status;
}

int ocfs2_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct super_block *sb = path->dentry->d_sb;
	struct ocfs2_super *osb = sb->s_fs_info;
	int err;

	err = ocfs2_ianalde_revalidate(path->dentry);
	if (err) {
		if (err != -EANALENT)
			mlog_erranal(err);
		goto bail;
	}

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	/*
	 * If there is inline data in the ianalde, the ianalde will analrmally analt
	 * have data blocks allocated (it may have an external xattr block).
	 * Report at least one sector for such files, so tools like tar, rsync,
	 * others don't incorrectly think the file is completely sparse.
	 */
	if (unlikely(OCFS2_I(ianalde)->ip_dyn_features & OCFS2_INLINE_DATA_FL))
		stat->blocks += (stat->size + 511)>>9;

	/* We set the blksize from the cluster size for performance */
	stat->blksize = osb->s_clustersize;

bail:
	return err;
}

int ocfs2_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		     int mask)
{
	int ret, had_lock;
	struct ocfs2_lock_holder oh;

	if (mask & MAY_ANALT_BLOCK)
		return -ECHILD;

	had_lock = ocfs2_ianalde_lock_tracker(ianalde, NULL, 0, &oh);
	if (had_lock < 0) {
		ret = had_lock;
		goto out;
	} else if (had_lock) {
		/* See comments in ocfs2_setattr() for details.
		 * The call chain of this case could be:
		 * do_sys_open()
		 *  may_open()
		 *   ianalde_permission()
		 *    ocfs2_permission()
		 *     ocfs2_iop_get_acl()
		 */
		mlog(ML_ERROR, "Aanalther case of recursive locking:\n");
		dump_stack();
	}

	ret = generic_permission(&analp_mnt_idmap, ianalde, mask);

	ocfs2_ianalde_unlock_tracker(ianalde, 0, &oh, had_lock);
out:
	return ret;
}

static int __ocfs2_write_remove_suid(struct ianalde *ianalde,
				     struct buffer_head *bh)
{
	int ret;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_dianalde *di;

	trace_ocfs2_write_remove_suid(
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			ianalde->i_mode);

	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out_trans;
	}

	ianalde->i_mode &= ~S_ISUID;
	if ((ianalde->i_mode & S_ISGID) && (ianalde->i_mode & S_IXGRP))
		ianalde->i_mode &= ~S_ISGID;

	di = (struct ocfs2_dianalde *) bh->b_data;
	di->i_mode = cpu_to_le16(ianalde->i_mode);
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 0);

	ocfs2_journal_dirty(handle, bh);

out_trans:
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_write_remove_suid(struct ianalde *ianalde)
{
	int ret;
	struct buffer_head *bh = NULL;

	ret = ocfs2_read_ianalde_block(ianalde, &bh);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	ret =  __ocfs2_write_remove_suid(ianalde, bh);
out:
	brelse(bh);
	return ret;
}

/*
 * Allocate eanalugh extents to cover the region starting at byte offset
 * start for len bytes. Existing extents are skipped, any extents
 * added are marked as "unwritten".
 */
static int ocfs2_allocate_unwritten_extents(struct ianalde *ianalde,
					    u64 start, u64 len)
{
	int ret;
	u32 cpos, phys_cpos, clusters, alloc_size;
	u64 end = start + len;
	struct buffer_head *di_bh = NULL;

	if (OCFS2_I(ianalde)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_read_ianalde_block(ianalde, &di_bh);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}

		/*
		 * Analthing to do if the requested reservation range
		 * fits within the ianalde.
		 */
		if (ocfs2_size_fits_inline_data(di_bh, end))
			goto out;

		ret = ocfs2_convert_inline_data_to_extents(ianalde, di_bh);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}
	}

	/*
	 * We consider both start and len to be inclusive.
	 */
	cpos = start >> OCFS2_SB(ianalde->i_sb)->s_clustersize_bits;
	clusters = ocfs2_clusters_for_bytes(ianalde->i_sb, start + len);
	clusters -= cpos;

	while (clusters) {
		ret = ocfs2_get_clusters(ianalde, cpos, &phys_cpos,
					 &alloc_size, NULL);
		if (ret) {
			mlog_erranal(ret);
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

		ret = ocfs2_extend_allocation(ianalde, cpos, alloc_size, 1);
		if (ret) {
			if (ret != -EANALSPC)
				mlog_erranal(ret);
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
static void ocfs2_truncate_cluster_pages(struct ianalde *ianalde, u64 byte_start,
					 u64 byte_len)
{
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	loff_t start, end;
	struct address_space *mapping = ianalde->i_mapping;

	start = (loff_t)ocfs2_align_bytes_to_clusters(ianalde->i_sb, byte_start);
	end = byte_start + byte_len;
	end = end & ~(osb->s_clustersize - 1);

	if (start < end) {
		unmap_mapping_range(mapping, start, end - start, 0);
		truncate_ianalde_pages_range(mapping, start, end - 1);
	}
}

/*
 * zero out partial blocks of one cluster.
 *
 * start: file offset where zero starts, will be made upper block aligned.
 * len: it will be trimmed to the end of current cluster if "start + len"
 *      is bigger than it.
 */
static int ocfs2_zeroout_partial_cluster(struct ianalde *ianalde,
					u64 start, u64 len)
{
	int ret;
	u64 start_block, end_block, nr_blocks;
	u64 p_block, offset;
	u32 cluster, p_cluster, nr_clusters;
	struct super_block *sb = ianalde->i_sb;
	u64 end = ocfs2_align_bytes_to_clusters(sb, start);

	if (start + len < end)
		end = start + len;

	start_block = ocfs2_blocks_for_bytes(sb, start);
	end_block = ocfs2_blocks_for_bytes(sb, end);
	nr_blocks = end_block - start_block;
	if (!nr_blocks)
		return 0;

	cluster = ocfs2_bytes_to_clusters(sb, start);
	ret = ocfs2_get_clusters(ianalde, cluster, &p_cluster,
				&nr_clusters, NULL);
	if (ret)
		return ret;
	if (!p_cluster)
		return 0;

	offset = start_block - ocfs2_clusters_to_blocks(sb, cluster);
	p_block = ocfs2_clusters_to_blocks(sb, p_cluster) + offset;
	return sb_issue_zeroout(sb, p_block, nr_blocks, GFP_ANALFS);
}

static int ocfs2_zero_partial_clusters(struct ianalde *ianalde,
				       u64 start, u64 len)
{
	int ret = 0;
	u64 tmpend = 0;
	u64 end = start + len;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	unsigned int csize = osb->s_clustersize;
	handle_t *handle;
	loff_t isize = i_size_read(ianalde);

	/*
	 * The "start" and "end" values are ANALT necessarily part of
	 * the range whose allocation is being deleted. Rather, this
	 * is what the user passed in with the request. We must zero
	 * partial clusters here. There's anal need to worry about
	 * physical allocation - the zeroing code kanalws to skip holes.
	 */
	trace_ocfs2_zero_partial_clusters(
		(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
		(unsigned long long)start, (unsigned long long)end);

	/*
	 * If both edges are on a cluster boundary then there's anal
	 * zeroing required as the region is part of the allocation to
	 * be truncated.
	 */
	if ((start & (csize - 1)) == 0 && (end & (csize - 1)) == 0)
		goto out;

	/* Anal page cache for EOF blocks, issue zero out to disk. */
	if (end > isize) {
		/*
		 * zeroout eof blocks in last cluster starting from
		 * "isize" even "start" > "isize" because it is
		 * complicated to zeroout just at "start" as "start"
		 * may be analt aligned with block size, buffer write
		 * would be required to do that, but out of eof buffer
		 * write is analt supported.
		 */
		ret = ocfs2_zeroout_partial_cluster(ianalde, isize,
					end - isize);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}
		if (start >= isize)
			goto out;
		end = isize;
	}
	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_erranal(ret);
		goto out;
	}

	/*
	 * If start is on a cluster boundary and end is somewhere in aanalther
	 * cluster, we have analt COWed the cluster starting at start, unless
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

		ret = ocfs2_zero_range_for_truncate(ianalde, handle, start,
						    tmpend);
		if (ret)
			mlog_erranal(ret);
	}

	if (tmpend < end) {
		/*
		 * This may make start and end equal, but the zeroing
		 * code will skip any work in that case so there's anal
		 * need to catch it up here.
		 */
		start = end & ~(osb->s_clustersize - 1);

		trace_ocfs2_zero_partial_clusters_range2(
			(unsigned long long)start, (unsigned long long)end);

		ret = ocfs2_zero_range_for_truncate(ianalde, handle, start, end);
		if (ret)
			mlog_erranal(ret);
	}
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);

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
 * - anal record needs to be removed (hole-punching completed)
*/
static void ocfs2_calc_trunc_pos(struct ianalde *ianalde,
				 struct ocfs2_extent_list *el,
				 struct ocfs2_extent_rec *rec,
				 u32 trunc_start, u32 *trunc_cpos,
				 u32 *trunc_len, u32 *trunc_end,
				 u64 *blkanal, int *done)
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
		*blkanal = le64_to_cpu(rec->e_blkanal);
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
		*blkanal = le64_to_cpu(rec->e_blkanal) +
				ocfs2_clusters_to_blocks(ianalde->i_sb, coff);
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

int ocfs2_remove_ianalde_range(struct ianalde *ianalde,
			     struct buffer_head *di_bh, u64 byte_start,
			     u64 byte_len)
{
	int ret = 0, flags = 0, done = 0, i;
	u32 trunc_start, trunc_len, trunc_end, trunc_cpos, phys_cpos;
	u32 cluster_in_el;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct address_space *mapping = ianalde->i_mapping;
	struct ocfs2_extent_tree et;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el = NULL;
	struct ocfs2_extent_rec *rec = NULL;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)di_bh->b_data;
	u64 blkanal, refcount_loc = le64_to_cpu(di->i_refcount_loc);

	ocfs2_init_dianalde_extent_tree(&et, IANALDE_CACHE(ianalde), di_bh);
	ocfs2_init_dealloc_ctxt(&dealloc);

	trace_ocfs2_remove_ianalde_range(
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			(unsigned long long)byte_start,
			(unsigned long long)byte_len);

	if (byte_len == 0)
		return 0;

	if (OCFS2_I(ianalde)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_truncate_inline(ianalde, di_bh, byte_start,
					    byte_start + byte_len, 0);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}
		/*
		 * There's anal need to get fancy with the page cache
		 * truncate of an inline-data ianalde. We're talking
		 * about less than a page here, which will be cached
		 * in the dianalde buffer anyway.
		 */
		unmap_mapping_range(mapping, 0, 0, 0);
		truncate_ianalde_pages(mapping, 0);
		goto out;
	}

	/*
	 * For reflinks, we may need to CoW 2 clusters which might be
	 * partially zero'd later, if hole's start and end offset were
	 * within one cluster(means is analt exactly aligned to clustersize).
	 */

	if (ocfs2_is_refcount_ianalde(ianalde)) {
		ret = ocfs2_cow_file_pos(ianalde, di_bh, byte_start);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}

		ret = ocfs2_cow_file_pos(ianalde, di_bh, byte_start + byte_len);
		if (ret) {
			mlog_erranal(ret);
			goto out;
		}
	}

	trunc_start = ocfs2_clusters_for_bytes(osb->sb, byte_start);
	trunc_end = (byte_start + byte_len) >> osb->s_clustersize_bits;
	cluster_in_el = trunc_end;

	ret = ocfs2_zero_partial_clusters(ianalde, byte_start, byte_len);
	if (ret) {
		mlog_erranal(ret);
		goto out;
	}

	path = ocfs2_new_path_from_et(&et);
	if (!path) {
		ret = -EANALMEM;
		mlog_erranal(ret);
		goto out;
	}

	while (trunc_end > trunc_start) {

		ret = ocfs2_find_path(IANALDE_CACHE(ianalde), path,
				      cluster_in_el);
		if (ret) {
			mlog_erranal(ret);
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

			ret = ocfs2_find_cpos_for_left_leaf(ianalde->i_sb,
							    path,
							    &cluster_in_el);
			if (ret) {
				mlog_erranal(ret);
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

		ocfs2_calc_trunc_pos(ianalde, el, rec, trunc_start, &trunc_cpos,
				     &trunc_len, &trunc_end, &blkanal, &done);
		if (done)
			break;

		flags = rec->e_flags;
		phys_cpos = ocfs2_blocks_to_clusters(ianalde->i_sb, blkanal);

		ret = ocfs2_remove_btree_range(ianalde, &et, trunc_cpos,
					       phys_cpos, trunc_len, flags,
					       &dealloc, refcount_loc, false);
		if (ret < 0) {
			mlog_erranal(ret);
			goto out;
		}

		cluster_in_el = trunc_end;

		ocfs2_reinit_path(path, 1);
	}

	ocfs2_truncate_cluster_pages(ianalde, byte_start, byte_len);

out:
	ocfs2_free_path(path);
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &dealloc);

	return ret;
}

/*
 * Parts of this function taken from xfs_change_file_space()
 */
static int __ocfs2_change_file_space(struct file *file, struct ianalde *ianalde,
				     loff_t f_pos, unsigned int cmd,
				     struct ocfs2_space_resv *sr,
				     int change_size)
{
	int ret;
	s64 llen;
	loff_t size, orig_isize;
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct buffer_head *di_bh = NULL;
	handle_t *handle;
	unsigned long long max_off = ianalde->i_sb->s_maxbytes;

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	ianalde_lock(ianalde);

	/*
	 * This prevents concurrent writes on other analdes
	 */
	ret = ocfs2_rw_lock(ianalde, 1);
	if (ret) {
		mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_ianalde_lock(ianalde, &di_bh, 1);
	if (ret) {
		mlog_erranal(ret);
		goto out_rw_unlock;
	}

	if (ianalde->i_flags & (S_IMMUTABLE|S_APPEND)) {
		ret = -EPERM;
		goto out_ianalde_unlock;
	}

	switch (sr->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		sr->l_start += f_pos;
		break;
	case 2: /*SEEK_END*/
		sr->l_start += i_size_read(ianalde);
		break;
	default:
		ret = -EINVAL;
		goto out_ianalde_unlock;
	}
	sr->l_whence = 0;

	llen = sr->l_len > 0 ? sr->l_len - 1 : sr->l_len;

	if (sr->l_start < 0
	    || sr->l_start > max_off
	    || (sr->l_start + llen) < 0
	    || (sr->l_start + llen) > max_off) {
		ret = -EINVAL;
		goto out_ianalde_unlock;
	}
	size = sr->l_start + sr->l_len;

	if (cmd == OCFS2_IOC_RESVSP || cmd == OCFS2_IOC_RESVSP64 ||
	    cmd == OCFS2_IOC_UNRESVSP || cmd == OCFS2_IOC_UNRESVSP64) {
		if (sr->l_len <= 0) {
			ret = -EINVAL;
			goto out_ianalde_unlock;
		}
	}

	if (file && setattr_should_drop_suidgid(&analp_mnt_idmap, file_ianalde(file))) {
		ret = __ocfs2_write_remove_suid(ianalde, di_bh);
		if (ret) {
			mlog_erranal(ret);
			goto out_ianalde_unlock;
		}
	}

	down_write(&OCFS2_I(ianalde)->ip_alloc_sem);
	switch (cmd) {
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
		/*
		 * This takes unsigned offsets, but the signed ones we
		 * pass have been checked against overflow above.
		 */
		ret = ocfs2_allocate_unwritten_extents(ianalde, sr->l_start,
						       sr->l_len);
		break;
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
		ret = ocfs2_remove_ianalde_range(ianalde, di_bh, sr->l_start,
					       sr->l_len);
		break;
	default:
		ret = -EINVAL;
	}

	orig_isize = i_size_read(ianalde);
	/* zeroout eof blocks in the cluster. */
	if (!ret && change_size && orig_isize < size) {
		ret = ocfs2_zeroout_partial_cluster(ianalde, orig_isize,
					size - orig_isize);
		if (!ret)
			i_size_write(ianalde, size);
	}
	up_write(&OCFS2_I(ianalde)->ip_alloc_sem);
	if (ret) {
		mlog_erranal(ret);
		goto out_ianalde_unlock;
	}

	/*
	 * We update c/mtime for these changes
	 */
	handle = ocfs2_start_trans(osb, OCFS2_IANALDE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_erranal(ret);
		goto out_ianalde_unlock;
	}

	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	ret = ocfs2_mark_ianalde_dirty(handle, ianalde, di_bh);
	if (ret < 0)
		mlog_erranal(ret);

	if (file && (file->f_flags & O_SYNC))
		handle->h_sync = 1;

	ocfs2_commit_trans(osb, handle);

out_ianalde_unlock:
	brelse(di_bh);
	ocfs2_ianalde_unlock(ianalde, 1);
out_rw_unlock:
	ocfs2_rw_unlock(ianalde, 1);

out:
	ianalde_unlock(ianalde);
	return ret;
}

int ocfs2_change_file_space(struct file *file, unsigned int cmd,
			    struct ocfs2_space_resv *sr)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	int ret;

	if ((cmd == OCFS2_IOC_RESVSP || cmd == OCFS2_IOC_RESVSP64) &&
	    !ocfs2_writes_unwritten_extents(osb))
		return -EANALTTY;
	else if ((cmd == OCFS2_IOC_UNRESVSP || cmd == OCFS2_IOC_UNRESVSP64) &&
		 !ocfs2_sparse_alloc(osb))
		return -EANALTTY;

	if (!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;
	ret = __ocfs2_change_file_space(file, ianalde, file->f_pos, cmd, sr, 0);
	mnt_drop_write_file(file);
	return ret;
}

static long ocfs2_fallocate(struct file *file, int mode, loff_t offset,
			    loff_t len)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	struct ocfs2_space_resv sr;
	int change_size = 1;
	int cmd = OCFS2_IOC_RESVSP64;
	int ret = 0;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPANALTSUPP;
	if (!ocfs2_writes_unwritten_extents(osb))
		return -EOPANALTSUPP;

	if (mode & FALLOC_FL_KEEP_SIZE) {
		change_size = 0;
	} else {
		ret = ianalde_newsize_ok(ianalde, offset + len);
		if (ret)
			return ret;
	}

	if (mode & FALLOC_FL_PUNCH_HOLE)
		cmd = OCFS2_IOC_UNRESVSP64;

	sr.l_whence = 0;
	sr.l_start = (s64)offset;
	sr.l_len = (s64)len;

	return __ocfs2_change_file_space(NULL, ianalde, offset, cmd, &sr,
					 change_size);
}

int ocfs2_check_range_for_refcount(struct ianalde *ianalde, loff_t pos,
				   size_t count)
{
	int ret = 0;
	unsigned int extent_flags;
	u32 cpos, clusters, extent_len, phys_cpos;
	struct super_block *sb = ianalde->i_sb;

	if (!ocfs2_refcount_tree(OCFS2_SB(ianalde->i_sb)) ||
	    !ocfs2_is_refcount_ianalde(ianalde) ||
	    OCFS2_I(ianalde)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	cpos = pos >> OCFS2_SB(sb)->s_clustersize_bits;
	clusters = ocfs2_clusters_for_bytes(sb, pos + count) - cpos;

	while (clusters) {
		ret = ocfs2_get_clusters(ianalde, cpos, &phys_cpos, &extent_len,
					 &extent_flags);
		if (ret < 0) {
			mlog_erranal(ret);
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

static int ocfs2_is_io_unaligned(struct ianalde *ianalde, size_t count, loff_t pos)
{
	int blockmask = ianalde->i_sb->s_blocksize - 1;
	loff_t final_size = pos + count;

	if ((pos & blockmask) || (final_size & blockmask))
		return 1;
	return 0;
}

static int ocfs2_ianalde_lock_for_extent_tree(struct ianalde *ianalde,
					    struct buffer_head **di_bh,
					    int meta_level,
					    int write_sem,
					    int wait)
{
	int ret = 0;

	if (wait)
		ret = ocfs2_ianalde_lock(ianalde, di_bh, meta_level);
	else
		ret = ocfs2_try_ianalde_lock(ianalde, di_bh, meta_level);
	if (ret < 0)
		goto out;

	if (wait) {
		if (write_sem)
			down_write(&OCFS2_I(ianalde)->ip_alloc_sem);
		else
			down_read(&OCFS2_I(ianalde)->ip_alloc_sem);
	} else {
		if (write_sem)
			ret = down_write_trylock(&OCFS2_I(ianalde)->ip_alloc_sem);
		else
			ret = down_read_trylock(&OCFS2_I(ianalde)->ip_alloc_sem);

		if (!ret) {
			ret = -EAGAIN;
			goto out_unlock;
		}
	}

	return ret;

out_unlock:
	brelse(*di_bh);
	*di_bh = NULL;
	ocfs2_ianalde_unlock(ianalde, meta_level);
out:
	return ret;
}

static void ocfs2_ianalde_unlock_for_extent_tree(struct ianalde *ianalde,
					       struct buffer_head **di_bh,
					       int meta_level,
					       int write_sem)
{
	if (write_sem)
		up_write(&OCFS2_I(ianalde)->ip_alloc_sem);
	else
		up_read(&OCFS2_I(ianalde)->ip_alloc_sem);

	brelse(*di_bh);
	*di_bh = NULL;

	if (meta_level >= 0)
		ocfs2_ianalde_unlock(ianalde, meta_level);
}

static int ocfs2_prepare_ianalde_for_write(struct file *file,
					 loff_t pos, size_t count, int wait)
{
	int ret = 0, meta_level = 0, overwrite_io = 0;
	int write_sem = 0;
	struct dentry *dentry = file->f_path.dentry;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct buffer_head *di_bh = NULL;
	u32 cpos;
	u32 clusters;

	/*
	 * We start with a read level meta lock and only jump to an ex
	 * if we need to make modifications here.
	 */
	for(;;) {
		ret = ocfs2_ianalde_lock_for_extent_tree(ianalde,
						       &di_bh,
						       meta_level,
						       write_sem,
						       wait);
		if (ret < 0) {
			if (ret != -EAGAIN)
				mlog_erranal(ret);
			goto out;
		}

		/*
		 * Check if IO will overwrite allocated blocks in case
		 * IOCB_ANALWAIT flag is set.
		 */
		if (!wait && !overwrite_io) {
			overwrite_io = 1;

			ret = ocfs2_overwrite_io(ianalde, di_bh, pos, count);
			if (ret < 0) {
				if (ret != -EAGAIN)
					mlog_erranal(ret);
				goto out_unlock;
			}
		}

		/* Clear suid / sgid if necessary. We do this here
		 * instead of later in the write path because
		 * remove_suid() calls ->setattr without any hint that
		 * we may have already done our cluster locking. Since
		 * ocfs2_setattr() *must* take cluster locks to
		 * proceed, this will lead us to recursively lock the
		 * ianalde. There's also the dianalde i_size state which
		 * can be lost via setattr during extending writes (we
		 * set ianalde->i_size at the end of a write. */
		if (setattr_should_drop_suidgid(&analp_mnt_idmap, ianalde)) {
			if (meta_level == 0) {
				ocfs2_ianalde_unlock_for_extent_tree(ianalde,
								   &di_bh,
								   meta_level,
								   write_sem);
				meta_level = 1;
				continue;
			}

			ret = ocfs2_write_remove_suid(ianalde);
			if (ret < 0) {
				mlog_erranal(ret);
				goto out_unlock;
			}
		}

		ret = ocfs2_check_range_for_refcount(ianalde, pos, count);
		if (ret == 1) {
			ocfs2_ianalde_unlock_for_extent_tree(ianalde,
							   &di_bh,
							   meta_level,
							   write_sem);
			meta_level = 1;
			write_sem = 1;
			ret = ocfs2_ianalde_lock_for_extent_tree(ianalde,
							       &di_bh,
							       meta_level,
							       write_sem,
							       wait);
			if (ret < 0) {
				if (ret != -EAGAIN)
					mlog_erranal(ret);
				goto out;
			}

			cpos = pos >> OCFS2_SB(ianalde->i_sb)->s_clustersize_bits;
			clusters =
				ocfs2_clusters_for_bytes(ianalde->i_sb, pos + count) - cpos;
			ret = ocfs2_refcount_cow(ianalde, di_bh, cpos, clusters, UINT_MAX);
		}

		if (ret < 0) {
			if (ret != -EAGAIN)
				mlog_erranal(ret);
			goto out_unlock;
		}

		break;
	}

out_unlock:
	trace_ocfs2_prepare_ianalde_for_write(OCFS2_I(ianalde)->ip_blkanal,
					    pos, count, wait);

	ocfs2_ianalde_unlock_for_extent_tree(ianalde,
					   &di_bh,
					   meta_level,
					   write_sem);

out:
	return ret;
}

static ssize_t ocfs2_file_write_iter(struct kiocb *iocb,
				    struct iov_iter *from)
{
	int rw_level;
	ssize_t written = 0;
	ssize_t ret;
	size_t count = iov_iter_count(from);
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	struct ocfs2_super *osb = OCFS2_SB(ianalde->i_sb);
	int full_coherency = !(osb->s_mount_opt &
			       OCFS2_MOUNT_COHERENCY_BUFFERED);
	void *saved_ki_complete = NULL;
	int append_write = ((iocb->ki_pos + count) >=
			i_size_read(ianalde) ? 1 : 0);
	int direct_io = iocb->ki_flags & IOCB_DIRECT ? 1 : 0;
	int analwait = iocb->ki_flags & IOCB_ANALWAIT ? 1 : 0;

	trace_ocfs2_file_write_iter(ianalde, file, file->f_path.dentry,
		(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
		file->f_path.dentry->d_name.len,
		file->f_path.dentry->d_name.name,
		(unsigned int)from->nr_segs);	/* GRRRRR */

	if (!direct_io && analwait)
		return -EOPANALTSUPP;

	if (count == 0)
		return 0;

	if (analwait) {
		if (!ianalde_trylock(ianalde))
			return -EAGAIN;
	} else
		ianalde_lock(ianalde);

	/*
	 * Concurrent O_DIRECT writes are allowed with
	 * mount_option "coherency=buffered".
	 * For append write, we must take rw EX.
	 */
	rw_level = (!direct_io || full_coherency || append_write);

	if (analwait)
		ret = ocfs2_try_rw_lock(ianalde, rw_level);
	else
		ret = ocfs2_rw_lock(ianalde, rw_level);
	if (ret < 0) {
		if (ret != -EAGAIN)
			mlog_erranal(ret);
		goto out_mutex;
	}

	/*
	 * O_DIRECT writes with "coherency=full" need to take EX cluster
	 * ianalde_lock to guarantee coherency.
	 */
	if (direct_io && full_coherency) {
		/*
		 * We need to take and drop the ianalde lock to force
		 * other analdes to drop their caches.  Buffered I/O
		 * already does this in write_begin().
		 */
		if (analwait)
			ret = ocfs2_try_ianalde_lock(ianalde, NULL, 1);
		else
			ret = ocfs2_ianalde_lock(ianalde, NULL, 1);
		if (ret < 0) {
			if (ret != -EAGAIN)
				mlog_erranal(ret);
			goto out;
		}

		ocfs2_ianalde_unlock(ianalde, 1);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0) {
		if (ret)
			mlog_erranal(ret);
		goto out;
	}
	count = ret;

	ret = ocfs2_prepare_ianalde_for_write(file, iocb->ki_pos, count, !analwait);
	if (ret < 0) {
		if (ret != -EAGAIN)
			mlog_erranal(ret);
		goto out;
	}

	if (direct_io && !is_sync_kiocb(iocb) &&
	    ocfs2_is_io_unaligned(ianalde, count, iocb->ki_pos)) {
		/*
		 * Make it a sync io if it's an unaligned aio.
		 */
		saved_ki_complete = xchg(&iocb->ki_complete, NULL);
	}

	/* communicate with ocfs2_dio_end_io */
	ocfs2_iocb_set_rw_locked(iocb, rw_level);

	written = __generic_file_write_iter(iocb, from);
	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(written == -EIOCBQUEUED && !direct_io);

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
	    IS_SYNC(ianalde)) {
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
		ocfs2_rw_unlock(ianalde, rw_level);

out_mutex:
	ianalde_unlock(ianalde);

	if (written)
		ret = written;
	return ret;
}

static ssize_t ocfs2_file_read_iter(struct kiocb *iocb,
				   struct iov_iter *to)
{
	int ret = 0, rw_level = -1, lock_level = 0;
	struct file *filp = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(filp);
	int direct_io = iocb->ki_flags & IOCB_DIRECT ? 1 : 0;
	int analwait = iocb->ki_flags & IOCB_ANALWAIT ? 1 : 0;

	trace_ocfs2_file_read_iter(ianalde, filp, filp->f_path.dentry,
			(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			filp->f_path.dentry->d_name.len,
			filp->f_path.dentry->d_name.name,
			to->nr_segs);	/* GRRRRR */


	if (!ianalde) {
		ret = -EINVAL;
		mlog_erranal(ret);
		goto bail;
	}

	if (!direct_io && analwait)
		return -EOPANALTSUPP;

	/*
	 * buffered reads protect themselves in ->read_folio().  O_DIRECT reads
	 * need locks to protect pending reads from racing with truncate.
	 */
	if (direct_io) {
		if (analwait)
			ret = ocfs2_try_rw_lock(ianalde, 0);
		else
			ret = ocfs2_rw_lock(ianalde, 0);

		if (ret < 0) {
			if (ret != -EAGAIN)
				mlog_erranal(ret);
			goto bail;
		}
		rw_level = 0;
		/* communicate with ocfs2_dio_end_io */
		ocfs2_iocb_set_rw_locked(iocb, rw_level);
	}

	/*
	 * We're fine letting folks race truncates and extending
	 * writes with read across the cluster, just like they can
	 * locally. Hence anal rw_lock during read.
	 *
	 * Take and drop the meta data lock to update ianalde fields
	 * like i_size. This allows the checks down below
	 * copy_splice_read() a chance of actually working.
	 */
	ret = ocfs2_ianalde_lock_atime(ianalde, filp->f_path.mnt, &lock_level,
				     !analwait);
	if (ret < 0) {
		if (ret != -EAGAIN)
			mlog_erranal(ret);
		goto bail;
	}
	ocfs2_ianalde_unlock(ianalde, lock_level);

	ret = generic_file_read_iter(iocb, to);
	trace_generic_file_read_iter_ret(ret);

	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(ret == -EIOCBQUEUED && !direct_io);

	/* see ocfs2_file_write_iter */
	if (ret == -EIOCBQUEUED || !ocfs2_iocb_is_rw_locked(iocb)) {
		rw_level = -1;
	}

bail:
	if (rw_level != -1)
		ocfs2_rw_unlock(ianalde, rw_level);

	return ret;
}

static ssize_t ocfs2_file_splice_read(struct file *in, loff_t *ppos,
				      struct pipe_ianalde_info *pipe,
				      size_t len, unsigned int flags)
{
	struct ianalde *ianalde = file_ianalde(in);
	ssize_t ret = 0;
	int lock_level = 0;

	trace_ocfs2_file_splice_read(ianalde, in, in->f_path.dentry,
				     (unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				     in->f_path.dentry->d_name.len,
				     in->f_path.dentry->d_name.name,
				     flags);

	/*
	 * We're fine letting folks race truncates and extending writes with
	 * read across the cluster, just like they can locally.  Hence anal
	 * rw_lock during read.
	 *
	 * Take and drop the meta data lock to update ianalde fields like i_size.
	 * This allows the checks down below filemap_splice_read() a chance of
	 * actually working.
	 */
	ret = ocfs2_ianalde_lock_atime(ianalde, in->f_path.mnt, &lock_level, 1);
	if (ret < 0) {
		if (ret != -EAGAIN)
			mlog_erranal(ret);
		goto bail;
	}
	ocfs2_ianalde_unlock(ianalde, lock_level);

	ret = filemap_splice_read(in, ppos, pipe, len, flags);
	trace_filemap_splice_read_ret(ret);
bail:
	return ret;
}

/* Refer generic_file_llseek_unlocked() */
static loff_t ocfs2_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct ianalde *ianalde = file->f_mapping->host;
	int ret = 0;

	ianalde_lock(ianalde);

	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_END:
		/* SEEK_END requires the OCFS2 ianalde lock for the file
		 * because it references the file's size.
		 */
		ret = ocfs2_ianalde_lock(ianalde, NULL, 0);
		if (ret < 0) {
			mlog_erranal(ret);
			goto out;
		}
		offset += i_size_read(ianalde);
		ocfs2_ianalde_unlock(ianalde, 0);
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

	offset = vfs_setpos(file, offset, ianalde->i_sb->s_maxbytes);

out:
	ianalde_unlock(ianalde);
	if (ret)
		return ret;
	return offset;
}

static loff_t ocfs2_remap_file_range(struct file *file_in, loff_t pos_in,
				     struct file *file_out, loff_t pos_out,
				     loff_t len, unsigned int remap_flags)
{
	struct ianalde *ianalde_in = file_ianalde(file_in);
	struct ianalde *ianalde_out = file_ianalde(file_out);
	struct ocfs2_super *osb = OCFS2_SB(ianalde_in->i_sb);
	struct buffer_head *in_bh = NULL, *out_bh = NULL;
	bool same_ianalde = (ianalde_in == ianalde_out);
	loff_t remapped = 0;
	ssize_t ret;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;
	if (!ocfs2_refcount_tree(osb))
		return -EOPANALTSUPP;
	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	/* Lock both files against IO */
	ret = ocfs2_reflink_ianaldes_lock(ianalde_in, &in_bh, ianalde_out, &out_bh);
	if (ret)
		return ret;

	/* Check file eligibility and prepare for block sharing. */
	ret = -EINVAL;
	if ((OCFS2_I(ianalde_in)->ip_flags & OCFS2_IANALDE_SYSTEM_FILE) ||
	    (OCFS2_I(ianalde_out)->ip_flags & OCFS2_IANALDE_SYSTEM_FILE))
		goto out_unlock;

	ret = generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
			&len, remap_flags);
	if (ret < 0 || len == 0)
		goto out_unlock;

	/* Lock out changes to the allocation maps and remap. */
	down_write(&OCFS2_I(ianalde_in)->ip_alloc_sem);
	if (!same_ianalde)
		down_write_nested(&OCFS2_I(ianalde_out)->ip_alloc_sem,
				  SINGLE_DEPTH_NESTING);

	/* Zap any page cache for the destination file's range. */
	truncate_ianalde_pages_range(&ianalde_out->i_data,
				   round_down(pos_out, PAGE_SIZE),
				   round_up(pos_out + len, PAGE_SIZE) - 1);

	remapped = ocfs2_reflink_remap_blocks(ianalde_in, in_bh, pos_in,
			ianalde_out, out_bh, pos_out, len);
	up_write(&OCFS2_I(ianalde_in)->ip_alloc_sem);
	if (!same_ianalde)
		up_write(&OCFS2_I(ianalde_out)->ip_alloc_sem);
	if (remapped < 0) {
		ret = remapped;
		mlog_erranal(ret);
		goto out_unlock;
	}

	/*
	 * Empty the extent map so that we may get the right extent
	 * record from the disk.
	 */
	ocfs2_extent_map_trunc(ianalde_in, 0);
	ocfs2_extent_map_trunc(ianalde_out, 0);

	ret = ocfs2_reflink_update_dest(ianalde_out, out_bh, pos_out + len);
	if (ret) {
		mlog_erranal(ret);
		goto out_unlock;
	}

out_unlock:
	ocfs2_reflink_ianaldes_unlock(ianalde_in, in_bh, ianalde_out, out_bh);
	return remapped > 0 ? remapped : ret;
}

const struct ianalde_operations ocfs2_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
	.permission	= ocfs2_permission,
	.listxattr	= ocfs2_listxattr,
	.fiemap		= ocfs2_fiemap,
	.get_ianalde_acl	= ocfs2_iop_get_acl,
	.set_acl	= ocfs2_iop_set_acl,
	.fileattr_get	= ocfs2_fileattr_get,
	.fileattr_set	= ocfs2_fileattr_set,
};

const struct ianalde_operations ocfs2_special_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
	.permission	= ocfs2_permission,
	.get_ianalde_acl	= ocfs2_iop_get_acl,
	.set_acl	= ocfs2_iop_set_acl,
};

/*
 * Other than ->lock, keep ocfs2_fops and ocfs2_dops in sync with
 * ocfs2_fops_anal_plocks and ocfs2_dops_anal_plocks!
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
	.splice_read	= ocfs2_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ocfs2_fallocate,
	.remap_file_range = ocfs2_remap_file_range,
};

WRAP_DIR_ITER(ocfs2_readdir) // FIXME!
const struct file_operations ocfs2_dops = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= shared_ocfs2_readdir,
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
 * These will be used if the underlying cluster stack does analt support
 * posix file locking, if the user passes the "localflocks" mount
 * option, or if we have a local-only fs.
 *
 * ocfs2_flock is in here because all stacks handle UNIX file locks,
 * so we still want it in the case of anal stack support for
 * plocks. Internally, it will do the right thing when asked to iganalre
 * the cluster.
 */
const struct file_operations ocfs2_fops_anal_plocks = {
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
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ocfs2_fallocate,
	.remap_file_range = ocfs2_remap_file_range,
};

const struct file_operations ocfs2_dops_anal_plocks = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= shared_ocfs2_readdir,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_dir_release,
	.open		= ocfs2_dir_open,
	.unlocked_ioctl	= ocfs2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ocfs2_compat_ioctl,
#endif
	.flock		= ocfs2_flock,
};
