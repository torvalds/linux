// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/buffer_head.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

#if BITS_PER_LONG == 32
#define __le_long __le32
#define lel_to_cpu(A) le32_to_cpu(A)
#define cpu_to_lel(A) cpu_to_le32(A)
#elif BITS_PER_LONG == 64
#define __le_long __le64
#define lel_to_cpu(A) le64_to_cpu(A)
#define cpu_to_lel(A) cpu_to_le64(A)
#else
#error "BITS_PER_LONG not 32 or 64"
#endif

/*
 *  Allocation Bitmap Management Functions
 */
static int exfat_allocate_bitmap(struct super_block *sb,
		struct exfat_dentry *ep)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long long map_size;
	unsigned int i, need_map_size;
	sector_t sector;

	sbi->map_clu = le32_to_cpu(ep->dentry.bitmap.start_clu);
	map_size = le64_to_cpu(ep->dentry.bitmap.size);
	need_map_size = ((EXFAT_DATA_CLUSTER_COUNT(sbi) - 1) / BITS_PER_BYTE)
		+ 1;
	if (need_map_size != map_size) {
		exfat_err(sb, "bogus allocation bitmap size(need : %u, cur : %lld)",
			  need_map_size, map_size);
		/*
		 * Only allowed when bogus allocation
		 * bitmap size is large
		 */
		if (need_map_size > map_size)
			return -EIO;
	}
	sbi->map_sectors = ((need_map_size - 1) >>
			(sb->s_blocksize_bits)) + 1;
	sbi->vol_amap = kvmalloc_array(sbi->map_sectors,
				sizeof(struct buffer_head *), GFP_KERNEL);
	if (!sbi->vol_amap)
		return -ENOMEM;

	sector = exfat_cluster_to_sector(sbi, sbi->map_clu);
	for (i = 0; i < sbi->map_sectors; i++) {
		sbi->vol_amap[i] = sb_bread(sb, sector + i);
		if (!sbi->vol_amap[i]) {
			/* release all buffers and free vol_amap */
			int j = 0;

			while (j < i)
				brelse(sbi->vol_amap[j++]);

			kvfree(sbi->vol_amap);
			sbi->vol_amap = NULL;
			return -EIO;
		}
	}

	return 0;
}

int exfat_load_bitmap(struct super_block *sb)
{
	unsigned int i, type;
	struct exfat_chain clu;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	exfat_chain_set(&clu, sbi->root_dir, 0, ALLOC_FAT_CHAIN);
	while (clu.dir != EXFAT_EOF_CLUSTER) {
		for (i = 0; i < sbi->dentries_per_clu; i++) {
			struct exfat_dentry *ep;
			struct buffer_head *bh;

			ep = exfat_get_dentry(sb, &clu, i, &bh);
			if (!ep)
				return -EIO;

			type = exfat_get_entry_type(ep);
			if (type == TYPE_BITMAP &&
			    ep->dentry.bitmap.flags == 0x0) {
				int err;

				err = exfat_allocate_bitmap(sb, ep);
				brelse(bh);
				return err;
			}
			brelse(bh);

			if (type == TYPE_UNUSED)
				return -EINVAL;
		}

		if (exfat_get_next_cluster(sb, &clu.dir))
			return -EIO;
	}

	return -EINVAL;
}

void exfat_free_bitmap(struct exfat_sb_info *sbi)
{
	int i;

	for (i = 0; i < sbi->map_sectors; i++)
		__brelse(sbi->vol_amap[i]);

	kvfree(sbi->vol_amap);
}

int exfat_set_bitmap(struct inode *inode, unsigned int clu, bool sync)
{
	int i, b;
	unsigned int ent_idx;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	if (!is_valid_cluster(sbi, clu))
		return -EINVAL;

	ent_idx = CLUSTER_TO_BITMAP_ENT(clu);
	i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	b = BITMAP_OFFSET_BIT_IN_SECTOR(sb, ent_idx);

	set_bit_le(b, sbi->vol_amap[i]->b_data);
	exfat_update_bh(sbi->vol_amap[i], sync);
	return 0;
}

void exfat_clear_bitmap(struct inode *inode, unsigned int clu, bool sync)
{
	int i, b;
	unsigned int ent_idx;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *opts = &sbi->options;

	if (!is_valid_cluster(sbi, clu))
		return;

	ent_idx = CLUSTER_TO_BITMAP_ENT(clu);
	i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	b = BITMAP_OFFSET_BIT_IN_SECTOR(sb, ent_idx);

	clear_bit_le(b, sbi->vol_amap[i]->b_data);
	exfat_update_bh(sbi->vol_amap[i], sync);

	if (opts->discard) {
		int ret_discard;

		ret_discard = sb_issue_discard(sb,
			exfat_cluster_to_sector(sbi, clu),
			(1 << sbi->sect_per_clus_bits), GFP_NOFS, 0);

		if (ret_discard == -EOPNOTSUPP) {
			exfat_err(sb, "discard not supported by device, disabling");
			opts->discard = 0;
		}
	}
}

/*
 * If the value of "clu" is 0, it means cluster 2 which is the first cluster of
 * the cluster heap.
 */
