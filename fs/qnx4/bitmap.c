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

#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include "qnx4.h"

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
