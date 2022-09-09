// SPDX-License-Identifier: GPL-2.0
/*
 * SLOB Allocator: Simple List Of Blocks
 *
 * Matt Mackall <mpm@selenic.com> 12/30/03
 *
 * NUMA support by Paul Mundt, 2007.
 *
 * How SLOB works:
 *
 * The core of SLOB is a traditional K&R style heap allocator, with
 * support for returning aligned objects. The granularity of this
 * allocator is as little as 2 bytes, however typically most architectures
 * will require 4 bytes on 32-bit and 8 bytes on 64-bit.
 *
 * The slob heap is a set of linked list of pages from alloc_pages(),
 * and within each page, there is a singly-linked list of free blocks
 * (slob_t). The heap is grown on demand. To reduce fragmentation,
 * heap pages are segregated into three lists, with objects less than
 * 256 bytes, objects less than 1024 bytes, and all other objects.
 *
 * Allocation from heap involves first searching for a page with
 * sufficient free blocks (using a next-fit-like approach) followed by
 * a first-fit scan of the page. Deallocation inserts objects back
 * into the free list in address order, so this is effectively an
 * address-ordered first fit.
 *
 * Above this is an implementation of kmalloc/kfree. Blocks returned
 * from kmalloc are prepended with a 4-byte header with the kmalloc size.
 * If kmalloc is asked for objects of PAGE_SIZE or larger, it calls
 * alloc_pages() directly, allocating compound pages so the page order
 * does not have to be separately tracked.
 * These objects are detected in kfree() because folio_test_slab()
 * is false for them.
 *
 * SLAB is emulated on top of SLOB by simply calling constructors and
 * destructors for every SLAB allocation. Objects are returned with the
 * 4-byte alignment unless the SLAB_HWCACHE_ALIGN flag is set, in which
 * case the low-level allocator will fragment blocks to create the proper
 * alignment. Again, objects of page-size or greater are allocated by
 * calling alloc_pages(). As SLAB objects know their size, no separate
 * size bookkeeping is necessary and there is essentially no allocation
 * space overhead, and compound pages aren't needed for multi-page
 * allocations.
 *
 * NUMA support in SLOB is fairly simplistic, pushing most of the real
 * logic down to the page allocator, and simply doing the node accounting
 * on the upper levels. In the event that a node id is explicitly
 * provided, __alloc_pages_node() with the specified node id is used
 * instead. The common case (or when the node id isn't explicitly provided)
 * will default to the current node, as per numa_node_id().
 *
 * Node aware pages are still inserted in to the global freelist, and
 * these are scanned for by matching against the node id encoded in the
 * page flags. As a result, block allocations that can be satisfied from
 * the freelist will only be done so on pages residing on the same node,
 * in order to prevent random node placement.
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/kmemleak.h>

#include <trace/events/kmem.h>

#include <linux/atomic.h>

#include "slab.h"
/*
 * slob_block has a field 'units', which indicates size of block if +ve,
 * or offset of next block if -ve (in SLOB_UNITs).
 *
 * Free blocks of size 1 unit simply contain the offset of the next block.
 * Those with larger size contain their size in the first SLOB_UNIT of
 * memory, and the offset of the next free block in the second SLOB_UNIT.
 */
#if PAGE_SIZE <= (32767 * 2)
typedef s16 slobidx_t;
#else
typedef s32 slobidx_t;
#endif

struct slob_block {
	slobidx_t units;
};
typedef struct slob_block slob_t;

/*
 * All partially free slob pages go on these lists.
 */
#define SLOB_BREAK1 256
#define SLOB_BREAK2 1024
static LIST_HEAD(free_slob_small);
static LIST_HEAD(free_slob_medium);
static LIST_HEAD(free_slob_large);

/*
 * slob_page_free: true for pages on free_slob_pages list.
 */
static inline int slob_page_free(struct slab *slab)
{
	return PageSlobFree(slab_page(slab));
}

static void set_slob_page_free(struct slab *slab, struct list_head *list)
{
	list_add(&slab->slab_list, list);
	__SetPageSlobFree(slab_page(slab));
}

static inline void clear_slob_page_free(struct slab *slab)
{
	list_del(&slab->slab_list);
	__ClearPageSlobFree(slab_page(slab));
}

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) DIV_ROUND_UP(size, SLOB_UNIT)

/*
 * struct slob_rcu is inserted at the tail of allocated slob blocks, which
 * were created with a SLAB_TYPESAFE_BY_RCU slab. slob_rcu is used to free
 * the block using call_rcu.
 */
