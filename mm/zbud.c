// SPDX-License-Identifier: GPL-2.0-only
/*
 * zbud.c
 *
 * Copyright (C) 2013, Seth Jennings, IBM
 *
 * Concepts based on zcache internal zbud allocator by Dan Magenheimer.
 *
 * zbud is an special purpose allocator for storing compressed pages.  Contrary
 * to what its name may suggest, zbud is not a buddy allocator, but rather an
 * allocator that "buddies" two compressed pages together in a single memory
 * page.
 *
 * While this design limits storage density, it has simple and deterministic
 * reclaim properties that make it preferable to a higher density approach when
 * reclaim will be used.
 *
 * zbud works by storing compressed pages, or "zpages", together in pairs in a
 * single memory page called a "zbud page".  The first buddy is "left
 * justified" at the beginning of the zbud page, and the last buddy is "right
 * justified" at the end of the zbud page.  The benefit is that if either
 * buddy is freed, the freed buddy space, coalesced with whatever slack space
 * that existed between the buddies, results in the largest possible free region
 * within the zbud page.
 *
 * zbud also provides an attractive lower bound on density. The ratio of zpages
 * to zbud pages can not be less than 1.  This ensures that zbud can never "do
 * harm" by using more pages to store zpages than the uncompressed zpages would
 * have used on their own.
 *
 * zbud pages are divided into "chunks".  The size of the chunks is fixed at
 * compile time and determined by NCHUNKS_ORDER below.  Dividing zbud pages
 * into chunks allows organizing unbuddied zbud pages into a manageable number
 * of unbuddied lists according to the number of free chunks available in the
 * zbud page.
 *
 * The zbud API differs from that of conventional allocators in that the
 * allocation function, zbud_alloc(), returns an opaque handle to the user,
 * not a dereferenceable pointer.  The user must map the handle using
 * zbud_map() in order to get a usable pointer by which to access the
 * allocation data and unmap the handle with zbud_unmap() when operations
 * on the allocation data are complete.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/zpool.h>

/*****************
 * Structures
*****************/
/*
 * NCHUNKS_ORDER determines the internal allocation granularity, effectively
 * adjusting internal fragmentation.  It also determines the number of
 * freelists maintained in each pool. NCHUNKS_ORDER of 6 means that the
 * allocation granularity will be in chunks of size PAGE_SIZE/64. As one chunk
 * in allocated page is occupied by zbud header, NCHUNKS will be calculated to
 * 63 which shows the max number of free chunks in zbud page, also there will be
 * 63 freelists per pool.
 */
#define NCHUNKS_ORDER	6

#define CHUNK_SHIFT	(PAGE_SHIFT - NCHUNKS_ORDER)
#define CHUNK_SIZE	(1 << CHUNK_SHIFT)
#define ZHDR_SIZE_ALIGNED CHUNK_SIZE
#define NCHUNKS		((PAGE_SIZE - ZHDR_SIZE_ALIGNED) >> CHUNK_SHIFT)

struct zbud_pool;

/**
 * struct zbud_pool - stores metadata for each zbud pool
 * @lock:	protects all pool fields and first|last_chunk fields of any
 *		zbud page in the pool
 * @unbuddied:	array of lists tracking zbud pages that only contain one buddy;
 *		the lists each zbud page is added to depends on the size of
 *		its free region.
 * @buddied:	list tracking the zbud pages that contain two buddies;
 *		these zbud pages are full
 * @pages_nr:	number of zbud pages in the pool.
 *
 * This structure is allocated at pool creation time and maintains metadata
 * pertaining to a particular zbud pool.
 */
struct zbud_pool {
	spinlock_t lock;
	union {
		/*
		 * Reuse unbuddied[0] as buddied on the ground that
		 * unbuddied[0] is unused.
		 */
		struct list_head buddied;
		struct list_head unbuddied[NCHUNKS];
	};
	u64 pages_nr;
};

/*
 * struct zbud_header - zbud page metadata occupying the first chunk of each
 *			zbud page.
 * @buddy:	links the zbud page into the unbuddied/buddied lists in the pool
 * @first_chunks:	the size of the first buddy in chunks, 0 if free
 * @last_chunks:	the size of the last buddy in chunks, 0 if free
 */
struct zbud_header {
	struct list_head buddy;
	unsigned int first_chunks;
	unsigned int last_chunks;
};

/*****************
 * Helpers
*****************/
/* Just to make the code easier to read */
enum buddy {
	FIRST,
	LAST
};

/* Converts an allocation size in bytes to size in zbud chunks */
static int size_to_chunks(size_t size)
{
	return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
}

#define for_each_unbuddied_list(_iter, _begin) \
	for ((_iter) = (_begin); (_iter) < NCHUNKS; (_iter)++)

