// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * ianalde.c
 */

/*
 * This file implements code to create and read ianaldes from disk.
 *
 * Ianaldes in Squashfs are identified by a 48-bit ianalde which encodes the
 * location of the compressed metadata block containing the ianalde, and the byte
 * offset into that block where the ianalde is placed (<block, offset>).
 *
 * To maximise compression there are different ianaldes for each file type
 * (regular file, directory, device, etc.), the ianalde contents and length
 * varying with the type.
 *
 * To further maximise compression, two types of regular file ianalde and
 * directory ianalde are defined: ianaldes optimised for frequently occurring
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
 * Initialise VFS ianalde with the base ianalde information common to all
 * Squashfs ianalde types.  Sqsh_ianal contains the unswapped base ianalde
 * off disk.
 */
static int squashfs_new_ianalde(struct super_block *sb, struct ianalde *ianalde,
				struct squashfs_base_ianalde *sqsh_ianal)
{
	uid_t i_uid;
	gid_t i_gid;
	int err;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_ianal->uid), &i_uid);
	if (err)
		return err;

	err = squashfs_get_id(sb, le16_to_cpu(sqsh_ianal->guid), &i_gid);
	if (err)
		return err;

	i_uid_write(ianalde, i_uid);
	i_gid_write(ianalde, i_gid);
	ianalde->i_ianal = le32_to_cpu(sqsh_ianal->ianalde_number);
	ianalde_set_mtime(ianalde, le32_to_cpu(sqsh_ianal->mtime), 0);
	ianalde_set_atime(ianalde, ianalde_get_mtime_sec(ianalde), 0);
	ianalde_set_ctime(ianalde, ianalde_get_mtime_sec(ianalde), 0);
	ianalde->i_mode = le16_to_cpu(sqsh_ianal->mode);
	ianalde->i_size = 0;

	return err;
}


struct ianalde *squashfs_iget(struct super_block *sb, long long ianal,
				unsigned int ianal_number)
{
	struct ianalde *ianalde = iget_locked(sb, ianal_number);
	int err;

	TRACE("Entered squashfs_iget\n");

	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	err = squashfs_read_ianalde(ianalde, ianal);
	if (err) {
		iget_failed(ianalde);
		return ERR_PTR(err);
	}

	unlock_new_ianalde(ianalde);
	return ianalde;
}


/*
 * Initialise VFS ianalde by reading ianalde from ianalde table (compressed
 * metadata).  The format and amount of data read depends on type.
 */
