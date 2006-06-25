/*
 *  linux/fs/ext2/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@dcs.ed.ac.uk)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 * 
 *  ext2fs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 * 
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s. 
 *        Andi Kleen, 1997
 *
 * Major simplications and cleanup - we only need to do the metadata, because
 * we can depend on generic_block_fdatasync() to sync the data blocks.
 */

#include "ext2.h"
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>		/* for sync_mapping_buffers() */


/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 */

int ext2_sync_file(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int err;
	int ret;

	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return ret;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return ret;

	err = ext2_sync_inode(inode);
	if (ret == 0)
		ret = err;
	return ret;
}
