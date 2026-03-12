// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/fs/fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/buffer_head.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

#define EXFAT_MAX_CACHE		16

struct exfat_cache {
	struct list_head cache_list;
	unsigned int nr_contig;	/* number of contiguous clusters */
	unsigned int fcluster;	/* cluster number in the file. */
	unsigned int dcluster;	/* cluster number on disk. */
};

struct exfat_cache_id {
	unsigned int id;
	unsigned int nr_contig;
	unsigned int fcluster;
	unsigned int dcluster;
};

static struct kmem_cache *exfat_cachep;

static void exfat_cache_init_once(void *c)
{
	struct exfat_cache *cache = (struct exfat_cache *)c;

	INIT_LIST_HEAD(&cache->cache_list);
}

int exfat_cache_init(void)
{
	exfat_cachep = kmem_cache_create("exfat_cache",
				sizeof(struct exfat_cache),
				0, SLAB_RECLAIM_ACCOUNT,
				exfat_cache_init_once);
	if (!exfat_cachep)
		return -ENOMEM;
	return 0;
}

void exfat_cache_shutdown(void)
{
	if (!exfat_cachep)
		return;
	kmem_cache_destroy(exfat_cachep);
}

static inline struct exfat_cache *exfat_cache_alloc(void)
{
	return kmem_cache_alloc(exfat_cachep, GFP_NOFS);
}

static inline void exfat_cache_free(struct exfat_cache *cache)
{
	WARN_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(exfat_cachep, cache);
}

static inline void exfat_cache_update_lru(struct inode *inode,
		struct exfat_cache *cache)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);

	if (ei->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &ei->cache_lru);
}

/*
 * Find the cache that covers or precedes 'fclus' and return the last
 * cluster before the next cache range.
 */
static inline unsigned int
exfat_cache_lookup(struct inode *inode, struct exfat_cache_id *cid,
		unsigned int fclus, unsigned int end,
		unsigned int *cached_fclus, unsigned int *cached_dclus)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);
	static struct exfat_cache nohit = { .fcluster = 0, };
	struct exfat_cache *hit = &nohit, *p;
	unsigned int tail = 0;		/* End boundary of hit cache */

	/*
	 * Search range [fclus, end]. Stop early if:
	 * 1. Cache covers entire range, or
	 * 2. Next cache starts at current cache tail
	 */
	spin_lock(&ei->cache_lru_lock);
	list_for_each_entry(p, &ei->cache_lru, cache_list) {
		/* Find the cache of "fclus" or nearest cache. */
		if (p->fcluster <= fclus) {
			if (p->fcluster < hit->fcluster)
				continue;

			hit = p;
			tail = hit->fcluster + hit->nr_contig;

			/* Current cache covers [fclus, end] completely */
			if (tail >= end)
				break;
		} else if (p->fcluster <= end) {
			end = p->fcluster - 1;

			/*
			 * If we have a hit and next cache starts within/at
			 * its tail, caches are contiguous, stop searching.
			 */
			if (tail && tail >= end)
				break;
		}
	}
	if (hit != &nohit) {
		unsigned int offset;

		exfat_cache_update_lru(inode, hit);
		cid->id = ei->cache_valid_id;
		cid->nr_contig = hit->nr_contig;
		cid->fcluster = hit->fcluster;
		cid->dcluster = hit->dcluster;

		offset = min(cid->nr_contig, fclus - cid->fcluster);
		*cached_fclus = cid->fcluster + offset;
		*cached_dclus = cid->dcluster + offset;
	}
	spin_unlock(&ei->cache_lru_lock);

	/* Return next cache start or 'end' if no more caches */
	return end;
}

static struct exfat_cache *exfat_cache_merge(struct inode *inode,
		struct exfat_cache_id *new)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_cache *p;

	list_for_each_entry(p, &ei->cache_lru, cache_list) {
		/* Find the same part as "new" in cluster-chain. */
		if (p->fcluster == new->fcluster) {
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			return p;
		}
	}
	return NULL;
}

