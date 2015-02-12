/*
 * Workingset detection
 *
 * Copyright (C) 2013 Red Hat, Inc., Johannes Weiner
 */

#include <linux/memcontrol.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/mm.h>

/*
 *		Double CLOCK lists
 *
 * Per zone, two clock lists are maintained for file pages: the
 * inactive and the active list.  Freshly faulted pages start out at
 * the head of the inactive list and page reclaim scans pages from the
 * tail.  Pages that are accessed multiple times on the inactive list
 * are promoted to the active list, to protect them from reclaim,
 * whereas active pages are demoted to the inactive list when the
 * active list grows too big.
 *
 *   fault ------------------------+
 *                                 |
 *              +--------------+   |            +-------------+
 *   reclaim <- |   inactive   | <-+-- demotion |    active   | <--+
 *              +--------------+                +-------------+    |
 *                     |                                           |
 *                     +-------------- promotion ------------------+
 *
 *
 *		Access frequency and refault distance
 *
 * A workload is thrashing when its pages are frequently used but they
 * are evicted from the inactive list every time before another access
 * would have promoted them to the active list.
 *
 * In cases where the average access distance between thrashing pages
 * is bigger than the size of memory there is nothing that can be
 * done - the thrashing set could never fit into memory under any
 * circumstance.
 *
 * However, the average access distance could be bigger than the
 * inactive list, yet smaller than the size of memory.  In this case,
 * the set could fit into memory if it weren't for the currently
 * active pages - which may be used more, hopefully less frequently:
 *
 *      +-memory available to cache-+
 *      |                           |
 *      +-inactive------+-active----+
 *  a b | c d e f g h i | J K L M N |
 *      +---------------+-----------+
 *
 * It is prohibitively expensive to accurately track access frequency
 * of pages.  But a reasonable approximation can be made to measure
 * thrashing on the inactive list, after which refaulting pages can be
 * activated optimistically to compete with the existing active pages.
 *
 * Approximating inactive page access frequency - Observations:
 *
 * 1. When a page is accessed for the first time, it is added to the
 *    head of the inactive list, slides every existing inactive page
 *    towards the tail by one slot, and pushes the current tail page
 *    out of memory.
 *
 * 2. When a page is accessed for the second time, it is promoted to
 *    the active list, shrinking the inactive list by one slot.  This
 *    also slides all inactive pages that were faulted into the cache
 *    more recently than the activated page towards the tail of the
 *    inactive list.
 *
 * Thus:
 *
 * 1. The sum of evictions and activations between any two points in
 *    time indicate the minimum number of inactive pages accessed in
 *    between.
 *
 * 2. Moving one inactive page N page slots towards the tail of the
 *    list requires at least N inactive page accesses.
 *
 * Combining these:
 *
 * 1. When a page is finally evicted from memory, the number of
 *    inactive pages accessed while the page was in cache is at least
 *    the number of page slots on the inactive list.
 *
 * 2. In addition, measuring the sum of evictions and activations (E)
 *    at the time of a page's eviction, and comparing it to another
 *    reading (R) at the time the page faults back into memory tells
 *    the minimum number of accesses while the page was not cached.
 *    This is called the refault distance.
 *
 * Because the first access of the page was the fault and the second
 * access the refault, we combine the in-cache distance with the
 * out-of-cache distance to get the complete minimum access distance
 * of this page:
 *
 *      NR_inactive + (R - E)
 *
 * And knowing the minimum access distance of a page, we can easily
 * tell if the page would be able to stay in cache assuming all page
 * slots in the cache were available:
 *
 *   NR_inactive + (R - E) <= NR_inactive + NR_active
 *
 * which can be further simplified to
 *
 *   (R - E) <= NR_active
 *
 * Put into words, the refault distance (out-of-cache) can be seen as
 * a deficit in inactive list space (in-cache).  If the inactive list
 * had (R - E) more page slots, the page would not have been evicted
 * in between accesses, but activated instead.  And on a full system,
 * the only thing eating into inactive list space is active pages.
 *
 *
 *		Activating refaulting pages
 *
 * All that is known about the active list is that the pages have been
 * accessed more than once in the past.  This means that at any given
 * time there is actually a good chance that pages on the active list
 * are no longer in active use.
 *
 * So when a refault distance of (R - E) is observed and there are at
 * least (R - E) active pages, the refaulting page is activated
 * optimistically in the hope that (R - E) active pages are actually
 * used less frequently than the refaulting page - or even not used at
 * all anymore.
 *
 * If this is wrong and demotion kicks in, the pages which are truly
 * used more frequently will be reactivated while the less frequently
 * used once will be evicted from memory.
 *
 * But if this is right, the stale pages will be pushed out of memory
 * and the used pages get to stay in cache.
 *
 *
 *		Implementation
 *
 * For each zone's file LRU lists, a counter for inactive evictions
 * and activations is maintained (zone->inactive_age).
 *
 * On eviction, a snapshot of this counter (along with some bits to
 * identify the zone) is stored in the now empty page cache radix tree
 * slot of the evicted page.  This is called a shadow entry.
 *
 * On cache misses for which there are shadow entries, an eligible
 * refault distance will immediately activate the refaulting page.
 */

