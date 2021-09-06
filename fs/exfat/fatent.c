// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/buffer_head.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static int exfat_mirror_bh(struct super_block *sb, sector_t sec,
		struct buffer_head *bh)
{
	struct buffer_head *c_bh;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	sector_t sec2;
	int err = 0;

	if (sbi->FAT2_start_sector != sbi->FAT1_start_sector) {
		sec2 = sec - sbi->FAT1_start_sector + sbi->FAT2_start_sector;
		c_bh = sb_getblk(sb, sec2);
		if (!c_bh)
			return -ENOMEM;
		memcpy(c_bh->b_data, bh->b_data, sb->s_blocksize);
		set_buffer_uptodate(c_bh);
		mark_buffer_dirty(c_bh);
		if (sb->s_flags & SB_SYNCHRONOUS)
			err = sync_dirty_buffer(c_bh);
		brelse(c_bh);
	}

	return err;
}

static int __exfat_ent_get(struct super_block *sb, unsigned int loc,
		unsigned int *content)
{
	unsigned int off;
	sector_t sec;
	struct buffer_head *bh;

	sec = FAT_ENT_OFFSET_SECTOR(sb, loc);
	off = FAT_ENT_OFFSET_BYTE_IN_SECTOR(sb, loc);

	bh = sb_bread(sb, sec);
	if (!bh)
		return -EIO;

	*content = le32_to_cpu(*(__le32 *)(&bh->b_data[off]));

	/* remap reserved clusters to simplify code */
	if (*content > EXFAT_BAD_CLUSTER)
		*content = EXFAT_EOF_CLUSTER;

	brelse(bh);
	return 0;
}

int exfat_ent_set(struct super_block *sb, unsigned int loc,
		unsigned int content)
{
	unsigned int off;
	sector_t sec;
	__le32 *fat_entry;
	struct buffer_head *bh;

	sec = FAT_ENT_OFFSET_SECTOR(sb, loc);
	off = FAT_ENT_OFFSET_BYTE_IN_SECTOR(sb, loc);

	bh = sb_bread(sb, sec);
	if (!bh)
		return -EIO;

	fat_entry = (__le32 *)&(bh->b_data[off]);
	*fat_entry = cpu_to_le32(content);
	exfat_update_bh(bh, sb->s_flags & SB_SYNCHRONOUS);
	exfat_mirror_bh(sb, sec, bh);
	brelse(bh);
	return 0;
}

static inline bool is_valid_cluster(struct exfat_sb_info *sbi,
		unsigned int clus)
{
	if (clus < EXFAT_FIRST_CLUSTER || sbi->num_clusters <= clus)
		return false;
	return true;
}

int exfat_ent_get(struct super_block *sb, unsigned int loc,
		unsigned int *content)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int err;

	if (!is_valid_cluster(sbi, loc)) {
		exfat_fs_error(sb, "invalid access to FAT (entry 0x%08x)",
			loc);
		return -EIO;
	}

	err = __exfat_ent_get(sb, loc, content);
	if (err) {
		exfat_fs_error(sb,
			"failed to access to FAT (entry 0x%08x, err:%d)",
			loc, err);
		return err;
	}

	if (*content == EXFAT_FREE_CLUSTER) {
		exfat_fs_error(sb,
			"invalid access to FAT free cluster (entry 0x%08x)",
			loc);
		return -EIO;
	}

	if (*content == EXFAT_BAD_CLUSTER) {
		exfat_fs_error(sb,
			"invalid access to FAT bad cluster (entry 0x%08x)",
			loc);
		return -EIO;
	}

	if (*content != EXFAT_EOF_CLUSTER && !is_valid_cluster(sbi, *content)) {
		exfat_fs_error(sb,
			"invalid access to FAT (entry 0x%08x) bogus content (0x%08x)",
			loc, *content);
		return -EIO;
	}

	return 0;
}

int exfat_chain_cont_cluster(struct super_block *sb, unsigned int chain,
		unsigned int len)
{
	if (!len)
		return 0;

	while (len > 1) {
		if (exfat_ent_set(sb, chain, chain + 1))
			return -EIO;
		chain++;
		len--;
	}

	if (exfat_ent_set(sb, chain, EXFAT_EOF_CLUSTER))
		return -EIO;
	return 0;
}

