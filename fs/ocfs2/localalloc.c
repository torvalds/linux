/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * localalloc.c
 *
 * Node local data allocation
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

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/bitops.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "blockcheck.h"
#include "dlmglue.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "suballoc.h"
#include "super.h"
#include "sysfile.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

#define OCFS2_LOCAL_ALLOC(dinode)	(&((dinode)->id2.i_lab))

static u32 ocfs2_local_alloc_count_bits(struct ocfs2_dinode *alloc);

static int ocfs2_local_alloc_find_clear_bits(struct ocfs2_super *osb,
					     struct ocfs2_dinode *alloc,
					     u32 *numbits,
					     struct ocfs2_alloc_reservation *resv);

static void ocfs2_clear_local_alloc(struct ocfs2_dinode *alloc);

static int ocfs2_sync_local_to_main(struct ocfs2_super *osb,
				    handle_t *handle,
				    struct ocfs2_dinode *alloc,
				    struct inode *main_bm_inode,
				    struct buffer_head *main_bm_bh);

static int ocfs2_local_alloc_reserve_for_window(struct ocfs2_super *osb,
						struct ocfs2_alloc_context **ac,
						struct inode **bitmap_inode,
						struct buffer_head **bitmap_bh);

static int ocfs2_local_alloc_new_window(struct ocfs2_super *osb,
					handle_t *handle,
					struct ocfs2_alloc_context *ac);

static int ocfs2_local_alloc_slide_window(struct ocfs2_super *osb,
					  struct inode *local_alloc_inode);

/*
 * ocfs2_la_default_mb() - determine a default size, in megabytes of
 * the local alloc.
 *
 * Generally, we'd like to pick as large a local alloc as
 * possible. Performance on large workloads tends to scale
 * proportionally to la size. In addition to that, the reservations
 * code functions more efficiently as it can reserve more windows for
 * write.
 *
 * Some things work against us when trying to choose a large local alloc:
 *
 * - We need to ensure our sizing is picked to leave enough space in
 *   group descriptors for other allocations (such as block groups,
 *   etc). Picking default sizes which are a multiple of 4 could help
 *   - block groups are allocated in 2mb and 4mb chunks.
 *
 * - Likewise, we don't want to starve other nodes of bits on small
 *   file systems. This can easily be taken care of by limiting our
 *   default to a reasonable size (256M) on larger cluster sizes.
 *
 * - Some file systems can't support very large sizes - 4k and 8k in
 *   particular are limited to less than 128 and 256 megabytes respectively.
 *
 * The following reference table shows group descriptor and local
 * alloc maximums at various cluster sizes (4k blocksize)
 *
 * csize: 4K	group: 126M	la: 121M
 * csize: 8K	group: 252M	la: 243M
 * csize: 16K	group: 504M	la: 486M
 * csize: 32K	group: 1008M	la: 972M
 * csize: 64K	group: 2016M	la: 1944M
 * csize: 128K	group: 4032M	la: 3888M
 * csize: 256K	group: 8064M	la: 7776M
 * csize: 512K	group: 16128M	la: 15552M
 * csize: 1024K	group: 32256M	la: 31104M
 */
#define	OCFS2_LA_MAX_DEFAULT_MB	256
#define	OCFS2_LA_OLD_DEFAULT	8
unsigned int ocfs2_la_default_mb(struct ocfs2_super *osb)
{
	unsigned int la_mb;
	unsigned int gd_mb;
	unsigned int la_max_mb;
	unsigned int megs_per_slot;
	struct super_block *sb = osb->sb;

	gd_mb = ocfs2_clusters_to_megabytes(osb->sb,
		8 * ocfs2_group_bitmap_size(sb, 0, osb->s_feature_incompat));

	/*
	 * This takes care of files systems with very small group
	 * descriptors - 512 byte blocksize at cluster sizes lower
	 * than 16K and also 1k blocksize with 4k cluster size.
	 */
	if ((sb->s_blocksize == 512 && osb->s_clustersize <= 8192)
	    || (sb->s_blocksize == 1024 && osb->s_clustersize == 4096))
		return OCFS2_LA_OLD_DEFAULT;

	/*
	 * Leave enough room for some block groups and make the final
	 * value we work from a multiple of 4.
	 */
	gd_mb -= 16;
	gd_mb &= 0xFFFFFFFB;

	la_mb = gd_mb;

	/*
	 * Keep window sizes down to a reasonable default
	 */
	if (la_mb > OCFS2_LA_MAX_DEFAULT_MB) {
		/*
		 * Some clustersize / blocksize combinations will have
		 * given us a larger than OCFS2_LA_MAX_DEFAULT_MB
		 * default size, but get poor distribution when
		 * limited to exactly 256 megabytes.
		 *
		 * As an example, 16K clustersize at 4K blocksize
		 * gives us a cluster group size of 504M. Paring the
		 * local alloc size down to 256 however, would give us
		 * only one window and around 200MB left in the
		 * cluster group. Instead, find the first size below
		 * 256 which would give us an even distribution.
		 *
		 * Larger cluster group sizes actually work out pretty
		 * well when pared to 256, so we don't have to do this
		 * for any group that fits more than two
		 * OCFS2_LA_MAX_DEFAULT_MB windows.
		 */
		if (gd_mb > (2 * OCFS2_LA_MAX_DEFAULT_MB))
			la_mb = 256;
		else {
			unsigned int gd_mult = gd_mb;

			while (gd_mult > 256)
				gd_mult = gd_mult >> 1;

			la_mb = gd_mult;
		}
	}

	megs_per_slot = osb->osb_clusters_at_boot / osb->max_slots;
	megs_per_slot = ocfs2_clusters_to_megabytes(osb->sb, megs_per_slot);
	/* Too many nodes, too few disk clusters. */
	if (megs_per_slot < la_mb)
		la_mb = megs_per_slot;

	/* We can't store more bits than we can in a block. */
	la_max_mb = ocfs2_clusters_to_megabytes(osb->sb,
						ocfs2_local_alloc_size(sb) * 8);
	if (la_mb > la_max_mb)
		la_mb = la_max_mb;

	return la_mb;
}