static void *pack_shadow(unsigned long eviction, struct zone *zone)
{
	eviction = (eviction << NODES_SHIFT) | zone_to_nid(zone);
	eviction = (eviction << ZONES_SHIFT) | zone_idx(zone);
	eviction = (eviction << RADIX_TREE_EXCEPTIONAL_SHIFT);

	return (void *)(eviction | RADIX_TREE_EXCEPTIONAL_ENTRY);
}

static void unpack_shadow(void *shadow,
			  struct zone **zone,
			  unsigned long *distance)
{
	unsigned long entry = (unsigned long)shadow;
	unsigned long eviction;
	unsigned long refault;
	unsigned long mask;
	int zid, nid;

	entry >>= RADIX_TREE_EXCEPTIONAL_SHIFT;
	zid = entry & ((1UL << ZONES_SHIFT) - 1);
	entry >>= ZONES_SHIFT;
	nid = entry & ((1UL << NODES_SHIFT) - 1);
	entry >>= NODES_SHIFT;
	eviction = entry;

	*zone = NODE_DATA(nid)->node_zones + zid;

	refault = atomic_long_read(&(*zone)->inactive_age);
	mask = ~0UL >> (NODES_SHIFT + ZONES_SHIFT +
			RADIX_TREE_EXCEPTIONAL_SHIFT);
	/*
	 * The unsigned subtraction here gives an accurate distance
	 * across inactive_age overflows in most cases.
	 *
	 * There is a special case: usually, shadow entries have a
	 * short lifetime and are either refaulted or reclaimed along
	 * with the inode before they get too old.  But it is not
	 * impossible for the inactive_age to lap a shadow entry in
	 * the field, which can then can result in a false small
	 * refault distance, leading to a false activation should this
	 * old entry actually refault again.  However, earlier kernels
	 * used to deactivate unconditionally with *every* reclaim
	 * invocation for the longest time, so the occasional
	 * inappropriate activation leading to pressure on the active
	 * list is not a problem.
	 */
	*distance = (refault - eviction) & mask;
}

/**
 * workingset_eviction - note the eviction of a page from memory
 * @mapping: address space the page was backing
 * @page: the page being evicted
 *
 * Returns a shadow entry to be stored in @mapping->page_tree in place
 * of the evicted @page so that a later refault can be detected.
 */
void *workingset_eviction(struct address_space *mapping, struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long eviction;

	eviction = atomic_long_inc_return(&zone->inactive_age);
	return pack_shadow(eviction, zone);
}

/**
 * workingset_refault - evaluate the refault of a previously evicted page
 * @shadow: shadow entry of the evicted page
 *
 * Calculates and evaluates the refault distance of the previously
 * evicted page in the context of the zone it was allocated in.
 *
 * Returns %true if the page should be activated, %false otherwise.
 */
bool workingset_refault(void *shadow)
{
	unsigned long refault_distance;
	struct zone *zone;

	unpack_shadow(shadow, &zone, &refault_distance);
	inc_zone_state(zone, WORKINGSET_REFAULT);

	if (refault_distance <= zone_page_state(zone, NR_ACTIVE_FILE)) {
		inc_zone_state(zone, WORKINGSET_ACTIVATE);
		return true;
	}
	return false;
}

/**
 * workingset_activation - note a page activation
 * @page: page that is being activated
 */
void workingset_activation(struct page *page)
{
	atomic_long_inc(&page_zone(page)->inactive_age);
}

/*
 * Shadow entries reflect the share of the working set that does not
 * fit into memory, so their number depends on the access pattern of
 * the workload.  In most cases, they will refault or get reclaimed
 * along with the inode, but a (malicious) workload that streams
 * through files with a total size several times that of available
 * memory, while preventing the inodes from being reclaimed, can
 * create excessive amounts of shadow nodes.  To keep a lid on this,
 * track shadow nodes and reclaim them when they grow way past the
 * point where they would still be useful.
 */

struct list_lru workingset_shadow_nodes;

static unsigned long count_shadow_nodes(struct shrinker *shrinker,
					struct shrink_control *sc)
{
	unsigned long shadow_nodes;
	unsigned long max_nodes;
	unsigned long pages;

