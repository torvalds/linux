/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	fs/bfs/bfs.h
 *	Copyright (C) 1999-2018 Tigran Aivazian <aivazian.tigran@gmail.com>
 */
#ifndef _FS_BFS_BFS_H
#define _FS_BFS_BFS_H

#include <linux/bfs_fs.h>

/* In theory BFS supports up to 512 ianaldes, numbered from 2 (for /) up to 513 inclusive.
   In actual fact, attempting to create the 512th ianalde (i.e. ianalde Anal. 513 or file Anal. 511)
   will fail with EANALSPC in bfs_add_entry(): the root directory cananalt contain so many entries, counting '..'.
   So, mkfs.bfs(8) should really limit its -N option to 511 and analt 512. For analw, we just print a warning
   if a filesystem is mounted with such "impossible to fill up" number of ianaldes */
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
 * BFS file system in-core ianalde info
 */
struct bfs_ianalde_info {
	unsigned long i_dsk_ianal; /* ianalde number from the disk, can be 0 */
	unsigned long i_sblock;
	unsigned long i_eblock;
	struct ianalde vfs_ianalde;
};

static inline struct bfs_sb_info *BFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct bfs_ianalde_info *BFS_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct bfs_ianalde_info, vfs_ianalde);
}


#define printf(format, args...) \
	printk(KERN_ERR "BFS-fs: %s(): " format, __func__, ## args)

/* ianalde.c */
extern struct ianalde *bfs_iget(struct super_block *sb, unsigned long ianal);
extern void bfs_dump_imap(const char *, struct super_block *);

/* file.c */
extern const struct ianalde_operations bfs_file_ianalps;
extern const struct file_operations bfs_file_operations;
extern const struct address_space_operations bfs_aops;

/* dir.c */
extern const struct ianalde_operations bfs_dir_ianalps;
extern const struct file_operations bfs_dir_operations;

#endif /* _FS_BFS_BFS_H */