struct slob_rcu {
	struct rcu_head head;
	int size;
};

/*
 * slob_lock protects all slob allocator structures.
 */
static DEFINE_SPINLOCK(slob_lock);

/*
 * Encode the given size and next info into a free slob block s.
 */
static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
	slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
	slobidx_t offset = next - base;

	if (size > 1) {
		s[0].units = size;
		s[1].units = offset;
	} else
		s[0].units = -offset;
}

/*
 * Return the size of a slob block.
 */
static slobidx_t slob_units(slob_t *s)
{
	if (s->units > 0)
		return s->units;
	return 1;
}

/*
 * Return the next free slob block pointer after this one.
 */
static slob_t *slob_next(slob_t *s)
{
	slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
	slobidx_t next;

	if (s[0].units < 0)
		next = -s[0].units;
	else
		next = s[1].units;
	return base+next;
}

/*
 * Returns true if s is the last free block in its page.
 */
static int slob_last(slob_t *s)
{
	return !((unsigned long)slob_next(s) & ~PAGE_MASK);
}

static void *slob_new_pages(gfp_t gfp, int order, int node)
{
	struct page *page;

#ifdef CONFIG_NUMA
	if (node != NUMA_NO_NODE)
		page = __alloc_pages_node(node, gfp, order);
	else
#endif
		page = alloc_pages(gfp, order);

	if (!page)
		return NULL;

	mod_node_page_state(page_pgdat(page), NR_SLAB_UNRECLAIMABLE_B,
			    PAGE_SIZE << order);
	return page_address(page);
}

static void slob_free_pages(void *b, int order)
{
	struct page *sp = virt_to_page(b);

	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += 1 << order;

	mod_node_page_state(page_pgdat(sp), NR_SLAB_UNRECLAIMABLE_B,
			    -(PAGE_SIZE << order));
	__free_pages(sp, order);
}

/*
 * slob_page_alloc() - Allocate a slob block within a given slob_page sp.
 * @sp: Page to look in.
 * @size: Size of the allocation.
 * @align: Allocation alignment.
 * @align_offset: Offset in the allocated block that will be aligned.
 * @page_removed_from_list: Return parameter.
 *
 * Tries to find a chunk of memory at least @size bytes big within @page.
 *
 * Return: Pointer to memory if allocated, %NULL otherwise.  If the
 *         allocation fills up @page then the page is removed from the
 *         freelist, in this case @page_removed_from_list will be set to
 *         true (set to false otherwise).
 */
static void *slob_page_alloc(struct slab *sp, size_t size, int align,
			      int align_offset, bool *page_removed_from_list)
{
	slob_t *prev, *cur, *aligned = NULL;
	int delta = 0, units = SLOB_UNITS(size);

	*page_removed_from_list = false;
	for (prev = NULL, cur = sp->freelist; ; prev = cur, cur = slob_next(cur)) {
		slobidx_t avail = slob_units(cur);

		/*
		 * 'aligned' will hold the address of the slob block so that the
		 * address 'aligned'+'align_offset' is aligned according to the
		 * 'align' parameter. This is for kmalloc() which prepends the
		 * allocated block with its size, so that the block itself is
		 * aligned when needed.
		 */
		if (align) {
			aligned = (slob_t *)
				(ALIGN((unsigned long)cur + align_offset, align)
				 - align_offset);
			delta = aligned - cur;
		}
		if (avail >= units + delta) { /* room enough? */
			slob_t *next;

			if (delta) { /* need to fragment head to align? */
				next = slob_next(cur);
				set_slob(aligned, avail - delta, next);
				set_slob(cur, delta, aligned);
				prev = cur;
				cur = aligned;
				avail = slob_units(cur);
			}

			next = slob_next(cur);
			if (avail == units) { /* exact fit? unlink. */
				if (prev)
					set_slob(prev, slob_units(prev), next);
				else
					sp->freelist = next;
			} else { /* fragment */
				if (prev)
					set_slob(prev, slob_units(prev), cur + units);
				else
					sp->freelist = cur + units;
				set_slob(cur + units, avail - units, next);
			}

			sp->units -= units;
			if (!sp->units) {
				clear_slob_page_free(sp);
				*page_removed_from_list = true;
			}
			return cur;
		}
		if (slob_last(cur))
			return NULL;
	}
}

/*
 * slob_alloc: entry point into the slob allocator.
 */
