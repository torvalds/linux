/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * suballoc.c
 *
 * metadata alloc and free
 * Inspired by ext3 block groups.
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

#define MLOG_MASK_PREFIX ML_DISK_ALLOC
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "suballoc.h"
#include "super.h"
#include "sysfile.h"
#include "uptodate.h"

#include "buffer_head_io.h"

static inline void ocfs2_debug_bg(struct ocfs2_group_desc *bg);
static inline void ocfs2_debug_suballoc_inode(struct ocfs2_dinode *fe);
static inline u16 ocfs2_find_victim_chain(struct ocfs2_chain_list *cl);
static int ocfs2_block_group_fill(handle_t *handle,
				  struct inode *alloc_inode,
				  struct buffer_head *bg_bh,
				  u64 group_blkno,
				  u16 my_chain,
				  struct ocfs2_chain_list *cl);
static int ocfs2_block_group_alloc(struct ocfs2_super *osb,
				   struct inode *alloc_inode,
				   struct buffer_head *bh);

static int ocfs2_cluster_group_search(struct inode *inode,
				      struct buffer_head *group_bh,
				      u32 bits_wanted, u32 min_bits,
				      u16 *bit_off, u16 *bits_found);
static int ocfs2_block_group_search(struct inode *inode,
				    struct buffer_head *group_bh,
				    u32 bits_wanted, u32 min_bits,
				    u16 *bit_off, u16 *bits_found);
static int ocfs2_claim_suballoc_bits(struct ocfs2_super *osb,
				     struct ocfs2_alloc_context *ac,
				     handle_t *handle,
				     u32 bits_wanted,
				     u32 min_bits,
				     u16 *bit_off,
				     unsigned int *num_bits,
				     u64 *bg_blkno);
static int ocfs2_test_bg_bit_allocatable(struct buffer_head *bg_bh,
					 int nr);
static inline int ocfs2_block_group_set_bits(handle_t *handle,
					     struct inode *alloc_inode,
					     struct ocfs2_group_desc *bg,
					     struct buffer_head *group_bh,
					     unsigned int bit_off,
					     unsigned int num_bits);
static inline int ocfs2_block_group_clear_bits(handle_t *handle,
					       struct inode *alloc_inode,
					       struct ocfs2_group_desc *bg,
					       struct buffer_head *group_bh,
					       unsigned int bit_off,
					       unsigned int num_bits);

static int ocfs2_relink_block_group(handle_t *handle,
				    struct inode *alloc_inode,
				    struct buffer_head *fe_bh,
				    struct buffer_head *bg_bh,
				    struct buffer_head *prev_bg_bh,
				    u16 chain);
static inline int ocfs2_block_group_reasonably_empty(struct ocfs2_group_desc *bg,
						     u32 wanted);
static inline u32 ocfs2_desc_bitmap_to_cluster_off(struct inode *inode,
						   u64 bg_blkno,
						   u16 bg_bit_off);
static inline u64 ocfs2_which_cluster_group(struct inode *inode,
					    u32 cluster);
static inline void ocfs2_block_to_cluster_group(struct inode *inode,
						u64 data_blkno,
						u64 *bg_blkno,
						u16 *bg_bit_off);

void ocfs2_free_alloc_context(struct ocfs2_alloc_context *ac)
{
	struct inode *inode = ac->ac_inode;

	if (inode) {
		if (ac->ac_which != OCFS2_AC_USE_LOCAL)
			ocfs2_meta_unlock(inode, 1);

		mutex_unlock(&inode->i_mutex);

		iput(inode);
	}
	if (ac->ac_bh)
		brelse(ac->ac_bh);
	kfree(ac);
}

static u32 ocfs2_bits_per_group(struct ocfs2_chain_list *cl)
{
	return (u32)le16_to_cpu(cl->cl_cpg) * (u32)le16_to_cpu(cl->cl_bpc);
}

/* somewhat more expensive than our other checks, so use sparingly. */
static int ocfs2_check_group_descriptor(struct super_block *sb,
					struct ocfs2_dinode *di,
					struct ocfs2_group_desc *gd)
{
	unsigned int max_bits;

	if (!OCFS2_IS_VALID_GROUP_DESC(gd)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(sb, gd);
		return -EIO;
	}

	if (di->i_blkno != gd->bg_parent_dinode) {
		ocfs2_error(sb, "Group descriptor # %llu has bad parent "
			    "pointer (%llu, expected %llu)",
			    (unsigned long long)le64_to_cpu(gd->bg_blkno),
			    (unsigned long long)le64_to_cpu(gd->bg_parent_dinode),
			    (unsigned long long)le64_to_cpu(di->i_blkno));
		return -EIO;
	}

	max_bits = le16_to_cpu(di->id2.i_chain.cl_cpg) * le16_to_cpu(di->id2.i_chain.cl_bpc);
	if (le16_to_cpu(gd->bg_bits) > max_bits) {
		ocfs2_error(sb, "Group descriptor # %llu has bit count of %u",
			    (unsigned long long)le64_to_cpu(gd->bg_blkno),
			    le16_to_cpu(gd->bg_bits));
		return -EIO;
	}

	if (le16_to_cpu(gd->bg_chain) >=
	    le16_to_cpu(di->id2.i_chain.cl_next_free_rec)) {
		ocfs2_error(sb, "Group descriptor # %llu has bad chain %u",
			    (unsigned long long)le64_to_cpu(gd->bg_blkno),
			    le16_to_cpu(gd->bg_chain));
		return -EIO;
	}

	if (le16_to_cpu(gd->bg_free_bits_count) > le16_to_cpu(gd->bg_bits)) {
		ocfs2_error(sb, "Group descriptor # %llu has bit count %u but "
			    "claims that %u are free",
			    (unsigned long long)le64_to_cpu(gd->bg_blkno),
			    le16_to_cpu(gd->bg_bits),
			    le16_to_cpu(gd->bg_free_bits_count));
		return -EIO;
	}

	if (le16_to_cpu(gd->bg_bits) > (8 * le16_to_cpu(gd->bg_size))) {
		ocfs2_error(sb, "Group descriptor # %llu has bit count %u but "
			    "max bitmap bits of %u",
			    (unsigned long long)le64_to_cpu(gd->bg_blkno),
			    le16_to_cpu(gd->bg_bits),
			    8 * le16_to_cpu(gd->bg_size));
		return -EIO;
	}

	return 0;
}