/* This function must be called with bitmap_lock held */
static int __exfat_free_cluster(struct inode *inode, struct exfat_chain *p_chain)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int cur_cmap_i, next_cmap_i;
	unsigned int num_clusters = 0;
	unsigned int clu;

	/* invalid cluster number */
	if (p_chain->dir == EXFAT_FREE_CLUSTER ||
	    p_chain->dir == EXFAT_EOF_CLUSTER ||
	    p_chain->dir < EXFAT_FIRST_CLUSTER)
		return 0;

	/* no cluster to truncate */
	if (p_chain->size == 0)
		return 0;

	/* check cluster validation */
	if (!is_valid_cluster(sbi, p_chain->dir)) {
		exfat_err(sb, "invalid start cluster (%u)", p_chain->dir);
		return -EIO;
	}

	clu = p_chain->dir;

	cur_cmap_i = next_cmap_i =
		BITMAP_OFFSET_SECTOR_INDEX(sb, CLUSTER_TO_BITMAP_ENT(clu));

	if (p_chain->flags == ALLOC_NO_FAT_CHAIN) {
		unsigned int last_cluster = p_chain->dir + p_chain->size - 1;
		do {
			bool sync = false;

			if (clu < last_cluster)
				next_cmap_i =
				  BITMAP_OFFSET_SECTOR_INDEX(sb, CLUSTER_TO_BITMAP_ENT(clu+1));

			/* flush bitmap only if index would be changed or for last cluster */
			if (clu == last_cluster || cur_cmap_i != next_cmap_i) {
				sync = true;
				cur_cmap_i = next_cmap_i;
			}

			exfat_clear_bitmap(inode, clu, (sync && IS_DIRSYNC(inode)));
			clu++;
			num_clusters++;
		} while (num_clusters < p_chain->size);
	} else {
		do {
			bool sync = false;
			unsigned int n_clu = clu;
			int err = exfat_get_next_cluster(sb, &n_clu);

			if (err || n_clu == EXFAT_EOF_CLUSTER)
				sync = true;
			else
				next_cmap_i =
				  BITMAP_OFFSET_SECTOR_INDEX(sb, CLUSTER_TO_BITMAP_ENT(n_clu));

			if (cur_cmap_i != next_cmap_i) {
				sync = true;
				cur_cmap_i = next_cmap_i;
			}

			exfat_clear_bitmap(inode, clu, (sync && IS_DIRSYNC(inode)));
			clu = n_clu;
			num_clusters++;

			if (err)
				goto dec_used_clus;
		} while (clu != EXFAT_EOF_CLUSTER);
	}

dec_used_clus:
	sbi->used_clusters -= num_clusters;
	return 0;
}

int exfat_free_cluster(struct inode *inode, struct exfat_chain *p_chain)
{
	int ret = 0;

	mutex_lock(&EXFAT_SB(inode->i_sb)->bitmap_lock);
	ret = __exfat_free_cluster(inode, p_chain);
	mutex_unlock(&EXFAT_SB(inode->i_sb)->bitmap_lock);

	return ret;
}

int exfat_find_last_cluster(struct super_block *sb, struct exfat_chain *p_chain,
		unsigned int *ret_clu)
{
	unsigned int clu, next;
	unsigned int count = 0;

	next = p_chain->dir;
	if (p_chain->flags == ALLOC_NO_FAT_CHAIN) {
		*ret_clu = next + p_chain->size - 1;
		return 0;
	}

	do {
		count++;
		clu = next;
		if (exfat_ent_get(sb, clu, &next))
			return -EIO;
	} while (next != EXFAT_EOF_CLUSTER);

	if (p_chain->size != count) {
		exfat_fs_error(sb,
			"bogus directory size (clus : ondisk(%d) != counted(%d))",
			p_chain->size, count);
		return -EIO;
	}

	*ret_clu = clu;
	return 0;
}

int exfat_zeroed_cluster(struct inode *dir, unsigned int clu)
{
	struct super_block *sb = dir->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	int nr_bhs = MAX_BUF_PER_PAGE;
	sector_t blknr, last_blknr;
	int err, i, n;

	blknr = exfat_cluster_to_sector(sbi, clu);
	last_blknr = blknr + sbi->sect_per_clus;

	if (last_blknr > sbi->num_sectors && sbi->num_sectors > 0) {
		exfat_fs_error_ratelimit(sb,
			"%s: out of range(sect:%llu len:%u)",
			__func__, (unsigned long long)blknr,
			sbi->sect_per_clus);
		return -EIO;
	}

	/* Zeroing the unused blocks on this cluster */
	while (blknr < last_blknr) {
		for (n = 0; n < nr_bhs && blknr < last_blknr; n++, blknr++) {
			bhs[n] = sb_getblk(sb, blknr);
			if (!bhs[n]) {
				err = -ENOMEM;
				goto release_bhs;
			}
			memset(bhs[n]->b_data, 0, sb->s_blocksize);
		}

		err = exfat_update_bhs(bhs, n, IS_DIRSYNC(dir));
		if (err)
			goto release_bhs;

		for (i = 0; i < n; i++)
			brelse(bhs[i]);
	}
	return 0;

release_bhs:
	exfat_err(sb, "failed zeroed sect %llu\n", (unsigned long long)blknr);
	for (i = 0; i < n; i++)
		bforget(bhs[i]);
	return err;
}

int exfat_alloc_cluster(struct inode *inode, unsigned int num_alloc,
		struct exfat_chain *p_chain, bool sync_bmap)
{
	int ret = -ENOSPC;
	unsigned int num_clusters = 0, total_cnt;
	unsigned int hint_clu, new_clu, last_clu = EXFAT_EOF_CLUSTER;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	total_cnt = EXFAT_DATA_CLUSTER_COUNT(sbi);

