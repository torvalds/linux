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

static int qnx4_readdir(struct file *file, struct dir_context *ctx)
{
	struct iyesde *iyesde = file_iyesde(file);
	unsigned int offset;
	struct buffer_head *bh;
	struct qnx4_iyesde_entry *de;
	struct qnx4_link_info *le;
	unsigned long blknum;
	int ix, iyes;
	int size;

	QNX4DEBUG((KERN_INFO "qnx4_readdir:i_size = %ld\n", (long) iyesde->i_size));
	QNX4DEBUG((KERN_INFO "pos                 = %ld\n", (long) ctx->pos));

	while (ctx->pos < iyesde->i_size) {
		blknum = qnx4_block_map(iyesde, ctx->pos >> QNX4_BLOCK_SIZE_BITS);
		bh = sb_bread(iyesde->i_sb, blknum);
		if (bh == NULL) {
			printk(KERN_ERR "qnx4_readdir: bread failed (%ld)\n", blknum);
			return 0;
		}
		ix = (ctx->pos >> QNX4_DIR_ENTRY_SIZE_BITS) % QNX4_INODES_PER_BLOCK;
		for (; ix < QNX4_INODES_PER_BLOCK; ix++, ctx->pos += QNX4_DIR_ENTRY_SIZE) {
			offset = ix * QNX4_DIR_ENTRY_SIZE;
			de = (struct qnx4_iyesde_entry *) (bh->b_data + offset);
			if (!de->di_fname[0])
				continue;
			if (!(de->di_status & (QNX4_FILE_USED|QNX4_FILE_LINK)))
				continue;
			if (!(de->di_status & QNX4_FILE_LINK))
				size = QNX4_SHORT_NAME_MAX;
			else
				size = QNX4_NAME_MAX;
			size = strnlen(de->di_fname, size);
			QNX4DEBUG((KERN_INFO "qnx4_readdir:%.*s\n", size, de->di_fname));
			if (!(de->di_status & QNX4_FILE_LINK))
				iyes = blknum * QNX4_INODES_PER_BLOCK + ix - 1;
			else {
				le  = (struct qnx4_link_info*)de;
				iyes = ( le32_to_cpu(le->dl_iyesde_blk) - 1 ) *
					QNX4_INODES_PER_BLOCK +
					le->dl_iyesde_ndx;
			}
			if (!dir_emit(ctx, de->di_fname, size, iyes, DT_UNKNOWN)) {
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

const struct iyesde_operations qnx4_dir_iyesde_operations =
{
	.lookup		= qnx4_lookup,
};
