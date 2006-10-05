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

#include "buffer_head_io.h"

#define OCFS2_LOCAL_ALLOC(dinode)	(&((dinode)->id2.i_lab))

static inline int ocfs2_local_alloc_window_bits(struct ocfs2_super *osb);

static u32 ocfs2_local_alloc_count_bits(struct ocfs2_dinode *alloc);

static int ocfs2_local_alloc_find_clear_bits(struct ocfs2_super *osb,
					     struct ocfs2_dinode *alloc,
					     u32 numbits);

static void ocfs2_clear_local_alloc(struct ocfs2_dinode *alloc);

static int ocfs2_sync_local_to_main(struct ocfs2_super *osb,
				    struct ocfs2_journal_handle *handle,
				    struct ocfs2_dinode *alloc,
				    struct inode *main_bm_inode,
				    struct buffer_head *main_bm_bh);

static int ocfs2_local_alloc_reserve_for_window(struct ocfs2_super *osb,
						struct ocfs2_journal_handle *handle,
						struct ocfs2_alloc_context **ac,
						struct inode **bitmap_inode,
						struct buffer_head **bitmap_bh);

static int ocfs2_local_alloc_new_window(struct ocfs2_super *osb,
					struct ocfs2_journal_handle *handle,
					struct ocfs2_alloc_context *ac);

static int ocfs2_local_alloc_slide_window(struct ocfs2_super *osb,
					  struct inode *local_alloc_inode);

/*
 * Determine how large our local alloc window should be, in bits.
 *
 * These values (and the behavior in ocfs2_alloc_should_use_local) have
 * been chosen so that most allocations, including new block groups go
 * through local alloc.
 */
static inline int ocfs2_local_alloc_window_bits(struct ocfs2_super *osb)
{
	BUG_ON(osb->s_clustersize_bits < 12);

	return 2048 >> (osb->s_clustersize_bits - 12);
}

/*
 * Tell us whether a given allocation should use the local alloc
 * file. Otherwise, it has to go to the main bitmap.
 */
int ocfs2_alloc_should_use_local(struct ocfs2_super *osb, u64 bits)
{
	int la_bits = ocfs2_local_alloc_window_bits(osb);

	if (osb->local_alloc_state != OCFS2_LA_ENABLED)
		return 0;

	/* la_bits should be at least twice the size (in clusters) of
	 * a new block group. We want to be sure block group
	 * allocations go through the local alloc, so allow an
	 * allocation to take up to half the bitmap. */
	if (bits > (la_bits / 2))
		return 0;

	return 1;
}

