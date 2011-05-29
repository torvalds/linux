/*
 * mdt.h - NILFS meta data file prototype and definitions
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 */

#ifndef _NILFS_MDT_H
#define _NILFS_MDT_H

#include <linux/buffer_head.h>
#include <linux/blockgroup_lock.h>
#include "nilfs.h"
#include "page.h"

struct nilfs_shadow_map {
	struct nilfs_bmap_store bmap_store;
	struct address_space frozen_data;
	struct address_space frozen_btnodes;
	struct list_head frozen_buffers;
};

/**
 * struct nilfs_mdt_info - on-memory private data of meta data files
 * @mi_sem: reader/writer semaphore for meta data operations
 * @mi_bgl: per-blockgroup locking
 * @mi_entry_size: size of an entry
 * @mi_first_entry_offset: offset to the first entry
 * @mi_entries_per_block: number of entries in a block
 * @mi_palloc_cache: persistent object allocator cache
 * @mi_shadow: shadow of bmap and page caches
 * @mi_blocks_per_group: number of blocks in a group
 * @mi_blocks_per_desc_block: number of blocks per descriptor block
 */
struct nilfs_mdt_info {
	struct rw_semaphore	mi_sem;
	struct blockgroup_lock *mi_bgl;
	unsigned		mi_entry_size;
	unsigned		mi_first_entry_offset;
	unsigned long		mi_entries_per_block;
	struct nilfs_palloc_cache *mi_palloc_cache;
	struct nilfs_shadow_map *mi_shadow;
	unsigned long		mi_blocks_per_group;
	unsigned long		mi_blocks_per_desc_block;
};

static inline struct nilfs_mdt_info *NILFS_MDT(const struct inode *inode)
{
	return inode->i_private;
}

static inline struct the_nilfs *NILFS_I_NILFS(struct inode *inode)
{
	return inode->i_sb->s_fs_info;
}

/* Default GFP flags using highmem */
#define NILFS_MDT_GFP      (__GFP_WAIT | __GFP_IO | __GFP_HIGHMEM)

int nilfs_mdt_get_block(struct inode *, unsigned long, int,
			void (*init_block)(struct inode *,
					   struct buffer_head *, void *),
			struct buffer_head **);
int nilfs_mdt_delete_block(struct inode *, unsigned long);
int nilfs_mdt_forget_block(struct inode *, unsigned long);
int nilfs_mdt_mark_block_dirty(struct inode *, unsigned long);
int nilfs_mdt_fetch_dirty(struct inode *);

int nilfs_mdt_init(struct inode *inode, gfp_t gfp_mask, size_t objsz);
void nilfs_mdt_set_entry_size(struct inode *, unsigned, unsigned);

int nilfs_mdt_setup_shadow_map(struct inode *inode,
			       struct nilfs_shadow_map *shadow);
int nilfs_mdt_save_to_shadow_map(struct inode *inode);
void nilfs_mdt_restore_from_shadow_map(struct inode *inode);
void nilfs_mdt_clear_shadow_map(struct inode *inode);
int nilfs_mdt_freeze_buffer(struct inode *inode, struct buffer_head *bh);
struct buffer_head *nilfs_mdt_get_frozen_buffer(struct inode *inode,
						struct buffer_head *bh);

#define nilfs_mdt_mark_buffer_dirty(bh)	nilfs_mark_buffer_dirty(bh)

static inline void nilfs_mdt_mark_dirty(struct inode *inode)
{
	if (!test_bit(NILFS_I_DIRTY, &NILFS_I(inode)->i_state))
		set_bit(NILFS_I_DIRTY, &NILFS_I(inode)->i_state);
}

static inline void nilfs_mdt_clear_dirty(struct inode *inode)
{
	clear_bit(NILFS_I_DIRTY, &NILFS_I(inode)->i_state);
}

static inline __u64 nilfs_mdt_cno(struct inode *inode)
{
	return NILFS_I_NILFS(inode)->ns_cno;
}

#define nilfs_mdt_bgl_lock(inode, bg) \
	(&NILFS_MDT(inode)->mi_bgl->locks[(bg) & (NR_BG_LOCKS-1)].lock)

#endif /* _NILFS_MDT_H */
