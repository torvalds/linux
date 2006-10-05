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

#define MLOG_MASK_PREFIX ML_INODE
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
#include "mmap.h"
#include "suballoc.h"
#include "super.h"

#include "buffer_head_io.h"

static int ocfs2_sync_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
	return sync_mapping_buffers(inode->i_mapping);
}

static int ocfs2_file_open(struct inode *inode, struct file *file)
{
	int status;
	int mode = file->f_flags;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", inode, file,
		   file->f_dentry->d_name.len, file->f_dentry->d_name.name);

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
	status = 0;
leave:
	mlog_exit(status);
	return status;
}

static int ocfs2_file_release(struct inode *inode, struct file *file)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", inode, file,
		       file->f_dentry->d_name.len,
		       file->f_dentry->d_name.name);

	spin_lock(&oi->ip_lock);
	if (!--oi->ip_open_count)
		oi->ip_flags &= ~OCFS2_INODE_OPEN_DIRECT;
	spin_unlock(&oi->ip_lock);

	mlog_exit(0);

	return 0;
}

static int ocfs2_sync_file(struct file *file,
			   struct dentry *dentry,
			   int datasync)
{
	int err = 0;
	journal_t *journal;
	struct inode *inode = dentry->d_inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry("(0x%p, 0x%p, %d, '%.*s')\n", file, dentry, datasync,
		   dentry->d_name.len, dentry->d_name.name);

	err = ocfs2_sync_inode(dentry->d_inode);
	if (err)
		goto bail;

	journal = osb->journal->j_journal;
	err = journal_force_commit(journal);

bail:
	mlog_exit(err);

	return (err < 0) ? -EIO : 0;
}

