/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	fs/bfs/bfs.h
 *	Copyright (C) 1999-2018 Tigran Aivazian <aivazian.tigran@gmail.com>
 */
#ifndef _FS_BFS_BFS_H
#define _FS_BFS_BFS_H

#include <linux/bfs_fs.h>

/* In theory BFS supports up to 512 inodes, numbered from 2 (for /) up to 513 inclusive.
   In actual fact, attempting to create the 512th inode (i.e. inode No. 513 or file No. 511)
   will fail with ENOSPC in bfs_add_entry(): the root directory cannot contain so many entries, counting '..'.
   So, mkfs.bfs(8) should really limit its -N option to 511 and not 512. For now, we just print a warning
   if a filesystem is mounted with such "impossible to fill up" number of inodes */
#define BFS_MAX_LASTI	513

/*
 * BFS file system in-core superblock info
 */
struct bfs_sb_info {
	unsigned long si_blocks;
	unsigned long si_freeb;
	unsigned long si_freei;
	unsigned long si_lf_eblk;
	unsigned long si_lasti;
	DECLARE_BITMAP(si_imap, BFS_MAX_LASTI+1);
	struct mutex bfs_lock;
};

/*
 * BFS file system in-core inode info
 */
struct bfs_inode_info {
	unsigned long i_dsk_ino; /* inode number from the disk, can be 0 */
	unsigned long i_sblock;
	unsigned long i_eblock;
	struct inode vfs_inode;
};

static inline struct bfs_sb_info *BFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct bfs_inode_info *BFS_I(struct inode *inode)
{
	return container_of(inode, struct bfs_inode_info, vfs_inode);
}


#define printf(format, args...) \
	printk(KERN_ERR "BFS-fs: %s(): " format, __func__, ## args)

/* inode.c */
extern struct inode *bfs_iget(struct super_block *sb, unsigned long ino);
extern void bfs_dump_imap(const char *, struct super_block *);

/* file.c */
extern const struct inode_operations bfs_file_inops;
extern const struct file_operations bfs_file_operations;
extern const struct address_space_operations bfs_aops;

/* dir.c */
extern const struct inode_operations bfs_dir_inops;
extern const struct file_operations bfs_dir_operations;

#endif /* _FS_BFS_BFS_H */