static void *slob_alloc(size_t size, gfp_t gfp, int align, int node,
							int align_offset)
{
	struct folio *folio;
	struct slab *sp;
	struct list_head *slob_list;
	slob_t *b = NULL;
	unsigned long flags;
	bool _unused;

	if (size < SLOB_BREAK1)
		slob_list = &free_slob_small;
	else if (size < SLOB_BREAK2)
		slob_list = &free_slob_medium;
	else
		slob_list = &free_slob_large;

	spin_lock_irqsave(&slob_lock, flags);
	/* Iterate through each partially free page, try to find room */
	list_for_each_entry(sp, slob_list, slab_list) {
		bool page_removed_from_list = false;
#ifdef CONFIG_NUMA
		/*
		 * If there's a node specification, search for a partial
		 * page with a matching node id in the freelist.
		 */
		if (node != NUMA_NO_NODE && slab_nid(sp) != node)
			continue;
#endif
		/* Enough room on this page? */
		if (sp->units < SLOB_UNITS(size))
			continue;

		b = slob_page_alloc(sp, size, align, align_offset, &page_removed_from_list);
		if (!b)
			continue;

		/*
		 * If slob_page_alloc() removed sp from the list then we
		 * cannot call list functions on sp.  If so allocation
		 * did not fragment the page anyway so optimisation is
		 * unnecessary.
		 */
		if (!page_removed_from_list) {
			/*
			 * Improve fragment distribution and reduce our average
			 * search time by starting our next search here. (see
			 * Knuth vol 1, sec 2.5, pg 449)
			 */
			if (!list_is_first(&sp->slab_list, slob_list))
				list_rotate_to_front(&sp->slab_list, slob_list);
		}
		break;
	}
	spin_unlock_irqrestore(&slob_lock, flags);

	/* Not enough space: must allocate a new page */
	if (!b) {
		b = slob_new_pages(gfp & ~__GFP_ZERO, 0, node);
		if (!b)
			return NULL;
		folio = virt_to_folio(b);
		__folio_set_slab(folio);
		sp = folio_slab(folio);

		spin_lock_irqsave(&slob_lock, flags);
		sp->units = SLOB_UNITS(PAGE_SIZE);
		sp->freelist = b;
		INIT_LIST_HEAD(&sp->slab_list);
		set_slob(b, SLOB_UNITS(PAGE_SIZE), b + SLOB_UNITS(PAGE_SIZE));
		set_slob_page_free(sp, slob_list);
		b = slob_page_alloc(sp, size, align, align_offset, &_unused);
		BUG_ON(!b);
		spin_unlock_irqrestore(&slob_lock, flags);
	}
	if (unlikely(gfp & __GFP_ZERO))
		memset(b, 0, size);
	return b;
}

/*
 * slob_free: entry point into the slob allocator.
 */
static void slob_free(void *block, int size)
{
	struct slab *sp;
	slob_t *prev, *next, *b = (slob_t *)block;
	slobidx_t units;
	unsigned long flags;
	struct list_head *slob_list;

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	BUG_ON(!size);

	sp = virt_to_slab(block);
	units = SLOB_UNITS(size);

	spin_lock_irqsave(&slob_lock, flags);

	if (sp->units + units == SLOB_UNITS(PAGE_SIZE)) {
		/* Go directly to page allocator. Do not pass slob allocator */
		if (slob_page_free(sp))
			clear_slob_page_free(sp);
		spin_unlock_irqrestore(&slob_lock, flags);
		__folio_clear_slab(slab_folio(sp));
		slob_free_pages(b, 0);
		return;
	}

	if (!slob_page_free(sp)) {
		/* This slob page is about to become partially free. Easy! */
		sp->units = units;
		sp->freelist = b;
		set_slob(b, units,
			(void *)((unsigned long)(b +
					SLOB_UNITS(PAGE_SIZE)) & PAGE_MASK));
		if (size < SLOB_BREAK1)
			slob_list = &free_slob_small;
		else if (size < SLOB_BREAK2)
			slob_list = &free_slob_medium;
		else
			slob_list = &free_slob_large;
		set_slob_page_free(sp, slob_list);
		goto out;
	}

	/*
	 * Otherwise the page is already partially free, so find reinsertion
	 * point.
	 */
	sp->units += units;

	if (b < (slob_t *)sp->freelist) {
		if (b + units == sp->freelist) {
			units += slob_units(sp->freelist);
			sp->freelist = slob_next(sp->freelist);
		}
		set_slob(b, units, sp->freelist);
		sp->freelist = b;
	} else {
		prev = sp->freelist;
		next = slob_next(prev);
		while (b > next) {
			prev = next;
			next = slob_next(prev);
		}

		if (!slob_last(prev) && b + units == next) {
			units += slob_units(next);
			set_slob(b, units, slob_next(next));
		} else
			set_slob(b, units, next);

		if (prev + slob_units(prev) == b) {
			units = slob_units(b) + slob_units(prev);
			set_slob(prev, units, slob_next(b));
		} else
			set_slob(prev, slob_units(prev), b);
	}
out:
	spin_unlock_irqrestore(&slob_lock, flags);
}

