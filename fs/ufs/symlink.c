/*
 *  linux/fs/ufs/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@emai.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/symlink.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 symlink handling code
 */

#include "ufs_fs.h"
#include "ufs.h"

const struct inode_operations ufs_fast_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= simple_follow_link,
	.setattr	= ufs_setattr,
};

const struct inode_operations ufs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.setattr	= ufs_setattr,
};
