/*
 * sb.h - NILFS on-memory super block structure.
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
 *
 */

#ifndef _NILFS_SB
#define _NILFS_SB

#include <linux/types.h>
#include <linux/fs.h>

/*
 * Mount options
 */
struct nilfs_mount_options {
	unsigned long mount_opt;
	__u64 snapshot_cno;
};

struct the_nilfs;
struct nilfs_sc_info;

/*
 * NILFS super-block data in memory
 */
struct nilfs_sb_info {
	/* Snapshot status */
	__u64 s_snapshot_cno;		/* Checkpoint number */
	atomic_t s_inodes_count;
	atomic_t s_blocks_count;	/* Reserved (might be deleted) */

	/* Mount options */
	unsigned long s_mount_opt;
	uid_t s_resuid;
	gid_t s_resgid;

	unsigned long s_interval;	/* construction interval */
	unsigned long s_watermark;	/* threshold of data amount
					   for the segment construction */

	/* Fundamental members */
	struct super_block *s_super;	/* reverse pointer to super_block */
	struct the_nilfs *s_nilfs;
	struct list_head s_list;	/* list head for nilfs->ns_supers */

	/* Segment constructor */
	struct list_head s_dirty_files;	/* dirty files list */
	struct nilfs_sc_info *s_sc_info; /* segment constructor info */
	spinlock_t s_inode_lock;	/* Lock for the nilfs inode.
					   It covers s_dirty_files list */

	/* Metadata files */
	struct inode *s_ifile;		/* index file inode */

	/* Inode allocator */
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
};

static inline struct nilfs_sb_info *NILFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct nilfs_sc_info *NILFS_SC(struct nilfs_sb_info *sbi)
{
	return sbi->s_sc_info;
}

/*
 * Bit operations for the mount option
 */
#define nilfs_clear_opt(sbi, opt)  \
	do { (sbi)->s_mount_opt &= ~NILFS_MOUNT_##opt; } while (0)
#define nilfs_set_opt(sbi, opt)  \
	do { (sbi)->s_mount_opt |= NILFS_MOUNT_##opt; } while (0)
#define nilfs_test_opt(sbi, opt)   ((sbi)->s_mount_opt & NILFS_MOUNT_##opt)
#define nilfs_write_opt(sbi, mask, opt)					\
	do { (sbi)->s_mount_opt =					\
		(((sbi)->s_mount_opt & ~NILFS_MOUNT_##mask) |		\
		 NILFS_MOUNT_##opt);					\
	} while (0)

#endif /* _NILFS_SB */
