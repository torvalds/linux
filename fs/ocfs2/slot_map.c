/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * slot_map.c
 *
 *
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "dlmglue.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "slot_map.h"
#include "super.h"
#include "sysfile.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"


struct ocfs2_slot {
	int sl_valid;
	unsigned int sl_node_num;
};

struct ocfs2_slot_info {
	int si_extended;
	int si_slots_per_block;
	struct inode *si_inode;
	unsigned int si_blocks;
	struct buffer_head **si_bh;
	unsigned int si_num_slots;
	struct ocfs2_slot *si_slots;
};


static int __ocfs2_node_num_to_slot(struct ocfs2_slot_info *si,
				    unsigned int node_num);

static void ocfs2_invalidate_slot(struct ocfs2_slot_info *si,
				  int slot_num)
{
	BUG_ON((slot_num < 0) || (slot_num >= si->si_num_slots));
	si->si_slots[slot_num].sl_valid = 0;
}

static void ocfs2_set_slot(struct ocfs2_slot_info *si,
			   int slot_num, unsigned int node_num)
{
	BUG_ON((slot_num < 0) || (slot_num >= si->si_num_slots));

	si->si_slots[slot_num].sl_valid = 1;
	si->si_slots[slot_num].sl_node_num = node_num;
}

/* This version is for the extended slot map */
static void ocfs2_update_slot_info_extended(struct ocfs2_slot_info *si)
{
	int b, i, slotno;
	struct ocfs2_slot_map_extended *se;

	slotno = 0;
	for (b = 0; b < si->si_blocks; b++) {
		se = (struct ocfs2_slot_map_extended *)si->si_bh[b]->b_data;
		for (i = 0;
		     (i < si->si_slots_per_block) &&
		     (slotno < si->si_num_slots);
		     i++, slotno++) {
			if (se->se_slots[i].es_valid)
				ocfs2_set_slot(si, slotno,
					       le32_to_cpu(se->se_slots[i].es_node_num));
			else
				ocfs2_invalidate_slot(si, slotno);
		}
	}
}

/*
 * Post the slot information on disk into our slot_info struct.
 * Must be protected by osb_lock.
 */
static void ocfs2_update_slot_info_old(struct ocfs2_slot_info *si)
{
	int i;
	struct ocfs2_slot_map *sm;

	sm = (struct ocfs2_slot_map *)si->si_bh[0]->b_data;

	for (i = 0; i < si->si_num_slots; i++) {
		if (le16_to_cpu(sm->sm_slots[i]) == (u16)OCFS2_INVALID_SLOT)
			ocfs2_invalidate_slot(si, i);
		else
			ocfs2_set_slot(si, i, le16_to_cpu(sm->sm_slots[i]));
	}
}

static void ocfs2_update_slot_info(struct ocfs2_slot_info *si)
{
	/*
	 * The slot data will have been refreshed when ocfs2_super_lock
	 * was taken.
	 */
	if (si->si_extended)
		ocfs2_update_slot_info_extended(si);
	else
		ocfs2_update_slot_info_old(si);
}

int ocfs2_refresh_slot_info(struct ocfs2_super *osb)
{
	int ret;
	struct ocfs2_slot_info *si = osb->slot_info;

	if (si == NULL)
		return 0;

	BUG_ON(si->si_blocks == 0);
	BUG_ON(si->si_bh == NULL);

	trace_ocfs2_refresh_slot_info(si->si_blocks);

	/*
	 * We pass -1 as blocknr because we expect all of si->si_bh to
	 * be !NULL.  Thus, ocfs2_read_blocks() will ignore blocknr.  If
	 * this is not true, the read of -1 (UINT64_MAX) will fail.
	 */
	ret = ocfs2_read_blocks(INODE_CACHE(si->si_inode), -1, si->si_blocks,
				si->si_bh, OCFS2_BH_IGNORE_CACHE, NULL);
	if (ret == 0) {
		spin_lock(&osb->osb_lock);
		ocfs2_update_slot_info(si);
		spin_unlock(&osb->osb_lock);
	}

	return ret;
}

/* post the our slot info stuff into it's destination bh and write it
 * out. */
static void ocfs2_update_disk_slot_extended(struct ocfs2_slot_info *si,
					    int slot_num,
					    struct buffer_head **bh)
{
	int blkind = slot_num / si->si_slots_per_block;
	int slotno = slot_num % si->si_slots_per_block;
	struct ocfs2_slot_map_extended *se;

	BUG_ON(blkind >= si->si_blocks);

	se = (struct ocfs2_slot_map_extended *)si->si_bh[blkind]->b_data;
	se->se_slots[slotno].es_valid = si->si_slots[slot_num].sl_valid;
	if (si->si_slots[slot_num].sl_valid)
		se->se_slots[slotno].es_node_num =
			cpu_to_le32(si->si_slots[slot_num].sl_node_num);
	*bh = si->si_bh[blkind];
}