#ifdef CONFIG_PRINTK
void __kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab)
{
	kpp->kp_ptr = object;
	kpp->kp_slab = slab;
}
#endif

/*
 * End of slob allocator proper. Begin kmem_cache_alloc and kmalloc frontend.
 */

static __always_inline void *
__do_kmalloc_node(size_t size, gfp_t gfp, int node, unsigned long caller)
{
	unsigned int *m;
	unsigned int minalign;
	void *ret;

	minalign = max_t(unsigned int, ARCH_KMALLOC_MINALIGN,
			 arch_slab_minalign());
	gfp &= gfp_allowed_mask;

	might_alloc(gfp);

	if (size < PAGE_SIZE - minalign) {
		int align = minalign;

		/*
		 * For power of two sizes, guarantee natural alignment for
		 * kmalloc()'d objects.
		 */
		if (is_power_of_2(size))
			align = max_t(unsigned int, minalign, size);

		if (!size)
			return ZERO_SIZE_PTR;

		m = slob_alloc(size + minalign, gfp, align, node, minalign);

		if (!m)
			return NULL;
		*m = size;
		ret = (void *)m + minalign;

		trace_kmalloc_node(caller, ret, NULL,
				   size, size + minalign, gfp, node);
	} else {
		unsigned int order = get_order(size);

		if (likely(order))
			gfp |= __GFP_COMP;
		ret = slob_new_pages(gfp, order, node);

		trace_kmalloc_node(caller, ret, NULL,
				   size, PAGE_SIZE << order, gfp, node);
	}

	kmemleak_alloc(ret, size, 1, gfp);
	return ret;
}

void *__kmalloc(size_t size, gfp_t gfp)
{
	return __do_kmalloc_node(size, gfp, NUMA_NO_NODE, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc);

void *__kmalloc_track_caller(size_t size, gfp_t gfp, unsigned long caller)
{
	return __do_kmalloc_node(size, gfp, NUMA_NO_NODE, caller);
}
EXPORT_SYMBOL(__kmalloc_track_caller);

#ifdef CONFIG_NUMA
void *__kmalloc_node_track_caller(size_t size, gfp_t gfp,
					int node, unsigned long caller)
{
	return __do_kmalloc_node(size, gfp, node, caller);
}
EXPORT_SYMBOL(__kmalloc_node_track_caller);
#endif

void kfree(const void *block)
{
	struct folio *sp;

	trace_kfree(_RET_IP_, block);

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	kmemleak_free(block);

	sp = virt_to_folio(block);
	if (folio_test_slab(sp)) {
		unsigned int align = max_t(unsigned int,
					   ARCH_KMALLOC_MINALIGN,
					   arch_slab_minalign());
		unsigned int *m = (unsigned int *)(block - align);

		slob_free(m, *m + align);
	} else {
		unsigned int order = folio_order(sp);

		mod_node_page_state(folio_pgdat(sp), NR_SLAB_UNRECLAIMABLE_B,
				    -(PAGE_SIZE << order));
		__free_pages(folio_page(sp, 0), order);

	}
}
EXPORT_SYMBOL(kfree);

/* can't use ksize for kmem_cache_alloc memory, only kmalloc */
size_t __ksize(const void *block)
{
	struct folio *folio;
	unsigned int align;
	unsigned int *m;

	BUG_ON(!block);
	if (unlikely(block == ZERO_SIZE_PTR))
		return 0;

	folio = virt_to_folio(block);
	if (unlikely(!folio_test_slab(folio)))
		return folio_size(folio);

	align = max_t(unsigned int, ARCH_KMALLOC_MINALIGN,
		      arch_slab_minalign());
	m = (unsigned int *)(block - align);
	return SLOB_UNITS(*m) * SLOB_UNIT;
}
EXPORT_SYMBOL(__ksize);

int __kmem_cache_create(struct kmem_cache *c, slab_flags_t flags)
{
	if (flags & SLAB_TYPESAFE_BY_RCU) {
		/* leave room for rcu footer at the end of object */
		c->size += sizeof(struct slob_rcu);
	}
	c->flags = flags;
	return 0;
}

static void *slob_alloc_node(struct kmem_cache *c, gfp_t flags, int node)
{
	void *b;

	flags &= gfp_allowed_mask;

	might_alloc(flags);

	if (c->size < PAGE_SIZE) {
		b = slob_alloc(c->size, flags, c->align, node, 0);
		trace_kmem_cache_alloc_node(_RET_IP_, b, NULL, c->object_size,
					    SLOB_UNITS(c->size) * SLOB_UNIT,
					    flags, node);
	} else {
		b = slob_new_pages(flags, get_order(c->size), node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, NULL, c->object_size,
					    PAGE_SIZE << get_order(c->size),
					    flags, node);
	}

	if (b && c->ctor) {
		WARN_ON_ONCE(flags & __GFP_ZERO);
		c->ctor(b);
	}

	kmemleak_alloc_recursive(b, c->size, 1, c->flags, flags);
	return b;
}

void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags)
{
	return slob_alloc_node(cachep, flags, NUMA_NO_NODE);
}
EXPORT_SYMBOL(kmem_cache_alloc);