	/* list_lru lock nests inside IRQ-safe mapping->tree_lock */
	local_irq_disable();
	shadow_nodes = list_lru_shrink_count(&workingset_shadow_nodes, sc);
	local_irq_enable();

	pages = node_present_pages(sc->nid);
	/*
	 * Active cache pages are limited to 50% of memory, and shadow
	 * entries that represent a refault distance bigger than that
	 * do not have any effect.  Limit the number of shadow nodes
	 * such that shadow entries do not exceed the number of active
	 * cache pages, assuming a worst-case node population density
	 * of 1/8th on average.
	 *
	 * On 64-bit with 7 radix_tree_nodes per page and 64 slots
	 * each, this will reclaim shadow entries when they consume
	 * ~2% of available memory:
	 *
	 * PAGE_SIZE / radix_tree_nodes / node_entries / PAGE_SIZE
	 */
	max_nodes = pages >> (1 + RADIX_TREE_MAP_SHIFT - 3);

	if (shadow_nodes <= max_nodes)
		return 0;

	return shadow_nodes - max_nodes;
}

static enum lru_status shadow_lru_isolate(struct list_head *item,
					  spinlock_t *lru_lock,
					  void *arg)
{
	struct address_space *mapping;
	struct radix_tree_node *node;
	unsigned int i;
	int ret;

	/*
	 * Page cache insertions and deletions synchroneously maintain
	 * the shadow node LRU under the mapping->tree_lock and the
	 * lru_lock.  Because the page cache tree is emptied before
	 * the inode can be destroyed, holding the lru_lock pins any
	 * address_space that has radix tree nodes on the LRU.
	 *
	 * We can then safely transition to the mapping->tree_lock to
	 * pin only the address_space of the particular node we want
	 * to reclaim, take the node off-LRU, and drop the lru_lock.
	 */

	node = container_of(item, struct radix_tree_node, private_list);
	mapping = node->private_data;

	/* Coming from the list, invert the lock order */
	if (!spin_trylock(&mapping->tree_lock)) {
		spin_unlock(lru_lock);
		ret = LRU_RETRY;
		goto out;
	}

	list_del_init(item);
	spin_unlock(lru_lock);

	/*
	 * The nodes should only contain one or more shadow entries,
	 * no pages, so we expect to be able to remove them all and
	 * delete and free the empty node afterwards.
	 */

	BUG_ON(!node->count);
	BUG_ON(node->count & RADIX_TREE_COUNT_MASK);

	for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
		if (node->slots[i]) {
			BUG_ON(!radix_tree_exceptional_entry(node->slots[i]));
			node->slots[i] = NULL;
			BUG_ON(node->count < (1U << RADIX_TREE_COUNT_SHIFT));
			node->count -= 1U << RADIX_TREE_COUNT_SHIFT;
			BUG_ON(!mapping->nrshadows);
			mapping->nrshadows--;
		}
	}
	BUG_ON(node->count);
	inc_zone_state(page_zone(virt_to_page(node)), WORKINGSET_NODERECLAIM);
	if (!__radix_tree_delete_node(&mapping->page_tree, node))
		BUG();

	spin_unlock(&mapping->tree_lock);
	ret = LRU_REMOVED_RETRY;
out:
	local_irq_enable();
	cond_resched();
	local_irq_disable();
	spin_lock(lru_lock);
	return ret;
}

static unsigned long scan_shadow_nodes(struct shrinker *shrinker,
				       struct shrink_control *sc)
{
	unsigned long ret;

	/* list_lru lock nests inside IRQ-safe mapping->tree_lock */
	local_irq_disable();
	ret =  list_lru_shrink_walk(&workingset_shadow_nodes, sc,
				    shadow_lru_isolate, NULL);
	local_irq_enable();
	return ret;
}

static struct shrinker workingset_shadow_shrinker = {
	.count_objects = count_shadow_nodes,
	.scan_objects = scan_shadow_nodes,
	.seeks = DEFAULT_SEEKS,
	.flags = SHRINKER_NUMA_AWARE,
};

/*
 * Our list_lru->lock is IRQ-safe as it nests inside the IRQ-safe
 * mapping->tree_lock.
 */
static struct lock_class_key shadow_nodes_key;

static int __init workingset_init(void)
{
	int ret;

	ret = list_lru_init_key(&workingset_shadow_nodes, &shadow_nodes_key);
	if (ret)
		goto err;
	ret = register_shrinker(&workingset_shadow_shrinker);
	if (ret)
		goto err_list_lru;
	return 0;
err_list_lru:
	list_lru_destroy(&workingset_shadow_nodes);
err:
	return ret;
}
module_init(workingset_init);