static int ocfs2_block_group_fill(handle_t *handle,
				  struct inode *alloc_inode,
				  struct buffer_head *bg_bh,
				  u64 group_blkno,
				  u16 my_chain,
				  struct ocfs2_chain_list *cl)
{
	int status = 0;
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *) bg_bh->b_data;
	struct super_block * sb = alloc_inode->i_sb;

	mlog_entry_void();

	if (((unsigned long long) bg_bh->b_blocknr) != group_blkno) {
		ocfs2_error(alloc_inode->i_sb, "group block (%llu) != "
			    "b_blocknr (%llu)",
			    (unsigned long long)group_blkno,
			    (unsigned long long) bg_bh->b_blocknr);
		status = -EIO;
		goto bail;
	}

	status = ocfs2_journal_access(handle,
				      alloc_inode,
				      bg_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	memset(bg, 0, sb->s_blocksize);
	strcpy(bg->bg_signature, OCFS2_GROUP_DESC_SIGNATURE);
	bg->bg_generation = cpu_to_le32(OCFS2_SB(sb)->fs_generation);
	bg->bg_size = cpu_to_le16(ocfs2_group_bitmap_size(sb));
	bg->bg_bits = cpu_to_le16(ocfs2_bits_per_group(cl));
	bg->bg_chain = cpu_to_le16(my_chain);
	bg->bg_next_group = cl->cl_recs[my_chain].c_blkno;
	bg->bg_parent_dinode = cpu_to_le64(OCFS2_I(alloc_inode)->ip_blkno);
	bg->bg_blkno = cpu_to_le64(group_blkno);
	/* set the 1st bit in the bitmap to account for the descriptor block */
	ocfs2_set_bit(0, (unsigned long *)bg->bg_bitmap);
	bg->bg_free_bits_count = cpu_to_le16(le16_to_cpu(bg->bg_bits) - 1);

	status = ocfs2_journal_dirty(handle, bg_bh);
	if (status < 0)
		mlog_errno(status);

	/* There is no need to zero out or otherwise initialize the
	 * other blocks in a group - All valid FS metadata in a block
	 * group stores the superblock fs_generation value at
	 * allocation time. */

bail:
	mlog_exit(status);
	return status;
}

static inline u16 ocfs2_find_smallest_chain(struct ocfs2_chain_list *cl)
{
	u16 curr, best;

	best = curr = 0;
	while (curr < le16_to_cpu(cl->cl_count)) {
		if (le32_to_cpu(cl->cl_recs[best].c_total) >
		    le32_to_cpu(cl->cl_recs[curr].c_total))
			best = curr;
		curr++;
	}
	return best;
}

/*
 * We expect the block group allocator to already be locked.
 */