	if (unlikely(total_cnt < sbi->used_clusters)) {
		exfat_fs_error_ratelimit(sb,
			"%s: invalid used clusters(t:%u,u:%u)\n",
			__func__, total_cnt, sbi->used_clusters);
		return -EIO;
	}

	if (num_alloc > total_cnt - sbi->used_clusters)
		return -ENOSPC;

	mutex_lock(&sbi->bitmap_lock);

	hint_clu = p_chain->dir;
	/* find new cluster */
	if (hint_clu == EXFAT_EOF_CLUSTER) {
		if (sbi->clu_srch_ptr < EXFAT_FIRST_CLUSTER) {
			exfat_err(sb, "sbi->clu_srch_ptr is invalid (%u)\n",
				  sbi->clu_srch_ptr);
			sbi->clu_srch_ptr = EXFAT_FIRST_CLUSTER;
		}

		hint_clu = exfat_find_free_bitmap(sb, sbi->clu_srch_ptr);
		if (hint_clu == EXFAT_EOF_CLUSTER) {
			ret = -ENOSPC;
			goto unlock;
		}
	}

	/* check cluster validation */
	if (!is_valid_cluster(sbi, hint_clu)) {
		exfat_err(sb, "hint_cluster is invalid (%u)",
			hint_clu);
		hint_clu = EXFAT_FIRST_CLUSTER;
		if (p_chain->flags == ALLOC_NO_FAT_CHAIN) {
			if (exfat_chain_cont_cluster(sb, p_chain->dir,
					num_clusters)) {
				ret = -EIO;
				goto unlock;
			}
			p_chain->flags = ALLOC_FAT_CHAIN;
		}
	}

	p_chain->dir = EXFAT_EOF_CLUSTER;

	while ((new_clu = exfat_find_free_bitmap(sb, hint_clu)) !=
	       EXFAT_EOF_CLUSTER) {
		if (new_clu != hint_clu &&
		    p_chain->flags == ALLOC_NO_FAT_CHAIN) {
			if (exfat_chain_cont_cluster(sb, p_chain->dir,
					num_clusters)) {
				ret = -EIO;
				goto free_cluster;
			}
			p_chain->flags = ALLOC_FAT_CHAIN;
		}

		/* update allocation bitmap */
		if (exfat_set_bitmap(inode, new_clu, sync_bmap)) {
			ret = -EIO;
			goto free_cluster;
		}

		num_clusters++;

		/* update FAT table */
		if (p_chain->flags == ALLOC_FAT_CHAIN) {
			if (exfat_ent_set(sb, new_clu, EXFAT_EOF_CLUSTER)) {
				ret = -EIO;
				goto free_cluster;
			}
		}

		if (p_chain->dir == EXFAT_EOF_CLUSTER) {
			p_chain->dir = new_clu;
		} else if (p_chain->flags == ALLOC_FAT_CHAIN) {
			if (exfat_ent_set(sb, last_clu, new_clu)) {
				ret = -EIO;
				goto free_cluster;
			}
		}
		last_clu = new_clu;

		if (--num_alloc == 0) {
			sbi->clu_srch_ptr = hint_clu;
			sbi->used_clusters += num_clusters;

			p_chain->size += num_clusters;
			mutex_unlock(&sbi->bitmap_lock);
			return 0;
		}

		hint_clu = new_clu + 1;
		if (hint_clu >= sbi->num_clusters) {
			hint_clu = EXFAT_FIRST_CLUSTER;

			if (p_chain->flags == ALLOC_NO_FAT_CHAIN) {
				if (exfat_chain_cont_cluster(sb, p_chain->dir,
						num_clusters)) {
					ret = -EIO;
					goto free_cluster;
				}
				p_chain->flags = ALLOC_FAT_CHAIN;
			}
		}
	}
free_cluster:
	if (num_clusters)
		__exfat_free_cluster(inode, p_chain);
unlock:
	mutex_unlock(&sbi->bitmap_lock);
	return ret;
}

int exfat_count_num_clusters(struct super_block *sb,
		struct exfat_chain *p_chain, unsigned int *ret_count)
{
	unsigned int i, count;
	unsigned int clu;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	if (!p_chain->dir || p_chain->dir == EXFAT_EOF_CLUSTER) {
		*ret_count = 0;
		return 0;
	}

	if (p_chain->flags == ALLOC_NO_FAT_CHAIN) {
		*ret_count = p_chain->size;
		return 0;
	}

	clu = p_chain->dir;
	count = 0;
	for (i = EXFAT_FIRST_CLUSTER; i < sbi->num_clusters; i++) {
		count++;
		if (exfat_ent_get(sb, clu, &clu))
			return -EIO;
		if (clu == EXFAT_EOF_CLUSTER)
			break;
	}

	*ret_count = count;
	return 0;
}
