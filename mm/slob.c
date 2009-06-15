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
 * does not have to be separately tracked, and also stores the exact
 * allocation size in page->private so that it can be used to accurately
 * provide ksize(). These objects are detected in kfree() because slob_page()
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
 * provided, alloc_pages_node() with the specified node id is used
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
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/kmemtrace.h>
#include <linux/kmemleak.h>
#include <asm/atomic.h>

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
 * We use struct page fields to manage some slob allocation aspects,
 * however to avoid the horrible mess in include/linux/mm_types.h, we'll
 * just define our own struct page type variant here.
 */
struct slob_page {
	union {
		struct {
			unsigned long flags;	/* mandatory */
			atomic_t _count;	/* mandatory */
			slobidx_t units;	/* free units left in page */
			unsigned long pad[2];
			slob_t *free;		/* first free slob_t in page */
			struct list_head list;	/* linked list of free pages */
		};
		struct page page;
	};
};
static inline void struct_slob_page_wrong_size(void)
{ BUILD_BUG_ON(sizeof(struct slob_page) != sizeof(struct page)); }

/*
 * free_slob_page: call before a slob_page is returned to the page allocator.
 */
static inline void free_slob_page(struct slob_page *sp)
{
	reset_page_mapcount(&sp->page);
	sp->page.mapping = NULL;
}

/*
 * All partially free slob pages go on these lists.
 */
#define SLOB_BREAK1 256
#define SLOB_BREAK2 1024
static LIST_HEAD(free_slob_small);
static LIST_HEAD(free_slob_medium);
static LIST_HEAD(free_slob_large);

/*
 * is_slob_page: True for all slob pages (false for bigblock pages)
 */
static inline int is_slob_page(struct slob_page *sp)
{
	return PageSlobPage((struct page *)sp);
}

static inline void set_slob_page(struct slob_page *sp)
{
	__SetPageSlobPage((struct page *)sp);
}

static inline void clear_slob_page(struct slob_page *sp)
{
	__ClearPageSlobPage((struct page *)sp);
}

static inline struct slob_page *slob_page(const void *addr)
{
	return (struct slob_page *)virt_to_page(addr);
}

/*
 * slob_page_free: true for pages on free_slob_pages list.
 */
static inline int slob_page_free(struct slob_page *sp)
{
	return PageSlobFree((struct page *)sp);
}

static void set_slob_page_free(struct slob_page *sp, struct list_head *list)
{
	list_add(&sp->list, list);
	__SetPageSlobFree((struct page *)sp);
}

static inline void clear_slob_page_free(struct slob_page *sp)
{
	list_del(&sp->list);
	__ClearPageSlobFree((struct page *)sp);
}

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1)/SLOB_UNIT)
#define SLOB_ALIGN L1_CACHE_BYTES

/*
 * struct slob_rcu is inserted at the tail of allocated slob blocks, which
 * were created with a SLAB_DESTROY_BY_RCU slab. slob_rcu is used to free
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
	void *page;

#ifdef CONFIG_NUMA
	if (node != -1)
		page = alloc_pages_node(node, gfp, order);
	else
#endif
		page = alloc_pages(gfp, order);

	if (!page)
		return NULL;

	return page_address(page);
}

static void slob_free_pages(void *b, int order)
{
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += 1 << order;
	free_pages((unsigned long)b, order);
}

/*
 * Allocate a slob block within a given slob_page sp.
 */
static void *slob_page_alloc(struct slob_page *sp, size_t size, int align)
{
	slob_t *prev, *cur, *aligned = NULL;
	int delta = 0, units = SLOB_UNITS(size);

	for (prev = NULL, cur = sp->free; ; prev = cur, cur = slob_next(cur)) {
		slobidx_t avail = slob_units(cur);

		if (align) {
			aligned = (slob_t *)ALIGN((unsigned long)cur, align);
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
					sp->free = next;
			} else { /* fragment */
				if (prev)
					set_slob(prev, slob_units(prev), cur + units);
				else
					sp->free = cur + units;
				set_slob(cur + units, avail - units, next);
			}

			sp->units -= units;
			if (!sp->units)
				clear_slob_page_free(sp);
			return cur;
		}
		if (slob_last(cur))
			return NULL;
	}
}

/*
 * slob_alloc: entry point into the slob allocator.
 */
