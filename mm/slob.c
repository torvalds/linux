/*
 * SLOB Allocator: Simple List Of Blocks
 *
 * Matt Mackall <mpm@selenic.com> 12/30/03
 *
 * How SLOB works:
 *
 * The core of SLOB is a traditional K&R style heap allocator, with
 * support for returning aligned objects. The granularity of this
 * allocator is 8 bytes on x86, though it's perhaps possible to reduce
 * this to 4 if it's deemed worth the effort. The slob heap is a
 * singly-linked list of pages from __get_free_page, grown on demand
 * and allocation from the heap is currently first-fit.
 *
 * Above this is an implementation of kmalloc/kfree. Blocks returned
 * from kmalloc are 8-byte aligned and prepended with a 8-byte header.
 * If kmalloc is asked for objects of PAGE_SIZE or larger, it calls
 * __get_free_pages directly so that it can return page-aligned blocks
 * and keeps a linked list of such pages and their orders. These
 * objects are detected in kfree() by their page alignment.
 *
 * SLAB is emulated on top of SLOB by simply calling constructors and
 * destructors for every SLAB allocation. Objects are returned with
 * the 8-byte alignment unless the SLAB_MUST_HWCACHE_ALIGN flag is
 * set, in which case the low-level allocator will fragment blocks to
 * create the proper alignment. Again, objects of page-size or greater
 * are allocated by calling __get_free_pages. As SLAB objects know
 * their size, no separate size bookkeeping is necessary and there is
 * essentially no allocation space overhead.
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>

struct slob_block {
	int units;
	struct slob_block *next;
};
typedef struct slob_block slob_t;

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1)/SLOB_UNIT)
#define SLOB_ALIGN L1_CACHE_BYTES

struct bigblock {
	int order;
	void *pages;
	struct bigblock *next;
};
typedef struct bigblock bigblock_t;

static slob_t arena = { .next = &arena, .units = 1 };
static slob_t *slobfree = &arena;
static bigblock_t *bigblocks;
static DEFINE_SPINLOCK(slob_lock);
static DEFINE_SPINLOCK(block_lock);

static void slob_free(void *b, int size);

static void *slob_alloc(size_t size, gfp_t gfp, int align)
{
	slob_t *prev, *cur, *aligned = 0;
	int delta = 0, units = SLOB_UNITS(size);
	unsigned long flags;

	spin_lock_irqsave(&slob_lock, flags);
	prev = slobfree;
	for (cur = prev->next; ; prev = cur, cur = cur->next) {
		if (align) {
			aligned = (slob_t *)ALIGN((unsigned long)cur, align);
			delta = aligned - cur;
		}
		if (cur->units >= units + delta) { /* room enough? */
			if (delta) { /* need to fragment head to align? */
				aligned->units = cur->units - delta;
				aligned->next = cur->next;
				cur->next = aligned;
				cur->units = delta;
				prev = cur;
				cur = aligned;
			}

			if (cur->units == units) /* exact fit? */
				prev->next = cur->next; /* unlink */
			else { /* fragment */
				prev->next = cur + units;
				prev->next->units = cur->units - units;
				prev->next->next = cur->next;
				cur->units = units;
			}

			slobfree = prev;
			spin_unlock_irqrestore(&slob_lock, flags);
			return cur;
		}
		if (cur == slobfree) {
			spin_unlock_irqrestore(&slob_lock, flags);

			if (size == PAGE_SIZE) /* trying to shrink arena? */
				return 0;

			cur = (slob_t *)__get_free_page(gfp);
			if (!cur)
				return 0;

			slob_free(cur, PAGE_SIZE);
			spin_lock_irqsave(&slob_lock, flags);
			cur = slobfree;
		}
	}
}

static void slob_free(void *block, int size)
{
	slob_t *cur, *b = (slob_t *)block;
	unsigned long flags;

	if (!block)
		return;

	if (size)
		b->units = SLOB_UNITS(size);

	/* Find reinsertion point */
	spin_lock_irqsave(&slob_lock, flags);
	for (cur = slobfree; !(b > cur && b < cur->next); cur = cur->next)
		if (cur >= cur->next && (b > cur || b < cur->next))
			break;

	if (b + b->units == cur->next) {
		b->units += cur->next->units;
		b->next = cur->next->next;
	} else
		b->next = cur->next;

	if (cur + cur->units == b) {
		cur->units += b->units;
		cur->next = b->next;
	} else
		cur->next = b;

	slobfree = cur;

	spin_unlock_irqrestore(&slob_lock, flags);
}

static int FASTCALL(find_order(int size));
static int fastcall find_order(int size)
{
	int order = 0;
	for ( ; size > 4096 ; size >>=1)
		order++;
	return order;
}

void *kmalloc(size_t size, gfp_t gfp)
{
	slob_t *m;
	bigblock_t *bb;
	unsigned long flags;

	if (size < PAGE_SIZE - SLOB_UNIT) {
		m = slob_alloc(size + SLOB_UNIT, gfp, 0);
		return m ? (void *)(m + 1) : 0;
	}

	bb = slob_alloc(sizeof(bigblock_t), gfp, 0);
	if (!bb)
		return 0;

	bb->order = find_order(size);
	bb->pages = (void *)__get_free_pages(gfp, bb->order);

	if (bb->pages) {
		spin_lock_irqsave(&block_lock, flags);
		bb->next = bigblocks;
		bigblocks = bb;
		spin_unlock_irqrestore(&block_lock, flags);
		return bb->pages;
	}

	slob_free(bb, sizeof(bigblock_t));
	return 0;
}