int ocfs2_load_local_alloc(struct ocfs2_super *osb)
{
	int status = 0;
	struct ocfs2_dinode *alloc = NULL;
	struct buffer_head *alloc_bh = NULL;
	u32 num_used;
	struct inode *inode = NULL;
	struct ocfs2_local_alloc *la;

	mlog_entry_void();

	/* read the alloc off disk */
	inode = ocfs2_get_system_file_inode(osb, LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno,
				  &alloc_bh, 0, inode);
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
		if (alloc_bh)
			brelse(alloc_bh);
	if (inode)
		iput(inode);

	mlog_exit(status);
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
	struct ocfs2_journal_handle *handle = NULL;
	struct inode *local_alloc_inode = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;
	struct ocfs2_dinode *alloc_copy = NULL;
	struct ocfs2_dinode *alloc = NULL;

	mlog_entry_void();

	if (osb->local_alloc_state == OCFS2_LA_UNUSED)
		goto bail;

	local_alloc_inode =
		ocfs2_get_system_file_inode(osb,
					    LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!local_alloc_inode) {
		status = -ENOENT;
		mlog_errno(status);
		goto bail;
	}

	osb->local_alloc_state = OCFS2_LA_DISABLED;

	handle = ocfs2_alloc_handle(osb);
	if (!handle) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	ocfs2_handle_add_inode(handle, main_bm_inode);
	status = ocfs2_meta_lock(main_bm_inode, handle, &main_bm_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* WINDOW_MOVE_CREDITS is a bit heavy... */
	handle = ocfs2_start_trans(osb, handle, OCFS2_WINDOW_MOVE_CREDITS);
	if (IS_ERR(handle)) {
		mlog_errno(PTR_ERR(handle));
		handle = NULL;
		goto bail;
	}

	bh = osb->local_alloc_bh;
	alloc = (struct ocfs2_dinode *) bh->b_data;

	alloc_copy = kmalloc(bh->b_size, GFP_KERNEL);
	if (!alloc_copy) {
		status = -ENOMEM;
		goto bail;
	}
	memcpy(alloc_copy, alloc, bh->b_size);

	status = ocfs2_journal_access(handle, local_alloc_inode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_clear_local_alloc(alloc);

	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	brelse(bh);
	osb->local_alloc_bh = NULL;
	osb->local_alloc_state = OCFS2_LA_UNUSED;

	status = ocfs2_sync_local_to_main(osb, handle, alloc_copy,
					  main_bm_inode, main_bm_bh);
	if (status < 0)
		mlog_errno(status);

bail:
	if (handle)
		ocfs2_commit_trans(handle);

	if (main_bm_bh)
		brelse(main_bm_bh);

	if (main_bm_inode)
		iput(main_bm_inode);

	if (local_alloc_inode)
		iput(local_alloc_inode);

	if (alloc_copy)
		kfree(alloc_copy);

	mlog_exit_void();
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

	mlog_entry("(slot_num = %d)\n", slot_num);

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

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno,
				  &alloc_bh, 0, inode);
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

	status = ocfs2_write_block(osb, alloc_bh, inode);
	if (status < 0)
		mlog_errno(status);

bail:
	if ((status < 0) && (*alloc_copy)) {
		kfree(*alloc_copy);
		*alloc_copy = NULL;
	}

	if (alloc_bh)
		brelse(alloc_bh);

	if (inode) {
		mutex_unlock(&inode->i_mutex);
		iput(inode);
	}

	mlog_exit(status);
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
	struct ocfs2_journal_handle *handle = NULL;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;

	mlog_entry_void();

	handle = ocfs2_alloc_handle(osb);
	if (!handle) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	ocfs2_handle_add_inode(handle, main_bm_inode);
	status = ocfs2_meta_lock(main_bm_inode, handle, &main_bm_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, handle, OCFS2_WINDOW_MOVE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	/* we want the bitmap change to be recorded on disk asap */
	handle->k_handle->h_sync = 1;

	status = ocfs2_sync_local_to_main(osb, handle, alloc,
					  main_bm_inode, main_bm_bh);
	if (status < 0)
		mlog_errno(status);

bail:
	if (handle)
		ocfs2_commit_trans(handle);

	if (main_bm_bh)
		brelse(main_bm_bh);

	if (main_bm_inode)
		iput(main_bm_inode);

	mlog_exit(status);
	return status;
}

/*
 * make sure we've got at least bitswanted contiguous bits in the
 * local alloc. You lose them when you drop i_mutex.
 *
 * We will add ourselves to the transaction passed in, but may start
 * our own in order to shift windows.
 */
int ocfs2_reserve_local_alloc_bits(struct ocfs2_super *osb,
				   struct ocfs2_journal_handle *passed_handle,
				   u32 bits_wanted,
				   struct ocfs2_alloc_context *ac)
{
	int status;
	struct ocfs2_dinode *alloc;
	struct inode *local_alloc_inode;
	unsigned int free_bits;

	mlog_entry_void();

	BUG_ON(!passed_handle);
	BUG_ON(!ac);
	BUG_ON(passed_handle->k_handle);

	local_alloc_inode =
		ocfs2_get_system_file_inode(osb,
					    LOCAL_ALLOC_SYSTEM_INODE,
					    osb->slot_num);
	if (!local_alloc_inode) {
		status = -ENOENT;
		mlog_errno(status);
		goto bail;
	}
	ocfs2_handle_add_inode(passed_handle, local_alloc_inode);

	if (osb->local_alloc_state != OCFS2_LA_ENABLED) {
		status = -ENOSPC;
		goto bail;
	}

	if (bits_wanted > ocfs2_local_alloc_window_bits(osb)) {
		mlog(0, "Asking for more than my max window size!\n");
		status = -ENOSPC;
		goto bail;
	}

	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;

	if (le32_to_cpu(alloc->id1.bitmap1.i_used) !=
	    ocfs2_local_alloc_count_bits(alloc)) {
		ocfs2_error(osb->sb, "local alloc inode %llu says it has "
			    "%u free bits, but a count shows %u",
			    (unsigned long long)le64_to_cpu(alloc->i_blkno),
			    le32_to_cpu(alloc->id1.bitmap1.i_used),
			    ocfs2_local_alloc_count_bits(alloc));
		status = -EIO;
		goto bail;
	}

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
	}

	ac->ac_inode = igrab(local_alloc_inode);
	get_bh(osb->local_alloc_bh);
	ac->ac_bh = osb->local_alloc_bh;
	ac->ac_which = OCFS2_AC_USE_LOCAL;
	status = 0;
bail:
	if (local_alloc_inode)
		iput(local_alloc_inode);

	mlog_exit(status);
	return status;
}

int ocfs2_claim_local_alloc_bits(struct ocfs2_super *osb,
				 struct ocfs2_journal_handle *handle,
				 struct ocfs2_alloc_context *ac,
				 u32 min_bits,
				 u32 *bit_off,
				 u32 *num_bits)
{
	int status, start;
	struct inode *local_alloc_inode;
	u32 bits_wanted;
	void *bitmap;
	struct ocfs2_dinode *alloc;
	struct ocfs2_local_alloc *la;

	mlog_entry_void();
	BUG_ON(ac->ac_which != OCFS2_AC_USE_LOCAL);

	bits_wanted = ac->ac_bits_wanted - ac->ac_bits_given;
	local_alloc_inode = ac->ac_inode;
	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	start = ocfs2_local_alloc_find_clear_bits(osb, alloc, bits_wanted);
	if (start == -1) {
		/* TODO: Shouldn't we just BUG here? */
		status = -ENOSPC;
		mlog_errno(status);
		goto bail;
	}

	bitmap = la->la_bitmap;
	*bit_off = le32_to_cpu(la->la_bm_off) + start;
	/* local alloc is always contiguous by nature -- we never
	 * delete bits from it! */
	*num_bits = bits_wanted;

	status = ocfs2_journal_access(handle, local_alloc_inode,
				      osb->local_alloc_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	while(bits_wanted--)
		ocfs2_set_bit(start++, bitmap);

	alloc->id1.bitmap1.i_used = cpu_to_le32(*num_bits +
				le32_to_cpu(alloc->id1.bitmap1.i_used));

	status = ocfs2_journal_dirty(handle, osb->local_alloc_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

static u32 ocfs2_local_alloc_count_bits(struct ocfs2_dinode *alloc)
{
	int i;
	u8 *buffer;
	u32 count = 0;
	struct ocfs2_local_alloc *la = OCFS2_LOCAL_ALLOC(alloc);

	mlog_entry_void();

	buffer = la->la_bitmap;
	for (i = 0; i < le16_to_cpu(la->la_size); i++)
		count += hweight8(buffer[i]);

	mlog_exit(count);
	return count;
}

static int ocfs2_local_alloc_find_clear_bits(struct ocfs2_super *osb,
					     struct ocfs2_dinode *alloc,
					     u32 numbits)
{
	int numfound, bitoff, left, startoff, lastzero;
	void *bitmap = NULL;

	mlog_entry("(numbits wanted = %u)\n", numbits);

	if (!alloc->id1.bitmap1.i_total) {
		mlog(0, "No bits in my window!\n");
		bitoff = -1;
		goto bail;
	}

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
		if (numfound == numbits) {
			/* mlog(0, "Found it all!\n"); */
			break;
		}
	}

	mlog(0, "Exiting loop, bitoff = %d, numfound = %d\n", bitoff,
	     numfound);

	if (numfound == numbits)
		bitoff = startoff - numfound;
	else
		bitoff = -1;

bail:
	mlog_exit(bitoff);
	return bitoff;
}

static void ocfs2_clear_local_alloc(struct ocfs2_dinode *alloc)
{
	struct ocfs2_local_alloc *la = OCFS2_LOCAL_ALLOC(alloc);
	int i;
	mlog_entry_void();

	alloc->id1.bitmap1.i_total = 0;
	alloc->id1.bitmap1.i_used = 0;
	la->la_bm_off = 0;
	for(i = 0; i < le16_to_cpu(la->la_size); i++)
		la->la_bitmap[i] = 0;

	mlog_exit_void();
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
				    struct ocfs2_journal_handle *handle,
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

	mlog_entry("total = %u, COUNT = %u, used = %u\n",
		   le32_to_cpu(alloc->id1.bitmap1.i_total),
		   ocfs2_local_alloc_count_bits(alloc),
		   le32_to_cpu(alloc->id1.bitmap1.i_used));

	if (!alloc->id1.bitmap1.i_total) {
		mlog(0, "nothing to sync!\n");
		goto bail;
	}

	if (le32_to_cpu(alloc->id1.bitmap1.i_used) ==
	    le32_to_cpu(alloc->id1.bitmap1.i_total)) {
		mlog(0, "all bits were taken!\n");
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

			mlog(0, "freeing %u bits starting at local alloc bit "
			     "%u (la_start_blk = %llu, blkno = %llu)\n",
			     count, start - count,
			     (unsigned long long)la_start_blk,
			     (unsigned long long)blkno);

			status = ocfs2_free_clusters(handle, main_bm_inode,
						     main_bm_bh, blkno, count);
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
	mlog_exit(status);
	return status;
}

static int ocfs2_local_alloc_reserve_for_window(struct ocfs2_super *osb,
						struct ocfs2_journal_handle *handle,
						struct ocfs2_alloc_context **ac,
						struct inode **bitmap_inode,
						struct buffer_head **bitmap_bh)
{
	int status;

	*ac = kcalloc(1, sizeof(struct ocfs2_alloc_context), GFP_KERNEL);
	if (!(*ac)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	(*ac)->ac_handle = handle;
	(*ac)->ac_bits_wanted = ocfs2_local_alloc_window_bits(osb);

	status = ocfs2_reserve_cluster_bitmap_bits(osb, *ac);
	if (status < 0) {
		if (status != -ENOSPC)
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

	mlog_exit(status);
	return status;
}

/*
 * pass it the bitmap lock in lock_bh if you have it.
 */
static int ocfs2_local_alloc_new_window(struct ocfs2_super *osb,
					struct ocfs2_journal_handle *handle,
					struct ocfs2_alloc_context *ac)
{
	int status = 0;
	u32 cluster_off, cluster_count;
	struct ocfs2_dinode *alloc = NULL;
	struct ocfs2_local_alloc *la;

	mlog_entry_void();

	alloc = (struct ocfs2_dinode *) osb->local_alloc_bh->b_data;
	la = OCFS2_LOCAL_ALLOC(alloc);

	if (alloc->id1.bitmap1.i_total)
		mlog(0, "asking me to alloc a new window over a non-empty "
		     "one\n");

	mlog(0, "Allocating %u clusters for a new window.\n",
	     ocfs2_local_alloc_window_bits(osb));

	/* Instruct the allocation code to try the most recently used
	 * cluster group. We'll re-record the group used this pass
	 * below. */
	ac->ac_last_group = osb->la_last_gd;

	/* we used the generic suballoc reserve function, but we set
	 * everything up nicely, so there's no reason why we can't use
	 * the more specific cluster api to claim bits. */
	status = ocfs2_claim_clusters(osb, handle, ac,
				      ocfs2_local_alloc_window_bits(osb),
				      &cluster_off, &cluster_count);
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

	mlog(0, "New window allocated:\n");
	mlog(0, "window la_bm_off = %u\n",
	     OCFS2_LOCAL_ALLOC(alloc)->la_bm_off);
	mlog(0, "window bits = %u\n", le32_to_cpu(alloc->id1.bitmap1.i_total));

bail:
	mlog_exit(status);
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
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_dinode *alloc;
	struct ocfs2_dinode *alloc_copy = NULL;
	struct ocfs2_alloc_context *ac = NULL;

	mlog_entry_void();

	handle = ocfs2_alloc_handle(osb);
	if (!handle) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	/* This will lock the main bitmap for us. */
	status = ocfs2_local_alloc_reserve_for_window(osb,
						      handle,
						      &ac,
						      &main_bm_inode,
						      &main_bm_bh);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, handle, OCFS2_WINDOW_MOVE_CREDITS);
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
	alloc_copy = kmalloc(osb->local_alloc_bh->b_size, GFP_KERNEL);
	if (!alloc_copy) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}
	memcpy(alloc_copy, alloc, osb->local_alloc_bh->b_size);

	status = ocfs2_journal_access(handle, local_alloc_inode,
				      osb->local_alloc_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_clear_local_alloc(alloc);

	status = ocfs2_journal_dirty(handle, osb->local_alloc_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

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

	status = 0;
bail:
	if (handle)
		ocfs2_commit_trans(handle);

	if (main_bm_bh)
		brelse(main_bm_bh);

	if (main_bm_inode)
		iput(main_bm_inode);

	if (alloc_copy)
		kfree(alloc_copy);

	if (ac)
		ocfs2_free_alloc_context(ac);

	mlog_exit(status);
	return status;
}

