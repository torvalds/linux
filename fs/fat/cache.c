// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of iyesde number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/slab.h>
#include "fat.h"

/* this must be > 0. */
#define FAT_MAX_CACHE	8

struct fat_cache {
	struct list_head cache_list;
	int nr_contig;	/* number of contiguous clusters */
	int fcluster;	/* cluster number in the file. */
	int dcluster;	/* cluster number on disk. */
};

struct fat_cache_id {
	unsigned int id;
	int nr_contig;
	int fcluster;
	int dcluster;
};

static inline int fat_max_cache(struct iyesde *iyesde)
{
	return FAT_MAX_CACHE;
}

static struct kmem_cache *fat_cache_cachep;

static void init_once(void *foo)
{
	struct fat_cache *cache = (struct fat_cache *)foo;

	INIT_LIST_HEAD(&cache->cache_list);
}

int __init fat_cache_init(void)
{
	fat_cache_cachep = kmem_cache_create("fat_cache",
				sizeof(struct fat_cache),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (fat_cache_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void fat_cache_destroy(void)
{
	kmem_cache_destroy(fat_cache_cachep);
}

static inline struct fat_cache *fat_cache_alloc(struct iyesde *iyesde)
{
	return kmem_cache_alloc(fat_cache_cachep, GFP_NOFS);
}

static inline void fat_cache_free(struct fat_cache *cache)
{
	BUG_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(fat_cache_cachep, cache);
}

static inline void fat_cache_update_lru(struct iyesde *iyesde,
					struct fat_cache *cache)
{
	if (MSDOS_I(iyesde)->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &MSDOS_I(iyesde)->cache_lru);
}

static int fat_cache_lookup(struct iyesde *iyesde, int fclus,
			    struct fat_cache_id *cid,
			    int *cached_fclus, int *cached_dclus)
{
	static struct fat_cache yeshit = { .fcluster = 0, };

	struct fat_cache *hit = &yeshit, *p;
	int offset = -1;

	spin_lock(&MSDOS_I(iyesde)->cache_lru_lock);
	list_for_each_entry(p, &MSDOS_I(iyesde)->cache_lru, cache_list) {
		/* Find the cache of "fclus" or nearest cache. */
		if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
			hit = p;
			if ((hit->fcluster + hit->nr_contig) < fclus) {
				offset = hit->nr_contig;
			} else {
				offset = fclus - hit->fcluster;
				break;
			}
		}
	}
	if (hit != &yeshit) {
		fat_cache_update_lru(iyesde, hit);

		cid->id = MSDOS_I(iyesde)->cache_valid_id;
		cid->nr_contig = hit->nr_contig;
		cid->fcluster = hit->fcluster;
		cid->dcluster = hit->dcluster;
		*cached_fclus = cid->fcluster + offset;
		*cached_dclus = cid->dcluster + offset;
	}
	spin_unlock(&MSDOS_I(iyesde)->cache_lru_lock);

	return offset;
}

static struct fat_cache *fat_cache_merge(struct iyesde *iyesde,
					 struct fat_cache_id *new)
{
	struct fat_cache *p;

	list_for_each_entry(p, &MSDOS_I(iyesde)->cache_lru, cache_list) {
		/* Find the same part as "new" in cluster-chain. */
		if (p->fcluster == new->fcluster) {
			BUG_ON(p->dcluster != new->dcluster);
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			return p;
		}
	}
	return NULL;
}

static void fat_cache_add(struct iyesde *iyesde, struct fat_cache_id *new)
{
	struct fat_cache *cache, *tmp;

	if (new->fcluster == -1) /* dummy cache */
		return;

	spin_lock(&MSDOS_I(iyesde)->cache_lru_lock);
	if (new->id != FAT_CACHE_VALID &&
	    new->id != MSDOS_I(iyesde)->cache_valid_id)
		goto out;	/* this cache was invalidated */

	cache = fat_cache_merge(iyesde, new);
	if (cache == NULL) {
		if (MSDOS_I(iyesde)->nr_caches < fat_max_cache(iyesde)) {
			MSDOS_I(iyesde)->nr_caches++;
			spin_unlock(&MSDOS_I(iyesde)->cache_lru_lock);

			tmp = fat_cache_alloc(iyesde);
			if (!tmp) {
				spin_lock(&MSDOS_I(iyesde)->cache_lru_lock);
				MSDOS_I(iyesde)->nr_caches--;
				spin_unlock(&MSDOS_I(iyesde)->cache_lru_lock);
				return;
			}

			spin_lock(&MSDOS_I(iyesde)->cache_lru_lock);
			cache = fat_cache_merge(iyesde, new);
			if (cache != NULL) {
				MSDOS_I(iyesde)->nr_caches--;
				fat_cache_free(tmp);
				goto out_update_lru;
			}
			cache = tmp;
		} else {
			struct list_head *p = MSDOS_I(iyesde)->cache_lru.prev;
			cache = list_entry(p, struct fat_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out_update_lru:
	fat_cache_update_lru(iyesde, cache);
out:
	spin_unlock(&MSDOS_I(iyesde)->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is yest updated. It
 * fixes itself after a while.
 */
static void __fat_cache_inval_iyesde(struct iyesde *iyesde)
{
	struct msdos_iyesde_info *i = MSDOS_I(iyesde);
	struct fat_cache *cache;

	while (!list_empty(&i->cache_lru)) {
		cache = list_entry(i->cache_lru.next,
				   struct fat_cache, cache_list);
		list_del_init(&cache->cache_list);
		i->nr_caches--;
		fat_cache_free(cache);
	}
	/* Update. The copy of caches before this id is discarded. */
	i->cache_valid_id++;
	if (i->cache_valid_id == FAT_CACHE_VALID)
		i->cache_valid_id++;
}

void fat_cache_inval_iyesde(struct iyesde *iyesde)
{
	spin_lock(&MSDOS_I(iyesde)->cache_lru_lock);
	__fat_cache_inval_iyesde(iyesde);
	spin_unlock(&MSDOS_I(iyesde)->cache_lru_lock);
}

static inline int cache_contiguous(struct fat_cache_id *cid, int dclus)
{
	cid->nr_contig++;
	return ((cid->dcluster + cid->nr_contig) == dclus);
}

static inline void cache_init(struct fat_cache_id *cid, int fclus, int dclus)
{
	cid->id = FAT_CACHE_VALID;
	cid->fcluster = fclus;
	cid->dcluster = dclus;
	cid->nr_contig = 0;
}

int fat_get_cluster(struct iyesde *iyesde, int cluster, int *fclus, int *dclus)
{
	struct super_block *sb = iyesde->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	const int limit = sb->s_maxbytes >> sbi->cluster_bits;
	struct fat_entry fatent;
	struct fat_cache_id cid;
	int nr;

	BUG_ON(MSDOS_I(iyesde)->i_start == 0);

	*fclus = 0;
	*dclus = MSDOS_I(iyesde)->i_start;
	if (!fat_valid_entry(sbi, *dclus)) {
		fat_fs_error_ratelimit(sb,
			"%s: invalid start cluster (i_pos %lld, start %08x)",
			__func__, MSDOS_I(iyesde)->i_pos, *dclus);
		return -EIO;
	}
	if (cluster == 0)
		return 0;

	if (fat_cache_lookup(iyesde, cluster, &cid, fclus, dclus) < 0) {
		/*
		 * dummy, always yest contiguous
		 * This is reinitialized by cache_init(), later.
		 */
		cache_init(&cid, -1, -1);
	}

	fatent_init(&fatent);
	while (*fclus < cluster) {
		/* prevent the infinite loop of cluster chain */
		if (*fclus > limit) {
			fat_fs_error_ratelimit(sb,
				"%s: detected the cluster chain loop (i_pos %lld)",
				__func__, MSDOS_I(iyesde)->i_pos);
			nr = -EIO;
			goto out;
		}

		nr = fat_ent_read(iyesde, &fatent, *dclus);
		if (nr < 0)
			goto out;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_error_ratelimit(sb,
				"%s: invalid cluster chain (i_pos %lld)",
				__func__, MSDOS_I(iyesde)->i_pos);
			nr = -EIO;
			goto out;
		} else if (nr == FAT_ENT_EOF) {
			fat_cache_add(iyesde, &cid);
			goto out;
		}
		(*fclus)++;
		*dclus = nr;
		if (!cache_contiguous(&cid, *dclus))
			cache_init(&cid, *fclus, *dclus);
	}
	nr = 0;
	fat_cache_add(iyesde, &cid);
out:
	fatent_brelse(&fatent);
	return nr;
}

static int fat_bmap_cluster(struct iyesde *iyesde, int cluster)
{
	struct super_block *sb = iyesde->i_sb;
	int ret, fclus, dclus;

	if (MSDOS_I(iyesde)->i_start == 0)
		return 0;

	ret = fat_get_cluster(iyesde, cluster, &fclus, &dclus);
	if (ret < 0)
		return ret;
	else if (ret == FAT_ENT_EOF) {
		fat_fs_error(sb, "%s: request beyond EOF (i_pos %lld)",
			     __func__, MSDOS_I(iyesde)->i_pos);
		return -EIO;
	}
	return dclus;
}

int fat_get_mapped_cluster(struct iyesde *iyesde, sector_t sector,
			   sector_t last_block,
			   unsigned long *mapped_blocks, sector_t *bmap)
{
	struct super_block *sb = iyesde->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int cluster, offset;

	cluster = sector >> (sbi->cluster_bits - sb->s_blocksize_bits);
	offset  = sector & (sbi->sec_per_clus - 1);
	cluster = fat_bmap_cluster(iyesde, cluster);
	if (cluster < 0)
		return cluster;
	else if (cluster) {
		*bmap = fat_clus_to_blknr(sbi, cluster) + offset;
		*mapped_blocks = sbi->sec_per_clus - offset;
		if (*mapped_blocks > last_block - sector)
			*mapped_blocks = last_block - sector;
	}

	return 0;
}

static int is_exceed_eof(struct iyesde *iyesde, sector_t sector,
			 sector_t *last_block, int create)
{
	struct super_block *sb = iyesde->i_sb;
	const unsigned long blocksize = sb->s_blocksize;
	const unsigned char blocksize_bits = sb->s_blocksize_bits;

	*last_block = (i_size_read(iyesde) + (blocksize - 1)) >> blocksize_bits;
	if (sector >= *last_block) {
		if (!create)
			return 1;

		/*
		 * ->mmu_private can access on only allocation path.
		 * (caller must hold ->i_mutex)
		 */
		*last_block = (MSDOS_I(iyesde)->mmu_private + (blocksize - 1))
			>> blocksize_bits;
		if (sector >= *last_block)
			return 1;
	}

	return 0;
}

int fat_bmap(struct iyesde *iyesde, sector_t sector, sector_t *phys,
	     unsigned long *mapped_blocks, int create, bool from_bmap)
{
	struct msdos_sb_info *sbi = MSDOS_SB(iyesde->i_sb);
	sector_t last_block;

	*phys = 0;
	*mapped_blocks = 0;
	if (!is_fat32(sbi) && (iyesde->i_iyes == MSDOS_ROOT_INO)) {
		if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits)) {
			*phys = sector + sbi->dir_start;
			*mapped_blocks = 1;
		}
		return 0;
	}

	if (!from_bmap) {
		if (is_exceed_eof(iyesde, sector, &last_block, create))
			return 0;
	} else {
		last_block = iyesde->i_blocks >>
				(iyesde->i_sb->s_blocksize_bits - 9);
		if (sector >= last_block)
			return 0;
	}

	return fat_get_mapped_cluster(iyesde, sector, last_block, mapped_blocks,
				      phys);
}
