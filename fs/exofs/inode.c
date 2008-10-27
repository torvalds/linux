/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/writeback.h>
#include <linux/buffer_head.h>

#include "exofs.h"

#ifdef CONFIG_EXOFS_DEBUG
#  define EXOFS_DEBUG_OBJ_ISIZE 1
#endif

/******************************************************************************
 * INODE OPERATIONS
 *****************************************************************************/

/*
 * Test whether an inode is a fast symlink.
 */
static inline int exofs_inode_is_fast_symlink(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);

	return S_ISLNK(inode->i_mode) && (oi->i_data[0] != 0);
}

/*
 * get_block_t - Fill in a buffer_head
 * An OSD takes care of block allocation so we just fake an allocation by
 * putting in the inode's sector_t in the buffer_head.
 * TODO: What about the case of create==0 and @iblock does not exist in the
 * object?
 */
static int exofs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	map_bh(bh_result, inode->i_sb, iblock);
	return 0;
}

const struct osd_attr g_attr_logical_length = ATTR_DEF(
	OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_LOGICAL_LENGTH, 8);

/*
 * Truncate a file to the specified size - all we have to do is set the size
 * attribute.  We make sure the object exists first.
 */
void exofs_truncate(struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct exofs_i_info *oi = exofs_i(inode);
	struct osd_obj_id obj = {sbi->s_pid, inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or;
	struct osd_attr attr;
	loff_t isize = i_size_read(inode);
	__be64 newsize;
	int ret;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
	     || S_ISLNK(inode->i_mode)))
		return;
	if (exofs_inode_is_fast_symlink(inode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	nobh_truncate_page(inode->i_mapping, isize, exofs_get_block);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("ERROR: exofs_truncate: osd_start_request failed\n");
		goto fail;
	}

	osd_req_set_attributes(or, &obj);

	newsize = cpu_to_be64((u64)isize);
	attr = g_attr_logical_length;
	attr.val_ptr = &newsize;
	osd_req_add_set_attr_list(or, &attr, 1);

	/* if we are about to truncate an object, and it hasn't been
	 * created yet, wait
	 */
	if (unlikely(wait_obj_created(oi)))
		goto fail;

	ret = exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
	osd_end_request(or);
	if (ret)
		goto fail;

out:
	mark_inode_dirty(inode);
	return;
fail:
	make_bad_inode(inode);
	goto out;
}

/*
 * Set inode attributes - just call generic functions.
 */
int exofs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;

	error = inode_setattr(inode, iattr);
	return error;
}
