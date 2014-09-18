/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * dir.c
 */

/*
 * This file implements code to read directories from disk.
 *
 * See namei.c for a description of directory organisation on disk.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"

static const unsigned char squashfs_filetype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_FIFO, DT_SOCK
};

/*
 * Lookup offset (f_pos) in the directory index, returning the
 * metadata block containing it.
 *
 * If we get an error reading the index then return the part of the index
 * (if any) we have managed to read - the index isn't essential, just
 * quicker.
 */
static int get_dir_index_using_offset(struct super_block *sb,
	u64 *next_block, int *next_offset, u64 index_start, int index_offset,
	int i_count, u64 f_pos)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int err, i, index, length = 0;
	unsigned int size;
	struct squashfs_dir_index dir_index;

	TRACE("Entered get_dir_index_using_offset, i_count %d, f_pos %lld\n",
					i_count, f_pos);

	/*
	 * Translate from external f_pos to the internal f_pos.  This
	 * is offset by 3 because we invent "." and ".." entries which are
	 * not actually stored in the directory.
	 */
	if (f_pos <= 3)
		return f_pos;
	f_pos -= 3;

	for (i = 0; i < i_count; i++) {
		err = squashfs_read_metadata(sb, &dir_index, &index_start,
				&index_offset, sizeof(dir_index));
		if (err < 0)
			break;

		index = le32_to_cpu(dir_index.index);
		if (index > f_pos)
			/*
			 * Found the index we're looking for.
			 */
			break;

		size = le32_to_cpu(dir_index.size) + 1;

		/* size should never be larger than SQUASHFS_NAME_LEN */
		if (size > SQUASHFS_NAME_LEN)
			break;

		err = squashfs_read_metadata(sb, NULL, &index_start,
				&index_offset, size);
		if (err < 0)
			break;

		length = index;
		*next_block = le32_to_cpu(dir_index.start_block) +
					msblk->directory_table;
	}

	*next_offset = (length + *next_offset) % SQUASHFS_METADATA_SIZE;

	/*
	 * Translate back from internal f_pos to external f_pos.
	 */
	return length + 3;
}


static int squashfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct squashfs_sb_info *msblk = inode->i_sb->s_fs_info;
	u64 block = squashfs_i(inode)->start + msblk->directory_table;
	int offset = squashfs_i(inode)->offset, length, err;
	unsigned int inode_number, dir_count, size, type;
	struct squashfs_dir_header dirh;
	struct squashfs_dir_entry *dire;

	TRACE("Entered squashfs_readdir [%llx:%x]\n", block, offset);

	dire = kmalloc(sizeof(*dire) + SQUASHFS_NAME_LEN + 1, GFP_KERNEL);
	if (dire == NULL) {
		ERROR("Failed to allocate squashfs_dir_entry\n");
		goto finish;
	}

	/*
	 * Return "." and  ".." entries as the first two filenames in the
	 * directory.  To maximise compression these two entries are not
	 * stored in the directory, and so we invent them here.
	 *
	 * It also means that the external f_pos is offset by 3 from the
	 * on-disk directory f_pos.
	 */
	while (ctx->pos < 3) {
		char *name;
		int i_ino;

		if (ctx->pos == 0) {
			name = ".";
			size = 1;
			i_ino = inode->i_ino;
		} else {
			name = "..";
			size = 2;
			i_ino = squashfs_i(inode)->parent;
		}

		if (!dir_emit(ctx, name, size, i_ino,
				squashfs_filetype_table[1]))
			goto finish;

		ctx->pos += size;
	}

	length = get_dir_index_using_offset(inode->i_sb, &block, &offset,
				squashfs_i(inode)->dir_idx_start,
				squashfs_i(inode)->dir_idx_offset,
				squashfs_i(inode)->dir_idx_cnt,
				ctx->pos);

	while (length < i_size_read(inode)) {
		/*
		 * Read directory header
		 */
		err = squashfs_read_metadata(inode->i_sb, &dirh, &block,
					&offset, sizeof(dirh));
		if (err < 0)
			goto failed_read;

		length += sizeof(dirh);

		dir_count = le32_to_cpu(dirh.count) + 1;

		if (dir_count > SQUASHFS_DIR_COUNT)
			goto failed_read;

		while (dir_count--) {
			/*
			 * Read directory entry.
			 */
			err = squashfs_read_metadata(inode->i_sb, dire, &block,
					&offset, sizeof(*dire));
			if (err < 0)
				goto failed_read;

			size = le16_to_cpu(dire->size) + 1;

			/* size should never be larger than SQUASHFS_NAME_LEN */
			if (size > SQUASHFS_NAME_LEN)
				goto failed_read;

			err = squashfs_read_metadata(inode->i_sb, dire->name,
					&block, &offset, size);
			if (err < 0)
				goto failed_read;

			length += sizeof(*dire) + size;

			if (ctx->pos >= length)
				continue;

			dire->name[size] = '\0';
			inode_number = le32_to_cpu(dirh.inode_number) +
				((short) le16_to_cpu(dire->inode_number));
			type = le16_to_cpu(dire->type);

			if (type > SQUASHFS_MAX_DIR_TYPE)
				goto failed_read;

			if (!dir_emit(ctx, dire->name, size,
					inode_number,
					squashfs_filetype_table[type]))
				goto finish;

			ctx->pos = length;
		}
	}

finish:
	kfree(dire);
	return 0;

failed_read:
	ERROR("Unable to read directory block [%llx:%x]\n", block, offset);
	kfree(dire);
	return 0;
}


const struct file_operations squashfs_dir_ops = {
	.read = generic_read_dir,
	.iterate = squashfs_readdir,
	.llseek = default_llseek,
};