static void ocfs2_update_disk_slot_old(struct ocfs2_slot_info *si,
				       int slot_num,
				       struct buffer_head **bh)
{
	int i;
	struct ocfs2_slot_map *sm;

	sm = (struct ocfs2_slot_map *)si->si_bh[0]->b_data;
	for (i = 0; i < si->si_num_slots; i++) {
		if (si->si_slots[i].sl_valid)
			sm->sm_slots[i] =
				cpu_to_le16(si->si_slots[i].sl_node_num);
		else
			sm->sm_slots[i] = cpu_to_le16(OCFS2_INVALID_SLOT);
	}
	*bh = si->si_bh[0];
}

static int ocfs2_update_disk_slot(struct ocfs2_super *osb,
				  struct ocfs2_slot_info *si,
				  int slot_num)
{
	int status;
	struct buffer_head *bh;

	spin_lock(&osb->osb_lock);
	if (si->si_extended)
		ocfs2_update_disk_slot_extended(si, slot_num, &bh);
	else
		ocfs2_update_disk_slot_old(si, slot_num, &bh);
	spin_unlock(&osb->osb_lock);

	status = ocfs2_write_block(osb, bh, INODE_CACHE(si->si_inode));
	if (status < 0)
		mlog_errno(status);

	return status;
}

/*
 * Calculate how many bytes are needed by the slot map.  Returns
 * an error if the slot map file is too small.
 */
static int ocfs2_slot_map_physical_size(struct ocfs2_super *osb,
					struct inode *inode,
					unsigned long long *bytes)
{
	unsigned long long bytes_needed;

	if (ocfs2_uses_extended_slot_map(osb)) {
		bytes_needed = osb->max_slots *
			sizeof(struct ocfs2_extended_slot);
	} else {
		bytes_needed = osb->max_slots * sizeof(__le16);
	}
	if (bytes_needed > i_size_read(inode)) {
		mlog(ML_ERROR,
		     "Slot map file is too small!  (size %llu, needed %llu)\n",
		     i_size_read(inode), bytes_needed);
		return -ENOSPC;
	}

	*bytes = bytes_needed;
	return 0;
}

/* try to find global node in the slot info. Returns -ENOENT
 * if nothing is found. */
static int __ocfs2_node_num_to_slot(struct ocfs2_slot_info *si,
				    unsigned int node_num)
{
	int i, ret = -ENOENT;

	for(i = 0; i < si->si_num_slots; i++) {
		if (si->si_slots[i].sl_valid &&
		    (node_num == si->si_slots[i].sl_node_num)) {
			ret = i;
			break;
		}
	}

	return ret;
}

static int __ocfs2_find_empty_slot(struct ocfs2_slot_info *si,
				   int preferred)
{
	int i, ret = -ENOSPC;

	if ((preferred >= 0) && (preferred < si->si_num_slots)) {
		if (!si->si_slots[preferred].sl_valid) {
			ret = preferred;
			goto out;
		}
	}

	for(i = 0; i < si->si_num_slots; i++) {
		if (!si->si_slots[i].sl_valid) {
			ret = i;
			break;
		}
	}
out:
	return ret;
}

int ocfs2_node_num_to_slot(struct ocfs2_super *osb, unsigned int node_num)
{
	int slot;
	struct ocfs2_slot_info *si = osb->slot_info;

	spin_lock(&osb->osb_lock);
	slot = __ocfs2_node_num_to_slot(si, node_num);
	spin_unlock(&osb->osb_lock);

	return slot;
}

int ocfs2_slot_to_node_num_locked(struct ocfs2_super *osb, int slot_num,
				  unsigned int *node_num)
{
	struct ocfs2_slot_info *si = osb->slot_info;

	assert_spin_locked(&osb->osb_lock);

	BUG_ON(slot_num < 0);
	BUG_ON(slot_num > osb->max_slots);

	if (!si->si_slots[slot_num].sl_valid)
		return -ENOENT;

	*node_num = si->si_slots[slot_num].sl_node_num;
	return 0;
}

static void __ocfs2_free_slot_info(struct ocfs2_slot_info *si)
{
	unsigned int i;

	if (si == NULL)
		return;

	if (si->si_inode)
		iput(si->si_inode);
	if (si->si_bh) {
		for (i = 0; i < si->si_blocks; i++) {
			if (si->si_bh[i]) {
				brelse(si->si_bh[i]);
				si->si_bh[i] = NULL;
			}
		}
		kfree(si->si_bh);
	}

	kfree(si);
}

int ocfs2_clear_slot(struct ocfs2_super *osb, int slot_num)
{
	struct ocfs2_slot_info *si = osb->slot_info;

	if (si == NULL)
		return 0;

	spin_lock(&osb->osb_lock);
	ocfs2_invalidate_slot(si, slot_num);
	spin_unlock(&osb->osb_lock);

	return ocfs2_update_disk_slot(osb, osb->slot_info, slot_num);
}