static void *slob_alloc(size_t size, gfp_t gfp, int align, int node)
{
	struct slob_page *sp;
	struct list_head *prev;
	struct list_head *slob_list;
	slob_t *b = NULL;
	unsigned long flags;

	if (size < SLOB_BREAK1)
		slob_list = &free_slob_small;
	else if (size < SLOB_BREAK2)
		slob_list = &free_slob_medium;
	else
		slob_list = &free_slob_large;

	spin_lock_irqsave(&slob_lock, flags);
	/* Iterate through each partially free page, try to find room */
	list_for_each_entry(sp, slob_list, list) {
#ifdef CONFIG_NUMA
		/*
		 * If there's a node specification, search for a partial
		 * page with a matching node id in the freelist.
		 */
		if (node != -1 && page_to_nid(&sp->page) != node)
			continue;
#endif
		/* Enough room on this page? */
		if (sp->units < SLOB_UNITS(size))
			continue;

		/* Attempt to alloc */
		prev = sp->list.prev;
		b = slob_page_alloc(sp, size, align);
		if (!b)
			continue;

		/* Improve fragment distribution and reduce our average
		 * search time by starting our next search here. (see
		 * Knuth vol 1, sec 2.5, pg 449) */
		if (prev != slob_list->prev &&
				slob_list->next != prev->next)
			list_move_tail(slob_list, prev->next);
		break;
	}
	spin_unlock_irqrestore(&slob_lock, flags);

	/* Not enough space: must allocate a new page */
	if (!b) {
		b = slob_new_pages(gfp & ~__GFP_ZERO, 0, node);
		if (!b)
			return NULL;
		sp = slob_page(b);
		set_slob_page(sp);

		spin_lock_irqsave(&slob_lock, flags);
		sp->units = SLOB_UNITS(PAGE_SIZE);
		sp->free = b;
		INIT_LIST_HEAD(&sp->list);
		set_slob(b, SLOB_UNITS(PAGE_SIZE), b + SLOB_UNITS(PAGE_SIZE));
		set_slob_page_free(sp, slob_list);
		b = slob_page_alloc(sp, size, align);
		BUG_ON(!b);
		spin_unlock_irqrestore(&slob_lock, flags);
	}
	if (unlikely((gfp & __GFP_ZERO) && b))
		memset(b, 0, size);
	return b;
}

/*
 * slob_free: entry point into the slob allocator.
 */