unsigned int exfat_find_free_bitmap(struct super_block *sb, unsigned int clu)
{
	unsigned int i, map_i, map_b, ent_idx;
	unsigned int clu_base, clu_free;
	unsigned long clu_bits, clu_mask;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	__le_long bitval;

	WARN_ON(clu < EXFAT_FIRST_CLUSTER);
	ent_idx = ALIGN_DOWN(CLUSTER_TO_BITMAP_ENT(clu), BITS_PER_LONG);
	clu_base = BITMAP_ENT_TO_CLUSTER(ent_idx);
	clu_mask = IGNORED_BITS_REMAINED(clu, clu_base);

	map_i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	map_b = BITMAP_OFFSET_BYTE_IN_SECTOR(sb, ent_idx);

	for (i = EXFAT_FIRST_CLUSTER; i < sbi->num_clusters;
	     i += BITS_PER_LONG) {
		bitval = *(__le_long *)(sbi->vol_amap[map_i]->b_data + map_b);
		if (clu_mask > 0) {
			bitval |= cpu_to_lel(clu_mask);
			clu_mask = 0;
		}
		if (lel_to_cpu(bitval) != ULONG_MAX) {
			clu_bits = lel_to_cpu(bitval);
			clu_free = clu_base + ffz(clu_bits);
			if (clu_free < sbi->num_clusters)
				return clu_free;
		}
		clu_base += BITS_PER_LONG;
		map_b += sizeof(long);

		if (map_b >= sb->s_blocksize ||
		    clu_base >= sbi->num_clusters) {
			if (++map_i >= sbi->map_sectors) {
				clu_base = EXFAT_FIRST_CLUSTER;
				map_i = 0;
			}
			map_b = 0;
		}
	}

	return EXFAT_EOF_CLUSTER;
}

int exfat_count_used_clusters(struct super_block *sb, unsigned int *ret_count)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned int count = 0;
	unsigned int i, map_i = 0, map_b = 0;
	unsigned int total_clus = EXFAT_DATA_CLUSTER_COUNT(sbi);
	unsigned int last_mask = total_clus & (BITS_PER_LONG - 1);
	unsigned long *bitmap, clu_bits;

	total_clus &= ~last_mask;
	for (i = 0; i < total_clus; i += BITS_PER_LONG) {
		bitmap = (void *)(sbi->vol_amap[map_i]->b_data + map_b);
		count += hweight_long(*bitmap);
		map_b += sizeof(long);
		if (map_b >= (unsigned int)sb->s_blocksize) {
			map_i++;
			map_b = 0;
		}
	}

	if (last_mask) {
		bitmap = (void *)(sbi->vol_amap[map_i]->b_data + map_b);
		clu_bits = lel_to_cpu(*(__le_long *)bitmap);
		count += hweight_long(clu_bits & BITMAP_LAST_WORD_MASK(last_mask));
	}

	*ret_count = count;
	return 0;
}

int exfat_trim_fs(struct inode *inode, struct fstrim_range *range)
{
	unsigned int trim_begin, trim_end, count, next_free_clu;
	u64 clu_start, clu_end, trim_minlen, trimmed_total = 0;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int err = 0;

	clu_start = max_t(u64, range->start >> sbi->cluster_size_bits,
				EXFAT_FIRST_CLUSTER);
	clu_end = clu_start + (range->len >> sbi->cluster_size_bits) - 1;
	trim_minlen = range->minlen >> sbi->cluster_size_bits;

	if (clu_start >= sbi->num_clusters || range->len < sbi->cluster_size)
		return -EINVAL;

	if (clu_end >= sbi->num_clusters)
		clu_end = sbi->num_clusters - 1;

	mutex_lock(&sbi->bitmap_lock);

	trim_begin = trim_end = exfat_find_free_bitmap(sb, clu_start);
	if (trim_begin == EXFAT_EOF_CLUSTER)
		goto unlock;

	next_free_clu = exfat_find_free_bitmap(sb, trim_end + 1);
	if (next_free_clu == EXFAT_EOF_CLUSTER)
		goto unlock;

	do {
		if (next_free_clu == trim_end + 1) {
			/* extend trim range for continuous free cluster */
			trim_end++;
		} else {
			/* trim current range if it's larger than trim_minlen */
			count = trim_end - trim_begin + 1;
			if (count >= trim_minlen) {
				err = sb_issue_discard(sb,
					exfat_cluster_to_sector(sbi, trim_begin),
					count * sbi->sect_per_clus, GFP_NOFS, 0);
				if (err)
					goto unlock;

				trimmed_total += count;
			}

			/* set next start point of the free hole */
			trim_begin = trim_end = next_free_clu;
		}

		if (next_free_clu >= clu_end)
			break;

		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			goto unlock;
		}

		next_free_clu = exfat_find_free_bitmap(sb, next_free_clu + 1);
	} while (next_free_clu != EXFAT_EOF_CLUSTER &&
			next_free_clu > trim_end);

	/* try to trim remainder */
	count = trim_end - trim_begin + 1;
	if (count >= trim_minlen) {
		err = sb_issue_discard(sb, exfat_cluster_to_sector(sbi, trim_begin),
			count * sbi->sect_per_clus, GFP_NOFS, 0);
		if (err)
			goto unlock;

		trimmed_total += count;
	}

unlock:
	mutex_unlock(&sbi->bitmap_lock);
	range->len = trimmed_total << sbi->cluster_size_bits;

	return err;
}