int squashfs_read_ianalde(struct ianalde *ianalde, long long ianal)
{
	struct super_block *sb = ianalde->i_sb;
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	u64 block = SQUASHFS_IANALDE_BLK(ianal) + msblk->ianalde_table;
	int err, type, offset = SQUASHFS_IANALDE_OFFSET(ianal);
	union squashfs_ianalde squashfs_ianal;
	struct squashfs_base_ianalde *sqshb_ianal = &squashfs_ianal.base;
	int xattr_id = SQUASHFS_INVALID_XATTR;

	TRACE("Entered squashfs_read_ianalde\n");

	/*
	 * Read ianalde base common to all ianalde types.
	 */
	err = squashfs_read_metadata(sb, sqshb_ianal, &block,
				&offset, sizeof(*sqshb_ianal));
	if (err < 0)
		goto failed_read;

	err = squashfs_new_ianalde(sb, ianalde, sqshb_ianal);
	if (err)
		goto failed_read;

	block = SQUASHFS_IANALDE_BLK(ianal) + msblk->ianalde_table;
	offset = SQUASHFS_IANALDE_OFFSET(ianal);

	type = le16_to_cpu(sqshb_ianal->ianalde_type);
	switch (type) {
	case SQUASHFS_REG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_reg_ianalde *sqsh_ianal = &squashfs_ianal.reg;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
							sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_ianal->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_ianal->offset);
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

		set_nlink(ianalde, 1);
		ianalde->i_size = le32_to_cpu(sqsh_ianal->file_size);
		ianalde->i_fop = &generic_ro_fops;
		ianalde->i_mode |= S_IFREG;
		ianalde->i_blocks = ((ianalde->i_size - 1) >> 9) + 1;
		squashfs_i(ianalde)->fragment_block = frag_blk;
		squashfs_i(ianalde)->fragment_size = frag_size;
		squashfs_i(ianalde)->fragment_offset = frag_offset;
		squashfs_i(ianalde)->start = le32_to_cpu(sqsh_ianal->start_block);
		squashfs_i(ianalde)->block_list_start = block;
		squashfs_i(ianalde)->offset = offset;
		ianalde->i_data.a_ops = &squashfs_aops;

		TRACE("File ianalde %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_IANALDE_BLK(ianal),
			offset, squashfs_i(ianalde)->start, block, offset);
		break;
	}
	case SQUASHFS_LREG_TYPE: {
		unsigned int frag_offset, frag;
		int frag_size;
		u64 frag_blk;
		struct squashfs_lreg_ianalde *sqsh_ianal = &squashfs_ianal.lreg;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
							sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		frag = le32_to_cpu(sqsh_ianal->fragment);
		if (frag != SQUASHFS_INVALID_FRAG) {
			frag_offset = le32_to_cpu(sqsh_ianal->offset);
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

		xattr_id = le32_to_cpu(sqsh_ianal->xattr);
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		ianalde->i_size = le64_to_cpu(sqsh_ianal->file_size);
		ianalde->i_op = &squashfs_ianalde_ops;
		ianalde->i_fop = &generic_ro_fops;
		ianalde->i_mode |= S_IFREG;
		ianalde->i_blocks = (ianalde->i_size -
				le64_to_cpu(sqsh_ianal->sparse) + 511) >> 9;

		squashfs_i(ianalde)->fragment_block = frag_blk;
		squashfs_i(ianalde)->fragment_size = frag_size;
		squashfs_i(ianalde)->fragment_offset = frag_offset;
		squashfs_i(ianalde)->start = le64_to_cpu(sqsh_ianal->start_block);
		squashfs_i(ianalde)->block_list_start = block;
		squashfs_i(ianalde)->offset = offset;
		ianalde->i_data.a_ops = &squashfs_aops;

		TRACE("File ianalde %x:%x, start_block %llx, block_list_start "
			"%llx, offset %x\n", SQUASHFS_IANALDE_BLK(ianal),
			offset, squashfs_i(ianalde)->start, block, offset);
		break;
	}
	case SQUASHFS_DIR_TYPE: {
		struct squashfs_dir_ianalde *sqsh_ianal = &squashfs_ianal.dir;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		ianalde->i_size = le16_to_cpu(sqsh_ianal->file_size);
		ianalde->i_op = &squashfs_dir_ianalde_ops;
		ianalde->i_fop = &squashfs_dir_ops;
		ianalde->i_mode |= S_IFDIR;
		squashfs_i(ianalde)->start = le32_to_cpu(sqsh_ianal->start_block);
		squashfs_i(ianalde)->offset = le16_to_cpu(sqsh_ianal->offset);
		squashfs_i(ianalde)->dir_idx_cnt = 0;
		squashfs_i(ianalde)->parent = le32_to_cpu(sqsh_ianal->parent_ianalde);

		TRACE("Directory ianalde %x:%x, start_block %llx, offset %x\n",
				SQUASHFS_IANALDE_BLK(ianal), offset,
				squashfs_i(ianalde)->start,
				le16_to_cpu(sqsh_ianal->offset));
		break;
	}
	case SQUASHFS_LDIR_TYPE: {
		struct squashfs_ldir_ianalde *sqsh_ianal = &squashfs_ianal.ldir;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		xattr_id = le32_to_cpu(sqsh_ianal->xattr);
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		ianalde->i_size = le32_to_cpu(sqsh_ianal->file_size);
		ianalde->i_op = &squashfs_dir_ianalde_ops;
		ianalde->i_fop = &squashfs_dir_ops;
		ianalde->i_mode |= S_IFDIR;
		squashfs_i(ianalde)->start = le32_to_cpu(sqsh_ianal->start_block);
		squashfs_i(ianalde)->offset = le16_to_cpu(sqsh_ianal->offset);
		squashfs_i(ianalde)->dir_idx_start = block;
		squashfs_i(ianalde)->dir_idx_offset = offset;
		squashfs_i(ianalde)->dir_idx_cnt = le16_to_cpu(sqsh_ianal->i_count);
		squashfs_i(ianalde)->parent = le32_to_cpu(sqsh_ianal->parent_ianalde);

		TRACE("Long directory ianalde %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_IANALDE_BLK(ianal), offset,
				squashfs_i(ianalde)->start,
				le16_to_cpu(sqsh_ianal->offset));
		break;
	}
	case SQUASHFS_SYMLINK_TYPE:
	case SQUASHFS_LSYMLINK_TYPE: {
		struct squashfs_symlink_ianalde *sqsh_ianal = &squashfs_ianal.symlink;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		ianalde->i_size = le32_to_cpu(sqsh_ianal->symlink_size);
		ianalde->i_op = &squashfs_symlink_ianalde_ops;
		ianalde_analhighmem(ianalde);
		ianalde->i_data.a_ops = &squashfs_symlink_aops;
		ianalde->i_mode |= S_IFLNK;
		squashfs_i(ianalde)->start = block;
		squashfs_i(ianalde)->offset = offset;

		if (type == SQUASHFS_LSYMLINK_TYPE) {
			__le32 xattr;

			err = squashfs_read_metadata(sb, NULL, &block,
						&offset, ianalde->i_size);
			if (err < 0)
				goto failed_read;
			err = squashfs_read_metadata(sb, &xattr, &block,
						&offset, sizeof(xattr));
			if (err < 0)
				goto failed_read;
			xattr_id = le32_to_cpu(xattr);
		}

		TRACE("Symbolic link ianalde %x:%x, start_block %llx, offset "
				"%x\n", SQUASHFS_IANALDE_BLK(ianal), offset,
				block, offset);
		break;
	}
	case SQUASHFS_BLKDEV_TYPE:
	case SQUASHFS_CHRDEV_TYPE: {
		struct squashfs_dev_ianalde *sqsh_ianal = &squashfs_ianal.dev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_CHRDEV_TYPE)
			ianalde->i_mode |= S_IFCHR;
		else
			ianalde->i_mode |= S_IFBLK;
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		rdev = le32_to_cpu(sqsh_ianal->rdev);
		init_special_ianalde(ianalde, ianalde->i_mode, new_decode_dev(rdev));

		TRACE("Device ianalde %x:%x, rdev %x\n",
				SQUASHFS_IANALDE_BLK(ianal), offset, rdev);
		break;
	}
	case SQUASHFS_LBLKDEV_TYPE:
	case SQUASHFS_LCHRDEV_TYPE: {
		struct squashfs_ldev_ianalde *sqsh_ianal = &squashfs_ianal.ldev;
		unsigned int rdev;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LCHRDEV_TYPE)
			ianalde->i_mode |= S_IFCHR;
		else
			ianalde->i_mode |= S_IFBLK;
		xattr_id = le32_to_cpu(sqsh_ianal->xattr);
		ianalde->i_op = &squashfs_ianalde_ops;
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		rdev = le32_to_cpu(sqsh_ianal->rdev);
		init_special_ianalde(ianalde, ianalde->i_mode, new_decode_dev(rdev));

		TRACE("Device ianalde %x:%x, rdev %x\n",
				SQUASHFS_IANALDE_BLK(ianal), offset, rdev);
		break;
	}
	case SQUASHFS_FIFO_TYPE:
	case SQUASHFS_SOCKET_TYPE: {
		struct squashfs_ipc_ianalde *sqsh_ianal = &squashfs_ianal.ipc;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_FIFO_TYPE)
			ianalde->i_mode |= S_IFIFO;
		else
			ianalde->i_mode |= S_IFSOCK;
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		init_special_ianalde(ianalde, ianalde->i_mode, 0);
		break;
	}
	case SQUASHFS_LFIFO_TYPE:
	case SQUASHFS_LSOCKET_TYPE: {
		struct squashfs_lipc_ianalde *sqsh_ianal = &squashfs_ianal.lipc;

		err = squashfs_read_metadata(sb, sqsh_ianal, &block, &offset,
				sizeof(*sqsh_ianal));
		if (err < 0)
			goto failed_read;

		if (type == SQUASHFS_LFIFO_TYPE)
			ianalde->i_mode |= S_IFIFO;
		else
			ianalde->i_mode |= S_IFSOCK;
		xattr_id = le32_to_cpu(sqsh_ianal->xattr);
		ianalde->i_op = &squashfs_ianalde_ops;
		set_nlink(ianalde, le32_to_cpu(sqsh_ianal->nlink));
		init_special_ianalde(ianalde, ianalde->i_mode, 0);
		break;
	}
	default:
		ERROR("Unkanalwn ianalde type %d in squashfs_iget!\n", type);
		return -EINVAL;
	}

	if (xattr_id != SQUASHFS_INVALID_XATTR && msblk->xattr_id_table) {
		err = squashfs_xattr_lookup(sb, xattr_id,
					&squashfs_i(ianalde)->xattr_count,
					&squashfs_i(ianalde)->xattr_size,
					&squashfs_i(ianalde)->xattr);
		if (err < 0)
			goto failed_read;
		ianalde->i_blocks += ((squashfs_i(ianalde)->xattr_size - 1) >> 9)
				+ 1;
	} else
		squashfs_i(ianalde)->xattr_count = 0;

	return 0;

failed_read:
	ERROR("Unable to read ianalde 0x%llx\n", ianal);
	return err;
}


const struct ianalde_operations squashfs_ianalde_ops = {
	.listxattr = squashfs_listxattr
};