void *kmem_cache_alloc_lru(struct kmem_cache *cachep, struct list_lru *lru, gfp_t flags)
{
	return slob_alloc_node(cachep, flags, NUMA_NO_NODE);
}
EXPORT_SYMBOL(kmem_cache_alloc_lru);
#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t gfp, int node)
{
	return __do_kmalloc_node(size, gfp, node, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc_node);

void *kmem_cache_alloc_node(struct kmem_cache *cachep, gfp_t gfp, int node)
{
	return slob_alloc_node(cachep, gfp, node);
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
#endif

static void __kmem_cache_free(void *b, int size)
{
	if (size < PAGE_SIZE)
		slob_free(b, size);
	else
		slob_free_pages(b, get_order(size));
}

static void kmem_rcu_free(struct rcu_head *head)
{
	struct slob_rcu *slob_rcu = (struct slob_rcu *)head;
	void *b = (void *)slob_rcu - (slob_rcu->size - sizeof(struct slob_rcu));

	__kmem_cache_free(b, slob_rcu->size);
}

void kmem_cache_free(struct kmem_cache *c, void *b)
{
	kmemleak_free_recursive(b, c->flags);
	trace_kmem_cache_free(_RET_IP_, b, c->name);
	if (unlikely(c->flags & SLAB_TYPESAFE_BY_RCU)) {
		struct slob_rcu *slob_rcu;
		slob_rcu = b + (c->size - sizeof(struct slob_rcu));
		slob_rcu->size = c->size;
		call_rcu(&slob_rcu->head, kmem_rcu_free);
	} else {
		__kmem_cache_free(b, c->size);
	}
}
EXPORT_SYMBOL(kmem_cache_free);

void kmem_cache_free_bulk(struct kmem_cache *s, size_t nr, void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		if (s)
			kmem_cache_free(s, p[i]);
		else
			kfree(p[i]);
	}
}
EXPORT_SYMBOL(kmem_cache_free_bulk);

int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t nr,
								void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		void *x = p[i] = kmem_cache_alloc(s, flags);

		if (!x) {
			kmem_cache_free_bulk(s, i, p);
			return 0;
		}
	}
	return i;
}
EXPORT_SYMBOL(kmem_cache_alloc_bulk);

int __kmem_cache_shutdown(struct kmem_cache *c)
{
	/* No way to check for remaining objects */
	return 0;
}

void __kmem_cache_release(struct kmem_cache *c)
{
}

int __kmem_cache_shrink(struct kmem_cache *d)
{
	return 0;
}

static struct kmem_cache kmem_cache_boot = {
	.name = "kmem_cache",
	.size = sizeof(struct kmem_cache),
	.flags = SLAB_PANIC,
	.align = ARCH_KMALLOC_MINALIGN,
};

void __init kmem_cache_init(void)
{
	kmem_cache = &kmem_cache_boot;
	slab_state = UP;
}

void __init kmem_cache_init_late(void)
{
	slab_state = FULL;
}
