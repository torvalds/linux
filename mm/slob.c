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
 * the 8-byte alignment unless the SLAB_HWCACHE_ALIGN flag is
 * set, in which case the low-level allocator will fragment blocks to
 * create the proper alignment. Again, objects of page-size or greater
 * are allocated by calling __get_free_pages. As SLAB objects know
 * their size, no separate size bookkeeping is necessary and there is
 * essentially no allocation space overhead.
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/rcupdate.h>

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

/*
 * struct slob_rcu is inserted at the tail of allocated slob blocks, which
 * were created with a SLAB_DESTROY_BY_RCU slab. slob_rcu is used to free
 * the block using call_rcu.
 */
struct slob_rcu {
	struct rcu_head head;
	int size;
};

static slob_t arena = { .next = &arena, .units = 1 };
static slob_t *slobfree = &arena;
static bigblock_t *bigblocks;
static DEFINE_SPINLOCK(slob_lock);
static DEFINE_SPINLOCK(block_lock);

static void slob_free(void *b, int size);
static void slob_timer_cbk(void);


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

void *__kmalloc(size_t size, gfp_t gfp)
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

	bb->order = get_order(size);
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
EXPORT_SYMBOL(__kmalloc);

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 *
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * The contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.  If @p is %NULL, krealloc()
 * behaves exactly like kmalloc().  If @size is 0 and @p is not a
 * %NULL pointer, the object pointed to is freed.
 */
void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	void *ret;

	if (unlikely(!p))
		return kmalloc_track_caller(new_size, flags);

	if (unlikely(!new_size)) {
		kfree(p);
		return NULL;
	}

	ret = kmalloc_track_caller(new_size, flags);
	if (ret) {
		memcpy(ret, p, min(new_size, ksize(p)));
		kfree(p);
	}
	return ret;
}
EXPORT_SYMBOL(krealloc);

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

size_t ksize(const void *block)
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
	unsigned long flags;
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
		if (flags & SLAB_DESTROY_BY_RCU) {
			BUG_ON(dtor);
			/* leave room for rcu footer at the end of object */
			c->size += sizeof(struct slob_rcu);
		}
		c->flags = flags;
		c->ctor = ctor;
		c->dtor = dtor;
		/* ignore alignment unless it's forced */
		c->align = (flags & SLAB_HWCACHE_ALIGN) ? SLOB_ALIGN : 0;
		if (c->align < align)
			c->align = align;
	} else if (flags & SLAB_PANIC)
		panic("Cannot create slab cache %s\n", name);

	return c;
}
EXPORT_SYMBOL(kmem_cache_create);

void kmem_cache_destroy(struct kmem_cache *c)
{
	slob_free(c, sizeof(struct kmem_cache));
}
EXPORT_SYMBOL(kmem_cache_destroy);

void *kmem_cache_alloc(struct kmem_cache *c, gfp_t flags)
{
	void *b;

	if (c->size < PAGE_SIZE)
		b = slob_alloc(c->size, flags, c->align);
	else
		b = (void *)__get_free_pages(flags, get_order(c->size));

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

static void __kmem_cache_free(void *b, int size)
{
	if (size < PAGE_SIZE)
		slob_free(b, size);
	else
		free_pages((unsigned long)b, get_order(size));
}

static void kmem_rcu_free(struct rcu_head *head)
{
	struct slob_rcu *slob_rcu = (struct slob_rcu *)head;
	void *b = (void *)slob_rcu - (slob_rcu->size - sizeof(struct slob_rcu));

	__kmem_cache_free(b, slob_rcu->size);
}

void kmem_cache_free(struct kmem_cache *c, void *b)
{
	if (unlikely(c->flags & SLAB_DESTROY_BY_RCU)) {
		struct slob_rcu *slob_rcu;
		slob_rcu = b + (c->size - sizeof(struct slob_rcu));
		INIT_RCU_HEAD(&slob_rcu->head);
		slob_rcu->size = c->size;
		call_rcu(&slob_rcu->head, kmem_rcu_free);
	} else {
		if (c->dtor)
			c->dtor(b, c, 0);
		__kmem_cache_free(b, c->size);
	}
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
	(void (*)(unsigned long))slob_timer_cbk, 0, 0);

int kmem_cache_shrink(struct kmem_cache *d)
{
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

int kmem_ptr_validate(struct kmem_cache *a, const void *b)
{
	return 0;
}

void __init kmem_cache_init(void)
{
	slob_timer_cbk();
}

static void slob_timer_cbk(void)
{
	void *p = slob_alloc(PAGE_SIZE, 0, PAGE_SIZE-1);

	if (p)
		free_page((unsigned long)p);

	mod_timer(&slob_timer, jiffies + HZ);
}
