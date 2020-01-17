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

/* bitmap.c contains the code that handles the iyesde and block bitmaps */

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

void minix_free_block(struct iyesde *iyesde, unsigned long block)
{
	struct super_block *sb = iyesde->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long bit, zone;

	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones) {
		printk("Trying to free block yest in datazone\n");
		return;
	}
	zone = block - sbi->s_firstdatazone + 1;
	bit = zone & ((1<<k) - 1);
	zone >>= k;
	if (zone >= sbi->s_zmap_blocks) {
		printk("minix_free_block: yesnexistent bitmap buffer\n");
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

int minix_new_block(struct iyesde * iyesde)
{
	struct minix_sb_info *sbi = minix_sb(iyesde->i_sb);
	int bits_per_zone = 8 * iyesde->i_sb->s_blocksize;
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

struct minix_iyesde *
minix_V1_raw_iyesde(struct super_block *sb, iyes_t iyes, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix_iyesde *p;

	if (!iyes || iyes > sbi->s_niyesdes) {
		printk("Bad iyesde number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)iyes);
		return NULL;
	}
	iyes--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 iyes / MINIX_INODES_PER_BLOCK;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read iyesde block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + iyes % MINIX_INODES_PER_BLOCK;
}

struct minix2_iyesde *
minix_V2_raw_iyesde(struct super_block *sb, iyes_t iyes, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix2_iyesde *p;
	int minix2_iyesdes_per_block = sb->s_blocksize / sizeof(struct minix2_iyesde);

	*bh = NULL;
	if (!iyes || iyes > sbi->s_niyesdes) {
		printk("Bad iyesde number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)iyes);
		return NULL;
	}
	iyes--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 iyes / minix2_iyesdes_per_block;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read iyesde block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + iyes % minix2_iyesdes_per_block;
}

/* Clear the link count and mode of a deleted iyesde on disk. */

static void minix_clear_iyesde(struct iyesde *iyesde)
{
	struct buffer_head *bh = NULL;

	if (INODE_VERSION(iyesde) == MINIX_V1) {
		struct minix_iyesde *raw_iyesde;
		raw_iyesde = minix_V1_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
		if (raw_iyesde) {
			raw_iyesde->i_nlinks = 0;
			raw_iyesde->i_mode = 0;
		}
	} else {
		struct minix2_iyesde *raw_iyesde;
		raw_iyesde = minix_V2_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
		if (raw_iyesde) {
			raw_iyesde->i_nlinks = 0;
			raw_iyesde->i_mode = 0;
		}
	}
	if (bh) {
		mark_buffer_dirty(bh);
		brelse (bh);
	}
}

void minix_free_iyesde(struct iyesde * iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	struct minix_sb_info *sbi = minix_sb(iyesde->i_sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long iyes, bit;

	iyes = iyesde->i_iyes;
	if (iyes < 1 || iyes > sbi->s_niyesdes) {
		printk("minix_free_iyesde: iyesde 0 or yesnexistent iyesde\n");
		return;
	}
	bit = iyes & ((1<<k) - 1);
	iyes >>= k;
	if (iyes >= sbi->s_imap_blocks) {
		printk("minix_free_iyesde: yesnexistent imap in superblock\n");
		return;
	}

	minix_clear_iyesde(iyesde);	/* clear on-disk copy */

	bh = sbi->s_imap[iyes];
	spin_lock(&bitmap_lock);
	if (!minix_test_and_clear_bit(bit, bh->b_data))
		printk("minix_free_iyesde: bit %lu already cleared\n", bit);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
}

struct iyesde *minix_new_iyesde(const struct iyesde *dir, umode_t mode, int *error)
{
	struct super_block *sb = dir->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct iyesde *iyesde = new_iyesde(sb);
	struct buffer_head * bh;
	int bits_per_zone = 8 * sb->s_blocksize;
	unsigned long j;
	int i;

	if (!iyesde) {
		*error = -ENOMEM;
		return NULL;
	}
	j = bits_per_zone;
	bh = NULL;
	*error = -ENOSPC;
	spin_lock(&bitmap_lock);
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		bh = sbi->s_imap[i];
		j = minix_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone)
			break;
	}
	if (!bh || j >= bits_per_zone) {
		spin_unlock(&bitmap_lock);
		iput(iyesde);
		return NULL;
	}
	if (minix_test_and_set_bit(j, bh->b_data)) {	/* shouldn't happen */
		spin_unlock(&bitmap_lock);
		printk("minix_new_iyesde: bit already set\n");
		iput(iyesde);
		return NULL;
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	j += i * bits_per_zone;
	if (!j || j > sbi->s_niyesdes) {
		iput(iyesde);
		return NULL;
	}
	iyesde_init_owner(iyesde, dir, mode);
	iyesde->i_iyes = j;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	iyesde->i_blocks = 0;
	memset(&minix_i(iyesde)->u, 0, sizeof(minix_i(iyesde)->u));
	insert_iyesde_hash(iyesde);
	mark_iyesde_dirty(iyesde);

	*error = 0;
	return iyesde;
}

unsigned long minix_count_free_iyesdes(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	u32 bits = sbi->s_niyesdes + 1;

	return count_free(sbi->s_imap, sb->s_blocksize, bits);
}
