#include <linux/gfp.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include "slab.h"
#include <linux/kmemcheck.h>

void kmemcheck_alloc_shadow(struct page *page, int order, gfp_t flags, int node)
{
	struct page *shadow;
	int pages;
	int i;

	pages = 1 << order;

	/*
	 * With kmemcheck enabled, we need to allocate a memory area for the
	 * shadow bits as well.
	 */
	shadow = alloc_pages_node(node, flags | __GFP_NOTRACK, order);
	if (!shadow) {
		if (printk_ratelimit())
			printk(KERN_ERR "kmemcheck: failed to allocate "
				"shadow bitmap\n");
		return;
	}

	for(i = 0; i < pages; ++i)
		page[i].shadow = page_address(&shadow[i]);

	/*
	 * Mark it as non-present for the MMU so that our accesses to
	 * this memory will trigger a page fault and let us analyze
	 * the memory accesses.
	 */
	kmemcheck_hide_pages(page, pages);
}

void kmemcheck_free_shadow(struct page *page, int order)
{
	struct page *shadow;
	int pages;
	int i;

	if (!kmemcheck_page_is_tracked(page))
		return;

	pages = 1 << order;

	kmemcheck_show_pages(page, pages);

	shadow = virt_to_page(page[0].shadow);

	for(i = 0; i < pages; ++i)
		page[i].shadow = NULL;

	__free_pages(shadow, order);
}

void kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object,
			  size_t size)
{
	/*
	 * Has already been memset(), which initializes the shadow for us
	 * as well.
	 */
	if (gfpflags & __GFP_ZERO)
		return;

	/* No need to initialize the shadow of a non-tracked slab. */
	if (s->flags & SLAB_NOTRACK)
		return;

	if (!kmemcheck_enabled || gfpflags & __GFP_NOTRACK) {
		/*
		 * Allow notracked objects to be allocated from
		 * tracked caches. Note however that these objects
		 * will still get page faults on access, they just
		 * won't ever be flagged as uninitialized. If page
		 * faults are not acceptable, the slab cache itself
		 * should be marked NOTRACK.
		 */
		kmemcheck_mark_initialized(object, size);
	} else if (!s->ctor) {
		/*
		 * New objects should be marked uninitialized before
		 * they're returned to the called.
		 */
		kmemcheck_mark_uninitialized(object, size);
	}
}

void kmemcheck_slab_free(struct kmem_cache *s, void *object, size_t size)
{
	/* TODO: RCU freeing is unsupported for now; hide false positives. */
	if (!s->ctor && !(s->flags & SLAB_DESTROY_BY_RCU))
		kmemcheck_mark_freed(object, size);
}

void kmemcheck_pagealloc_alloc(struct page *page, unsigned int order,
			       gfp_t gfpflags)
{
	int pages;

	if (gfpflags & (__GFP_HIGHMEM | __GFP_NOTRACK))
		return;

	pages = 1 << order;

	/*
	 * NOTE: We choose to track GFP_ZERO pages too; in fact, they
	 * can become uninitialized by copying uninitialized memory
	 * into them.
	 */

	/* XXX: Can use zone->node for node? */
	kmemcheck_alloc_shadow(page, order, gfpflags, -1);

	if (gfpflags & __GFP_ZERO)
		kmemcheck_mark_initialized_pages(page, pages);
	else
		kmemcheck_mark_uninitialized_pages(page, pages);
}