/* Initializes the zbud header of a newly allocated zbud page */
static struct zbud_header *init_zbud_page(struct page *page)
{
	struct zbud_header *zhdr = page_address(page);
	zhdr->first_chunks = 0;
	zhdr->last_chunks = 0;
	INIT_LIST_HEAD(&zhdr->buddy);
	return zhdr;
}

/* Resets the struct page fields and frees the page */
static void free_zbud_page(struct zbud_header *zhdr)
{
	__free_page(virt_to_page(zhdr));
}

/*
 * Encodes the handle of a particular buddy within a zbud page
 * Pool lock should be held as this function accesses first|last_chunks
 */
static unsigned long encode_handle(struct zbud_header *zhdr, enum buddy bud)
{
	unsigned long handle;

	/*
	 * For now, the encoded handle is actually just the pointer to the data
	 * but this might not always be the case.  A little information hiding.
	 * Add CHUNK_SIZE to the handle if it is the first allocation to jump
	 * over the zbud header in the first chunk.
	 */
	handle = (unsigned long)zhdr;
	if (bud == FIRST)
		/* skip over zbud header */
		handle += ZHDR_SIZE_ALIGNED;
	else /* bud == LAST */
		handle += PAGE_SIZE - (zhdr->last_chunks  << CHUNK_SHIFT);
	return handle;
}

/* Returns the zbud page where a given handle is stored */
static struct zbud_header *handle_to_zbud_header(unsigned long handle)
{
	return (struct zbud_header *)(handle & PAGE_MASK);
}

/* Returns the number of free chunks in a zbud page */
static int num_free_chunks(struct zbud_header *zhdr)
{
	/*
	 * Rather than branch for different situations, just use the fact that
	 * free buddies have a length of zero to simplify everything.
	 */
	return NCHUNKS - zhdr->first_chunks - zhdr->last_chunks;
}

/*****************
 * API Functions
*****************/
/**
 * zbud_create_pool() - create a new zbud pool
 * @gfp:	gfp flags when allocating the zbud pool structure
 *
 * Return: pointer to the new zbud pool or NULL if the metadata allocation
 * failed.
 */
static struct zbud_pool *zbud_create_pool(gfp_t gfp)
{
	struct zbud_pool *pool;
	int i;

	pool = kzalloc(sizeof(struct zbud_pool), gfp);
	if (!pool)
		return NULL;
	spin_lock_init(&pool->lock);
	for_each_unbuddied_list(i, 0)
		INIT_LIST_HEAD(&pool->unbuddied[i]);
	INIT_LIST_HEAD(&pool->buddied);
	pool->pages_nr = 0;
	return pool;
}

/**
 * zbud_destroy_pool() - destroys an existing zbud pool
 * @pool:	the zbud pool to be destroyed
 *
 * The pool should be emptied before this function is called.
 */
static void zbud_destroy_pool(struct zbud_pool *pool)
{
	kfree(pool);
}

/**
 * zbud_alloc() - allocates a region of a given size
 * @pool:	zbud pool from which to allocate
 * @size:	size in bytes of the desired allocation
 * @gfp:	gfp flags used if the pool needs to grow
 * @handle:	handle of the new allocation
 *
 * This function will attempt to find a free region in the pool large enough to
 * satisfy the allocation request.  A search of the unbuddied lists is
 * performed first. If no suitable free region is found, then a new page is
 * allocated and added to the pool to satisfy the request.
 *
 * gfp should not set __GFP_HIGHMEM as highmem pages cannot be used
 * as zbud pool pages.
 *
 * Return: 0 if success and handle is set, otherwise -EINVAL if the size or
 * gfp arguments are invalid or -ENOMEM if the pool was unable to allocate
 * a new page.
 */
static int zbud_alloc(struct zbud_pool *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	int chunks, i, freechunks;
	struct zbud_header *zhdr = NULL;
	enum buddy bud;
	struct page *page;

	if (!size || (gfp & __GFP_HIGHMEM))
		return -EINVAL;
	if (size > PAGE_SIZE - ZHDR_SIZE_ALIGNED - CHUNK_SIZE)
		return -ENOSPC;
	chunks = size_to_chunks(size);
	spin_lock(&pool->lock);

	/* First, try to find an unbuddied zbud page. */
	for_each_unbuddied_list(i, chunks) {
		if (!list_empty(&pool->unbuddied[i])) {
			zhdr = list_first_entry(&pool->unbuddied[i],
					struct zbud_header, buddy);
			list_del(&zhdr->buddy);
			if (zhdr->first_chunks == 0)
				bud = FIRST;
			else
				bud = LAST;
			goto found;
		}
	}

	/* Couldn't find unbuddied zbud page, create new one */
	spin_unlock(&pool->lock);
	page = alloc_page(gfp);
	if (!page)
		return -ENOMEM;
	spin_lock(&pool->lock);
	pool->pages_nr++;
	zhdr = init_zbud_page(page);
	bud = FIRST;

found:
	if (bud == FIRST)
		zhdr->first_chunks = chunks;
	else
		zhdr->last_chunks = chunks;

	if (zhdr->first_chunks == 0 || zhdr->last_chunks == 0) {
		/* Add to unbuddied list */
		freechunks = num_free_chunks(zhdr);
		list_add(&zhdr->buddy, &pool->unbuddied[freechunks]);
	} else {
		/* Add to buddied list */
		list_add(&zhdr->buddy, &pool->buddied);
	}

	*handle = encode_handle(zhdr, bud);
	spin_unlock(&pool->lock);

	return 0;
}