static int ocfs2_map_slot_buffers(struct ocfs2_super *osb,
				  struct ocfs2_slot_info *si)
{
	int status = 0;
	u64 blkno;
	unsigned long long blocks, bytes = 0;
	unsigned int i;
	struct buffer_head *bh;

	status = ocfs2_slot_map_physical_size(osb, si->si_inode, &bytes);
	if (status)
		goto bail;

	blocks = ocfs2_blocks_for_bytes(si->si_inode->i_sb, bytes);
	BUG_ON(blocks > UINT_MAX);
	si->si_blocks = blocks;
	if (!si->si_blocks)
		goto bail;

	if (si->si_extended)
		si->si_slots_per_block =
			(osb->sb->s_blocksize /
			 sizeof(struct ocfs2_extended_slot));
	else
		si->si_slots_per_block = osb->sb->s_blocksize / sizeof(__le16);

	/* The size checks above should ensure this */
	BUG_ON((osb->max_slots / si->si_slots_per_block) > blocks);

	trace_ocfs2_map_slot_buffers(bytes, si->si_blocks);

	si->si_bh = kcalloc(si->si_blocks, sizeof(struct buffer_head *),
			    GFP_KERNEL);
	if (!si->si_bh) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	for (i = 0; i < si->si_blocks; i++) {
		status = ocfs2_extent_map_get_blocks(si->si_inode, i,
						     &blkno, NULL, NULL);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		trace_ocfs2_map_slot_buffers_block((unsigned long long)blkno, i);

		bh = NULL;  /* Acquire a fresh bh */
		status = ocfs2_read_blocks(INODE_CACHE(si->si_inode), blkno,
					   1, &bh, OCFS2_BH_IGNORE_CACHE, NULL);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		si->si_bh[i] = bh;
	}

bail:
	return status;
}

int ocfs2_init_slot_info(struct ocfs2_super *osb)
{
	int status;
	struct inode *inode = NULL;
	struct ocfs2_slot_info *si;

	si = kzalloc(sizeof(struct ocfs2_slot_info) +
		     (sizeof(struct ocfs2_slot) * osb->max_slots),
		     GFP_KERNEL);
	if (!si) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	si->si_extended = ocfs2_uses_extended_slot_map(osb);
	si->si_num_slots = osb->max_slots;
	si->si_slots = (struct ocfs2_slot *)((char *)si +
					     sizeof(struct ocfs2_slot_info));

	inode = ocfs2_get_system_file_inode(osb, SLOT_MAP_SYSTEM_INODE,
					    OCFS2_INVALID_SLOT);
	if (!inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	si->si_inode = inode;
	status = ocfs2_map_slot_buffers(osb, si);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	osb->slot_info = (struct ocfs2_slot_info *)si;
bail:
	if (status < 0 && si)
		__ocfs2_free_slot_info(si);

	return status;
}

void ocfs2_free_slot_info(struct ocfs2_super *osb)
{
	struct ocfs2_slot_info *si = osb->slot_info;

	osb->slot_info = NULL;
	__ocfs2_free_slot_info(si);
}

int ocfs2_find_slot(struct ocfs2_super *osb)
{
	int status;
	int slot;
	struct ocfs2_slot_info *si;

	si = osb->slot_info;

	spin_lock(&osb->osb_lock);
	ocfs2_update_slot_info(si);

	/* search for ourselves first and take the slot if it already
	 * exists. Perhaps we need to mark this in a variable for our
	 * own journal recovery? Possibly not, though we certainly
	 * need to warn to the user */
	slot = __ocfs2_node_num_to_slot(si, osb->node_num);
	if (slot < 0) {
		/* if no slot yet, then just take 1st available
		 * one. */
		slot = __ocfs2_find_empty_slot(si, osb->preferred_slot);
		if (slot < 0) {
			spin_unlock(&osb->osb_lock);
			mlog(ML_ERROR, "no free slots available!\n");
			status = -EINVAL;
			goto bail;
		}
	} else
		printk(KERN_INFO "ocfs2: Slot %d on device (%s) was already "
		       "allocated to this node!\n", slot, osb->dev_str);

	ocfs2_set_slot(si, slot, osb->node_num);
	osb->slot_num = slot;
	spin_unlock(&osb->osb_lock);

	trace_ocfs2_find_slot(osb->slot_num);

	status = ocfs2_update_disk_slot(osb, si, osb->slot_num);
	if (status < 0)
		mlog_errno(status);

bail:
	return status;
}

void ocfs2_put_slot(struct ocfs2_super *osb)
{
	int status, slot_num;
	struct ocfs2_slot_info *si = osb->slot_info;

	if (!si)
		return;

	spin_lock(&osb->osb_lock);
	ocfs2_update_slot_info(si);

	slot_num = osb->slot_num;
	ocfs2_invalidate_slot(si, osb->slot_num);
	osb->slot_num = OCFS2_INVALID_SLOT;
	spin_unlock(&osb->osb_lock);

	status = ocfs2_update_disk_slot(osb, si, slot_num);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	ocfs2_free_slot_info(osb);
}

