// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * iyesde.c
 */

/*
 * This file implements code to create and read iyesdes from disk.
 *
 * Iyesdes in Squashfs are identified by a 48-bit iyesde which encodes the
 * location of the compressed metadata block containing the iyesde, and the byte
 * offset into that block where the iyesde is placed (<block, offset>).
 *
 * To maximise compression there are different iyesdes for each file type
 * (regular file, directory, device, etc.), the iyesde contents and length
 * varying with the type.
 *
 * To further maximise compression, two types of regular file iyesde and
 * directory iyesde are defined: iyesdes optimised for frequently occurring
 * regular files and directories, and extended types where extra
 * information has to be stored.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/xattr.h>
#include <linux/pagemap.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"
#include "xattr.h"

/*
 * Initialise VFS iyesde with the base iyesde information common to all
 * Squashfs iyesde types.  Sqsh_iyes contains the unswapped base iyesde
 * off disk.
 */
static int squashfs_new_iyesde(struct super_block *sb, struct iyesde *iyesde,
				struct squashfs_base_iyesde *sqsh_iyes)
{
	uid_t i_uid;
	gid_t i_gid;
	int err;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_iyes->uid), &i_uid);
	if (err)
		return err;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_iyes->guid), &i_gid);
	if (err)
		return err;

	i_uid_write(iyesde, i_uid);
	i_gid_write(iyesde, i_gid);
	iyesde->i_iyes = le32_to_cpu(sqsh_iyes->iyesde_number);
	iyesde->i_mtime.tv_sec = le32_to_cpu(sqsh_iyes->mtime);
	iyesde->i_atime.tv_sec = iyesde->i_mtime.tv_sec;
	iyesde->i_ctime.tv_sec = iyesde->i_mtime.tv_sec;
	iyesde->i_mode = le16_to_cpu(sqsh_iyes->mode);
	iyesde->i_size = 0;

	return err;
}


struct iyesde *squashfs_iget(struct super_block *sb, long long iyes,
				unsigned int iyes_number)
{
	struct iyesde *iyesde = iget_locked(sb, iyes_number);
	int err;

	TRACE("Entered squashfs_iget\n");

	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	err = squashfs_read_iyesde(iyesde, iyes);
	if (err) {
		iget_failed(iyesde);
		return ERR_PTR(err);
	}

	unlock_new_iyesde(iyesde);
	return iyesde;
}


/*
 * Initialise VFS iyesde by reading iyesde from iyesde table (compressed
 * metadata).  The format and amount of data read depends on type.
 */
