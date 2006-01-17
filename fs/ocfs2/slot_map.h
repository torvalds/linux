/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * slotmap.h
 *
 * description here
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


#ifndef SLOTMAP_H
#define SLOTMAP_H

struct ocfs2_slot_info {
	spinlock_t si_lock;

       	struct inode *si_inode;
	struct buffer_head *si_bh;
	unsigned int si_num_slots;
	unsigned int si_size;
	s16 si_global_node_nums[OCFS2_MAX_SLOTS];
};

int ocfs2_init_slot_info(struct ocfs2_super *osb);
void ocfs2_free_slot_info(struct ocfs2_slot_info *si);

int ocfs2_find_slot(struct ocfs2_super *osb);
void ocfs2_put_slot(struct ocfs2_super *osb);

void ocfs2_update_slot_info(struct ocfs2_slot_info *si);
int ocfs2_update_disk_slots(struct ocfs2_super *osb,
			    struct ocfs2_slot_info *si);

s16 ocfs2_node_num_to_slot(struct ocfs2_slot_info *si,
			   s16 global);
void ocfs2_clear_slot(struct ocfs2_slot_info *si,
		      s16 slot_num);

void ocfs2_populate_mounted_map(struct ocfs2_super *osb);

static inline int ocfs2_is_empty_slot(struct ocfs2_slot_info *si,
				      int slot_num)
{
	BUG_ON(slot_num == OCFS2_INVALID_SLOT);
	assert_spin_locked(&si->si_lock);

	return si->si_global_node_nums[slot_num] == OCFS2_INVALID_SLOT;
}

#endif
