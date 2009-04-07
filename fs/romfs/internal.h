/* RomFS internal definitions
 *
 * Copyright © 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/romfs_fs.h>

struct romfs_inode_info {
	struct inode	vfs_inode;
	unsigned long	i_metasize;	/* size of non-data area */
	unsigned long	i_dataoffset;	/* from the start of fs */
};

static inline size_t romfs_maxsize(struct super_block *sb)
{
	return (size_t) (unsigned long) sb->s_fs_info;
}

static inline struct romfs_inode_info *ROMFS_I(struct inode *inode)
{
	return container_of(inode, struct romfs_inode_info, vfs_inode);
}

/*
 * mmap-nommu.c
 */
#if !defined(CONFIG_MMU) && defined(CONFIG_ROMFS_ON_MTD)
extern const struct file_operations romfs_ro_fops;
#else
#define romfs_ro_fops	generic_ro_fops
#endif

/*
 * storage.c
 */
extern int romfs_dev_read(struct super_block *sb, unsigned long pos,
			  void *buf, size_t buflen);
extern ssize_t romfs_dev_strnlen(struct super_block *sb,
				 unsigned long pos, size_t maxlen);
extern int romfs_dev_strncmp(struct super_block *sb, unsigned long pos,
			     const char *str, size_t size);
