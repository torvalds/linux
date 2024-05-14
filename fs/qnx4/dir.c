// SPDX-License-Identifier: GPL-2.0
/*
 * QNX4 file system, Linux implementation.
 *
 * Version : 0.2.1
 *
 * Using parts of the xiafs filesystem.
 *
 * History :
 *
 * 28-05-1998 by Richard Frowijn : first release.
 * 20-06-1998 by Frank Denis : Linux 2.1.99+ & dcache support.
 */

#include <linux/buffer_head.h>
#include "qnx4.h"

/*
 * A qnx4 directory entry is an inode entry or link info
 * depending on the status field in the last byte. The
 * first byte is where the name start either way, and a
 * zero means it's empty.
 *
 * Also, due to a bug in gcc, we don't want to use the
 * real (differently sized) name arrays in the inode and
 * link entries, but always the 'de_name[]' one in the
 * fake struct entry.
 *
 * See
 *
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99578#c6
 *
 * for details, but basically gcc will take the size of the
 * 'name' array from one of the used union entries randomly.
 *
 * This use of 'de_name[]' (48 bytes) avoids the false positive
 * warnings that would happen if gcc decides to use 'inode.di_name'
 * (16 bytes) even when the pointer and size were to come from
 * 'link.dl_name' (48 bytes).
 *
 * In all cases the actual name pointer itself is the same, it's
 * only the gcc internal 'what is the size of this field' logic
 * that can get confused.
 */
union qnx4_directory_entry {
	struct {
		const char de_name[48];
		u8 de_pad[15];
		u8 de_status;
	};
	struct qnx4_inode_entry inode;
	struct qnx4_link_info link;
};

static int qnx4_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	unsigned int offset;
	struct buffer_head *bh;
	unsigned long blknum;
	int ix, ino;
	int size;

	QNX4DEBUG((KERN_INFO "qnx4_readdir:i_size = %ld\n", (long) inode->i_size));
	QNX4DEBUG((KERN_INFO "pos                 = %ld\n", (long) ctx->pos));

	while (ctx->pos < inode->i_size) {
		blknum = qnx4_block_map(inode, ctx->pos >> QNX4_BLOCK_SIZE_BITS);
		bh = sb_bread(inode->i_sb, blknum);
		if (bh == NULL) {
			printk(KERN_ERR "qnx4_readdir: bread failed (%ld)\n", blknum);
			return 0;
		}
		ix = (ctx->pos >> QNX4_DIR_ENTRY_SIZE_BITS) % QNX4_INODES_PER_BLOCK;
		for (; ix < QNX4_INODES_PER_BLOCK; ix++, ctx->pos += QNX4_DIR_ENTRY_SIZE) {
			union qnx4_directory_entry *de;

			offset = ix * QNX4_DIR_ENTRY_SIZE;
			de = (union qnx4_directory_entry *) (bh->b_data + offset);

			if (!de->de_name[0])
				continue;
			if (!(de->de_status & (QNX4_FILE_USED|QNX4_FILE_LINK)))
				continue;
			if (!(de->de_status & QNX4_FILE_LINK)) {
				size = sizeof(de->inode.di_fname);
				ino = blknum * QNX4_INODES_PER_BLOCK + ix - 1;
			} else {
				size = sizeof(de->link.dl_fname);
				ino = ( le32_to_cpu(de->link.dl_inode_blk) - 1 ) *
					QNX4_INODES_PER_BLOCK +
					de->link.dl_inode_ndx;
			}
			size = strnlen(de->de_name, size);
			QNX4DEBUG((KERN_INFO "qnx4_readdir:%.*s\n", size, name));
			if (!dir_emit(ctx, de->de_name, size, ino, DT_UNKNOWN)) {
				brelse(bh);
				return 0;
			}
		}
		brelse(bh);
	}
	return 0;
}

const struct file_operations qnx4_dir_operations =
{
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= qnx4_readdir,
	.fsync		= generic_file_fsync,
};

const struct inode_operations qnx4_dir_inode_operations =
{
	.lookup		= qnx4_lookup,
};
