/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	fs/bfs/bfs.h
 *	Copyright (C) 1999-2018 Tigran Aivazian <aivazian.tigran@gmail.com>
 */
#ifndef _FS_BFS_BFS_H
#define _FS_BFS_BFS_H

#include <linux/bfs_fs.h>

/* In theory BFS supports up to 512 iyesdes, numbered from 2 (for /) up to 513 inclusive.
   In actual fact, attempting to create the 512th iyesde (i.e. iyesde No. 513 or file No. 511)
   will fail with ENOSPC in bfs_add_entry(): the root directory canyest contain so many entries, counting '..'.
   So, mkfs.bfs(8) should really limit its -N option to 511 and yest 512. For yesw, we just print a warning
   if a filesystem is mounted with such "impossible to fill up" number of iyesdes */
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
 * BFS file system in-core iyesde info
 */
struct bfs_iyesde_info {
	unsigned long i_dsk_iyes; /* iyesde number from the disk, can be 0 */
	unsigned long i_sblock;
	unsigned long i_eblock;
	struct iyesde vfs_iyesde;
};

static inline struct bfs_sb_info *BFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct bfs_iyesde_info *BFS_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct bfs_iyesde_info, vfs_iyesde);
}


#define printf(format, args...) \
	printk(KERN_ERR "BFS-fs: %s(): " format, __func__, ## args)

/* iyesde.c */
extern struct iyesde *bfs_iget(struct super_block *sb, unsigned long iyes);
extern void bfs_dump_imap(const char *, struct super_block *);

/* file.c */
extern const struct iyesde_operations bfs_file_iyesps;
extern const struct file_operations bfs_file_operations;
extern const struct address_space_operations bfs_aops;

/* dir.c */
extern const struct iyesde_operations bfs_dir_iyesps;
extern const struct file_operations bfs_dir_operations;

#endif /* _FS_BFS_BFS_H */