static int ocfs2_block_group_alloc(struct ocfs2_super *osb,
				   struct inode *alloc_inode,
				   struct buffer_head *bh)
{
	int status, credits;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) bh->b_data;
	struct ocfs2_chain_list *cl;
	struct ocfs2_alloc_context *ac = NULL;
	handle_t *handle = NULL;
	u32 bit_off, num_bits;
	u16 alloc_rec;
	u64 bg_blkno;
	struct buffer_head *bg_bh = NULL;
	struct ocfs2_group_desc *bg;

	BUG_ON(ocfs2_is_cluster_bitmap(alloc_inode));

	mlog_entry_void();

	cl = &fe->id2.i_chain;
	status = ocfs2_reserve_clusters(osb,
					le16_to_cpu(cl->cl_cpg),
					&ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	credits = ocfs2_calc_group_alloc_credits(osb->sb,
						 le16_to_cpu(cl->cl_cpg));
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_claim_clusters(osb,
				      handle,
				      ac,
				      le16_to_cpu(cl->cl_cpg),
				      &bit_off,
				      &num_bits);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	alloc_rec = ocfs2_find_smallest_chain(cl);

	/* setup the group */
	bg_blkno = ocfs2_clusters_to_blocks(osb->sb, bit_off);
	mlog(0, "new descriptor, record %u, at block %llu\n",
	     alloc_rec, (unsigned long long)bg_blkno);

	bg_bh = sb_getblk(osb->sb, bg_blkno);
	if (!bg_bh) {
		status = -EIO;
		mlog_errno(status);
		goto bail;
	}
	ocfs2_set_new_buffer_uptodate(alloc_inode, bg_bh);

	status = ocfs2_block_group_fill(handle,
					alloc_inode,
					bg_bh,
					bg_blkno,
					alloc_rec,
					cl);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	bg = (struct ocfs2_group_desc *) bg_bh->b_data;

	status = ocfs2_journal_access(handle, alloc_inode,
				      bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	le32_add_cpu(&cl->cl_recs[alloc_rec].c_free,
		     le16_to_cpu(bg->bg_free_bits_count));
	le32_add_cpu(&cl->cl_recs[alloc_rec].c_total, le16_to_cpu(bg->bg_bits));
	cl->cl_recs[alloc_rec].c_blkno  = cpu_to_le64(bg_blkno);
	if (le16_to_cpu(cl->cl_next_free_rec) < le16_to_cpu(cl->cl_count))
		le16_add_cpu(&cl->cl_next_free_rec, 1);

	le32_add_cpu(&fe->id1.bitmap1.i_used, le16_to_cpu(bg->bg_bits) -
					le16_to_cpu(bg->bg_free_bits_count));
	le32_add_cpu(&fe->id1.bitmap1.i_total, le16_to_cpu(bg->bg_bits));
	le32_add_cpu(&fe->i_clusters, le16_to_cpu(cl->cl_cpg));

	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	spin_lock(&OCFS2_I(alloc_inode)->ip_lock);
	OCFS2_I(alloc_inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	fe->i_size = cpu_to_le64(ocfs2_clusters_to_bytes(alloc_inode->i_sb,
					     le32_to_cpu(fe->i_clusters)));
	spin_unlock(&OCFS2_I(alloc_inode)->ip_lock);
	i_size_write(alloc_inode, le64_to_cpu(fe->i_size));
	alloc_inode->i_blocks = ocfs2_inode_sector_count(alloc_inode);

	status = 0;
bail:
	if (handle)
		ocfs2_commit_trans(osb, handle);

	if (ac)
		ocfs2_free_alloc_context(ac);

	if (bg_bh)
		brelse(bg_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_reserve_suballoc_bits(struct ocfs2_super *osb,
				       struct ocfs2_alloc_context *ac,
				       int type,
				       u32 slot)
{
	int status;
	u32 bits_wanted = ac->ac_bits_wanted;
	struct inode *alloc_inode;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *fe;
	u32 free_bits;

	mlog_entry_void();

	alloc_inode = ocfs2_get_system_file_inode(osb, type, slot);
	if (!alloc_inode) {
		mlog_errno(-EINVAL);
		return -EINVAL;
	}

	mutex_lock(&alloc_inode->i_mutex);

	status = ocfs2_meta_lock(alloc_inode, &bh, 1);
	if (status < 0) {
		mutex_unlock(&alloc_inode->i_mutex);
		iput(alloc_inode);

		mlog_errno(status);
		return status;
	}

	ac->ac_inode = alloc_inode;

	fe = (struct ocfs2_dinode *) bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(alloc_inode->i_sb, fe);
		status = -EIO;
		goto bail;
	}
	if (!(fe->i_flags & cpu_to_le32(OCFS2_CHAIN_FL))) {
		ocfs2_error(alloc_inode->i_sb, "Invalid chain allocator %llu",
			    (unsigned long long)le64_to_cpu(fe->i_blkno));
		status = -EIO;
		goto bail;
	}

	free_bits = le32_to_cpu(fe->id1.bitmap1.i_total) -
		le32_to_cpu(fe->id1.bitmap1.i_used);

	if (bits_wanted > free_bits) {
		/* cluster bitmap never grows */
		if (ocfs2_is_cluster_bitmap(alloc_inode)) {
			mlog(0, "Disk Full: wanted=%u, free_bits=%u\n",
			     bits_wanted, free_bits);
			status = -ENOSPC;
			goto bail;
		}

		status = ocfs2_block_group_alloc(osb, alloc_inode, bh);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto bail;
		}
		atomic_inc(&osb->alloc_stats.bg_extends);

		/* You should never ask for this much metadata */
		BUG_ON(bits_wanted >
		       (le32_to_cpu(fe->id1.bitmap1.i_total)
			- le32_to_cpu(fe->id1.bitmap1.i_used)));
	}

	get_bh(bh);
	ac->ac_bh = bh;
bail:
	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

int ocfs2_reserve_new_metadata(struct ocfs2_super *osb,
			       struct ocfs2_dinode *fe,
			       struct ocfs2_alloc_context **ac)
{
	int status;
	u32 slot;

	*ac = kzalloc(sizeof(struct ocfs2_alloc_context), GFP_KERNEL);
	if (!(*ac)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	(*ac)->ac_bits_wanted = ocfs2_extend_meta_needed(fe);
	(*ac)->ac_which = OCFS2_AC_USE_META;
	slot = osb->slot_num;
	(*ac)->ac_group_search = ocfs2_block_group_search;

	status = ocfs2_reserve_suballoc_bits(osb, (*ac),
					     EXTENT_ALLOC_SYSTEM_INODE, slot);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	if ((status < 0) && *ac) {
		ocfs2_free_alloc_context(*ac);
		*ac = NULL;
	}

	mlog_exit(status);
	return status;
}

int ocfs2_reserve_new_inode(struct ocfs2_super *osb,
			    struct ocfs2_alloc_context **ac)
{
	int status;

	*ac = kzalloc(sizeof(struct ocfs2_alloc_context), GFP_KERNEL);
	if (!(*ac)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	(*ac)->ac_bits_wanted = 1;
	(*ac)->ac_which = OCFS2_AC_USE_INODE;

	(*ac)->ac_group_search = ocfs2_block_group_search;

	status = ocfs2_reserve_suballoc_bits(osb, *ac,
					     INODE_ALLOC_SYSTEM_INODE,
					     osb->slot_num);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	if ((status < 0) && *ac) {
		ocfs2_free_alloc_context(*ac);
		*ac = NULL;
	}

	mlog_exit(status);
	return status;
}

/* local alloc code has to do the same thing, so rather than do this
 * twice.. */
int ocfs2_reserve_cluster_bitmap_bits(struct ocfs2_super *osb,
				      struct ocfs2_alloc_context *ac)
{
	int status;

	ac->ac_which = OCFS2_AC_USE_MAIN;
	ac->ac_group_search = ocfs2_cluster_group_search;

	status = ocfs2_reserve_suballoc_bits(osb, ac,
					     GLOBAL_BITMAP_SYSTEM_INODE,
					     OCFS2_INVALID_SLOT);
	if (status < 0 && status != -ENOSPC) {
		mlog_errno(status);
		goto bail;
	}

bail:
	return status;
}

/* Callers don't need to care which bitmap (local alloc or main) to
 * use so we figure it out for them, but unfortunately this clutters
 * things a bit. */
int ocfs2_reserve_clusters(struct ocfs2_super *osb,
			   u32 bits_wanted,
			   struct ocfs2_alloc_context **ac)
{
	int status;

	mlog_entry_void();

	*ac = kzalloc(sizeof(struct ocfs2_alloc_context), GFP_KERNEL);
	if (!(*ac)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	(*ac)->ac_bits_wanted = bits_wanted;

	status = -ENOSPC;
	if (ocfs2_alloc_should_use_local(osb, bits_wanted)) {
		status = ocfs2_reserve_local_alloc_bits(osb,
							bits_wanted,
							*ac);
		if ((status < 0) && (status != -ENOSPC)) {
			mlog_errno(status);
			goto bail;
		} else if (status == -ENOSPC) {
			/* reserve_local_bits will return enospc with
			 * the local alloc inode still locked, so we
			 * can change this safely here. */
			mlog(0, "Disabling local alloc\n");
			/* We set to OCFS2_LA_DISABLED so that umount
			 * can clean up what's left of the local
			 * allocation */
			osb->local_alloc_state = OCFS2_LA_DISABLED;
		}
	}

	if (status == -ENOSPC) {
		status = ocfs2_reserve_cluster_bitmap_bits(osb, *ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto bail;
		}
	}

	status = 0;
bail:
	if ((status < 0) && *ac) {
		ocfs2_free_alloc_context(*ac);
		*ac = NULL;
	}

	mlog_exit(status);
	return status;
}

/*
 * More or less lifted from ext3. I'll leave their description below:
 *
 * "For ext3 allocations, we must not reuse any blocks which are
 * allocated in the bitmap buffer's "last committed data" copy.  This
 * prevents deletes from freeing up the page for reuse until we have
 * committed the delete transaction.
 *
 * If we didn't do this, then deleting something and reallocating it as
 * data would allow the old block to be overwritten before the
 * transaction committed (because we force data to disk before commit).
 * This would lead to corruption if we crashed between overwriting the
 * data and committing the delete.
 *
 * @@@ We may want to make this allocation behaviour conditional on
 * data-writes at some point, and disable it for metadata allocations or
 * sync-data inodes."
 *
 * Note: OCFS2 already does this differently for metadata vs data
 * allocations, as those bitmaps are seperate and undo access is never
 * called on a metadata group descriptor.
 */
static int ocfs2_test_bg_bit_allocatable(struct buffer_head *bg_bh,
					 int nr)
{
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *) bg_bh->b_data;

	if (ocfs2_test_bit(nr, (unsigned long *)bg->bg_bitmap))
		return 0;
	if (!buffer_jbd(bg_bh) || !bh2jh(bg_bh)->b_committed_data)
		return 1;

	bg = (struct ocfs2_group_desc *) bh2jh(bg_bh)->b_committed_data;
	return !ocfs2_test_bit(nr, (unsigned long *)bg->bg_bitmap);
}

static int ocfs2_block_group_find_clear_bits(struct ocfs2_super *osb,
					     struct buffer_head *bg_bh,
					     unsigned int bits_wanted,
					     unsigned int total_bits,
					     u16 *bit_off,
					     u16 *bits_found)
{
	void *bitmap;
	u16 best_offset, best_size;
	int offset, start, found, status = 0;
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *) bg_bh->b_data;

	if (!OCFS2_IS_VALID_GROUP_DESC(bg)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(osb->sb, bg);
		return -EIO;
	}

	found = start = best_offset = best_size = 0;
	bitmap = bg->bg_bitmap;

	while((offset = ocfs2_find_next_zero_bit(bitmap, total_bits, start)) != -1) {
		if (offset == total_bits)
			break;

		if (!ocfs2_test_bg_bit_allocatable(bg_bh, offset)) {
			/* We found a zero, but we can't use it as it
			 * hasn't been put to disk yet! */
			found = 0;
			start = offset + 1;
		} else if (offset == start) {
			/* we found a zero */
			found++;
			/* move start to the next bit to test */
			start++;
		} else {
			/* got a zero after some ones */
			found = 1;
			start = offset + 1;
		}
		if (found > best_size) {
			best_size = found;
			best_offset = start - found;
		}
		/* we got everything we needed */
		if (found == bits_wanted) {
			/* mlog(0, "Found it all!\n"); */
			break;
		}
	}

	/* XXX: I think the first clause is equivalent to the second
	 * 	- jlbec */
	if (found == bits_wanted) {
		*bit_off = start - found;
		*bits_found = found;
	} else if (best_size) {
		*bit_off = best_offset;
		*bits_found = best_size;
	} else {
		status = -ENOSPC;
		/* No error log here -- see the comment above
		 * ocfs2_test_bg_bit_allocatable */
	}

	return status;
}

static inline int ocfs2_block_group_set_bits(handle_t *handle,
					     struct inode *alloc_inode,
					     struct ocfs2_group_desc *bg,
					     struct buffer_head *group_bh,
					     unsigned int bit_off,
					     unsigned int num_bits)
{
	int status;
	void *bitmap = bg->bg_bitmap;
	int journal_type = OCFS2_JOURNAL_ACCESS_WRITE;

	mlog_entry_void();

	if (!OCFS2_IS_VALID_GROUP_DESC(bg)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(alloc_inode->i_sb, bg);
		status = -EIO;
		goto bail;
	}
	BUG_ON(le16_to_cpu(bg->bg_free_bits_count) < num_bits);

	mlog(0, "block_group_set_bits: off = %u, num = %u\n", bit_off,
	     num_bits);

	if (ocfs2_is_cluster_bitmap(alloc_inode))
		journal_type = OCFS2_JOURNAL_ACCESS_UNDO;

	status = ocfs2_journal_access(handle,
				      alloc_inode,
				      group_bh,
				      journal_type);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	le16_add_cpu(&bg->bg_free_bits_count, -num_bits);

	while(num_bits--)
		ocfs2_set_bit(bit_off++, bitmap);

	status = ocfs2_journal_dirty(handle,
				     group_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	mlog_exit(status);
	return status;
}

/* find the one with the most empty bits */
static inline u16 ocfs2_find_victim_chain(struct ocfs2_chain_list *cl)
{
	u16 curr, best;

	BUG_ON(!cl->cl_next_free_rec);

	best = curr = 0;
	while (curr < le16_to_cpu(cl->cl_next_free_rec)) {
		if (le32_to_cpu(cl->cl_recs[curr].c_free) >
		    le32_to_cpu(cl->cl_recs[best].c_free))
			best = curr;
		curr++;
	}

	BUG_ON(best >= le16_to_cpu(cl->cl_next_free_rec));
	return best;
}

static int ocfs2_relink_block_group(handle_t *handle,
				    struct inode *alloc_inode,
				    struct buffer_head *fe_bh,
				    struct buffer_head *bg_bh,
				    struct buffer_head *prev_bg_bh,
				    u16 chain)
{
	int status;
	/* there is a really tiny chance the journal calls could fail,
	 * but we wouldn't want inconsistent blocks in *any* case. */
	u64 fe_ptr, bg_ptr, prev_bg_ptr;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) fe_bh->b_data;
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *) bg_bh->b_data;
	struct ocfs2_group_desc *prev_bg = (struct ocfs2_group_desc *) prev_bg_bh->b_data;

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(alloc_inode->i_sb, fe);
		status = -EIO;
		goto out;
	}
	if (!OCFS2_IS_VALID_GROUP_DESC(bg)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(alloc_inode->i_sb, bg);
		status = -EIO;
		goto out;
	}
	if (!OCFS2_IS_VALID_GROUP_DESC(prev_bg)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(alloc_inode->i_sb, prev_bg);
		status = -EIO;
		goto out;
	}

	mlog(0, "Suballoc %llu, chain %u, move group %llu to top, prev = %llu\n",
	     (unsigned long long)le64_to_cpu(fe->i_blkno), chain,
	     (unsigned long long)le64_to_cpu(bg->bg_blkno),
	     (unsigned long long)le64_to_cpu(prev_bg->bg_blkno));

	fe_ptr = le64_to_cpu(fe->id2.i_chain.cl_recs[chain].c_blkno);
	bg_ptr = le64_to_cpu(bg->bg_next_group);
	prev_bg_ptr = le64_to_cpu(prev_bg->bg_next_group);

	status = ocfs2_journal_access(handle, alloc_inode, prev_bg_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	prev_bg->bg_next_group = bg->bg_next_group;

	status = ocfs2_journal_dirty(handle, prev_bg_bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	status = ocfs2_journal_access(handle, alloc_inode, bg_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	bg->bg_next_group = fe->id2.i_chain.cl_recs[chain].c_blkno;

	status = ocfs2_journal_dirty(handle, bg_bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	status = ocfs2_journal_access(handle, alloc_inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	fe->id2.i_chain.cl_recs[chain].c_blkno = bg->bg_blkno;

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto out_rollback;
	}

	status = 0;
out_rollback:
	if (status < 0) {
		fe->id2.i_chain.cl_recs[chain].c_blkno = cpu_to_le64(fe_ptr);
		bg->bg_next_group = cpu_to_le64(bg_ptr);
		prev_bg->bg_next_group = cpu_to_le64(prev_bg_ptr);
	}
out:
	mlog_exit(status);
	return status;
}

static inline int ocfs2_block_group_reasonably_empty(struct ocfs2_group_desc *bg,
						     u32 wanted)
{
	return le16_to_cpu(bg->bg_free_bits_count) > wanted;
}

/* return 0 on success, -ENOSPC to keep searching and any other < 0
 * value on error. */
static int ocfs2_cluster_group_search(struct inode *inode,
				      struct buffer_head *group_bh,
				      u32 bits_wanted, u32 min_bits,
				      u16 *bit_off, u16 *bits_found)
{
	int search = -ENOSPC;
	int ret;
	struct ocfs2_group_desc *gd = (struct ocfs2_group_desc *) group_bh->b_data;
	u16 tmp_off, tmp_found;
	unsigned int max_bits, gd_cluster_off;

	BUG_ON(!ocfs2_is_cluster_bitmap(inode));

	if (gd->bg_free_bits_count) {
		max_bits = le16_to_cpu(gd->bg_bits);

		/* Tail groups in cluster bitmaps which aren't cpg
		 * aligned are prone to partial extention by a failed
		 * fs resize. If the file system resize never got to
		 * update the dinode cluster count, then we don't want
		 * to trust any clusters past it, regardless of what
		 * the group descriptor says. */
		gd_cluster_off = ocfs2_blocks_to_clusters(inode->i_sb,
							  le64_to_cpu(gd->bg_blkno));
		if ((gd_cluster_off + max_bits) >
		    OCFS2_I(inode)->ip_clusters) {
			max_bits = OCFS2_I(inode)->ip_clusters - gd_cluster_off;
			mlog(0, "Desc %llu, bg_bits %u, clusters %u, use %u\n",
			     (unsigned long long)le64_to_cpu(gd->bg_blkno),
			     le16_to_cpu(gd->bg_bits),
			     OCFS2_I(inode)->ip_clusters, max_bits);
		}

		ret = ocfs2_block_group_find_clear_bits(OCFS2_SB(inode->i_sb),
							group_bh, bits_wanted,
							max_bits,
							&tmp_off, &tmp_found);
		if (ret)
			return ret;

		/* ocfs2_block_group_find_clear_bits() might
		 * return success, but we still want to return
		 * -ENOSPC unless it found the minimum number
		 * of bits. */
		if (min_bits <= tmp_found) {
			*bit_off = tmp_off;
			*bits_found = tmp_found;
			search = 0; /* success */
		}
	}

	return search;
}

static int ocfs2_block_group_search(struct inode *inode,
				    struct buffer_head *group_bh,
				    u32 bits_wanted, u32 min_bits,
				    u16 *bit_off, u16 *bits_found)
{
	int ret = -ENOSPC;
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *) group_bh->b_data;

	BUG_ON(min_bits != 1);
	BUG_ON(ocfs2_is_cluster_bitmap(inode));

	if (bg->bg_free_bits_count)
		ret = ocfs2_block_group_find_clear_bits(OCFS2_SB(inode->i_sb),
							group_bh, bits_wanted,
							le16_to_cpu(bg->bg_bits),
							bit_off, bits_found);

	return ret;
}

static int ocfs2_alloc_dinode_update_counts(struct inode *inode,
				       handle_t *handle,
				       struct buffer_head *di_bh,
				       u32 num_bits,
				       u16 chain)
{
	int ret;
	u32 tmp_used;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *) di_bh->b_data;
	struct ocfs2_chain_list *cl = (struct ocfs2_chain_list *) &di->id2.i_chain;

	ret = ocfs2_journal_access(handle, inode, di_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	tmp_used = le32_to_cpu(di->id1.bitmap1.i_used);
	di->id1.bitmap1.i_used = cpu_to_le32(num_bits + tmp_used);
	le32_add_cpu(&cl->cl_recs[chain].c_free, -num_bits);

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret < 0)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_search_one_group(struct ocfs2_alloc_context *ac,
				  handle_t *handle,
				  u32 bits_wanted,
				  u32 min_bits,
				  u16 *bit_off,
				  unsigned int *num_bits,
				  u64 gd_blkno,
				  u16 *bits_left)
{
	int ret;
	u16 found;
	struct buffer_head *group_bh = NULL;
	struct ocfs2_group_desc *gd;
	struct inode *alloc_inode = ac->ac_inode;

	ret = ocfs2_read_block(OCFS2_SB(alloc_inode->i_sb), gd_blkno,
			       &group_bh, OCFS2_BH_CACHED, alloc_inode);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	gd = (struct ocfs2_group_desc *) group_bh->b_data;
	if (!OCFS2_IS_VALID_GROUP_DESC(gd)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(alloc_inode->i_sb, gd);
		ret = -EIO;
		goto out;
	}

	ret = ac->ac_group_search(alloc_inode, group_bh, bits_wanted, min_bits,
				  bit_off, &found);
	if (ret < 0) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		goto out;
	}

	*num_bits = found;

	ret = ocfs2_alloc_dinode_update_counts(alloc_inode, handle, ac->ac_bh,
					       *num_bits,
					       le16_to_cpu(gd->bg_chain));
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_block_group_set_bits(handle, alloc_inode, gd, group_bh,
					 *bit_off, *num_bits);
	if (ret < 0)
		mlog_errno(ret);

	*bits_left = le16_to_cpu(gd->bg_free_bits_count);

out:
	brelse(group_bh);

	return ret;
}

static int ocfs2_search_chain(struct ocfs2_alloc_context *ac,
			      handle_t *handle,
			      u32 bits_wanted,
			      u32 min_bits,
			      u16 *bit_off,
			      unsigned int *num_bits,
			      u64 *bg_blkno,
			      u16 *bits_left)
{
	int status;
	u16 chain, tmp_bits;
	u32 tmp_used;
	u64 next_group;
	struct inode *alloc_inode = ac->ac_inode;
	struct buffer_head *group_bh = NULL;
	struct buffer_head *prev_group_bh = NULL;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) ac->ac_bh->b_data;
	struct ocfs2_chain_list *cl = (struct ocfs2_chain_list *) &fe->id2.i_chain;
	struct ocfs2_group_desc *bg;

	chain = ac->ac_chain;
	mlog(0, "trying to alloc %u bits from chain %u, inode %llu\n",
	     bits_wanted, chain,
	     (unsigned long long)OCFS2_I(alloc_inode)->ip_blkno);

	status = ocfs2_read_block(OCFS2_SB(alloc_inode->i_sb),
				  le64_to_cpu(cl->cl_recs[chain].c_blkno),
				  &group_bh, OCFS2_BH_CACHED, alloc_inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	bg = (struct ocfs2_group_desc *) group_bh->b_data;
	status = ocfs2_check_group_descriptor(alloc_inode->i_sb, fe, bg);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	status = -ENOSPC;
	/* for now, the chain search is a bit simplistic. We just use
	 * the 1st group with any empty bits. */
	while ((status = ac->ac_group_search(alloc_inode, group_bh,
					     bits_wanted, min_bits, bit_off,
					     &tmp_bits)) == -ENOSPC) {
		if (!bg->bg_next_group)
			break;

		if (prev_group_bh) {
			brelse(prev_group_bh);
			prev_group_bh = NULL;
		}
		next_group = le64_to_cpu(bg->bg_next_group);
		prev_group_bh = group_bh;
		group_bh = NULL;
		status = ocfs2_read_block(OCFS2_SB(alloc_inode->i_sb),
					  next_group, &group_bh,
					  OCFS2_BH_CACHED, alloc_inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		bg = (struct ocfs2_group_desc *) group_bh->b_data;
		status = ocfs2_check_group_descriptor(alloc_inode->i_sb, fe, bg);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	mlog(0, "alloc succeeds: we give %u bits from block group %llu\n",
	     tmp_bits, (unsigned long long)le64_to_cpu(bg->bg_blkno));

	*num_bits = tmp_bits;

	BUG_ON(*num_bits == 0);

	/*
	 * Keep track of previous block descriptor read. When
	 * we find a target, if we have read more than X
	 * number of descriptors, and the target is reasonably
	 * empty, relink him to top of his chain.
	 *
	 * We've read 0 extra blocks and only send one more to
	 * the transaction, yet the next guy to search has a
	 * much easier time.
	 *
	 * Do this *after* figuring out how many bits we're taking out
	 * of our target group.
	 */
	if (ac->ac_allow_chain_relink &&
	    (prev_group_bh) &&
	    (ocfs2_block_group_reasonably_empty(bg, *num_bits))) {
		status = ocfs2_relink_block_group(handle, alloc_inode,
						  ac->ac_bh, group_bh,
						  prev_group_bh, chain);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* Ok, claim our bits now: set the info on dinode, chainlist
	 * and then the group */
	status = ocfs2_journal_access(handle,
				      alloc_inode,
				      ac->ac_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	tmp_used = le32_to_cpu(fe->id1.bitmap1.i_used);
	fe->id1.bitmap1.i_used = cpu_to_le32(*num_bits + tmp_used);
	le32_add_cpu(&cl->cl_recs[chain].c_free, -(*num_bits));

	status = ocfs2_journal_dirty(handle,
				     ac->ac_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_block_group_set_bits(handle,
					    alloc_inode,
					    bg,
					    group_bh,
					    *bit_off,
					    *num_bits);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "Allocated %u bits from suballocator %llu\n", *num_bits,
	     (unsigned long long)le64_to_cpu(fe->i_blkno));

	*bg_blkno = le64_to_cpu(bg->bg_blkno);
	*bits_left = le16_to_cpu(bg->bg_free_bits_count);
bail:
	if (group_bh)
		brelse(group_bh);
	if (prev_group_bh)
		brelse(prev_group_bh);

	mlog_exit(status);
	return status;
}

/* will give out up to bits_wanted contiguous bits. */
static int ocfs2_claim_suballoc_bits(struct ocfs2_super *osb,
				     struct ocfs2_alloc_context *ac,
				     handle_t *handle,
				     u32 bits_wanted,
				     u32 min_bits,
				     u16 *bit_off,
				     unsigned int *num_bits,
				     u64 *bg_blkno)
{
	int status;
	u16 victim, i;
	u16 bits_left = 0;
	u64 hint_blkno = ac->ac_last_group;
	struct ocfs2_chain_list *cl;
	struct ocfs2_dinode *fe;

	mlog_entry_void();

	BUG_ON(ac->ac_bits_given >= ac->ac_bits_wanted);
	BUG_ON(bits_wanted > (ac->ac_bits_wanted - ac->ac_bits_given));
	BUG_ON(!ac->ac_bh);

	fe = (struct ocfs2_dinode *) ac->ac_bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(osb->sb, fe);
		status = -EIO;
		goto bail;
	}
	if (le32_to_cpu(fe->id1.bitmap1.i_used) >=
	    le32_to_cpu(fe->id1.bitmap1.i_total)) {
		ocfs2_error(osb->sb, "Chain allocator dinode %llu has %u used "
			    "bits but only %u total.",
			    (unsigned long long)le64_to_cpu(fe->i_blkno),
			    le32_to_cpu(fe->id1.bitmap1.i_used),
			    le32_to_cpu(fe->id1.bitmap1.i_total));
		status = -EIO;
		goto bail;
	}

	if (hint_blkno) {
		/* Attempt to short-circuit the usual search mechanism
		 * by jumping straight to the most recently used
		 * allocation group. This helps us mantain some
		 * contiguousness across allocations. */
		status = ocfs2_search_one_group(ac, handle, bits_wanted,
						min_bits, bit_off, num_bits,
						hint_blkno, &bits_left);
		if (!status) {
			/* Be careful to update *bg_blkno here as the
			 * caller is expecting it to be filled in, and
			 * ocfs2_search_one_group() won't do that for
			 * us. */
			*bg_blkno = hint_blkno;
			goto set_hint;
		}
		if (status < 0 && status != -ENOSPC) {
			mlog_errno(status);
			goto bail;
		}
	}

	cl = (struct ocfs2_chain_list *) &fe->id2.i_chain;

	victim = ocfs2_find_victim_chain(cl);
	ac->ac_chain = victim;
	ac->ac_allow_chain_relink = 1;

	status = ocfs2_search_chain(ac, handle, bits_wanted, min_bits, bit_off,
				    num_bits, bg_blkno, &bits_left);
	if (!status)
		goto set_hint;
	if (status < 0 && status != -ENOSPC) {
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "Search of victim chain %u came up with nothing, "
	     "trying all chains now.\n", victim);

	/* If we didn't pick a good victim, then just default to
	 * searching each chain in order. Don't allow chain relinking
	 * because we only calculate enough journal credits for one
	 * relink per alloc. */
	ac->ac_allow_chain_relink = 0;
	for (i = 0; i < le16_to_cpu(cl->cl_next_free_rec); i ++) {
		if (i == victim)
			continue;
		if (!cl->cl_recs[i].c_free)
			continue;

		ac->ac_chain = i;
		status = ocfs2_search_chain(ac, handle, bits_wanted, min_bits,
					    bit_off, num_bits, bg_blkno,
					    &bits_left);
		if (!status)
			break;
		if (status < 0 && status != -ENOSPC) {
			mlog_errno(status);
			goto bail;
		}
	}

set_hint:
	if (status != -ENOSPC) {
		/* If the next search of this group is not likely to
		 * yield a suitable extent, then we reset the last
		 * group hint so as to not waste a disk read */
		if (bits_left < min_bits)
			ac->ac_last_group = 0;
		else
			ac->ac_last_group = *bg_blkno;
	}

bail:
	mlog_exit(status);
	return status;
}

int ocfs2_claim_metadata(struct ocfs2_super *osb,
			 handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 bits_wanted,
			 u16 *suballoc_bit_start,
			 unsigned int *num_bits,
			 u64 *blkno_start)
{
	int status;
	u64 bg_blkno;

	BUG_ON(!ac);
	BUG_ON(ac->ac_bits_wanted < (ac->ac_bits_given + bits_wanted));
	BUG_ON(ac->ac_which != OCFS2_AC_USE_META);

	status = ocfs2_claim_suballoc_bits(osb,
					   ac,
					   handle,
					   bits_wanted,
					   1,
					   suballoc_bit_start,
					   num_bits,
					   &bg_blkno);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	atomic_inc(&osb->alloc_stats.bg_allocs);

	*blkno_start = bg_blkno + (u64) *suballoc_bit_start;
	ac->ac_bits_given += (*num_bits);
	status = 0;
bail:
	mlog_exit(status);
	return status;
}

int ocfs2_claim_new_inode(struct ocfs2_super *osb,
			  handle_t *handle,
			  struct ocfs2_alloc_context *ac,
			  u16 *suballoc_bit,
			  u64 *fe_blkno)
{
	int status;
	unsigned int num_bits;
	u64 bg_blkno;

	mlog_entry_void();

	BUG_ON(!ac);
	BUG_ON(ac->ac_bits_given != 0);
	BUG_ON(ac->ac_bits_wanted != 1);
	BUG_ON(ac->ac_which != OCFS2_AC_USE_INODE);

	status = ocfs2_claim_suballoc_bits(osb,
					   ac,
					   handle,
					   1,
					   1,
					   suballoc_bit,
					   &num_bits,
					   &bg_blkno);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	atomic_inc(&osb->alloc_stats.bg_allocs);

	BUG_ON(num_bits != 1);

	*fe_blkno = bg_blkno + (u64) (*suballoc_bit);
	ac->ac_bits_given++;
	status = 0;
bail:
	mlog_exit(status);
	return status;
}

/* translate a group desc. blkno and it's bitmap offset into
 * disk cluster offset. */
static inline u32 ocfs2_desc_bitmap_to_cluster_off(struct inode *inode,
						   u64 bg_blkno,
						   u16 bg_bit_off)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	u32 cluster = 0;

	BUG_ON(!ocfs2_is_cluster_bitmap(inode));

	if (bg_blkno != osb->first_cluster_group_blkno)
		cluster = ocfs2_blocks_to_clusters(inode->i_sb, bg_blkno);
	cluster += (u32) bg_bit_off;
	return cluster;
}

/* given a cluster offset, calculate which block group it belongs to
 * and return that block offset. */
static inline u64 ocfs2_which_cluster_group(struct inode *inode,
					    u32 cluster)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	u32 group_no;

	BUG_ON(!ocfs2_is_cluster_bitmap(inode));

	group_no = cluster / osb->bitmap_cpg;
	if (!group_no)
		return osb->first_cluster_group_blkno;
	return ocfs2_clusters_to_blocks(inode->i_sb,
					group_no * osb->bitmap_cpg);
}

/* given the block number of a cluster start, calculate which cluster
 * group and descriptor bitmap offset that corresponds to. */
static inline void ocfs2_block_to_cluster_group(struct inode *inode,
						u64 data_blkno,
						u64 *bg_blkno,
						u16 *bg_bit_off)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	u32 data_cluster = ocfs2_blocks_to_clusters(osb->sb, data_blkno);

	BUG_ON(!ocfs2_is_cluster_bitmap(inode));

	*bg_blkno = ocfs2_which_cluster_group(inode,
					      data_cluster);

	if (*bg_blkno == osb->first_cluster_group_blkno)
		*bg_bit_off = (u16) data_cluster;
	else
		*bg_bit_off = (u16) ocfs2_blocks_to_clusters(osb->sb,
							     data_blkno - *bg_blkno);
}

/*
 * min_bits - minimum contiguous chunk from this total allocation we
 * can handle. set to what we asked for originally for a full
 * contig. allocation, set to '1' to indicate we can deal with extents
 * of any size.
 */
int __ocfs2_claim_clusters(struct ocfs2_super *osb,
			   handle_t *handle,
			   struct ocfs2_alloc_context *ac,
			   u32 min_clusters,
			   u32 max_clusters,
			   u32 *cluster_start,
			   u32 *num_clusters)
{
	int status;
	unsigned int bits_wanted = max_clusters;
	u64 bg_blkno = 0;
	u16 bg_bit_off;

	mlog_entry_void();

	BUG_ON(ac->ac_bits_given >= ac->ac_bits_wanted);

	BUG_ON(ac->ac_which != OCFS2_AC_USE_LOCAL
	       && ac->ac_which != OCFS2_AC_USE_MAIN);

	if (ac->ac_which == OCFS2_AC_USE_LOCAL) {
		status = ocfs2_claim_local_alloc_bits(osb,
						      handle,
						      ac,
						      bits_wanted,
						      cluster_start,
						      num_clusters);
		if (!status)
			atomic_inc(&osb->alloc_stats.local_data);
	} else {
		if (min_clusters > (osb->bitmap_cpg - 1)) {
			/* The only paths asking for contiguousness
			 * should know about this already. */
			mlog(ML_ERROR, "minimum allocation requested exceeds "
				       "group bitmap size!");
			status = -ENOSPC;
			goto bail;
		}
		/* clamp the current request down to a realistic size. */
		if (bits_wanted > (osb->bitmap_cpg - 1))
			bits_wanted = osb->bitmap_cpg - 1;

		status = ocfs2_claim_suballoc_bits(osb,
						   ac,
						   handle,
						   bits_wanted,
						   min_clusters,
						   &bg_bit_off,
						   num_clusters,
						   &bg_blkno);
		if (!status) {
			*cluster_start =
				ocfs2_desc_bitmap_to_cluster_off(ac->ac_inode,
								 bg_blkno,
								 bg_bit_off);
			atomic_inc(&osb->alloc_stats.bitmap_data);
		}
	}
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	ac->ac_bits_given += *num_clusters;

bail:
	mlog_exit(status);
	return status;
}

int ocfs2_claim_clusters(struct ocfs2_super *osb,
			 handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 min_clusters,
			 u32 *cluster_start,
			 u32 *num_clusters)
{
	unsigned int bits_wanted = ac->ac_bits_wanted - ac->ac_bits_given;

	return __ocfs2_claim_clusters(osb, handle, ac, min_clusters,
				      bits_wanted, cluster_start, num_clusters);
}

static inline int ocfs2_block_group_clear_bits(handle_t *handle,
					       struct inode *alloc_inode,
					       struct ocfs2_group_desc *bg,
					       struct buffer_head *group_bh,
					       unsigned int bit_off,
					       unsigned int num_bits)
{
	int status;
	unsigned int tmp;
	int journal_type = OCFS2_JOURNAL_ACCESS_WRITE;
	struct ocfs2_group_desc *undo_bg = NULL;

	mlog_entry_void();

	if (!OCFS2_IS_VALID_GROUP_DESC(bg)) {
		OCFS2_RO_ON_INVALID_GROUP_DESC(alloc_inode->i_sb, bg);
		status = -EIO;
		goto bail;
	}

	mlog(0, "off = %u, num = %u\n", bit_off, num_bits);

	if (ocfs2_is_cluster_bitmap(alloc_inode))
		journal_type = OCFS2_JOURNAL_ACCESS_UNDO;

	status = ocfs2_journal_access(handle, alloc_inode, group_bh,
				      journal_type);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (ocfs2_is_cluster_bitmap(alloc_inode))
		undo_bg = (struct ocfs2_group_desc *) bh2jh(group_bh)->b_committed_data;

	tmp = num_bits;
	while(tmp--) {
		ocfs2_clear_bit((bit_off + tmp),
				(unsigned long *) bg->bg_bitmap);
		if (ocfs2_is_cluster_bitmap(alloc_inode))
			ocfs2_set_bit(bit_off + tmp,
				      (unsigned long *) undo_bg->bg_bitmap);
	}
	le16_add_cpu(&bg->bg_free_bits_count, num_bits);

	status = ocfs2_journal_dirty(handle, group_bh);
	if (status < 0)
		mlog_errno(status);
bail:
	return status;
}

/*
 * expects the suballoc inode to already be locked.
 */
int ocfs2_free_suballoc_bits(handle_t *handle,
			     struct inode *alloc_inode,
			     struct buffer_head *alloc_bh,
			     unsigned int start_bit,
			     u64 bg_blkno,
			     unsigned int count)
{
	int status = 0;
	u32 tmp_used;
	struct ocfs2_super *osb = OCFS2_SB(alloc_inode->i_sb);
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) alloc_bh->b_data;
	struct ocfs2_chain_list *cl = &fe->id2.i_chain;
	struct buffer_head *group_bh = NULL;
	struct ocfs2_group_desc *group;

	mlog_entry_void();

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(alloc_inode->i_sb, fe);
		status = -EIO;
		goto bail;
	}
	BUG_ON((count + start_bit) > ocfs2_bits_per_group(cl));

	mlog(0, "%llu: freeing %u bits from group %llu, starting at %u\n",
	     (unsigned long long)OCFS2_I(alloc_inode)->ip_blkno, count,
	     (unsigned long long)bg_blkno, start_bit);

	status = ocfs2_read_block(osb, bg_blkno, &group_bh, OCFS2_BH_CACHED,
				  alloc_inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	group = (struct ocfs2_group_desc *) group_bh->b_data;
	status = ocfs2_check_group_descriptor(alloc_inode->i_sb, fe, group);
	if (status) {
		mlog_errno(status);
		goto bail;
	}
	BUG_ON((count + start_bit) > le16_to_cpu(group->bg_bits));

	status = ocfs2_block_group_clear_bits(handle, alloc_inode,
					      group, group_bh,
					      start_bit, count);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_journal_access(handle, alloc_inode, alloc_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	le32_add_cpu(&cl->cl_recs[le16_to_cpu(group->bg_chain)].c_free,
		     count);
	tmp_used = le32_to_cpu(fe->id1.bitmap1.i_used);
	fe->id1.bitmap1.i_used = cpu_to_le32(tmp_used - count);

	status = ocfs2_journal_dirty(handle, alloc_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	if (group_bh)
		brelse(group_bh);

	mlog_exit(status);
	return status;
}

int ocfs2_free_dinode(handle_t *handle,
		      struct inode *inode_alloc_inode,
		      struct buffer_head *inode_alloc_bh,
		      struct ocfs2_dinode *di)
{
	u64 blk = le64_to_cpu(di->i_blkno);
	u16 bit = le16_to_cpu(di->i_suballoc_bit);
	u64 bg_blkno = ocfs2_which_suballoc_group(blk, bit);

	return ocfs2_free_suballoc_bits(handle, inode_alloc_inode,
					inode_alloc_bh, bit, bg_blkno, 1);
}

int ocfs2_free_clusters(handle_t *handle,
		       struct inode *bitmap_inode,
		       struct buffer_head *bitmap_bh,
		       u64 start_blk,
		       unsigned int num_clusters)
{
	int status;
	u16 bg_start_bit;
	u64 bg_blkno;
	struct ocfs2_dinode *fe;

	/* You can't ever have a contiguous set of clusters
	 * bigger than a block group bitmap so we never have to worry
	 * about looping on them. */

	mlog_entry_void();

	/* This is expensive. We can safely remove once this stuff has
	 * gotten tested really well. */
	BUG_ON(start_blk != ocfs2_clusters_to_blocks(bitmap_inode->i_sb, ocfs2_blocks_to_clusters(bitmap_inode->i_sb, start_blk)));

	fe = (struct ocfs2_dinode *) bitmap_bh->b_data;

	ocfs2_block_to_cluster_group(bitmap_inode, start_blk, &bg_blkno,
				     &bg_start_bit);

	mlog(0, "want to free %u clusters starting at block %llu\n",
	     num_clusters, (unsigned long long)start_blk);
	mlog(0, "bg_blkno = %llu, bg_start_bit = %u\n",
	     (unsigned long long)bg_blkno, bg_start_bit);

	status = ocfs2_free_suballoc_bits(handle, bitmap_inode, bitmap_bh,
					  bg_start_bit, bg_blkno,
					  num_clusters);
	if (status < 0)
		mlog_errno(status);

	mlog_exit(status);
	return status;
}

static inline void ocfs2_debug_bg(struct ocfs2_group_desc *bg)
{
	printk("Block Group:\n");
	printk("bg_signature:       %s\n", bg->bg_signature);
	printk("bg_size:            %u\n", bg->bg_size);
	printk("bg_bits:            %u\n", bg->bg_bits);
	printk("bg_free_bits_count: %u\n", bg->bg_free_bits_count);
	printk("bg_chain:           %u\n", bg->bg_chain);
	printk("bg_generation:      %u\n", le32_to_cpu(bg->bg_generation));
	printk("bg_next_group:      %llu\n",
	       (unsigned long long)bg->bg_next_group);
	printk("bg_parent_dinode:   %llu\n",
	       (unsigned long long)bg->bg_parent_dinode);
	printk("bg_blkno:           %llu\n",
	       (unsigned long long)bg->bg_blkno);
}

static inline void ocfs2_debug_suballoc_inode(struct ocfs2_dinode *fe)
{
	int i;

	printk("Suballoc Inode %llu:\n", (unsigned long long)fe->i_blkno);
	printk("i_signature:                  %s\n", fe->i_signature);
	printk("i_size:                       %llu\n",
	       (unsigned long long)fe->i_size);
	printk("i_clusters:                   %u\n", fe->i_clusters);
	printk("i_generation:                 %u\n",
	       le32_to_cpu(fe->i_generation));
	printk("id1.bitmap1.i_used:           %u\n",
	       le32_to_cpu(fe->id1.bitmap1.i_used));
	printk("id1.bitmap1.i_total:          %u\n",
	       le32_to_cpu(fe->id1.bitmap1.i_total));
	printk("id2.i_chain.cl_cpg:           %u\n", fe->id2.i_chain.cl_cpg);
	printk("id2.i_chain.cl_bpc:           %u\n", fe->id2.i_chain.cl_bpc);
	printk("id2.i_chain.cl_count:         %u\n", fe->id2.i_chain.cl_count);
	printk("id2.i_chain.cl_next_free_rec: %u\n",
	       fe->id2.i_chain.cl_next_free_rec);
	for(i = 0; i < fe->id2.i_chain.cl_next_free_rec; i++) {
		printk("fe->id2.i_chain.cl_recs[%d].c_free:  %u\n", i,
		       fe->id2.i_chain.cl_recs[i].c_free);
		printk("fe->id2.i_chain.cl_recs[%d].c_total: %u\n", i,
		       fe->id2.i_chain.cl_recs[i].c_total);
		printk("fe->id2.i_chain.cl_recs[%d].c_blkno: %llu\n", i,
		       (unsigned long long)fe->id2.i_chain.cl_recs[i].c_blkno);
	}
}