void ocfs2_la_set_sizes(struct ocfs2_super *osb, int requested_mb)
{
	struct super_block *sb = osb->sb;
	unsigned int la_default_mb = ocfs2_la_default_mb(osb);
	unsigned int la_max_mb;

	la_max_mb = ocfs2_clusters_to_megabytes(sb,
						ocfs2_local_alloc_size(sb) * 8);

	trace_ocfs2_la_set_sizes(requested_mb, la_max_mb, la_default_mb);

	if (requested_mb == -1) {
		/* No user request - use defaults */
		osb->local_alloc_default_bits =
			ocfs2_megabytes_to_clusters(sb, la_default_mb);
	} else if (requested_mb > la_max_mb) {
		/* Request is too big, we give the maximum available */
		osb->local_alloc_default_bits =
			ocfs2_megabytes_to_clusters(sb, la_max_mb);
	} else {
		osb->local_alloc_default_bits =
			ocfs2_megabytes_to_clusters(sb, requested_mb);
	}

	osb->local_alloc_bits = osb->local_alloc_default_bits;
}

static inline int ocfs2_la_state_enabled(struct ocfs2_super *osb)
{
	return (osb->local_alloc_state == OCFS2_LA_THROTTLED ||
		osb->local_alloc_state == OCFS2_LA_ENABLED);
}

void ocfs2_local_alloc_seen_free_bits(struct ocfs2_super *osb,
				      unsigned int num_clusters)
{
	spin_lock(&osb->osb_lock);
	if (osb->local_alloc_state == OCFS2_LA_DISABLED ||
	    osb->local_alloc_state == OCFS2_LA_THROTTLED)
		if (num_clusters >= osb->local_alloc_default_bits) {
			cancel_delayed_work(&osb->la_enable_wq);
			osb->local_alloc_state = OCFS2_LA_ENABLED;
		}
	spin_unlock(&osb->osb_lock);
}

void ocfs2_la_enable_worker(struct work_struct *work)
{
	struct ocfs2_super *osb =
		container_of(work, struct ocfs2_super,
			     la_enable_wq.work);
	spin_lock(&osb->osb_lock);
	osb->local_alloc_state = OCFS2_LA_ENABLED;
	spin_unlock(&osb->osb_lock);
}

/*
 * Tell us whether a given allocation should use the local alloc
 * file. Otherwise, it has to go to the main bitmap.
 *
 * This function does semi-dirty reads of local alloc size and state!
 * This is ok however, as the values are re-checked once under mutex.
 */
int ocfs2_alloc_should_use_local(struct ocfs2_super *osb, u64 bits)
{
	int ret = 0;
	int la_bits;

	spin_lock(&osb->osb_lock);
	la_bits = osb->local_alloc_bits;

	if (!ocfs2_la_state_enabled(osb))
		goto bail;

	/* la_bits should be at least twice the size (in clusters) of
	 * a new block group. We want to be sure block group
	 * allocations go through the local alloc, so allow an
	 * allocation to take up to half the bitmap. */
	if (bits > (la_bits / 2))
		goto bail;

	ret = 1;
bail:
	trace_ocfs2_alloc_should_use_local(
	     (unsigned long long)bits, osb->local_alloc_state, la_bits, ret);
	spin_unlock(&osb->osb_lock);
	return ret;
}