static void slob_free(void *block, int size)
{
	struct slob_page *sp;
	slob_t *prev, *next, *b = (slob_t *)block;
	slobidx_t units;
	unsigned long flags;

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	BUG_ON(!size);

	sp = slob_page(block);
	units = SLOB_UNITS(size);

	spin_lock_irqsave(&slob_lock, flags);

	if (sp->units + units == SLOB_UNITS(PAGE_SIZE)) {
		/* Go directly to page allocator. Do not pass slob allocator */
		if (slob_page_free(sp))
			clear_slob_page_free(sp);
		spin_unlock_irqrestore(&slob_lock, flags);
		clear_slob_page(sp);
		free_slob_page(sp);
		slob_free_pages(b, 0);
		return;
	}

	if (!slob_page_free(sp)) {
		/* This slob page is about to become partially free. Easy! */
		sp->units = units;
		sp->free = b;
		set_slob(b, units,
			(void *)((unsigned long)(b +
					SLOB_UNITS(PAGE_SIZE)) & PAGE_MASK));
		set_slob_page_free(sp, &free_slob_small);
		goto out;
	}

	/*
	 * Otherwise the page is already partially free, so find reinsertion
	 * point.
	 */
	sp->units += units;

	if (b < sp->free) {
		if (b + units == sp->free) {
			units += slob_units(sp->free);
			sp->free = slob_next(sp->free);
		}
		set_slob(b, units, sp->free);
		sp->free = b;
	} else {
		prev = sp->free;
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

/*
 * End of slob allocator proper. Begin kmem_cache_alloc and kmalloc frontend.
 */

#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long)
#endif

#ifndef ARCH_SLAB_MINALIGN
#define ARCH_SLAB_MINALIGN __alignof__(unsigned long)
#endif

void *__kmalloc_node(size_t size, gfp_t gfp, int node)
{
	unsigned int *m;
	int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
	void *ret;

	lockdep_trace_alloc(gfp);

	if (size < PAGE_SIZE - align) {
		if (!size)
			return ZERO_SIZE_PTR;

		m = slob_alloc(size + align, gfp, align, node);

		if (!m)
			return NULL;
		*m = size;
		ret = (void *)m + align;

		trace_kmalloc_node(_RET_IP_, ret,
				   size, size + align, gfp, node);
	} else {
		unsigned int order = get_order(size);

		ret = slob_new_pages(gfp | __GFP_COMP, get_order(size), node);
		if (ret) {
			struct page *page;
			page = virt_to_page(ret);
			page->private = size;
		}

		trace_kmalloc_node(_RET_IP_, ret,
				   size, PAGE_SIZE << order, gfp, node);
	}

	kmemleak_alloc(ret, size, 1, gfp);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_node);

void kfree(const void *block)
{
	struct slob_page *sp;

	trace_kfree(_RET_IP_, block);

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	kmemleak_free(block);

	sp = slob_page(block);
	if (is_slob_page(sp)) {
		int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
		unsigned int *m = (unsigned int *)(block - align);
		slob_free(m, *m + align);
	} else
		put_page(&sp->page);
}
EXPORT_SYMBOL(kfree);

/* can't use ksize for kmem_cache_alloc memory, only kmalloc */
size_t ksize(const void *block)
{
	struct slob_page *sp;

	BUG_ON(!block);
	if (unlikely(block == ZERO_SIZE_PTR))
		return 0;

	sp = slob_page(block);
	if (is_slob_page(sp)) {
		int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
		unsigned int *m = (unsigned int *)(block - align);
		return SLOB_UNITS(*m) * SLOB_UNIT;
	} else
		return sp->page.private;
}
EXPORT_SYMBOL(ksize);

struct kmem_cache {
	unsigned int size, align;
	unsigned long flags;
	const char *name;
	void (*ctor)(void *);
};

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
	size_t align, unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *c;

	c = slob_alloc(sizeof(struct kmem_cache),
		GFP_KERNEL, ARCH_KMALLOC_MINALIGN, -1);

	if (c) {
		c->name = name;
		c->size = size;
		if (flags & SLAB_DESTROY_BY_RCU) {
			/* leave room for rcu footer at the end of object */
			c->size += sizeof(struct slob_rcu);
		}
		c->flags = flags;
		c->ctor = ctor;
		/* ignore alignment unless it's forced */
		c->align = (flags & SLAB_HWCACHE_ALIGN) ? SLOB_ALIGN : 0;
		if (c->align < ARCH_SLAB_MINALIGN)
			c->align = ARCH_SLAB_MINALIGN;
		if (c->align < align)
			c->align = align;
	} else if (flags & SLAB_PANIC)
		panic("Cannot create slab cache %s\n", name);

	kmemleak_alloc(c, sizeof(struct kmem_cache), 1, GFP_KERNEL);
	return c;
}
EXPORT_SYMBOL(kmem_cache_create);

void kmem_cache_destroy(struct kmem_cache *c)
{
	kmemleak_free(c);
	slob_free(c, sizeof(struct kmem_cache));
}
EXPORT_SYMBOL(kmem_cache_destroy);

void *kmem_cache_alloc_node(struct kmem_cache *c, gfp_t flags, int node)
{
	void *b;

	if (c->size < PAGE_SIZE) {
		b = slob_alloc(c->size, flags, c->align, node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->size,
					    SLOB_UNITS(c->size) * SLOB_UNIT,
					    flags, node);
	} else {
		b = slob_new_pages(flags, get_order(c->size), node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->size,
					    PAGE_SIZE << get_order(c->size),
					    flags, node);
	}

	if (c->ctor)
		c->ctor(b);

	kmemleak_alloc_recursive(b, c->size, 1, c->flags, flags);
	return b;
}
EXPORT_SYMBOL(kmem_cache_alloc_node);

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
	if (unlikely(c->flags & SLAB_DESTROY_BY_RCU)) {
		struct slob_rcu *slob_rcu;
		slob_rcu = b + (c->size - sizeof(struct slob_rcu));
		INIT_RCU_HEAD(&slob_rcu->head);
		slob_rcu->size = c->size;
		call_rcu(&slob_rcu->head, kmem_rcu_free);
	} else {
		__kmem_cache_free(b, c->size);
	}

	trace_kmem_cache_free(_RET_IP_, b);
}
EXPORT_SYMBOL(kmem_cache_free);

unsigned int kmem_cache_size(struct kmem_cache *c)
{
	return c->size;
}
EXPORT_SYMBOL(kmem_cache_size);

const char *kmem_cache_name(struct kmem_cache *c)
{
	return c->name;
}
EXPORT_SYMBOL(kmem_cache_name);

int kmem_cache_shrink(struct kmem_cache *d)
{
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

int kmem_ptr_validate(struct kmem_cache *a, const void *b)
{
	return 0;
}

static unsigned int slob_ready __read_mostly;

int slab_is_available(void)
{
	return slob_ready;
}

void __init kmem_cache_init(void)
{
	slob_ready = 1;
}
