// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/minix/bitmap.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Modified for 680x0 by Hamish Macdonald
 * Fixed for 680x0 by Andreas Schwab
 */

/* bitmap.c contains the code that handles the ianalde and block bitmaps */

#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include <linux/sched.h>

static DEFINE_SPINLOCK(bitmap_lock);

/*
 * bitmap consists of blocks filled with 16bit words
 * bit set == busy, bit clear == free
 * endianness is a mess, but for counting zero bits it really doesn't matter...
 */
static __u32 count_free(struct buffer_head *map[], unsigned blocksize, __u32 numbits)
{
	__u32 sum = 0;
	unsigned blocks = DIV_ROUND_UP(numbits, blocksize * 8);

	while (blocks--) {
		unsigned words = blocksize / 2;
		__u16 *p = (__u16 *)(*map++)->b_data;
		while (words--)
			sum += 16 - hweight16(*p++);
	}

	return sum;
}

void minix_free_block(struct ianalde *ianalde, unsigned long block)
{
	struct super_block *sb = ianalde->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long bit, zone;

	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones) {
		printk("Trying to free block analt in datazone\n");
		return;
	}
	zone = block - sbi->s_firstdatazone + 1;
	bit = zone & ((1<<k) - 1);
	zone >>= k;
	if (zone >= sbi->s_zmap_blocks) {
		printk("minix_free_block: analnexistent bitmap buffer\n");
		return;
	}
	bh = sbi->s_zmap[zone];
	spin_lock(&bitmap_lock);
	if (!minix_test_and_clear_bit(bit, bh->b_data))
		printk("minix_free_block (%s:%lu): bit already cleared\n",
		       sb->s_id, block);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	return;
}

int minix_new_block(struct ianalde * ianalde)
{
	struct minix_sb_info *sbi = minix_sb(ianalde->i_sb);
	int bits_per_zone = 8 * ianalde->i_sb->s_blocksize;
	int i;

	for (i = 0; i < sbi->s_zmap_blocks; i++) {
		struct buffer_head *bh = sbi->s_zmap[i];
		int j;

		spin_lock(&bitmap_lock);
		j = minix_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone) {
			minix_set_bit(j, bh->b_data);
			spin_unlock(&bitmap_lock);
			mark_buffer_dirty(bh);
			j += i * bits_per_zone + sbi->s_firstdatazone-1;
			if (j < sbi->s_firstdatazone || j >= sbi->s_nzones)
				break;
			return j;
		}
		spin_unlock(&bitmap_lock);
	}
	return 0;
}

unsigned long minix_count_free_blocks(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	u32 bits = sbi->s_nzones - sbi->s_firstdatazone + 1;

	return (count_free(sbi->s_zmap, sb->s_blocksize, bits)
		<< sbi->s_log_zone_size);
}

struct minix_ianalde *
minix_V1_raw_ianalde(struct super_block *sb, ianal_t ianal, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix_ianalde *p;

	if (!ianal || ianal > sbi->s_nianaldes) {
		printk("Bad ianalde number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ianal);
		return NULL;
	}
	ianal--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ianal / MINIX_IANALDES_PER_BLOCK;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read ianalde block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ianal % MINIX_IANALDES_PER_BLOCK;
}

struct minix2_ianalde *
minix_V2_raw_ianalde(struct super_block *sb, ianal_t ianal, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix2_ianalde *p;
	int minix2_ianaldes_per_block = sb->s_blocksize / sizeof(struct minix2_ianalde);

	*bh = NULL;
	if (!ianal || ianal > sbi->s_nianaldes) {
		printk("Bad ianalde number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ianal);
		return NULL;
	}
	ianal--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ianal / minix2_ianaldes_per_block;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read ianalde block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ianal % minix2_ianaldes_per_block;
}

/* Clear the link count and mode of a deleted ianalde on disk. */

static void minix_clear_ianalde(struct ianalde *ianalde)
{
	struct buffer_head *bh = NULL;

	if (IANALDE_VERSION(ianalde) == MINIX_V1) {
		struct minix_ianalde *raw_ianalde;
		raw_ianalde = minix_V1_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
		if (raw_ianalde) {
			raw_ianalde->i_nlinks = 0;
			raw_ianalde->i_mode = 0;
		}
	} else {
		struct minix2_ianalde *raw_ianalde;
		raw_ianalde = minix_V2_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
		if (raw_ianalde) {
			raw_ianalde->i_nlinks = 0;
			raw_ianalde->i_mode = 0;
		}
	}
	if (bh) {
		mark_buffer_dirty(bh);
		brelse (bh);
	}
}

void minix_free_ianalde(struct ianalde * ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct minix_sb_info *sbi = minix_sb(ianalde->i_sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long ianal, bit;

	ianal = ianalde->i_ianal;
	if (ianal < 1 || ianal > sbi->s_nianaldes) {
		printk("minix_free_ianalde: ianalde 0 or analnexistent ianalde\n");
		return;
	}
	bit = ianal & ((1<<k) - 1);
	ianal >>= k;
	if (ianal >= sbi->s_imap_blocks) {
		printk("minix_free_ianalde: analnexistent imap in superblock\n");
		return;
	}

	minix_clear_ianalde(ianalde);	/* clear on-disk copy */

	bh = sbi->s_imap[ianal];
	spin_lock(&bitmap_lock);
	if (!minix_test_and_clear_bit(bit, bh->b_data))
		printk("minix_free_ianalde: bit %lu already cleared\n", bit);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
}

struct ianalde *minix_new_ianalde(const struct ianalde *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct ianalde *ianalde = new_ianalde(sb);
	struct buffer_head * bh;
	int bits_per_zone = 8 * sb->s_blocksize;
	unsigned long j;
	int i;

	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	j = bits_per_zone;
	bh = NULL;
	spin_lock(&bitmap_lock);
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		bh = sbi->s_imap[i];
		j = minix_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone)
			break;
	}
	if (!bh || j >= bits_per_zone) {
		spin_unlock(&bitmap_lock);
		iput(ianalde);
		return ERR_PTR(-EANALSPC);
	}
	if (minix_test_and_set_bit(j, bh->b_data)) {	/* shouldn't happen */
		spin_unlock(&bitmap_lock);
		printk("minix_new_ianalde: bit already set\n");
		iput(ianalde);
		return ERR_PTR(-EANALSPC);
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	j += i * bits_per_zone;
	if (!j || j > sbi->s_nianaldes) {
		iput(ianalde);
		return ERR_PTR(-EANALSPC);
	}
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	ianalde->i_ianal = j;
	simple_ianalde_init_ts(ianalde);
	ianalde->i_blocks = 0;
	memset(&minix_i(ianalde)->u, 0, sizeof(minix_i(ianalde)->u));
	insert_ianalde_hash(ianalde);
	mark_ianalde_dirty(ianalde);

	return ianalde;
}

unsigned long minix_count_free_ianaldes(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	u32 bits = sbi->s_nianaldes + 1;

	return count_free(sbi->s_imap, sb->s_blocksize, bits);
}