int ocfs2_set_inode_size(struct ocfs2_journal_handle *handle,
			 struct inode *inode,
			 struct buffer_head *fe_bh,
			 u64 new_i_size)
{
	int status;

	mlog_entry_void();
	i_size_write(inode, new_i_size);
	inode->i_blocks = ocfs2_align_bytes_to_sectors(new_i_size);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_simple_size_update(struct inode *inode,
				    struct buffer_head *di_bh,
				    u64 new_i_size)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_journal_handle *handle = NULL;

	handle = ocfs2_start_trans(osb, NULL,
				   OCFS2_INODE_UPDATE_CREDITS);
	if (handle == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_set_inode_size(handle, inode, di_bh,
				   new_i_size);
	if (ret < 0)
		mlog_errno(ret);

	ocfs2_commit_trans(handle);
out:
	return ret;
}

static int ocfs2_orphan_for_truncate(struct ocfs2_super *osb,
				     struct inode *inode,
				     struct buffer_head *fe_bh,
				     u64 new_i_size)
{
	int status;
	struct ocfs2_journal_handle *handle;

	mlog_entry_void();

	/* TODO: This needs to actually orphan the inode in this
	 * transaction. */

	handle = ocfs2_start_trans(osb, NULL, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_set_inode_size(handle, inode, fe_bh, new_i_size);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(handle);
out:
	mlog_exit(status);
	return status;
}

static int ocfs2_truncate_file(struct inode *inode,
			       struct buffer_head *di_bh,
			       u64 new_i_size)
{
	int status = 0;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_truncate_context *tc = NULL;

	mlog_entry("(inode = %llu, new_i_size = %llu\n",
		   (unsigned long long)OCFS2_I(inode)->ip_blkno,
		   (unsigned long long)new_i_size);

	truncate_inode_pages(inode->i_mapping, new_i_size);

	fe = (struct ocfs2_dinode *) di_bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		status = -EIO;
		goto bail;
	}

	mlog_bug_on_msg(le64_to_cpu(fe->i_size) != i_size_read(inode),
			"Inode %llu, inode i_size = %lld != di "
			"i_size = %llu, i_flags = 0x%x\n",
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			i_size_read(inode),
			(unsigned long long)le64_to_cpu(fe->i_size),
			le32_to_cpu(fe->i_flags));

	if (new_i_size > le64_to_cpu(fe->i_size)) {
		mlog(0, "asked to truncate file with size (%llu) to size (%llu)!\n",
		     (unsigned long long)le64_to_cpu(fe->i_size),
		     (unsigned long long)new_i_size);
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "inode %llu, i_size = %llu, new_i_size = %llu\n",
	     (unsigned long long)le64_to_cpu(fe->i_blkno),
	     (unsigned long long)le64_to_cpu(fe->i_size),
	     (unsigned long long)new_i_size);

	/* lets handle the simple truncate cases before doing any more
	 * cluster locking. */
	if (new_i_size == le64_to_cpu(fe->i_size))
		goto bail;

	/* This forces other nodes to sync and drop their pages. Do
	 * this even if we have a truncate without allocation change -
	 * ocfs2 cluster sizes can be much greater than page size, so
	 * we have to truncate them anyway.  */
	status = ocfs2_data_lock(inode, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	ocfs2_data_unlock(inode, 1);

	if (le32_to_cpu(fe->i_clusters) ==
	    ocfs2_clusters_for_bytes(osb->sb, new_i_size)) {
		mlog(0, "fe->i_clusters = %u, so we do a simple truncate\n",
		     fe->i_clusters);
		/* No allocation change is required, so lets fast path
		 * this truncate. */
		status = ocfs2_simple_size_update(inode, di_bh, new_i_size);
		if (status < 0)
			mlog_errno(status);
		goto bail;
	}

	/* alright, we're going to need to do a full blown alloc size
	 * change. Orphan the inode so that recovery can complete the
	 * truncate if necessary. This does the task of marking
	 * i_size. */
	status = ocfs2_orphan_for_truncate(osb, inode, di_bh, new_i_size);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_prepare_truncate(osb, inode, di_bh, &tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_commit_truncate(osb, inode, di_bh, tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* TODO: orphan dir cleanup here. */
bail:

	mlog_exit(status);
	return status;
}

/*
 * extend allocation only here.
 * we'll update all the disk stuff, and oip->alloc_size
 *
 * expect stuff to be locked, a transaction started and enough data /
 * metadata reservations in the contexts.
 *
 * Will return -EAGAIN, and a reason if a restart is needed.
 * If passed in, *reason will always be set, even in error.
 */
int ocfs2_do_extend_allocation(struct ocfs2_super *osb,
			       struct inode *inode,
			       u32 clusters_to_add,
			       struct buffer_head *fe_bh,
			       struct ocfs2_journal_handle *handle,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       enum ocfs2_alloc_restarted *reason_ret)
{
	int status = 0;
	int free_extents;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) fe_bh->b_data;
	enum ocfs2_alloc_restarted reason = RESTART_NONE;
	u32 bit_off, num_bits;
	u64 block;

	BUG_ON(!clusters_to_add);

	free_extents = ocfs2_num_free_extents(osb, inode, fe);
	if (free_extents < 0) {
		status = free_extents;
		mlog_errno(status);
		goto leave;
	}

	/* there are two cases which could cause us to EAGAIN in the
	 * we-need-more-metadata case:
	 * 1) we haven't reserved *any*
	 * 2) we are so fragmented, we've needed to add metadata too
	 *    many times. */
	if (!free_extents && !meta_ac) {
		mlog(0, "we haven't reserved any metadata!\n");
		status = -EAGAIN;
		reason = RESTART_META;
		goto leave;
	} else if ((!free_extents)
		   && (ocfs2_alloc_context_bits_left(meta_ac)
		       < ocfs2_extend_meta_needed(fe))) {
		mlog(0, "filesystem is really fragmented...\n");
		status = -EAGAIN;
		reason = RESTART_META;
		goto leave;
	}

	status = ocfs2_claim_clusters(osb, handle, data_ac, 1,
				      &bit_off, &num_bits);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	BUG_ON(num_bits > clusters_to_add);

	/* reserve our write early -- insert_extent may update the inode */
	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	block = ocfs2_clusters_to_blocks(osb->sb, bit_off);
	mlog(0, "Allocating %u clusters at block %u for inode %llu\n",
	     num_bits, bit_off, (unsigned long long)OCFS2_I(inode)->ip_blkno);
	status = ocfs2_insert_extent(osb, handle, inode, fe_bh, block,
				     num_bits, meta_ac);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	le32_add_cpu(&fe->i_clusters, num_bits);
	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	clusters_to_add -= num_bits;

	if (clusters_to_add) {
		mlog(0, "need to alloc once more, clusters = %u, wanted = "
		     "%u\n", fe->i_clusters, clusters_to_add);
		status = -EAGAIN;
		reason = RESTART_TRANS;
	}

leave:
	mlog_exit(status);
	if (reason_ret)
		*reason_ret = reason;
	return status;
}

static int ocfs2_extend_allocation(struct inode *inode,
				   u32 clusters_to_add)
{
	int status = 0;
	int restart_func = 0;
	int drop_alloc_sem = 0;
	int credits, num_free_extents;
	u32 prev_clusters;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry("(clusters_to_add = %u)\n", clusters_to_add);

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno, &bh,
				  OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	fe = (struct ocfs2_dinode *) bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		status = -EIO;
		goto leave;
	}

restart_all:
	BUG_ON(le32_to_cpu(fe->i_clusters) != OCFS2_I(inode)->ip_clusters);

	mlog(0, "extend inode %llu, i_size = %lld, fe->i_clusters = %u, "
	     "clusters_to_add = %u\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno, i_size_read(inode),
	     fe->i_clusters, clusters_to_add);

	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto leave;
	}

	num_free_extents = ocfs2_num_free_extents(osb,
						  inode,
						  fe);
	if (num_free_extents < 0) {
		status = num_free_extents;
		mlog_errno(status);
		goto leave;
	}

	if (!num_free_extents) {
		status = ocfs2_reserve_new_metadata(osb,
						    handle,
						    fe,
						    &meta_ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto leave;
		}
	}

	status = ocfs2_reserve_clusters(osb,
					handle,
					clusters_to_add,
					&data_ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	/* blocks peope in read/write from reading our allocation
	 * until we're done changing it. We depend on i_mutex to block
	 * other extend/truncate calls while we're here. Ordering wrt
	 * start_trans is important here -- always do it before! */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);
	drop_alloc_sem = 1;

	credits = ocfs2_calc_extend_credits(osb->sb, fe, clusters_to_add);
	handle = ocfs2_start_trans(osb, handle, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

restarted_transaction:
	/* reserve a write to the file entry early on - that we if we
	 * run out of credits in the allocation path, we can still
	 * update i_size. */
	status = ocfs2_journal_access(handle, inode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	prev_clusters = OCFS2_I(inode)->ip_clusters;

	status = ocfs2_do_extend_allocation(osb,
					    inode,
					    clusters_to_add,
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

	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	spin_lock(&OCFS2_I(inode)->ip_lock);
	clusters_to_add -= (OCFS2_I(inode)->ip_clusters - prev_clusters);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	if (why != RESTART_NONE && clusters_to_add) {
		if (why == RESTART_META) {
			mlog(0, "restarting function.\n");
			restart_func = 1;
		} else {
			BUG_ON(why != RESTART_TRANS);

			mlog(0, "restarting transaction.\n");
			/* TODO: This can be more intelligent. */
			credits = ocfs2_calc_extend_credits(osb->sb,
							    fe,
							    clusters_to_add);
			status = ocfs2_extend_trans(handle->k_handle, credits);
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

	mlog(0, "fe: i_clusters = %u, i_size=%llu\n",
	     fe->i_clusters, (unsigned long long)fe->i_size);
	mlog(0, "inode: ip_clusters=%u, i_size=%lld\n",
	     OCFS2_I(inode)->ip_clusters, i_size_read(inode));

leave:
	if (drop_alloc_sem) {
		up_write(&OCFS2_I(inode)->ip_alloc_sem);
		drop_alloc_sem = 0;
	}
	if (handle) {
		ocfs2_commit_trans(handle);
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
	if (bh) {
		brelse(bh);
		bh = NULL;
	}

	mlog_exit(status);
	return status;
}

/* Some parts of this taken from generic_cont_expand, which turned out
 * to be too fragile to do exactly what we need without us having to
 * worry about recursive locking in ->prepare_write() and
 * ->commit_write(). */
static int ocfs2_write_zero_page(struct inode *inode,
				 u64 size)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	unsigned long index;
	unsigned int offset;
	struct ocfs2_journal_handle *handle = NULL;
	int ret;

	offset = (size & (PAGE_CACHE_SIZE-1)); /* Within page */
	/* ugh.  in prepare/commit_write, if from==to==start of block, we 
	** skip the prepare.  make sure we never send an offset for the start
	** of a block
	*/
	if ((offset & (inode->i_sb->s_blocksize - 1)) == 0) {
		offset++;
	}
	index = size >> PAGE_CACHE_SHIFT;

	page = grab_cache_page(mapping, index);
	if (!page) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_prepare_write_nolock(inode, page, offset, offset);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_unlock;
	}

	if (ocfs2_should_order_data(inode)) {
		handle = ocfs2_start_walk_page_trans(inode, page, offset,
						     offset);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			handle = NULL;
			goto out_unlock;
		}
	}

	/* must not update i_size! */
	ret = block_commit_write(page, offset, offset);
	if (ret < 0)
		mlog_errno(ret);
	else
		ret = 0;

	if (handle)
		ocfs2_commit_trans(handle);
out_unlock:
	unlock_page(page);
	page_cache_release(page);
out:
	return ret;
}

static int ocfs2_zero_extend(struct inode *inode,
			     u64 zero_to_size)
{
	int ret = 0;
	u64 start_off;
	struct super_block *sb = inode->i_sb;

	start_off = ocfs2_align_bytes_to_blocks(sb, i_size_read(inode));
	while (start_off < zero_to_size) {
		ret = ocfs2_write_zero_page(inode, start_off);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		start_off += sb->s_blocksize;

		/*
		 * Very large extends have the potential to lock up
		 * the cpu for extended periods of time.
		 */
		cond_resched();
	}

out:
	return ret;
}

/* 
 * A tail_to_skip value > 0 indicates that we're being called from
 * ocfs2_file_aio_write(). This has the following implications:
 *
 * - we don't want to update i_size
 * - di_bh will be NULL, which is fine because it's only used in the
 *   case where we want to update i_size.
 * - ocfs2_zero_extend() will then only be filling the hole created
 *   between i_size and the start of the write.
 */
static int ocfs2_extend_file(struct inode *inode,
			     struct buffer_head *di_bh,
			     u64 new_i_size,
			     size_t tail_to_skip)
{
	int ret = 0;
	u32 clusters_to_add;

	BUG_ON(!tail_to_skip && !di_bh);

	/* setattr sometimes calls us like this. */
	if (new_i_size == 0)
		goto out;

	if (i_size_read(inode) == new_i_size)
  		goto out;
	BUG_ON(new_i_size < i_size_read(inode));

	clusters_to_add = ocfs2_clusters_for_bytes(inode->i_sb, new_i_size) - 
		OCFS2_I(inode)->ip_clusters;

	/* 
	 * protect the pages that ocfs2_zero_extend is going to be
	 * pulling into the page cache.. we do this before the
	 * metadata extend so that we don't get into the situation
	 * where we've extended the metadata but can't get the data
	 * lock to zero.
	 */
	ret = ocfs2_data_lock(inode, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	if (clusters_to_add) {
		ret = ocfs2_extend_allocation(inode, clusters_to_add);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_unlock;
		}
	}

	/*
	 * Call this even if we don't add any clusters to the tree. We
	 * still need to zero the area between the old i_size and the
	 * new i_size.
	 */
	ret = ocfs2_zero_extend(inode, (u64)new_i_size - tail_to_skip);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_unlock;
	}

	if (!tail_to_skip) {
		/* We're being called from ocfs2_setattr() which wants
		 * us to update i_size */
		ret = ocfs2_simple_size_update(inode, di_bh, new_i_size);
		if (ret < 0)
			mlog_errno(ret);
	}

out_unlock:
	ocfs2_data_unlock(inode, 1);

out:
	return ret;
}

int ocfs2_setattr(struct dentry *dentry, struct iattr *attr)
{
	int status = 0, size_change;
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct buffer_head *bh = NULL;
	struct ocfs2_journal_handle *handle = NULL;

	mlog_entry("(0x%p, '%.*s')\n", dentry,
	           dentry->d_name.len, dentry->d_name.name);

	if (attr->ia_valid & ATTR_MODE)
		mlog(0, "mode change: %d\n", attr->ia_mode);
	if (attr->ia_valid & ATTR_UID)
		mlog(0, "uid change: %d\n", attr->ia_uid);
	if (attr->ia_valid & ATTR_GID)
		mlog(0, "gid change: %d\n", attr->ia_gid);
	if (attr->ia_valid & ATTR_SIZE)
		mlog(0, "size change...\n");
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME))
		mlog(0, "time change...\n");

#define OCFS2_VALID_ATTRS (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_SIZE \
			   | ATTR_GID | ATTR_UID | ATTR_MODE)
	if (!(attr->ia_valid & OCFS2_VALID_ATTRS)) {
		mlog(0, "can't handle attrs: 0x%x\n", attr->ia_valid);
		return 0;
	}

	status = inode_change_ok(inode, attr);
	if (status)
		return status;

	size_change = S_ISREG(inode->i_mode) && attr->ia_valid & ATTR_SIZE;
	if (size_change) {
		status = ocfs2_rw_lock(inode, 1);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = ocfs2_meta_lock(inode, NULL, &bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail_unlock_rw;
	}

	if (size_change && attr->ia_size != i_size_read(inode)) {
		if (i_size_read(inode) > attr->ia_size)
			status = ocfs2_truncate_file(inode, bh, attr->ia_size);
		else
			status = ocfs2_extend_file(inode, bh, attr->ia_size, 0);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			status = -ENOSPC;
			goto bail_unlock;
		}
	}

	handle = ocfs2_start_trans(osb, NULL, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto bail_unlock;
	}

	status = inode_setattr(inode, attr);
	if (status < 0) {
		mlog_errno(status);
		goto bail_commit;
	}

	status = ocfs2_mark_inode_dirty(handle, inode, bh);
	if (status < 0)
		mlog_errno(status);

bail_commit:
	ocfs2_commit_trans(handle);
bail_unlock:
	ocfs2_meta_unlock(inode, 1);
bail_unlock_rw:
	if (size_change)
		ocfs2_rw_unlock(inode, 1);
bail:
	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

int ocfs2_getattr(struct vfsmount *mnt,
		  struct dentry *dentry,
		  struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dentry->d_inode->i_sb;
	struct ocfs2_super *osb = sb->s_fs_info;
	int err;

	mlog_entry_void();

	err = ocfs2_inode_revalidate(dentry);
	if (err) {
		if (err != -ENOENT)
			mlog_errno(err);
		goto bail;
	}

	generic_fillattr(inode, stat);

	/* We set the blksize from the cluster size for performance */
	stat->blksize = osb->s_clustersize;

bail:
	mlog_exit(err);

	return err;
}

static int ocfs2_write_remove_suid(struct inode *inode)
{
	int ret;
	struct buffer_head *bh = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_journal_handle *handle;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di;

	mlog_entry("(Inode %llu, mode 0%o)\n",
		   (unsigned long long)oi->ip_blkno, inode->i_mode);

	handle = ocfs2_start_trans(osb, NULL, OCFS2_INODE_UPDATE_CREDITS);
	if (handle == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_block(osb, oi->ip_blkno, &bh, OCFS2_BH_CACHED, inode);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_trans;
	}

	ret = ocfs2_journal_access(handle, inode, bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_bh;
	}

	inode->i_mode &= ~S_ISUID;
	if ((inode->i_mode & S_ISGID) && (inode->i_mode & S_IXGRP))
		inode->i_mode &= ~S_ISGID;

	di = (struct ocfs2_dinode *) bh->b_data;
	di->i_mode = cpu_to_le16(inode->i_mode);

	ret = ocfs2_journal_dirty(handle, bh);
	if (ret < 0)
		mlog_errno(ret);
out_bh:
	brelse(bh);
out_trans:
	ocfs2_commit_trans(handle);
out:
	mlog_exit(ret);
	return ret;
}

static inline int ocfs2_write_should_remove_suid(struct inode *inode)
{
	mode_t mode = inode->i_mode;

	if (!capable(CAP_FSETID)) {
		if (unlikely(mode & S_ISUID))
			return 1;

		if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
			return 1;
	}
	return 0;
}

static ssize_t ocfs2_file_aio_write(struct kiocb *iocb,
				    const struct iovec *iov,
				    unsigned long nr_segs,
				    loff_t pos)
{
	int ret, rw_level = -1, meta_level = -1, have_alloc_sem = 0;
	u32 clusters;
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_dentry->d_inode;
	loff_t newsize, saved_pos;

	mlog_entry("(0x%p, %u, '%.*s')\n", filp,
		   (unsigned int)nr_segs,
		   filp->f_dentry->d_name.len,
		   filp->f_dentry->d_name.name);

	/* happy write of zero bytes */
	if (iocb->ki_left == 0)
		return 0;

	if (!inode) {
		mlog(0, "bad inode\n");
		return -EIO;
	}

	mutex_lock(&inode->i_mutex);
	/* to match setattr's i_mutex -> i_alloc_sem -> rw_lock ordering */
	if (filp->f_flags & O_DIRECT) {
		have_alloc_sem = 1;
		down_read(&inode->i_alloc_sem);
	}

	/* concurrent O_DIRECT writes are allowed */
	rw_level = (filp->f_flags & O_DIRECT) ? 0 : 1;
	ret = ocfs2_rw_lock(inode, rw_level);
	if (ret < 0) {
		rw_level = -1;
		mlog_errno(ret);
		goto out;
	}

	/* 
	 * We sample i_size under a read level meta lock to see if our write
	 * is extending the file, if it is we back off and get a write level
	 * meta lock.
	 */
	meta_level = (filp->f_flags & O_APPEND) ? 1 : 0;
	for(;;) {
		ret = ocfs2_meta_lock(inode, NULL, NULL, meta_level);
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
		 * proceeed, this will lead us to recursively lock the
		 * inode. There's also the dinode i_size state which
		 * can be lost via setattr during extending writes (we
		 * set inode->i_size at the end of a write. */
		if (ocfs2_write_should_remove_suid(inode)) {
			if (meta_level == 0) {
				ocfs2_meta_unlock(inode, meta_level);
				meta_level = 1;
				continue;
			}

			ret = ocfs2_write_remove_suid(inode);
			if (ret < 0) {
				mlog_errno(ret);
				goto out;
			}
		}

		/* work on a copy of ppos until we're sure that we won't have
		 * to recalculate it due to relocking. */
		if (filp->f_flags & O_APPEND) {
			saved_pos = i_size_read(inode);
			mlog(0, "O_APPEND: inode->i_size=%llu\n", saved_pos);
		} else {
			saved_pos = iocb->ki_pos;
		}
		newsize = iocb->ki_left + saved_pos;

		mlog(0, "pos=%lld newsize=%lld cursize=%lld\n",
		     (long long) saved_pos, (long long) newsize,
		     (long long) i_size_read(inode));

		/* No need for a higher level metadata lock if we're
		 * never going past i_size. */
		if (newsize <= i_size_read(inode))
			break;

		if (meta_level == 0) {
			ocfs2_meta_unlock(inode, meta_level);
			meta_level = 1;
			continue;
		}

		spin_lock(&OCFS2_I(inode)->ip_lock);
		clusters = ocfs2_clusters_for_bytes(inode->i_sb, newsize) -
			OCFS2_I(inode)->ip_clusters;
		spin_unlock(&OCFS2_I(inode)->ip_lock);

		mlog(0, "Writing at EOF, may need more allocation: "
		     "i_size = %lld, newsize = %lld, need %u clusters\n",
		     (long long) i_size_read(inode), (long long) newsize,
		     clusters);

		/* We only want to continue the rest of this loop if
		 * our extend will actually require more
		 * allocation. */
		if (!clusters)
			break;

		ret = ocfs2_extend_file(inode, NULL, newsize, iocb->ki_left);
		if (ret < 0) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}
		break;
	}

	/* ok, we're done with i_size and alloc work */
	iocb->ki_pos = saved_pos;
	ocfs2_meta_unlock(inode, meta_level);
	meta_level = -1;

	/* communicate with ocfs2_dio_end_io */
	ocfs2_iocb_set_rw_locked(iocb);

	ret = generic_file_aio_write_nolock(iocb, iov, nr_segs, iocb->ki_pos);

	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(ret == -EIOCBQUEUED && !(filp->f_flags & O_DIRECT));

	/* 
	 * deep in g_f_a_w_n()->ocfs2_direct_IO we pass in a ocfs2_dio_end_io
	 * function pointer which is called when o_direct io completes so that
	 * it can unlock our rw lock.  (it's the clustered equivalent of
	 * i_alloc_sem; protects truncate from racing with pending ios).
	 * Unfortunately there are error cases which call end_io and others
	 * that don't.  so we don't have to unlock the rw_lock if either an
	 * async dio is going to do it in the future or an end_io after an
	 * error has already done it.
	 */
	if (ret == -EIOCBQUEUED || !ocfs2_iocb_is_rw_locked(iocb)) {
		rw_level = -1;
		have_alloc_sem = 0;
	}

out:
	if (meta_level != -1)
		ocfs2_meta_unlock(inode, meta_level);
	if (have_alloc_sem)
		up_read(&inode->i_alloc_sem);
	if (rw_level != -1) 
		ocfs2_rw_unlock(inode, rw_level);
	mutex_unlock(&inode->i_mutex);

	mlog_exit(ret);
	return ret;
}

static ssize_t ocfs2_file_aio_read(struct kiocb *iocb,
				   const struct iovec *iov,
				   unsigned long nr_segs,
				   loff_t pos)
{
	int ret = 0, rw_level = -1, have_alloc_sem = 0;
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_dentry->d_inode;

	mlog_entry("(0x%p, %u, '%.*s')\n", filp,
		   (unsigned int)nr_segs,
		   filp->f_dentry->d_name.len,
		   filp->f_dentry->d_name.name);

	if (!inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto bail;
	}

	/* 
	 * buffered reads protect themselves in ->readpage().  O_DIRECT reads
	 * need locks to protect pending reads from racing with truncate.
	 */
	if (filp->f_flags & O_DIRECT) {
		down_read(&inode->i_alloc_sem);
		have_alloc_sem = 1;

		ret = ocfs2_rw_lock(inode, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto bail;
		}
		rw_level = 0;
		/* communicate with ocfs2_dio_end_io */
		ocfs2_iocb_set_rw_locked(iocb);
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
	ret = ocfs2_meta_lock(inode, NULL, NULL, 0);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail;
	}
	ocfs2_meta_unlock(inode, 0);

	ret = generic_file_aio_read(iocb, iov, nr_segs, iocb->ki_pos);
	if (ret == -EINVAL)
		mlog(ML_ERROR, "generic_file_aio_read returned -EINVAL\n");

	/* buffered aio wouldn't have proper lock coverage today */
	BUG_ON(ret == -EIOCBQUEUED && !(filp->f_flags & O_DIRECT));

	/* see ocfs2_file_aio_write */
	if (ret == -EIOCBQUEUED || !ocfs2_iocb_is_rw_locked(iocb)) {
		rw_level = -1;
		have_alloc_sem = 0;
	}

bail:
	if (have_alloc_sem)
		up_read(&inode->i_alloc_sem);
	if (rw_level != -1) 
		ocfs2_rw_unlock(inode, rw_level);
	mlog_exit(ret);

	return ret;
}

struct inode_operations ocfs2_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
};

struct inode_operations ocfs2_special_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
};

const struct file_operations ocfs2_fops = {
	.read		= do_sync_read,
	.write		= do_sync_write,
	.sendfile	= generic_file_sendfile,
	.mmap		= ocfs2_mmap,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_file_release,
	.open		= ocfs2_file_open,
	.aio_read	= ocfs2_file_aio_read,
	.aio_write	= ocfs2_file_aio_write,
	.ioctl		= ocfs2_ioctl,
};

const struct file_operations ocfs2_dops = {
	.read		= generic_read_dir,
	.readdir	= ocfs2_readdir,
	.fsync		= ocfs2_sync_file,
	.ioctl		= ocfs2_ioctl,
};
