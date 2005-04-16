/*
 *	fs/bfs/bfs.h
 *	Copyright (C) 1999 Tigran Aivazian <tigran@veritas.com>
 */
#ifndef _FS_BFS_BFS_H
#define _FS_BFS_BFS_H

#include <linux/bfs_fs.h>

/*
 * BFS file system in-core superblock info
 */
struct bfs_sb_info {
	unsigned long si_blocks;
	unsigned long si_freeb;
	unsigned long si_freei;
	unsigned long si_lf_ioff;
	unsigned long si_lf_sblk;
	unsigned long si_lf_eblk;
	unsigned long si_lasti;
	unsigned long * si_imap;
	struct buffer_head * si_sbh;		/* buffer header w/superblock */
	struct bfs_super_block * si_bfs_sb;	/* superblock in si_sbh->b_data */
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
	return list_entry(inode, struct bfs_inode_info, vfs_inode);
}


#define printf(format, args...) \
	printk(KERN_ERR "BFS-fs: %s(): " format, __FUNCTION__, ## args)


/* file.c */
extern struct inode_operations bfs_file_inops;
extern struct file_operations bfs_file_operations;
extern struct address_space_operations bfs_aops;

/* dir.c */
extern struct inode_operations bfs_dir_inops;
extern struct file_operations bfs_dir_operations;

#endif /* _FS_BFS_BFS_H */