/**
 * zbud_free() - frees the allocation associated with the given handle
 * @pool:	pool in which the allocation resided
 * @handle:	handle associated with the allocation returned by zbud_alloc()
 */
static void zbud_free(struct zbud_pool *pool, unsigned long handle)
{
	struct zbud_header *zhdr;
	int freechunks;

	spin_lock(&pool->lock);
	zhdr = handle_to_zbud_header(handle);

	/* If first buddy, handle will be page aligned */
	if ((handle - ZHDR_SIZE_ALIGNED) & ~PAGE_MASK)
		zhdr->last_chunks = 0;
	else
		zhdr->first_chunks = 0;

	/* Remove from existing buddy list */
	list_del(&zhdr->buddy);

	if (zhdr->first_chunks == 0 && zhdr->last_chunks == 0) {
		/* zbud page is empty, free */
		free_zbud_page(zhdr);
		pool->pages_nr--;
	} else {
		/* Add to unbuddied list */
		freechunks = num_free_chunks(zhdr);
		list_add(&zhdr->buddy, &pool->unbuddied[freechunks]);
	}

	spin_unlock(&pool->lock);
}

/**
 * zbud_map() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be mapped
 *
 * While trivial for zbud, the mapping functions for others allocators
 * implementing this allocation API could have more complex information encoded
 * in the handle and could create temporary mappings to make the data
 * accessible to the user.
 *
 * Returns: a pointer to the mapped allocation
 */
static void *zbud_map(struct zbud_pool *pool, unsigned long handle)
{
	return (void *)(handle);
}

/**
 * zbud_unmap() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be unmapped
 */
static void zbud_unmap(struct zbud_pool *pool, unsigned long handle)
{
}

/**
 * zbud_get_pool_pages() - gets the zbud pool size in pages
 * @pool:	pool whose size is being queried
 *
 * Returns: size in pages of the given pool.  The pool lock need not be
 * taken to access pages_nr.
 */
static u64 zbud_get_pool_pages(struct zbud_pool *pool)
{
	return pool->pages_nr;
}

/*****************
 * zpool
 ****************/

static void *zbud_zpool_create(const char *name, gfp_t gfp)
{
	return zbud_create_pool(gfp);
}

static void zbud_zpool_destroy(void *pool)
{
	zbud_destroy_pool(pool);
}

static int zbud_zpool_malloc(void *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	return zbud_alloc(pool, size, gfp, handle);
}
static void zbud_zpool_free(void *pool, unsigned long handle)
{
	zbud_free(pool, handle);
}

static void *zbud_zpool_map(void *pool, unsigned long handle,
			enum zpool_mapmode mm)
{
	return zbud_map(pool, handle);
}
static void zbud_zpool_unmap(void *pool, unsigned long handle)
{
	zbud_unmap(pool, handle);
}

static u64 zbud_zpool_total_pages(void *pool)
{
	return zbud_get_pool_pages(pool);
}

static struct zpool_driver zbud_zpool_driver = {
	.type =		"zbud",
	.sleep_mapped = true,
	.owner =	THIS_MODULE,
	.create =	zbud_zpool_create,
	.destroy =	zbud_zpool_destroy,
	.malloc =	zbud_zpool_malloc,
	.free =		zbud_zpool_free,
	.map =		zbud_zpool_map,
	.unmap =	zbud_zpool_unmap,
	.total_pages =	zbud_zpool_total_pages,
};

MODULE_ALIAS("zpool-zbud");

static int __init init_zbud(void)
{
	/* Make sure the zbud header will fit in one chunk */
	BUILD_BUG_ON(sizeof(struct zbud_header) > ZHDR_SIZE_ALIGNED);
	pr_info("loaded\n");

	zpool_register_driver(&zbud_zpool_driver);

	return 0;
}

static void __exit exit_zbud(void)
{
	zpool_unregister_driver(&zbud_zpool_driver);
	pr_info("unloaded\n");
}

module_init(init_zbud);
module_exit(exit_zbud);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seth Jennings <sjennings@variantweb.net>");
MODULE_DESCRIPTION("Buddy Allocator for Compressed Pages");
