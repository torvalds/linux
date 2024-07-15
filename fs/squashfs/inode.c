// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * inode.c
 */

/*
 * This file implements code to create and read inodes from disk.
 *
 * Inodes in Squashfs are identified by a 48-bit inode which encodes the
 * location of the compressed metadata block containing the inode, and the byte
 * offset into that block where the inode is placed (<block, offset>).
 *
 * To maximise compression there are different inodes for each file type
 * (regular file, directory, device, etc.), the inode contents and length
 * varying with the type.
 *
 * To further maximise compression, two types of regular file inode and
 * directory inode are defined: inodes optimised for frequently occurring
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
 * Initialise VFS inode with the base inode information common to all
 * Squashfs inode types.  Sqsh_ino contains the unswapped base inode
 * off disk.
 */
static int squashfs_new_inode(struct super_block *sb, struct inode *inode,
				struct squashfs_base_inode *sqsh_ino)
{
	uid_t i_uid;
	gid_t i_gid;
	int err;

	inode->i_ino = le32_to_cpu(sqsh_ino->inode_number);
	if (inode->i_ino == 0)
		return -EINVAL;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_ino->uid), &i_uid);
	if (err)
		return err;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_ino->guid), &i_gid);
	if (err)
		return err;

	i_uid_write(inode, i_uid);
	i_gid_write(inode, i_gid);
	inode_set_mtime(inode, le32_to_cpu(sqsh_ino->mtime), 0);
	inode_set_atime(inode, inode_get_mtime_sec(inode), 0);
	inode_set_ctime(inode, inode_get_mtime_sec(inode), 0);
	inode->i_mode = le16_to_cpu(sqsh_ino->mode);
	inode->i_size = 0;

	return err;
}


struct inode *squashfs_iget(struct super_block *sb, long long ino,
				unsigned int ino_number)
{
	struct inode *inode = iget_locked(sb, ino_number);
	int err;

	TRACE("Entered squashfs_iget\n");

	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	err = squashfs_read_inode(inode, ino);
	if (err) {
		iget_failed(inode);
		return ERR_PTR(err);
	}

	unlock_new_inode(inode);
	return inode;
}


/*
 * Initialise VFS inode by reading inode from inode table (compressed
 * metadata).  The format and amount of data read depends on type.
 */
