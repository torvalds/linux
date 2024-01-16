// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/mempool.h>
#include <linux/slab.h>

#define SG_MEMPOOL_NR		ARRAY_SIZE(sg_pools)
#define SG_MEMPOOL_SIZE		2

struct sg_pool {
	size_t		size;
	char		*name;
	struct kmem_cache	*slab;
	mempool_t	*pool;
};

#define SP(x) { .size = x, "sgpool-" __stringify(x) }
#if (SG_CHUNK_SIZE < 32)
#error SG_CHUNK_SIZE is too small (must be 32 or greater)
#endif
static struct sg_pool sg_pools[] = {
	SP(8),
	SP(16),
#if (SG_CHUNK_SIZE > 32)
	SP(32),
#if (SG_CHUNK_SIZE > 64)
	SP(64),
#if (SG_CHUNK_SIZE > 128)
	SP(128),
#if (SG_CHUNK_SIZE > 256)
#error SG_CHUNK_SIZE is too large (256 MAX)
#endif
#endif
#endif
#endif
	SP(SG_CHUNK_SIZE)
};
#undef SP

static inline unsigned int sg_pool_index(unsigned short nents)
{
	unsigned int index;

	BUG_ON(nents > SG_CHUNK_SIZE);

	if (nents <= 8)
		index = 0;
	else
		index = get_count_order(nents) - 3;

	return index;
}

static void sg_pool_free(struct scatterlist *sgl, unsigned int nents)
{
	struct sg_pool *sgp;

	sgp = sg_pools + sg_pool_index(nents);
	mempool_free(sgl, sgp->pool);
}

static struct scatterlist *sg_pool_alloc(unsigned int nents, gfp_t gfp_mask)
{
	struct sg_pool *sgp;

	sgp = sg_pools + sg_pool_index(nents);
	return mempool_alloc(sgp->pool, gfp_mask);
}

/**
 * sg_free_table_chained - Free a previously mapped sg table
 * @table:	The sg table header to use
 * @nents_first_chunk: size of the first_chunk SGL passed to
 *		sg_alloc_table_chained
 *
 *  Description:
 *    Free an sg table previously allocated and setup with
 *    sg_alloc_table_chained().
 *
 *    @nents_first_chunk has to be same with that same parameter passed
 *    to sg_alloc_table_chained().
 *
 **/
void sg_free_table_chained(struct sg_table *table,
		unsigned nents_first_chunk)
{
	if (table->orig_nents <= nents_first_chunk)
		return;

	if (nents_first_chunk == 1)
		nents_first_chunk = 0;

	__sg_free_table(table, SG_CHUNK_SIZE, nents_first_chunk, sg_pool_free,
			table->orig_nents);
}
EXPORT_SYMBOL_GPL(sg_free_table_chained);

/**
 * sg_alloc_table_chained - Allocate and chain SGLs in an sg table
 * @table:	The sg table header to use
 * @nents:	Number of entries in sg list
 * @first_chunk: first SGL
 * @nents_first_chunk: number of the SGL of @first_chunk
 *
 *  Description:
 *    Allocate and chain SGLs in an sg table. If @nents@ is larger than
 *    @nents_first_chunk a chained sg table will be setup. @first_chunk is
 *    ignored if nents_first_chunk <= 1 because user expects the SGL points
 *    non-chain SGL.
 *
 **/
int sg_alloc_table_chained(struct sg_table *table, int nents,
		struct scatterlist *first_chunk, unsigned nents_first_chunk)
{
	int ret;

	BUG_ON(!nents);

	if (first_chunk && nents_first_chunk) {
		if (nents <= nents_first_chunk) {
			table->nents = table->orig_nents = nents;
			sg_init_table(table->sgl, nents);
			return 0;
		}
	}

	/* User supposes that the 1st SGL includes real entry */
	if (nents_first_chunk <= 1) {
		first_chunk = NULL;
		nents_first_chunk = 0;
	}

	ret = __sg_alloc_table(table, nents, SG_CHUNK_SIZE,
			       first_chunk, nents_first_chunk,
			       GFP_ATOMIC, sg_pool_alloc);
	if (unlikely(ret))
		sg_free_table_chained(table, nents_first_chunk);
	return ret;
}
EXPORT_SYMBOL_GPL(sg_alloc_table_chained);

static __init int sg_pool_init(void)
{
	int i;

	for (i = 0; i < SG_MEMPOOL_NR; i++) {
		struct sg_pool *sgp = sg_pools + i;
		int size = sgp->size * sizeof(struct scatterlist);

		sgp->slab = kmem_cache_create(sgp->name, size, 0,
				SLAB_HWCACHE_ALIGN, NULL);
		if (!sgp->slab) {
			printk(KERN_ERR "SG_POOL: can't init sg slab %s\n",
					sgp->name);
			goto cleanup_sdb;
		}

		sgp->pool = mempool_create_slab_pool(SG_MEMPOOL_SIZE,
						     sgp->slab);
		if (!sgp->pool) {
			printk(KERN_ERR "SG_POOL: can't init sg mempool %s\n",
					sgp->name);
			goto cleanup_sdb;
		}
	}

	return 0;

cleanup_sdb:
	for (i = 0; i < SG_MEMPOOL_NR; i++) {
		struct sg_pool *sgp = sg_pools + i;

		mempool_destroy(sgp->pool);
		kmem_cache_destroy(sgp->slab);
	}

	return -ENOMEM;
}

subsys_initcall(sg_pool_init);
