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

/**
 * struct nilfs_mdt_info - on-memory private data of meta data files
 * @mi_nilfs: back pointer to the_nilfs struct
 * @mi_sem: reader/writer semaphore for meta data operations
 * @mi_bgl: per-blockgroup locking
 * @mi_entry_size: size of an entry
 * @mi_first_entry_offset: offset to the first entry
 * @mi_entries_per_block: number of entries in a block
 * @mi_palloc_cache: persistent object allocator cache
 * @mi_blocks_per_group: number of blocks in a group
 * @mi_blocks_per_desc_block: number of blocks per descriptor block
 */
struct nilfs_mdt_info {
	struct the_nilfs       *mi_nilfs;
	struct rw_semaphore	mi_sem;
	struct blockgroup_lock *mi_bgl;
	unsigned		mi_entry_size;
	unsigned		mi_first_entry_offset;
	unsigned long		mi_entries_per_block;
	struct nilfs_palloc_cache *mi_palloc_cache;
	unsigned long		mi_blocks_per_group;
	unsigned long		mi_blocks_per_desc_block;
};

static inline struct nilfs_mdt_info *NILFS_MDT(const struct inode *inode)
{
	return inode->i_private;
}

static inline struct the_nilfs *NILFS_I_NILFS(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	return sb ? NILFS_SB(sb)->s_nilfs : NILFS_MDT(inode)->mi_nilfs;
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

struct inode *nilfs_mdt_new(struct the_nilfs *, struct super_block *, ino_t,
			    size_t);
struct inode *nilfs_mdt_new_common(struct the_nilfs *, struct super_block *,
				   ino_t, gfp_t, size_t);
void nilfs_mdt_destroy(struct inode *);
void nilfs_mdt_set_entry_size(struct inode *, unsigned, unsigned);
void nilfs_mdt_set_shadow(struct inode *, struct inode *);


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
	return NILFS_MDT(inode)->mi_nilfs->ns_cno;
}

#define nilfs_mdt_bgl_lock(inode, bg) \
	(&NILFS_MDT(inode)->mi_bgl->locks[(bg) & (NR_BG_LOCKS-1)].lock)

#endif /* _NILFS_MDT_H */
