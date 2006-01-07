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
 * 20-06-1998 by Frank Denis : basic optimisations.
 * 25-06-1998 by Frank Denis : qnx4_is_free, qnx4_set_bitmap, qnx4_bmap .
 * 28-06-1998 by Frank Denis : qnx4_free_inode (to be fixed) .
 */

#include <linux/config.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/qnx4_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/bitops.h>

#if 0
int qnx4_new_block(struct super_block *sb)
{
	return 0;
}
#endif  /*  0  */

static void count_bits(register const char *bmPart, register int size,
		       int *const tf)
{
	char b;
	int tot = *tf;

	if (size > QNX4_BLOCK_SIZE) {
		size = QNX4_BLOCK_SIZE;
	}
	do {
		b = *bmPart++;
		if ((b & 1) == 0)
			tot++;
		if ((b & 2) == 0)
			tot++;
		if ((b & 4) == 0)
			tot++;
		if ((b & 8) == 0)
			tot++;
		if ((b & 16) == 0)
			tot++;
		if ((b & 32) == 0)
			tot++;
		if ((b & 64) == 0)
			tot++;
		if ((b & 128) == 0)
			tot++;
		size--;
	} while (size != 0);
	*tf = tot;
}

unsigned long qnx4_count_free_blocks(struct super_block *sb)
{
	int start = le32_to_cpu(qnx4_sb(sb)->BitMap->di_first_xtnt.xtnt_blk) - 1;
	int total = 0;
	int total_free = 0;
	int offset = 0;
	int size = le32_to_cpu(qnx4_sb(sb)->BitMap->di_size);
	struct buffer_head *bh;

	while (total < size) {
		if ((bh = sb_bread(sb, start + offset)) == NULL) {
			printk("qnx4: I/O error in counting free blocks\n");
			break;
		}
		count_bits(bh->b_data, size - total, &total_free);
		brelse(bh);
		total += QNX4_BLOCK_SIZE;
		offset++;
	}

	return total_free;
}

#ifdef CONFIG_QNX4FS_RW

int qnx4_is_free(struct super_block *sb, long block)
{
	int start = le32_to_cpu(qnx4_sb(sb)->BitMap->di_first_xtnt.xtnt_blk) - 1;
	int size = le32_to_cpu(qnx4_sb(sb)->BitMap->di_size);
	struct buffer_head *bh;
	const char *g;
	int ret = -EIO;

	start += block / (QNX4_BLOCK_SIZE * 8);
	QNX4DEBUG(("qnx4: is_free requesting block [%lu], bitmap in block [%lu]\n",
		   (unsigned long) block, (unsigned long) start));
	(void) size;		/* CHECKME */
	bh = sb_bread(sb, start);
	if (bh == NULL) {
		return -EIO;
	}
	g = bh->b_data + (block % QNX4_BLOCK_SIZE);
	if (((*g) & (1 << (block % 8))) == 0) {
		QNX4DEBUG(("qnx4: is_free -> block is free\n"));
		ret = 1;
	} else {
		QNX4DEBUG(("qnx4: is_free -> block is busy\n"));
		ret = 0;
	}
	brelse(bh);

	return ret;
}

int qnx4_set_bitmap(struct super_block *sb, long block, int busy)
{
	int start = le32_to_cpu(qnx4_sb(sb)->BitMap->di_first_xtnt.xtnt_blk) - 1;
	int size = le32_to_cpu(qnx4_sb(sb)->BitMap->di_size);
	struct buffer_head *bh;
	char *g;

	start += block / (QNX4_BLOCK_SIZE * 8);
	QNX4DEBUG(("qnx4: set_bitmap requesting block [%lu], bitmap in block [%lu]\n",
		   (unsigned long) block, (unsigned long) start));
	(void) size;		/* CHECKME */
	bh = sb_bread(sb, start);
	if (bh == NULL) {
		return -EIO;
	}
	g = bh->b_data + (block % QNX4_BLOCK_SIZE);
	if (busy == 0) {
		(*g) &= ~(1 << (block % 8));
	} else {
		(*g) |= (1 << (block % 8));
	}
	mark_buffer_dirty(bh);
	brelse(bh);

	return 0;
}

static void qnx4_clear_inode(struct inode *inode)
{
	struct qnx4_inode_entry *qnx4_ino = qnx4_raw_inode(inode);
	/* What for? */
	memset(qnx4_ino->di_fname, 0, sizeof qnx4_ino->di_fname);
	qnx4_ino->di_size = 0;
	qnx4_ino->di_num_xtnts = 0;
	qnx4_ino->di_mode = 0;
	qnx4_ino->di_status = 0;
}

void qnx4_free_inode(struct inode *inode)
{
	if (inode->i_ino < 1) {
		printk("free_inode: inode 0 or nonexistent inode\n");
		return;
	}
	qnx4_clear_inode(inode);
	clear_inode(inode);
}

#endif