int ocfs2_load_local_alloc(struct ocfs2_super *osb)
{
	int status = 0;
	struct ocfs2_dinode *alloc = NULL;
	struct buffer_head *alloc_bh = NULL;
	u32 num_used;
	struct inode *inode = NULL;
	struct ocfs2_local_alloc *la;

	if (osb->local_alloc_bits == 0)
		goto bail;

	if (osb->local_alloc_bits >= osb->bitmap_cpg) {
		mlog(ML_NOTICE, "Requested local alloc window %d is larger "
		     "than max possible %u. Using defaults.\n",
		     osb->local_alloc_bits, (osb->bitmap_cpg - 1));
		osb->local_alloc_bits =
			ocfs2_megabytes_to_clusters(osb->sb,
						    ocfs2_la_default_mb(osb));
	}

	/* read the alloc off disk */
	inode = ocfs2_get_system_file_inode(osb, LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_read_inode_block_full(inode, &alloc_bh,
					     OCFS2_BH_IGNORE_CACHE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	alloc = (struct ocfs2_dinode *) alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	if (!(le32_to_cpu(alloc->i_flags) &
	    (OCFS2_LOCAL_ALLOC_FL|OCFS2_BITMAP_FL))) {
		mlog(ML_ERROR, "Invalid local alloc inode, %llu\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		status = -EINVAL;
		goto bail;
	}

	if ((la->la_size == 0) ||
	    (le16_to_cpu(la->la_size) > ocfs2_local_alloc_size(inode->i_sb))) {
		mlog(ML_ERROR, "Local alloc size is invalid (la_size = %u)\n",
		     le16_to_cpu(la->la_size));
		status = -EINVAL;
		goto bail;
	}

	/* do a little verification. */
	num_used = ocfs2_local_alloc_count_bits(alloc);

	/* hopefully the local alloc has always been recovered before
	 * we load it. */
	if (num_used
	    || alloc->id1.bitmap1.i_used
	    || alloc->id1.bitmap1.i_total
	    || la->la_bm_off)
		mlog(ML_ERROR, "Local alloc hasn't been recovered!\n"
		     "found = %u, set = %u, taken = %u, off = %u\n",
		     num_used, le32_to_cpu(alloc->id1.bitmap1.i_used),
		     le32_to_cpu(alloc->id1.bitmap1.i_total),
		     OCFS2_LOCAL_ALLOC(alloc)->la_bm_off);

	osb->local_alloc_bh = alloc_bh;
	osb->local_alloc_state = OCFS2_LA_ENABLED;

bail:
	if (status < 0)
		brelse(alloc_bh);
	if (inode)
		iput(inode);

	trace_ocfs2_load_local_alloc(osb->local_alloc_bits);

	if (status)
		mlog_errno(status);
	return status;
}

/*
 * return any unused bits to the bitmap and write out a clean
 * local_alloc.
 *
 * local_alloc_bh is optional. If not passed, we will simply use the
 * one off osb. If you do pass it however, be warned that it *will* be
 * returned brelse'd and NULL'd out.*/
void ocfs2_shutdown_local_alloc(struct ocfs2_super *osb)
{
	int status;
	handle_t *handle;
	struct inode *local_alloc_inode = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;
	struct ocfs2_dinode *alloc_copy = NULL;
	struct ocfs2_dinode *alloc = NULL;

	cancel_delayed_work(&osb->la_enable_wq);
	flush_workqueue(ocfs2_wq);

	if (osb->local_alloc_state == OCFS2_LA_UNUSED)
		goto out;

	local_alloc_inode =
		ocfs2_get_system_file_inode(osb,
					    LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!local_alloc_inode) {
		status = -ENOENT;
		mlog_errno(status);
		goto out;
	}

	osb->local_alloc_state = OCFS2_LA_DISABLED;

	ocfs2_resmap_uninit(&osb->osb_la_resmap);

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto out;
	}

	mutex_lock(&main_bm_inode->i_mutex);

	status = ocfs2_inode_lock(main_bm_inode, &main_bm_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto out_mutex;
	}

	/* WINDOW_MOVE_CREDITS is a bit heavy... */
	handle = ocfs2_start_trans(osb, OCFS2_WINDOW_MOVE_CREDITS);
	if (IS_ERR(handle)) {
		mlog_errno(PTR_ERR(handle));
		handle = NULL;
		goto out_unlock;
	}

	bh = osb->local_alloc_bh;
	alloc = (struct ocfs2_dinode *) bh->b_data;

	alloc_copy = kmalloc(bh->b_size, GFP_NOFS);
	if (!alloc_copy) {
		status = -ENOMEM;
		goto out_commit;
	}
	memcpy(alloc_copy, alloc, bh->b_size);

	status = ocfs2_journal_access_di(handle, INODE_CACHE(local_alloc_inode),
					 bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_commit;
	}

	ocfs2_clear_local_alloc(alloc);
	ocfs2_journal_dirty(handle, bh);

	brelse(bh);
	osb->local_alloc_bh = NULL;
	osb->local_alloc_state = OCFS2_LA_UNUSED;

	status = ocfs2_sync_local_to_main(osb, handle, alloc_copy,
					  main_bm_inode, main_bm_bh);
	if (status < 0)
		mlog_errno(status);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_unlock:
	brelse(main_bm_bh);

	ocfs2_inode_unlock(main_bm_inode, 1);

out_mutex:
	mutex_unlock(&main_bm_inode->i_mutex);
	iput(main_bm_inode);

out:
	if (local_alloc_inode)
		iput(local_alloc_inode);

	kfree(alloc_copy);
}

/*
 * We want to free the bitmap bits outside of any recovery context as
 * we'll need a cluster lock to do so, but we must clear the local
 * alloc before giving up the recovered nodes journal. To solve this,
 * we kmalloc a copy of the local alloc before it's change for the
 * caller to process with ocfs2_complete_local_alloc_recovery
 */
int ocfs2_begin_local_alloc_recovery(struct ocfs2_super *osb,
				     int slot_num,
				     struct ocfs2_dinode **alloc_copy)
{
	int status = 0;
	struct buffer_head *alloc_bh = NULL;
	struct inode *inode = NULL;
	struct ocfs2_dinode *alloc;

	trace_ocfs2_begin_local_alloc_recovery(slot_num);

	*alloc_copy = NULL;

	inode = ocfs2_get_system_file_inode(osb,
					    LOCAL_ALLOC_SYSTEM_INODE,
					    slot_num);
	if (!inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	mutex_lock(&inode->i_mutex);

	status = ocfs2_read_inode_block_full(inode, &alloc_bh,
					     OCFS2_BH_IGNORE_CACHE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*alloc_copy = kmalloc(alloc_bh->b_size, GFP_KERNEL);
	if (!(*alloc_copy)) {
		status = -ENOMEM;
		goto bail;
	}
	memcpy((*alloc_copy), alloc_bh->b_data, alloc_bh->b_size);

	alloc = (struct ocfs2_dinode *) alloc_bh->b_data;
	ocfs2_clear_local_alloc(alloc);

	ocfs2_compute_meta_ecc(osb->sb, alloc_bh->b_data, &alloc->i_check);
	status = ocfs2_write_block(osb, alloc_bh, INODE_CACHE(inode));
	if (status < 0)
		mlog_errno(status);

bail:
	if (status < 0) {
		kfree(*alloc_copy);
		*alloc_copy = NULL;
	}

	brelse(alloc_bh);

	if (inode) {
		mutex_unlock(&inode->i_mutex);
		iput(inode);
	}

	if (status)
		mlog_errno(status);
	return status;
}

/*
 * Step 2: By now, we've completed the journal recovery, we've stamped
 * a clean local alloc on disk and dropped the node out of the
 * recovery map. Dlm locks will no longer stall, so lets clear out the
 * main bitmap.
 */
int ocfs2_complete_local_alloc_recovery(struct ocfs2_super *osb,
					struct ocfs2_dinode *alloc)
{
	int status;
	handle_t *handle;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode;

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto out;
	}

	mutex_lock(&main_bm_inode->i_mutex);

	status = ocfs2_inode_lock(main_bm_inode, &main_bm_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto out_mutex;
	}

	handle = ocfs2_start_trans(osb, OCFS2_WINDOW_MOVE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto out_unlock;
	}

	/* we want the bitmap change to be recorded on disk asap */
	handle->h_sync = 1;

	status = ocfs2_sync_local_to_main(osb, handle, alloc,
					  main_bm_inode, main_bm_bh);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(osb, handle);

out_unlock:
	ocfs2_inode_unlock(main_bm_inode, 1);

out_mutex:
	mutex_unlock(&main_bm_inode->i_mutex);

	brelse(main_bm_bh);

	iput(main_bm_inode);

out:
	if (!status)
		ocfs2_init_steal_slots(osb);
	if (status)
		mlog_errno(status);
	return status;
}

/*
 * make sure we've got at least bits_wanted contiguous bits in the
 * local alloc. You lose them when you drop i_mutex.
 *
 * We will add ourselves to the transaction passed in, but may start
 * our own in order to shift windows.
 */
int ocfs2_reserve_local_alloc_bits(struct ocfs2_super *osb,
				   u32 bits_wanted,
				   struct ocfs2_alloc_context *ac)
{
	int status;
	struct ocfs2_dinode *alloc;
	struct inode *local_alloc_inode;
	unsigned int free_bits;

	BUG_ON(!ac);

	local_alloc_inode =
		ocfs2_get_system_file_inode(osb,
					    LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!local_alloc_inode) {
		status = -ENOENT;
		mlog_errno(status);
		goto bail;
	}

	mutex_lock(&local_alloc_inode->i_mutex);

	/*
	 * We must double check state and allocator bits because
	 * another process may have changed them while holding i_mutex.
	 */
	spin_lock(&osb->osb_lock);
	if (!ocfs2_la_state_enabled(osb) ||
	    (bits_wanted > osb->local_alloc_bits)) {
		spin_unlock(&osb->osb_lock);
		status = -ENOSPC;
		goto bail;
	}
	spin_unlock(&osb->osb_lock);

	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;

#ifdef CONFIG_OCFS2_DEBUG_FS
	if (le32_to_cpu(alloc->id1.bitmap1.i_used) !=
	    ocfs2_local_alloc_count_bits(alloc)) {
		ocfs2_error(osb->sb, "local alloc inode %llu says it has "
			    "%u used bits, but a count shows %u",
			    (unsigned long long)le64_to_cpu(alloc->i_blkno),
			    le32_to_cpu(alloc->id1.bitmap1.i_used),
			    ocfs2_local_alloc_count_bits(alloc));
		status = -EIO;
		goto bail;
	}
#endif

	free_bits = le32_to_cpu(alloc->id1.bitmap1.i_total) -
		le32_to_cpu(alloc->id1.bitmap1.i_used);
	if (bits_wanted > free_bits) {
		/* uhoh, window change time. */
		status =
			ocfs2_local_alloc_slide_window(osb, local_alloc_inode);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto bail;
		}

		/*
		 * Under certain conditions, the window slide code
		 * might have reduced the number of bits available or
		 * disabled the the local alloc entirely. Re-check
		 * here and return -ENOSPC if necessary.
		 */
		status = -ENOSPC;
		if (!ocfs2_la_state_enabled(osb))
			goto bail;

		free_bits = le32_to_cpu(alloc->id1.bitmap1.i_total) -
			le32_to_cpu(alloc->id1.bitmap1.i_used);
		if (bits_wanted > free_bits)
			goto bail;
	}

	ac->ac_inode = local_alloc_inode;
	/* We should never use localalloc from another slot */
	ac->ac_alloc_slot = osb->slot_num;
	ac->ac_which = OCFS2_AC_USE_LOCAL;
	get_bh(osb->local_alloc_bh);
	ac->ac_bh = osb->local_alloc_bh;
	status = 0;
bail:
	if (status < 0 && local_alloc_inode) {
		mutex_unlock(&local_alloc_inode->i_mutex);
		iput(local_alloc_inode);
	}

	trace_ocfs2_reserve_local_alloc_bits(
		(unsigned long long)ac->ac_max_block,
		bits_wanted, osb->slot_num, status);

	if (status)
		mlog_errno(status);
	return status;
}

int ocfs2_claim_local_alloc_bits(struct ocfs2_super *osb,
				 handle_t *handle,
				 struct ocfs2_alloc_context *ac,
				 u32 bits_wanted,
				 u32 *bit_off,
				 u32 *num_bits)
{
	int status, start;
	struct inode *local_alloc_inode;
	void *bitmap;
	struct ocfs2_dinode *alloc;
	struct ocfs2_local_alloc *la;

	BUG_ON(ac->ac_which != OCFS2_AC_USE_LOCAL);

	local_alloc_inode = ac->ac_inode;
	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	start = ocfs2_local_alloc_find_clear_bits(osb, alloc, &bits_wanted,
						  ac->ac_resv);
	if (start == -1) {
		/* TODO: Shouldn't we just BUG here? */
		status = -ENOSPC;
		mlog_errno(status);
		goto bail;
	}

	bitmap = la->la_bitmap;
	*bit_off = le32_to_cpu(la->la_bm_off) + start;
	*num_bits = bits_wanted;

	status = ocfs2_journal_access_di(handle,
					 INODE_CACHE(local_alloc_inode),
					 osb->local_alloc_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_resmap_claimed_bits(&osb->osb_la_resmap, ac->ac_resv, start,
				  bits_wanted);

	while(bits_wanted--)
		ocfs2_set_bit(start++, bitmap);

	le32_add_cpu(&alloc->id1.bitmap1.i_used, *num_bits);
	ocfs2_journal_dirty(handle, osb->local_alloc_bh);

bail:
	if (status)
		mlog_errno(status);
	return status;
}

int ocfs2_free_local_alloc_bits(struct ocfs2_super *osb,
				handle_t *handle,
				struct ocfs2_alloc_context *ac,
				u32 bit_off,
				u32 num_bits)
{
	int status, start;
	u32 clear_bits;
	struct inode *local_alloc_inode;
	void *bitmap;
	struct ocfs2_dinode *alloc;
	struct ocfs2_local_alloc *la;

	BUG_ON(ac->ac_which != OCFS2_AC_USE_LOCAL);

	local_alloc_inode = ac->ac_inode;
	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	bitmap = la->la_bitmap;
	start = bit_off - le32_to_cpu(la->la_bm_off);
	clear_bits = num_bits;

	status = ocfs2_journal_access_di(handle,
			INODE_CACHE(local_alloc_inode),
			osb->local_alloc_bh,
			OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	while (clear_bits--)
		ocfs2_clear_bit(start++, bitmap);

	le32_add_cpu(&alloc->id1.bitmap1.i_used, -num_bits);
	ocfs2_journal_dirty(handle, osb->local_alloc_bh);

bail:
	return status;
}

static u32 ocfs2_local_alloc_count_bits(struct ocfs2_dinode *alloc)
{
	u32 count;
	struct ocfs2_local_alloc *la = OCFS2_LOCAL_ALLOC(alloc);

	count = memweight(la->la_bitmap, le16_to_cpu(la->la_size));

	trace_ocfs2_local_alloc_count_bits(count);
	return count;
}

static int ocfs2_local_alloc_find_clear_bits(struct ocfs2_super *osb,
				     struct ocfs2_dinode *alloc,
				     u32 *numbits,
				     struct ocfs2_alloc_reservation *resv)
{
	int numfound = 0, bitoff, left, startoff, lastzero;
	int local_resv = 0;
	struct ocfs2_alloc_reservation r;
	void *bitmap = NULL;
	struct ocfs2_reservation_map *resmap = &osb->osb_la_resmap;

	if (!alloc->id1.bitmap1.i_total) {
		bitoff = -1;
		goto bail;
	}

	if (!resv) {
		local_resv = 1;
		ocfs2_resv_init_once(&r);
		ocfs2_resv_set_type(&r, OCFS2_RESV_FLAG_TMP);
		resv = &r;
	}

	numfound = *numbits;
	if (ocfs2_resmap_resv_bits(resmap, resv, &bitoff, &numfound) == 0) {
		if (numfound < *numbits)
			*numbits = numfound;
		goto bail;
	}

	/*
	 * Code error. While reservations are enabled, local
	 * allocation should _always_ go through them.
	 */
	BUG_ON(osb->osb_resv_level != 0);

	/*
	 * Reservations are disabled. Handle this the old way.
	 */

	bitmap = OCFS2_LOCAL_ALLOC(alloc)->la_bitmap;

	numfound = bitoff = startoff = 0;
	lastzero = -1;
	left = le32_to_cpu(alloc->id1.bitmap1.i_total);
	while ((bitoff = ocfs2_find_next_zero_bit(bitmap, left, startoff)) != -1) {
		if (bitoff == left) {
			/* mlog(0, "bitoff (%d) == left", bitoff); */
			break;
		}
		/* mlog(0, "Found a zero: bitoff = %d, startoff = %d, "
		   "numfound = %d\n", bitoff, startoff, numfound);*/

		/* Ok, we found a zero bit... is it contig. or do we
		 * start over?*/
		if (bitoff == startoff) {
			/* we found a zero */
			numfound++;
			startoff++;
		} else {
			/* got a zero after some ones */
			numfound = 1;
			startoff = bitoff+1;
		}
		/* we got everything we needed */
		if (numfound == *numbits) {
			/* mlog(0, "Found it all!\n"); */
			break;
		}
	}

	trace_ocfs2_local_alloc_find_clear_bits_search_bitmap(bitoff, numfound);

	if (numfound == *numbits)
		bitoff = startoff - numfound;
	else
		bitoff = -1;

bail:
	if (local_resv)
		ocfs2_resv_discard(resmap, resv);

	trace_ocfs2_local_alloc_find_clear_bits(*numbits,
		le32_to_cpu(alloc->id1.bitmap1.i_total),
		bitoff, numfound);

	return bitoff;
}

static void ocfs2_clear_local_alloc(struct ocfs2_dinode *alloc)
{
	struct ocfs2_local_alloc *la = OCFS2_LOCAL_ALLOC(alloc);
	int i;

	alloc->id1.bitmap1.i_total = 0;
	alloc->id1.bitmap1.i_used = 0;
	la->la_bm_off = 0;
	for(i = 0; i < le16_to_cpu(la->la_size); i++)
		la->la_bitmap[i] = 0;
}

#if 0
/* turn this on and uncomment below to aid debugging window shifts. */
static void ocfs2_verify_zero_bits(unsigned long *bitmap,
				   unsigned int start,
				   unsigned int count)
{
	unsigned int tmp = count;
	while(tmp--) {
		if (ocfs2_test_bit(start + tmp, bitmap)) {
			printk("ocfs2_verify_zero_bits: start = %u, count = "
			       "%u\n", start, count);
			printk("ocfs2_verify_zero_bits: bit %u is set!",
			       start + tmp);
			BUG();
		}
	}
}
#endif

/*
 * sync the local alloc to main bitmap.
 *
 * assumes you've already locked the main bitmap -- the bitmap inode
 * passed is used for caching.
 */
static int ocfs2_sync_local_to_main(struct ocfs2_super *osb,
				    handle_t *handle,
				    struct ocfs2_dinode *alloc,
				    struct inode *main_bm_inode,
				    struct buffer_head *main_bm_bh)
{
	int status = 0;
	int bit_off, left, count, start;
	u64 la_start_blk;
	u64 blkno;
	void *bitmap;
	struct ocfs2_local_alloc *la = OCFS2_LOCAL_ALLOC(alloc);

	trace_ocfs2_sync_local_to_main(
	     le32_to_cpu(alloc->id1.bitmap1.i_total),
	     le32_to_cpu(alloc->id1.bitmap1.i_used));

	if (!alloc->id1.bitmap1.i_total) {
		goto bail;
	}

	if (le32_to_cpu(alloc->id1.bitmap1.i_used) ==
	    le32_to_cpu(alloc->id1.bitmap1.i_total)) {
		goto bail;
	}

	la_start_blk = ocfs2_clusters_to_blocks(osb->sb,
						le32_to_cpu(la->la_bm_off));
	bitmap = la->la_bitmap;
	start = count = bit_off = 0;
	left = le32_to_cpu(alloc->id1.bitmap1.i_total);

	while ((bit_off = ocfs2_find_next_zero_bit(bitmap, left, start))
	       != -1) {
		if ((bit_off < left) && (bit_off == start)) {
			count++;
			start++;
			continue;
		}
		if (count) {
			blkno = la_start_blk +
				ocfs2_clusters_to_blocks(osb->sb,
							 start - count);

			trace_ocfs2_sync_local_to_main_free(
			     count, start - count,
			     (unsigned long long)la_start_blk,
			     (unsigned long long)blkno);

			status = ocfs2_release_clusters(handle,
							main_bm_inode,
							main_bm_bh, blkno,
							count);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}
		if (bit_off >= left)
			break;
		count = 1;
		start = bit_off + 1;
	}

bail:
	if (status)
		mlog_errno(status);
	return status;
}

enum ocfs2_la_event {
	OCFS2_LA_EVENT_SLIDE,		/* Normal window slide. */
	OCFS2_LA_EVENT_FRAGMENTED,	/* The global bitmap has
					 * enough bits theoretically
					 * free, but a contiguous
					 * allocation could not be
					 * found. */
	OCFS2_LA_EVENT_ENOSPC,		/* Global bitmap doesn't have
					 * enough bits free to satisfy
					 * our request. */
};
#define OCFS2_LA_ENABLE_INTERVAL (30 * HZ)
/*
 * Given an event, calculate the size of our next local alloc window.
 *
 * This should always be called under i_mutex of the local alloc inode
 * so that local alloc disabling doesn't race with processes trying to
 * use the allocator.
 *
 * Returns the state which the local alloc was left in. This value can
 * be ignored by some paths.
 */
static int ocfs2_recalc_la_window(struct ocfs2_super *osb,
				  enum ocfs2_la_event event)
{
	unsigned int bits;
	int state;

	spin_lock(&osb->osb_lock);
	if (osb->local_alloc_state == OCFS2_LA_DISABLED) {
		WARN_ON_ONCE(osb->local_alloc_state == OCFS2_LA_DISABLED);
		goto out_unlock;
	}

	/*
	 * ENOSPC and fragmentation are treated similarly for now.
	 */
	if (event == OCFS2_LA_EVENT_ENOSPC ||
	    event == OCFS2_LA_EVENT_FRAGMENTED) {
		/*
		 * We ran out of contiguous space in the primary
		 * bitmap. Drastically reduce the number of bits used
		 * by local alloc until we have to disable it.
		 */
		bits = osb->local_alloc_bits >> 1;
		if (bits > ocfs2_megabytes_to_clusters(osb->sb, 1)) {
			/*
			 * By setting state to THROTTLED, we'll keep
			 * the number of local alloc bits used down
			 * until an event occurs which would give us
			 * reason to assume the bitmap situation might
			 * have changed.
			 */
			osb->local_alloc_state = OCFS2_LA_THROTTLED;
			osb->local_alloc_bits = bits;
		} else {
			osb->local_alloc_state = OCFS2_LA_DISABLED;
		}
		queue_delayed_work(ocfs2_wq, &osb->la_enable_wq,
				   OCFS2_LA_ENABLE_INTERVAL);
		goto out_unlock;
	}

	/*
	 * Don't increase the size of the local alloc window until we
	 * know we might be able to fulfill the request. Otherwise, we
	 * risk bouncing around the global bitmap during periods of
	 * low space.
	 */
	if (osb->local_alloc_state != OCFS2_LA_THROTTLED)
		osb->local_alloc_bits = osb->local_alloc_default_bits;

out_unlock:
	state = osb->local_alloc_state;
	spin_unlock(&osb->osb_lock);

	return state;
}

static int ocfs2_local_alloc_reserve_for_window(struct ocfs2_super *osb,
						struct ocfs2_alloc_context **ac,
						struct inode **bitmap_inode,
						struct buffer_head **bitmap_bh)
{
	int status;

	*ac = kzalloc(sizeof(struct ocfs2_alloc_context), GFP_KERNEL);
	if (!(*ac)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

retry_enospc:
	(*ac)->ac_bits_wanted = osb->local_alloc_bits;
	status = ocfs2_reserve_cluster_bitmap_bits(osb, *ac);
	if (status == -ENOSPC) {
		if (ocfs2_recalc_la_window(osb, OCFS2_LA_EVENT_ENOSPC) ==
		    OCFS2_LA_DISABLED)
			goto bail;

		ocfs2_free_ac_resource(*ac);
		memset(*ac, 0, sizeof(struct ocfs2_alloc_context));
		goto retry_enospc;
	}
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*bitmap_inode = (*ac)->ac_inode;
	igrab(*bitmap_inode);
	*bitmap_bh = (*ac)->ac_bh;
	get_bh(*bitmap_bh);
	status = 0;
bail:
	if ((status < 0) && *ac) {
		ocfs2_free_alloc_context(*ac);
		*ac = NULL;
	}

	if (status)
		mlog_errno(status);
	return status;
}

/*
 * pass it the bitmap lock in lock_bh if you have it.
 */
static int ocfs2_local_alloc_new_window(struct ocfs2_super *osb,
					handle_t *handle,
					struct ocfs2_alloc_context *ac)
{
	int status = 0;
	u32 cluster_off, cluster_count;
	struct ocfs2_dinode *alloc = NULL;
	struct ocfs2_local_alloc *la;

	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	trace_ocfs2_local_alloc_new_window(
		le32_to_cpu(alloc->id1.bitmap1.i_total),
		osb->local_alloc_bits);

	/* Instruct the allocation code to try the most recently used
	 * cluster group. We'll re-record the group used this pass
	 * below. */
	ac->ac_last_group = osb->la_last_gd;

	/* we used the generic suballoc reserve function, but we set
	 * everything up nicely, so there's no reason why we can't use
	 * the more specific cluster api to claim bits. */
	status = ocfs2_claim_clusters(handle, ac, osb->local_alloc_bits,
				      &cluster_off, &cluster_count);
	if (status == -ENOSPC) {
retry_enospc:
		/*
		 * Note: We could also try syncing the journal here to
		 * allow use of any free bits which the current
		 * transaction can't give us access to. --Mark
		 */
		if (ocfs2_recalc_la_window(osb, OCFS2_LA_EVENT_FRAGMENTED) ==
		    OCFS2_LA_DISABLED)
			goto bail;

		ac->ac_bits_wanted = osb->local_alloc_bits;
		status = ocfs2_claim_clusters(handle, ac,
					      osb->local_alloc_bits,
					      &cluster_off,
					      &cluster_count);
		if (status == -ENOSPC)
			goto retry_enospc;
		/*
		 * We only shrunk the *minimum* number of in our
		 * request - it's entirely possible that the allocator
		 * might give us more than we asked for.
		 */
		if (status == 0) {
			spin_lock(&osb->osb_lock);
			osb->local_alloc_bits = cluster_count;
			spin_unlock(&osb->osb_lock);
		}
	}
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	osb->la_last_gd = ac->ac_last_group;

	la->la_bm_off = cpu_to_le32(cluster_off);
	alloc->id1.bitmap1.i_total = cpu_to_le32(cluster_count);
	/* just in case... In the future when we find space ourselves,
	 * we don't have to get all contiguous -- but we'll have to
	 * set all previously used bits in bitmap and update
	 * la_bits_set before setting the bits in the main bitmap. */
	alloc->id1.bitmap1.i_used = 0;
	memset(OCFS2_LOCAL_ALLOC(alloc)->la_bitmap, 0,
	       le16_to_cpu(la->la_size));

	ocfs2_resmap_restart(&osb->osb_la_resmap, cluster_count,
			     OCFS2_LOCAL_ALLOC(alloc)->la_bitmap);

	trace_ocfs2_local_alloc_new_window_result(
		OCFS2_LOCAL_ALLOC(alloc)->la_bm_off,
		le32_to_cpu(alloc->id1.bitmap1.i_total));

bail:
	if (status)
		mlog_errno(status);
	return status;
}

/* Note that we do *NOT* lock the local alloc inode here as
 * it's been locked already for us. */
static int ocfs2_local_alloc_slide_window(struct ocfs2_super *osb,
					  struct inode *local_alloc_inode)
{
	int status = 0;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;
	handle_t *handle = NULL;
	struct ocfs2_dinode *alloc;
	struct ocfs2_dinode *alloc_copy = NULL;
	struct ocfs2_alloc_context *ac = NULL;

	ocfs2_recalc_la_window(osb, OCFS2_LA_EVENT_SLIDE);

	/* This will lock the main bitmap for us. */
	status = ocfs2_local_alloc_reserve_for_window(osb,
						      &ac,
						      &main_bm_inode,
						      &main_bm_bh);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, OCFS2_WINDOW_MOVE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;

	/* We want to clear the local alloc before doing anything
	 * else, so that if we error later during this operation,
	 * local alloc shutdown won't try to double free main bitmap
	 * bits. Make a copy so the sync function knows which bits to
	 * free. */
	alloc_copy = kmalloc(osb->local_alloc_bh->b_size, GFP_NOFS);
	if (!alloc_copy) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}
	memcpy(alloc_copy, alloc, osb->local_alloc_bh->b_size);

	status = ocfs2_journal_access_di(handle,
					 INODE_CACHE(local_alloc_inode),
					 osb->local_alloc_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_clear_local_alloc(alloc);
	ocfs2_journal_dirty(handle, osb->local_alloc_bh);

	status = ocfs2_sync_local_to_main(osb, handle, alloc_copy,
					  main_bm_inode, main_bm_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_local_alloc_new_window(osb, handle, ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	atomic_inc(&osb->alloc_stats.moves);

bail:
	if (handle)
		ocfs2_commit_trans(osb, handle);

	brelse(main_bm_bh);

	if (main_bm_inode)
		iput(main_bm_inode);

	kfree(alloc_copy);

	if (ac)
		ocfs2_free_alloc_context(ac);

	if (status)
		mlog_errno(status);
	return status;
}