int squashfs_read_iyesde(struct iyesde *iyesde, long long iyes)
{
	struct super_block *sb = iyesde->i_sb;
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	u64 block = SQUASHFS_INODE_BLK(iyes) + msblk->iyesde_table;
	int err, type, offset = SQUASHFS_INODE_OFFSET(iyes);
	union squashfs_iyesde squashfs_iyes;
	struct squashfs_base_iyesde *sqshb_iyes = &squashfs_iyes.base;
	int xattr_id = SQUASHFS_INVALID_XATTR;

	TRACE("Entered squashfs_read_iyesde\n");

	/*
	 * Read iyesde base common to all iyesde types.
	 */
	err = squashfs_read_metadata(sb, sqshb_iyes, &block,
				&offset, sizeof(*sqshb_iyes));
	if (err < 0)
		goto failed_read;

	err = squashfs_new_iyesde(sb, iyesde, sqshb_iyes);
	if (err)
		goto failed_read;

	block = SQUASHFS_INODE_BLK(iyes) + msblk->iyesde_table;
	offset = SQUASHFS_INODE_OFFSET(iyes);

	type = le16_to_cpu(sqshb_iyes->iyesde_type);
	switch (type) {
	case SQUASHFS_REG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_reg_iyesde *sqsh_iyes = &squashfs_iyes.reg;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
							sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_iyes->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_iyes->offset);
			frag_size = squashfs_frag_lookup(sb, frag, &frag_blk);
			if (frag_size < 0) {
				err = frag_size;
				goto failed_read;
			}
		} else {
			frag_blk = SQUASHFS_INVALID_BLK;
			frag_size = 0;
			frag_offset = 0;
		}

		set_nlink(iyesde, 1);
		iyesde->i_size = le32_to_cpu(sqsh_iyes->file_size);
		iyesde->i_fop = &generic_ro_fops;
		iyesde->i_mode |= S_IFREG;
		iyesde->i_blocks = ((iyesde->i_size - 1) >> 9) + 1;
		squashfs_i(iyesde)->fragment_block = frag_blk;
		squashfs_i(iyesde)->fragment_size = frag_size;
		squashfs_i(iyesde)->fragment_offset = frag_offset;
		squashfs_i(iyesde)->start = le32_to_cpu(sqsh_iyes->start_block);
		squashfs_i(iyesde)->block_list_start = block;
		squashfs_i(iyesde)->offset = offset;
		iyesde->i_data.a_ops = &squashfs_aops;

		TRACE("File iyesde %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_INODE_BLK(iyes),
			offset, squashfs_i(iyesde)->start, block, offset);
		break;
	}
	case SQUASHFS_LREG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_lreg_iyesde *sqsh_iyes = &squashfs_iyes.lreg;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
							sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_iyes->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_iyes->offset);
			frag_size = squashfs_frag_lookup(sb, frag, &frag_blk);
			if (frag_size < 0) {
				err = frag_size;
				goto failed_read;
			}
		} else {
			frag_blk = SQUASHFS_INVALID_BLK;
			frag_size = 0;
			frag_offset = 0;
		}

		xattr_id = le32_to_cpu(sqsh_iyes->xattr);
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		iyesde->i_size = le64_to_cpu(sqsh_iyes->file_size);
		iyesde->i_op = &squashfs_iyesde_ops;
		iyesde->i_fop = &generic_ro_fops;
		iyesde->i_mode |= S_IFREG;
		iyesde->i_blocks = (iyesde->i_size -
				le64_to_cpu(sqsh_iyes->sparse) + 511) >> 9;

		squashfs_i(iyesde)->fragment_block = frag_blk;
		squashfs_i(iyesde)->fragment_size = frag_size;
		squashfs_i(iyesde)->fragment_offset = frag_offset;
		squashfs_i(iyesde)->start = le64_to_cpu(sqsh_iyes->start_block);
		squashfs_i(iyesde)->block_list_start = block;
		squashfs_i(iyesde)->offset = offset;
		iyesde->i_data.a_ops = &squashfs_aops;

		TRACE("File iyesde %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_INODE_BLK(iyes),
			offset, squashfs_i(iyesde)->start, block, offset);
		break;
	}
	case SQUASHFS_DIR_TYPE: {
		struct squashfs_dir_iyesde *sqsh_iyes = &squashfs_iyes.dir;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		iyesde->i_size = le16_to_cpu(sqsh_iyes->file_size);
		iyesde->i_op = &squashfs_dir_iyesde_ops;
		iyesde->i_fop = &squashfs_dir_ops;
		iyesde->i_mode |= S_IFDIR;
		squashfs_i(iyesde)->start = le32_to_cpu(sqsh_iyes->start_block);
		squashfs_i(iyesde)->offset = le16_to_cpu(sqsh_iyes->offset);
		squashfs_i(iyesde)->dir_idx_cnt = 0;
		squashfs_i(iyesde)->parent = le32_to_cpu(sqsh_iyes->parent_iyesde);

		TRACE("Directory iyesde %x:%x, start_block %llx, offset %x\n",
				SQUASHFS_INODE_BLK(iyes), offset,
				squashfs_i(iyesde)->start,
				le16_to_cpu(sqsh_iyes->offset));
		break;
	}
	case SQUASHFS_LDIR_TYPE: {
		struct squashfs_ldir_iyesde *sqsh_iyes = &squashfs_iyes.ldir;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		xattr_id = le32_to_cpu(sqsh_iyes->xattr);
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		iyesde->i_size = le32_to_cpu(sqsh_iyes->file_size);
		iyesde->i_op = &squashfs_dir_iyesde_ops;
		iyesde->i_fop = &squashfs_dir_ops;
		iyesde->i_mode |= S_IFDIR;
		squashfs_i(iyesde)->start = le32_to_cpu(sqsh_iyes->start_block);
		squashfs_i(iyesde)->offset = le16_to_cpu(sqsh_iyes->offset);
		squashfs_i(iyesde)->dir_idx_start = block;
		squashfs_i(iyesde)->dir_idx_offset = offset;
		squashfs_i(iyesde)->dir_idx_cnt = le16_to_cpu(sqsh_iyes->i_count);
		squashfs_i(iyesde)->parent = le32_to_cpu(sqsh_iyes->parent_iyesde);

		TRACE("Long directory iyesde %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_INODE_BLK(iyes), offset,
				squashfs_i(iyesde)->start,
				le16_to_cpu(sqsh_iyes->offset));
		break;
	}
	case SQUASHFS_SYMLINK_TYPE:
	case SQUASHFS_LSYMLINK_TYPE: {
		struct squashfs_symlink_iyesde *sqsh_iyes = &squashfs_iyes.symlink;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		iyesde->i_size = le32_to_cpu(sqsh_iyes->symlink_size);
		iyesde->i_op = &squashfs_symlink_iyesde_ops;
		iyesde_yeshighmem(iyesde);
		iyesde->i_data.a_ops = &squashfs_symlink_aops;
		iyesde->i_mode |= S_IFLNK;
		squashfs_i(iyesde)->start = block;
		squashfs_i(iyesde)->offset = offset;

		if (type == SQUASHFS_LSYMLINK_TYPE) {
			__le32 xattr;

			err = squashfs_read_metadata(sb, NULL, &block,
						&offset, iyesde->i_size);
			if (err < 0)
				goto failed_read;
			err = squashfs_read_metadata(sb, &xattr, &block,
						&offset, sizeof(xattr));
			if (err < 0)
				goto failed_read;
			xattr_id = le32_to_cpu(xattr);
		}

		TRACE("Symbolic link iyesde %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_INODE_BLK(iyes), offset,
				block, offset);
		break;
	}
	case SQUASHFS_BLKDEV_TYPE:
	case SQUASHFS_CHRDEV_TYPE: {
		struct squashfs_dev_iyesde *sqsh_iyes = &squashfs_iyes.dev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_CHRDEV_TYPE)
			iyesde->i_mode |= S_IFCHR;
		else
			iyesde->i_mode |= S_IFBLK;
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		rdev = le32_to_cpu(sqsh_iyes->rdev);
		init_special_iyesde(iyesde, iyesde->i_mode, new_decode_dev(rdev));

		TRACE("Device iyesde %x:%x, rdev %x\n",
				SQUASHFS_INODE_BLK(iyes), offset, rdev);
		break;
	}
	case SQUASHFS_LBLKDEV_TYPE:
	case SQUASHFS_LCHRDEV_TYPE: {
		struct squashfs_ldev_iyesde *sqsh_iyes = &squashfs_iyes.ldev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LCHRDEV_TYPE)
			iyesde->i_mode |= S_IFCHR;
		else
			iyesde->i_mode |= S_IFBLK;
		xattr_id = le32_to_cpu(sqsh_iyes->xattr);
		iyesde->i_op = &squashfs_iyesde_ops;
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		rdev = le32_to_cpu(sqsh_iyes->rdev);
		init_special_iyesde(iyesde, iyesde->i_mode, new_decode_dev(rdev));

		TRACE("Device iyesde %x:%x, rdev %x\n",
				SQUASHFS_INODE_BLK(iyes), offset, rdev);
		break;
	}
	case SQUASHFS_FIFO_TYPE:
	case SQUASHFS_SOCKET_TYPE: {
		struct squashfs_ipc_iyesde *sqsh_iyes = &squashfs_iyes.ipc;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_FIFO_TYPE)
			iyesde->i_mode |= S_IFIFO;
		else
			iyesde->i_mode |= S_IFSOCK;
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		init_special_iyesde(iyesde, iyesde->i_mode, 0);
		break;
	}
	case SQUASHFS_LFIFO_TYPE:
	case SQUASHFS_LSOCKET_TYPE: {
		struct squashfs_lipc_iyesde *sqsh_iyes = &squashfs_iyes.lipc;

		err = squashfs_read_metadata(sb, sqsh_iyes, &block, &offset,
				sizeof(*sqsh_iyes));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LFIFO_TYPE)
			iyesde->i_mode |= S_IFIFO;
		else
			iyesde->i_mode |= S_IFSOCK;
		xattr_id = le32_to_cpu(sqsh_iyes->xattr);
		iyesde->i_op = &squashfs_iyesde_ops;
		set_nlink(iyesde, le32_to_cpu(sqsh_iyes->nlink));
		init_special_iyesde(iyesde, iyesde->i_mode, 0);
		break;
	}
	default:
		ERROR("Unkyeswn iyesde type %d in squashfs_iget!\n", type);
		return -EINVAL;
	}

	if (xattr_id != SQUASHFS_INVALID_XATTR && msblk->xattr_id_table) {
		err = squashfs_xattr_lookup(sb, xattr_id,
					&squashfs_i(iyesde)->xattr_count,
					&squashfs_i(iyesde)->xattr_size,
					&squashfs_i(iyesde)->xattr);
		if (err < 0)
			goto failed_read;
		iyesde->i_blocks += ((squashfs_i(iyesde)->xattr_size - 1) >> 9)
				+ 1;
	} else
		squashfs_i(iyesde)->xattr_count = 0;

	return 0;

failed_read:
	ERROR("Unable to read iyesde 0x%llx\n", iyes);
	return err;
}


const struct iyesde_operations squashfs_iyesde_ops = {
	.listxattr = squashfs_listxattr
};