int squashfs_read_inode(struct inode *inode, long long ino)
{
	struct super_block *sb = inode->i_sb;
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	u64 block = SQUASHFS_INODE_BLK(ino) + msblk->inode_table;
	int err, type, offset = SQUASHFS_INODE_OFFSET(ino);
	union squashfs_inode squashfs_ino;
	struct squashfs_base_inode *sqshb_ino = &squashfs_ino.base;
	int xattr_id = SQUASHFS_INVALID_XATTR;

	TRACE("Entered squashfs_read_inode\n");

	/*
	 * Read inode base common to all inode types.
	 */
	err = squashfs_read_metadata(sb, sqshb_ino, &block,
				&offset, sizeof(*sqshb_ino));
	if (err < 0)
		goto failed_read;

	err = squashfs_new_inode(sb, inode, sqshb_ino);
	if (err)
		goto failed_read;

	block = SQUASHFS_INODE_BLK(ino) + msblk->inode_table;
	offset = SQUASHFS_INODE_OFFSET(ino);

	type = le16_to_cpu(sqshb_ino->inode_type);
	switch (type) {
	case SQUASHFS_REG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_reg_inode *sqsh_ino = &squashfs_ino.reg;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
							sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_ino->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_ino->offset);
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

		set_nlink(inode, 1);
		inode->i_size = le32_to_cpu(sqsh_ino->file_size);
		inode->i_fop = &generic_ro_fops;
		inode->i_mode |= S_IFREG;
		inode->i_blocks = ((inode->i_size - 1) >> 9) + 1;
		squashfs_i(inode)->fragment_block = frag_blk;
		squashfs_i(inode)->fragment_size = frag_size;
		squashfs_i(inode)->fragment_offset = frag_offset;
		squashfs_i(inode)->start = le32_to_cpu(sqsh_ino->start_block);
		squashfs_i(inode)->block_list_start = block;
		squashfs_i(inode)->offset = offset;
		inode->i_data.a_ops = &squashfs_aops;

		TRACE("File inode %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_INODE_BLK(ino),
			offset, squashfs_i(inode)->start, block, offset);
		break;
	}
	case SQUASHFS_LREG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_lreg_inode *sqsh_ino = &squashfs_ino.lreg;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
							sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_ino->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_ino->offset);
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

		xattr_id = le32_to_cpu(sqsh_ino->xattr);
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		inode->i_size = le64_to_cpu(sqsh_ino->file_size);
		inode->i_op = &squashfs_inode_ops;
		inode->i_fop = &generic_ro_fops;
		inode->i_mode |= S_IFREG;
		inode->i_blocks = (inode->i_size -
				le64_to_cpu(sqsh_ino->sparse) + 511) >> 9;

		squashfs_i(inode)->fragment_block = frag_blk;
		squashfs_i(inode)->fragment_size = frag_size;
		squashfs_i(inode)->fragment_offset = frag_offset;
		squashfs_i(inode)->start = le64_to_cpu(sqsh_ino->start_block);
		squashfs_i(inode)->block_list_start = block;
		squashfs_i(inode)->offset = offset;
		inode->i_data.a_ops = &squashfs_aops;

		TRACE("File inode %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_INODE_BLK(ino),
			offset, squashfs_i(inode)->start, block, offset);
		break;
	}
	case SQUASHFS_DIR_TYPE: {
		struct squashfs_dir_inode *sqsh_ino = &squashfs_ino.dir;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		inode->i_size = le16_to_cpu(sqsh_ino->file_size);
		inode->i_op = &squashfs_dir_inode_ops;
		inode->i_fop = &squashfs_dir_ops;
		inode->i_mode |= S_IFDIR;
		squashfs_i(inode)->start = le32_to_cpu(sqsh_ino->start_block);
		squashfs_i(inode)->offset = le16_to_cpu(sqsh_ino->offset);
		squashfs_i(inode)->dir_idx_cnt = 0;
		squashfs_i(inode)->parent = le32_to_cpu(sqsh_ino->parent_inode);

		TRACE("Directory inode %x:%x, start_block %llx, offset %x\n",
				SQUASHFS_INODE_BLK(ino), offset,
				squashfs_i(inode)->start,
				le16_to_cpu(sqsh_ino->offset));
		break;
	}
	case SQUASHFS_LDIR_TYPE: {
		struct squashfs_ldir_inode *sqsh_ino = &squashfs_ino.ldir;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		xattr_id = le32_to_cpu(sqsh_ino->xattr);
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		inode->i_size = le32_to_cpu(sqsh_ino->file_size);
		inode->i_op = &squashfs_dir_inode_ops;
		inode->i_fop = &squashfs_dir_ops;
		inode->i_mode |= S_IFDIR;
		squashfs_i(inode)->start = le32_to_cpu(sqsh_ino->start_block);
		squashfs_i(inode)->offset = le16_to_cpu(sqsh_ino->offset);
		squashfs_i(inode)->dir_idx_start = block;
		squashfs_i(inode)->dir_idx_offset = offset;
		squashfs_i(inode)->dir_idx_cnt = le16_to_cpu(sqsh_ino->i_count);
		squashfs_i(inode)->parent = le32_to_cpu(sqsh_ino->parent_inode);

		TRACE("Long directory inode %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_INODE_BLK(ino), offset,
				squashfs_i(inode)->start,
				le16_to_cpu(sqsh_ino->offset));
		break;
	}
	case SQUASHFS_SYMLINK_TYPE:
	case SQUASHFS_LSYMLINK_TYPE: {
		struct squashfs_symlink_inode *sqsh_ino = &squashfs_ino.symlink;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		inode->i_size = le32_to_cpu(sqsh_ino->symlink_size);
		inode->i_op = &squashfs_symlink_inode_ops;
		inode_nohighmem(inode);
		inode->i_data.a_ops = &squashfs_symlink_aops;
		inode->i_mode |= S_IFLNK;
		squashfs_i(inode)->start = block;
		squashfs_i(inode)->offset = offset;

		if (type == SQUASHFS_LSYMLINK_TYPE) {
			__le32 xattr;

			err = squashfs_read_metadata(sb, NULL, &block,
						&offset, inode->i_size);
			if (err < 0)
				goto failed_read;
			err = squashfs_read_metadata(sb, &xattr, &block,
						&offset, sizeof(xattr));
			if (err < 0)
				goto failed_read;
			xattr_id = le32_to_cpu(xattr);
		}

		TRACE("Symbolic link inode %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_INODE_BLK(ino), offset,
				block, offset);
		break;
	}
	case SQUASHFS_BLKDEV_TYPE:
	case SQUASHFS_CHRDEV_TYPE: {
		struct squashfs_dev_inode *sqsh_ino = &squashfs_ino.dev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_CHRDEV_TYPE)
			inode->i_mode |= S_IFCHR;
		else
			inode->i_mode |= S_IFBLK;
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		rdev = le32_to_cpu(sqsh_ino->rdev);
		init_special_inode(inode, inode->i_mode, new_decode_dev(rdev));

		TRACE("Device inode %x:%x, rdev %x\n",
				SQUASHFS_INODE_BLK(ino), offset, rdev);
		break;
	}
	case SQUASHFS_LBLKDEV_TYPE:
	case SQUASHFS_LCHRDEV_TYPE: {
		struct squashfs_ldev_inode *sqsh_ino = &squashfs_ino.ldev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LCHRDEV_TYPE)
			inode->i_mode |= S_IFCHR;
		else
			inode->i_mode |= S_IFBLK;
		xattr_id = le32_to_cpu(sqsh_ino->xattr);
		inode->i_op = &squashfs_inode_ops;
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		rdev = le32_to_cpu(sqsh_ino->rdev);
		init_special_inode(inode, inode->i_mode, new_decode_dev(rdev));

		TRACE("Device inode %x:%x, rdev %x\n",
				SQUASHFS_INODE_BLK(ino), offset, rdev);
		break;
	}
	case SQUASHFS_FIFO_TYPE:
	case SQUASHFS_SOCKET_TYPE: {
		struct squashfs_ipc_inode *sqsh_ino = &squashfs_ino.ipc;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_FIFO_TYPE)
			inode->i_mode |= S_IFIFO;
		else
			inode->i_mode |= S_IFSOCK;
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		init_special_inode(inode, inode->i_mode, 0);
		break;
	}
	case SQUASHFS_LFIFO_TYPE:
	case SQUASHFS_LSOCKET_TYPE: {
		struct squashfs_lipc_inode *sqsh_ino = &squashfs_ino.lipc;

		err = squashfs_read_metadata(sb, sqsh_ino, &block, &offset,
				sizeof(*sqsh_ino));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LFIFO_TYPE)
			inode->i_mode |= S_IFIFO;
		else
			inode->i_mode |= S_IFSOCK;
		xattr_id = le32_to_cpu(sqsh_ino->xattr);
		inode->i_op = &squashfs_inode_ops;
		set_nlink(inode, le32_to_cpu(sqsh_ino->nlink));
		init_special_inode(inode, inode->i_mode, 0);
		break;
	}
	default:
		ERROR("Unknown inode type %d in squashfs_iget!\n", type);
		return -EINVAL;
	}

	if (xattr_id != SQUASHFS_INVALID_XATTR && msblk->xattr_id_table) {
		err = squashfs_xattr_lookup(sb, xattr_id,
					&squashfs_i(inode)->xattr_count,
					&squashfs_i(inode)->xattr_size,
					&squashfs_i(inode)->xattr);
		if (err < 0)
			goto failed_read;
		inode->i_blocks += ((squashfs_i(inode)->xattr_size - 1) >> 9)
				+ 1;
	} else
		squashfs_i(inode)->xattr_count = 0;

	return 0;

failed_read:
	ERROR("Unable to read inode 0x%llx\n", ino);
	return err;
}


const struct inode_operations squashfs_inode_ops = {
	.listxattr = squashfs_listxattr
};