static void exfat_cache_add(struct inode *inode,
		struct exfat_cache_id *new)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_cache *cache, *tmp;

	if (new->fcluster == EXFAT_EOF_CLUSTER) /* dummy cache */
		return;

	spin_lock(&ei->cache_lru_lock);
	if (new->id != EXFAT_CACHE_VALID &&
	    new->id != ei->cache_valid_id)
		goto unlock;	/* this cache was invalidated */

	cache = exfat_cache_merge(inode, new);
	if (cache == NULL) {
		if (ei->nr_caches < EXFAT_MAX_CACHE) {
			ei->nr_caches++;
			spin_unlock(&ei->cache_lru_lock);

			tmp = exfat_cache_alloc();
			if (!tmp) {
				spin_lock(&ei->cache_lru_lock);
				ei->nr_caches--;
				spin_unlock(&ei->cache_lru_lock);
				return;
			}

			spin_lock(&ei->cache_lru_lock);
			cache = exfat_cache_merge(inode, new);
			if (cache != NULL) {
				ei->nr_caches--;
				exfat_cache_free(tmp);
				goto out_update_lru;
			}
			cache = tmp;
		} else {
			struct list_head *p = ei->cache_lru.prev;

			cache = list_entry(p,
					struct exfat_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out_update_lru:
	exfat_cache_update_lru(inode, cache);
unlock:
	spin_unlock(&ei->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __exfat_cache_inval_inode(struct inode *inode)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_cache *cache;

	while (!list_empty(&ei->cache_lru)) {
		cache = list_entry(ei->cache_lru.next,
				   struct exfat_cache, cache_list);
		list_del_init(&cache->cache_list);
		ei->nr_caches--;
		exfat_cache_free(cache);
	}
	/* Update. The copy of caches before this id is discarded. */
	ei->cache_valid_id++;
	if (ei->cache_valid_id == EXFAT_CACHE_VALID)
		ei->cache_valid_id++;
}

void exfat_cache_inval_inode(struct inode *inode)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);

	spin_lock(&ei->cache_lru_lock);
	__exfat_cache_inval_inode(inode);
	spin_unlock(&ei->cache_lru_lock);
}

static inline int cache_contiguous(struct exfat_cache_id *cid,
		unsigned int dclus)
{
	cid->nr_contig++;
	return cid->dcluster + cid->nr_contig == dclus;
}

static inline void cache_init(struct exfat_cache_id *cid,
		unsigned int fclus, unsigned int dclus)
{
	cid->id = EXFAT_CACHE_VALID;
	cid->fcluster = fclus;
	cid->dcluster = dclus;
	cid->nr_contig = 0;
}

int exfat_get_cluster(struct inode *inode, unsigned int cluster,
		unsigned int *dclus, unsigned int *count,
		unsigned int *last_dclus)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct buffer_head *bh = NULL;
	struct exfat_cache_id cid;
	unsigned int content, fclus;
	unsigned int end = cluster + *count - 1;

	if (ei->start_clu == EXFAT_FREE_CLUSTER) {
		exfat_fs_error(sb,
			"invalid access to exfat cache (entry 0x%08x)",
			ei->start_clu);
		return -EIO;
	}

	fclus = 0;
	*dclus = ei->start_clu;
	*last_dclus = *dclus;

	/*
	 * This case should not exist, as exfat_map_cluster function doesn't
	 * call this routine when start_clu == EXFAT_EOF_CLUSTER.
	 * This case is retained here for routine completeness.
	 */
	if (*dclus == EXFAT_EOF_CLUSTER) {
		*count = 0;
		return 0;
	}

	/* If only the first cluster is needed, return now. */
	if (fclus == cluster && *count == 1)
		return 0;

	cache_init(&cid, fclus, *dclus);
	/*
	 * Update the 'end' to exclude the next cache range, as clusters in
	 * different cache are typically not contiguous.
	 */
	end = exfat_cache_lookup(inode, &cid, cluster, end, &fclus, dclus);

	/* Return if the cache covers the entire range. */
	if (cid.fcluster + cid.nr_contig >= end) {
		*count = end - cluster + 1;
		return 0;
	}

	/* Find the first cluster we need. */
	while (fclus < cluster) {
		if (exfat_ent_get(sb, *dclus, &content, &bh))
			return -EIO;

		*last_dclus = *dclus;
		*dclus = content;
		fclus++;

		if (content == EXFAT_EOF_CLUSTER)
			break;

		if (!cache_contiguous(&cid, *dclus))
			cache_init(&cid, fclus, *dclus);
	}

	/*
	 * Now the cid cache contains the first cluster requested, collect
	 * the remaining clusters of this contiguous extent.
	 */
	if (*dclus != EXFAT_EOF_CLUSTER) {
		unsigned int clu = *dclus;

		while (fclus < end) {
			if (exfat_ent_get(sb, clu, &content, &bh))
				return -EIO;
			if (++clu != content)
				break;
			fclus++;
		}
		cid.nr_contig = fclus - cid.fcluster;
		*count = fclus - cluster + 1;

		/*
		 * Cache this discontiguous cluster, we'll definitely need
		 * it later
		 */
		if (fclus < end && content != EXFAT_EOF_CLUSTER) {
			exfat_cache_add(inode, &cid);
			cache_init(&cid, fclus + 1, content);
		}
	} else {
		*count = 0;
	}
	brelse(bh);
	exfat_cache_add(inode, &cid);
	return 0;
}
