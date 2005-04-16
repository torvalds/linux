/*
 *  linux/include/linux/adfs_fs_i.h
 *
 * Copyright (C) 1997 Russell King
 */

#ifndef _ADFS_FS_I
#define _ADFS_FS_I

/*
 * adfs file system inode data in memory
 */
struct adfs_inode_info {
	loff_t		mmu_private;
	unsigned long	parent_id;	/* object id of parent		*/
	__u32		loadaddr;	/* RISC OS load address		*/
	__u32		execaddr;	/* RISC OS exec address		*/
	unsigned int	filetype;	/* RISC OS file type		*/
	unsigned int	attr;		/* RISC OS permissions		*/
	unsigned int	stamped:1;	/* RISC OS file has date/time	*/
	struct inode vfs_inode;
};

#endif