EXPORT_SYMBOL(kmalloc);

void kfree(const void *block)
{
	bigblock_t *bb, **last = &bigblocks;
	unsigned long flags;

	if (!block)
		return;

	if (!((unsigned long)block & (PAGE_SIZE-1))) {
		/* might be on the big block list */
		spin_lock_irqsave(&block_lock, flags);
		for (bb = bigblocks; bb; last = &bb->next, bb = bb->next) {
			if (bb->pages == block) {
				*last = bb->next;
				spin_unlock_irqrestore(&block_lock, flags);
				free_pages((unsigned long)block, bb->order);
				slob_free(bb, sizeof(bigblock_t));
				return;
			}
		}
		spin_unlock_irqrestore(&block_lock, flags);
	}

	slob_free((slob_t *)block - 1, 0);
	return;
}

EXPORT_SYMBOL(kfree);

unsigned int ksize(const void *block)
{
	bigblock_t *bb;
	unsigned long flags;

	if (!block)
		return 0;

	if (!((unsigned long)block & (PAGE_SIZE-1))) {
		spin_lock_irqsave(&block_lock, flags);
		for (bb = bigblocks; bb; bb = bb->next)
			if (bb->pages == block) {
				spin_unlock_irqrestore(&slob_lock, flags);
				return PAGE_SIZE << bb->order;
			}
		spin_unlock_irqrestore(&block_lock, flags);
	}

	return ((slob_t *)block - 1)->units * SLOB_UNIT;
}

struct kmem_cache {
	unsigned int size, align;
	const char *name;
	void (*ctor)(void *, struct kmem_cache *, unsigned long);
	void (*dtor)(void *, struct kmem_cache *, unsigned long);
};

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
	size_t align, unsigned long flags,
	void (*ctor)(void*, struct kmem_cache *, unsigned long),
	void (*dtor)(void*, struct kmem_cache *, unsigned long))
{
	struct kmem_cache *c;

	c = slob_alloc(sizeof(struct kmem_cache), flags, 0);

	if (c) {
		c->name = name;
		c->size = size;
		c->ctor = ctor;
		c->dtor = dtor;
		/* ignore alignment unless it's forced */
		c->align = (flags & SLAB_MUST_HWCACHE_ALIGN) ? SLOB_ALIGN : 0;
		if (c->align < align)
			c->align = align;
	}

	return c;
}
EXPORT_SYMBOL(kmem_cache_create);

int kmem_cache_destroy(struct kmem_cache *c)
{
	slob_free(c, sizeof(struct kmem_cache));
	return 0;
}
EXPORT_SYMBOL(kmem_cache_destroy);

void *kmem_cache_alloc(struct kmem_cache *c, gfp_t flags)
{
	void *b;

	if (c->size < PAGE_SIZE)
		b = slob_alloc(c->size, flags, c->align);
	else
		b = (void *)__get_free_pages(flags, find_order(c->size));

	if (c->ctor)
		c->ctor(b, c, SLAB_CTOR_CONSTRUCTOR);

	return b;
}
EXPORT_SYMBOL(kmem_cache_alloc);

void *kmem_cache_zalloc(struct kmem_cache *c, gfp_t flags)
{
	void *ret = kmem_cache_alloc(c, flags);
	if (ret)
		memset(ret, 0, c->size);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_zalloc);

void kmem_cache_free(struct kmem_cache *c, void *b)
{
	if (c->dtor)
		c->dtor(b, c, 0);

	if (c->size < PAGE_SIZE)
		slob_free(b, c->size);
	else
		free_pages((unsigned long)b, find_order(c->size));
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

static struct timer_list slob_timer = TIMER_INITIALIZER(
	(void (*)(unsigned long))kmem_cache_init, 0, 0);

void kmem_cache_init(void)
{
	void *p = slob_alloc(PAGE_SIZE, 0, PAGE_SIZE-1);

	if (p)
		free_page((unsigned long)p);

	mod_timer(&slob_timer, jiffies + HZ);
}

atomic_t slab_reclaim_pages = ATOMIC_INIT(0);
EXPORT_SYMBOL(slab_reclaim_pages);

#ifdef CONFIG_SMP

void *__alloc_percpu(size_t size)
{
	int i;
	struct percpu_data *pdata = kmalloc(sizeof (*pdata), GFP_KERNEL);

	if (!pdata)
		return NULL;

	for_each_possible_cpu(i) {
		pdata->ptrs[i] = kmalloc(size, GFP_KERNEL);
		if (!pdata->ptrs[i])
			goto unwind_oom;
		memset(pdata->ptrs[i], 0, size);
	}

	/* Catch derefs w/o wrappers */
	return (void *) (~(unsigned long) pdata);

unwind_oom:
	while (--i >= 0) {
		if (!cpu_possible(i))
			continue;
		kfree(pdata->ptrs[i]);
	}
	kfree(pdata);
	return NULL;
}
EXPORT_SYMBOL(__alloc_percpu);

void
free_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for_each_possible_cpu(i)
		kfree(p->ptrs[i]);

	kfree(p);
}
EXPORT_SYMBOL(free_percpu);

#endif
