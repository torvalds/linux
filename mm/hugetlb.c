/*
 * Generic hugetlb support.
 * (C) William Irwin, April 2004
 */
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/nodemask.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/cpuset.h>
#include <linux/mutex.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/hugetlb.h>
#include "internal.h"

const unsigned long hugetlb_zero = 0, hugetlb_infinity = ~0UL;
static unsigned long nr_huge_pages, free_huge_pages, resv_huge_pages;
static unsigned long surplus_huge_pages;
static unsigned long nr_overcommit_huge_pages;
unsigned long max_huge_pages;
unsigned long sysctl_overcommit_huge_pages;
static struct list_head hugepage_freelists[MAX_NUMNODES];
static unsigned int nr_huge_pages_node[MAX_NUMNODES];
static unsigned int free_huge_pages_node[MAX_NUMNODES];
static unsigned int surplus_huge_pages_node[MAX_NUMNODES];
static gfp_t htlb_alloc_mask = GFP_HIGHUSER;
unsigned long hugepages_treat_as_movable;
static int hugetlb_next_nid;

/*
 * Protects updates to hugepage_freelists, nr_huge_pages, and free_huge_pages
 */
static DEFINE_SPINLOCK(hugetlb_lock);

/*
 * Convert the address within this vma to the page offset within
 * the mapping, in base page units.
 */
static pgoff_t vma_page_offset(struct vm_area_struct *vma,
				unsigned long address)
{
	return ((address - vma->vm_start) >> PAGE_SHIFT) +
					(vma->vm_pgoff >> PAGE_SHIFT);
}

/*
 * Convert the address within this vma to the page offset within
 * the mapping, in pagecache page units; huge pages here.
 */
static pgoff_t vma_pagecache_offset(struct vm_area_struct *vma,
					unsigned long address)
{
	return ((address - vma->vm_start) >> HPAGE_SHIFT) +
			(vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));
}

#define HPAGE_RESV_OWNER    (1UL << (BITS_PER_LONG - 1))
#define HPAGE_RESV_UNMAPPED (1UL << (BITS_PER_LONG - 2))
#define HPAGE_RESV_MASK (HPAGE_RESV_OWNER | HPAGE_RESV_UNMAPPED)
/*
 * These helpers are used to track how many pages are reserved for
 * faults in a MAP_PRIVATE mapping. Only the process that called mmap()
 * is guaranteed to have their future faults succeed.
 *
 * With the exception of reset_vma_resv_huge_pages() which is called at fork(),
 * the reserve counters are updated with the hugetlb_lock held. It is safe
 * to reset the VMA at fork() time as it is not in use yet and there is no
 * chance of the global counters getting corrupted as a result of the values.
 */
static unsigned long get_vma_private_data(struct vm_area_struct *vma)
{
	return (unsigned long)vma->vm_private_data;
}

static void set_vma_private_data(struct vm_area_struct *vma,
							unsigned long value)
{
	vma->vm_private_data = (void *)value;
}

static unsigned long vma_resv_huge_pages(struct vm_area_struct *vma)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	if (!(vma->vm_flags & VM_SHARED))
		return get_vma_private_data(vma) & ~HPAGE_RESV_MASK;
	return 0;
}

static void set_vma_resv_huge_pages(struct vm_area_struct *vma,
							unsigned long reserve)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	VM_BUG_ON(vma->vm_flags & VM_SHARED);

	set_vma_private_data(vma,
		(get_vma_private_data(vma) & HPAGE_RESV_MASK) | reserve);
}

static void set_vma_resv_flags(struct vm_area_struct *vma, unsigned long flags)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	VM_BUG_ON(vma->vm_flags & VM_SHARED);

	set_vma_private_data(vma, get_vma_private_data(vma) | flags);
}

static int is_vma_resv_set(struct vm_area_struct *vma, unsigned long flag)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));

	return (get_vma_private_data(vma) & flag) != 0;
}

/* Decrement the reserved pages in the hugepage pool by one */
static void decrement_hugepage_resv_vma(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHARED) {
		/* Shared mappings always use reserves */
		resv_huge_pages--;
	} else {
		/*
		 * Only the process that called mmap() has reserves for
		 * private mappings.
		 */
		if (is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
			unsigned long flags, reserve;
			resv_huge_pages--;
			flags = (unsigned long)vma->vm_private_data &
							HPAGE_RESV_MASK;
			reserve = (unsigned long)vma->vm_private_data - 1;
			vma->vm_private_data = (void *)(reserve | flags);
		}
	}
}

/* Reset counters to 0 and clear all HPAGE_RESV_* flags */
void reset_vma_resv_huge_pages(struct vm_area_struct *vma)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	if (!(vma->vm_flags & VM_SHARED))
		vma->vm_private_data = (void *)0;
}

/* Returns true if the VMA has associated reserve pages */
static int vma_has_private_reserves(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHARED)
		return 0;
	if (!vma_resv_huge_pages(vma))
		return 0;
	return 1;
}

static void clear_huge_page(struct page *page, unsigned long addr)
{
	int i;

	might_sleep();
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); i++) {
		cond_resched();
		clear_user_highpage(page + i, addr + i * PAGE_SIZE);
	}
}

static void copy_huge_page(struct page *dst, struct page *src,
			   unsigned long addr, struct vm_area_struct *vma)
{
	int i;

	might_sleep();
	for (i = 0; i < HPAGE_SIZE/PAGE_SIZE; i++) {
		cond_resched();
		copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE, vma);
	}
}

static void enqueue_huge_page(struct page *page)
{
	int nid = page_to_nid(page);
	list_add(&page->lru, &hugepage_freelists[nid]);
	free_huge_pages++;
	free_huge_pages_node[nid]++;
}

static struct page *dequeue_huge_page(void)
{
	int nid;
	struct page *page = NULL;

	for (nid = 0; nid < MAX_NUMNODES; ++nid) {
		if (!list_empty(&hugepage_freelists[nid])) {
			page = list_entry(hugepage_freelists[nid].next,
					  struct page, lru);
			list_del(&page->lru);
			free_huge_pages--;
			free_huge_pages_node[nid]--;
			break;
		}
	}
	return page;
}

static struct page *dequeue_huge_page_vma(struct vm_area_struct *vma,
				unsigned long address, int avoid_reserve)
{
	int nid;
	struct page *page = NULL;
	struct mempolicy *mpol;
	nodemask_t *nodemask;
	struct zonelist *zonelist = huge_zonelist(vma, address,
					htlb_alloc_mask, &mpol, &nodemask);
	struct zone *zone;
	struct zoneref *z;

	/*
	 * A child process with MAP_PRIVATE mappings created by their parent
	 * have no page reserves. This check ensures that reservations are
	 * not "stolen". The child may still get SIGKILLed
	 */
	if (!vma_has_private_reserves(vma) &&
			free_huge_pages - resv_huge_pages == 0)
		return NULL;

	/* If reserves cannot be used, ensure enough pages are in the pool */
	if (avoid_reserve && free_huge_pages - resv_huge_pages == 0)
		return NULL;

	for_each_zone_zonelist_nodemask(zone, z, zonelist,
						MAX_NR_ZONES - 1, nodemask) {
		nid = zone_to_nid(zone);
		if (cpuset_zone_allowed_softwall(zone, htlb_alloc_mask) &&
		    !list_empty(&hugepage_freelists[nid])) {
			page = list_entry(hugepage_freelists[nid].next,
					  struct page, lru);
			list_del(&page->lru);
			free_huge_pages--;
			free_huge_pages_node[nid]--;

			if (!avoid_reserve)
				decrement_hugepage_resv_vma(vma);

			break;
		}
	}
	mpol_cond_put(mpol);
	return page;
}

static void update_and_free_page(struct page *page)
{
	int i;
	nr_huge_pages--;
	nr_huge_pages_node[page_to_nid(page)]--;
	for (i = 0; i < (HPAGE_SIZE / PAGE_SIZE); i++) {
		page[i].flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
				1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback);
	}
	set_compound_page_dtor(page, NULL);
	set_page_refcounted(page);
	arch_release_hugepage(page);
	__free_pages(page, HUGETLB_PAGE_ORDER);
}

static void free_huge_page(struct page *page)
{
	int nid = page_to_nid(page);
	struct address_space *mapping;

	mapping = (struct address_space *) page_private(page);
	set_page_private(page, 0);
	BUG_ON(page_count(page));
	INIT_LIST_HEAD(&page->lru);

	spin_lock(&hugetlb_lock);
	if (surplus_huge_pages_node[nid]) {
		update_and_free_page(page);
		surplus_huge_pages--;
		surplus_huge_pages_node[nid]--;
	} else {
		enqueue_huge_page(page);
	}
	spin_unlock(&hugetlb_lock);
	if (mapping)
		hugetlb_put_quota(mapping, 1);
}

/*
 * Increment or decrement surplus_huge_pages.  Keep node-specific counters
 * balanced by operating on them in a round-robin fashion.
 * Returns 1 if an adjustment was made.
 */
static int adjust_pool_surplus(int delta)
{
	static int prev_nid;
	int nid = prev_nid;
	int ret = 0;

	VM_BUG_ON(delta != -1 && delta != 1);
	do {
		nid = next_node(nid, node_online_map);
		if (nid == MAX_NUMNODES)
			nid = first_node(node_online_map);

		/* To shrink on this node, there must be a surplus page */
		if (delta < 0 && !surplus_huge_pages_node[nid])
			continue;
		/* Surplus cannot exceed the total number of pages */
		if (delta > 0 && surplus_huge_pages_node[nid] >=
						nr_huge_pages_node[nid])
			continue;

		surplus_huge_pages += delta;
		surplus_huge_pages_node[nid] += delta;
		ret = 1;
		break;
	} while (nid != prev_nid);

	prev_nid = nid;
	return ret;
}

static struct page *alloc_fresh_huge_page_node(int nid)
{
	struct page *page;

	page = alloc_pages_node(nid,
		htlb_alloc_mask|__GFP_COMP|__GFP_THISNODE|
						__GFP_REPEAT|__GFP_NOWARN,
		HUGETLB_PAGE_ORDER);
	if (page) {
		if (arch_prepare_hugepage(page)) {
			__free_pages(page, HUGETLB_PAGE_ORDER);
			return NULL;
		}
		set_compound_page_dtor(page, free_huge_page);
		spin_lock(&hugetlb_lock);
		nr_huge_pages++;
		nr_huge_pages_node[nid]++;
		spin_unlock(&hugetlb_lock);
		put_page(page); /* free it into the hugepage allocator */
	}

	return page;
}

static int alloc_fresh_huge_page(void)
{
	struct page *page;
	int start_nid;
	int next_nid;
	int ret = 0;

	start_nid = hugetlb_next_nid;

	do {
		page = alloc_fresh_huge_page_node(hugetlb_next_nid);
		if (page)
			ret = 1;
		/*
		 * Use a helper variable to find the next node and then
		 * copy it back to hugetlb_next_nid afterwards:
		 * otherwise there's a window in which a racer might
		 * pass invalid nid MAX_NUMNODES to alloc_pages_node.
		 * But we don't need to use a spin_lock here: it really
		 * doesn't matter if occasionally a racer chooses the
		 * same nid as we do.  Move nid forward in the mask even
		 * if we just successfully allocated a hugepage so that
		 * the next caller gets hugepages on the next node.
		 */
		next_nid = next_node(hugetlb_next_nid, node_online_map);
		if (next_nid == MAX_NUMNODES)
			next_nid = first_node(node_online_map);
		hugetlb_next_nid = next_nid;
	} while (!page && hugetlb_next_nid != start_nid);

	if (ret)
		count_vm_event(HTLB_BUDDY_PGALLOC);
	else
		count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);

	return ret;
}

static struct page *alloc_buddy_huge_page(struct vm_area_struct *vma,
						unsigned long address)
{
	struct page *page;
	unsigned int nid;

	/*
	 * Assume we will successfully allocate the surplus page to
	 * prevent racing processes from causing the surplus to exceed
	 * overcommit
	 *
	 * This however introduces a different race, where a process B
	 * tries to grow the static hugepage pool while alloc_pages() is
	 * called by process A. B will only examine the per-node
	 * counters in determining if surplus huge pages can be
	 * converted to normal huge pages in adjust_pool_surplus(). A
	 * won't be able to increment the per-node counter, until the
	 * lock is dropped by B, but B doesn't drop hugetlb_lock until
	 * no more huge pages can be converted from surplus to normal
	 * state (and doesn't try to convert again). Thus, we have a
	 * case where a surplus huge page exists, the pool is grown, and
	 * the surplus huge page still exists after, even though it
	 * should just have been converted to a normal huge page. This
	 * does not leak memory, though, as the hugepage will be freed
	 * once it is out of use. It also does not allow the counters to
	 * go out of whack in adjust_pool_surplus() as we don't modify
	 * the node values until we've gotten the hugepage and only the
	 * per-node value is checked there.
	 */
	spin_lock(&hugetlb_lock);
	if (surplus_huge_pages >= nr_overcommit_huge_pages) {
		spin_unlock(&hugetlb_lock);
		return NULL;
	} else {
		nr_huge_pages++;
		surplus_huge_pages++;
	}
	spin_unlock(&hugetlb_lock);

	page = alloc_pages(htlb_alloc_mask|__GFP_COMP|
					__GFP_REPEAT|__GFP_NOWARN,
					HUGETLB_PAGE_ORDER);

	spin_lock(&hugetlb_lock);
	if (page) {
		/*
		 * This page is now managed by the hugetlb allocator and has
		 * no users -- drop the buddy allocator's reference.
		 */
		put_page_testzero(page);
		VM_BUG_ON(page_count(page));
		nid = page_to_nid(page);
		set_compound_page_dtor(page, free_huge_page);
		/*
		 * We incremented the global counters already
		 */
		nr_huge_pages_node[nid]++;
		surplus_huge_pages_node[nid]++;
		__count_vm_event(HTLB_BUDDY_PGALLOC);
	} else {
		nr_huge_pages--;
		surplus_huge_pages--;
		__count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);
	}
	spin_unlock(&hugetlb_lock);

	return page;
}

/*
 * Increase the hugetlb pool such that it can accomodate a reservation
 * of size 'delta'.
 */
static int gather_surplus_pages(int delta)
{
	struct list_head surplus_list;
	struct page *page, *tmp;
	int ret, i;
	int needed, allocated;

	needed = (resv_huge_pages + delta) - free_huge_pages;
	if (needed <= 0) {
		resv_huge_pages += delta;
		return 0;
	}

	allocated = 0;
	INIT_LIST_HEAD(&surplus_list);

	ret = -ENOMEM;
retry:
	spin_unlock(&hugetlb_lock);
	for (i = 0; i < needed; i++) {
		page = alloc_buddy_huge_page(NULL, 0);
		if (!page) {
			/*
			 * We were not able to allocate enough pages to
			 * satisfy the entire reservation so we free what
			 * we've allocated so far.
			 */
			spin_lock(&hugetlb_lock);
			needed = 0;
			goto free;
		}

		list_add(&page->lru, &surplus_list);
	}
	allocated += needed;

	/*
	 * After retaking hugetlb_lock, we need to recalculate 'needed'
	 * because either resv_huge_pages or free_huge_pages may have changed.
	 */
	spin_lock(&hugetlb_lock);
	needed = (resv_huge_pages + delta) - (free_huge_pages + allocated);
	if (needed > 0)
		goto retry;

	/*
	 * The surplus_list now contains _at_least_ the number of extra pages
	 * needed to accomodate the reservation.  Add the appropriate number
	 * of pages to the hugetlb pool and free the extras back to the buddy
	 * allocator.  Commit the entire reservation here to prevent another
	 * process from stealing the pages as they are added to the pool but
	 * before they are reserved.
	 */
	needed += allocated;
	resv_huge_pages += delta;
	ret = 0;
free:
	/* Free the needed pages to the hugetlb pool */
	list_for_each_entry_safe(page, tmp, &surplus_list, lru) {
		if ((--needed) < 0)
			break;
		list_del(&page->lru);
		enqueue_huge_page(page);
	}

	/* Free unnecessary surplus pages to the buddy allocator */
	if (!list_empty(&surplus_list)) {
		spin_unlock(&hugetlb_lock);
		list_for_each_entry_safe(page, tmp, &surplus_list, lru) {
			list_del(&page->lru);
			/*
			 * The page has a reference count of zero already, so
			 * call free_huge_page directly instead of using
			 * put_page.  This must be done with hugetlb_lock
			 * unlocked which is safe because free_huge_page takes
			 * hugetlb_lock before deciding how to free the page.
			 */
			free_huge_page(page);
		}
		spin_lock(&hugetlb_lock);
	}

	return ret;
}

/*
 * When releasing a hugetlb pool reservation, any surplus pages that were
 * allocated to satisfy the reservation must be explicitly freed if they were
 * never used.
 */
static void return_unused_surplus_pages(unsigned long unused_resv_pages)
{
	static int nid = -1;
	struct page *page;
	unsigned long nr_pages;

	/*
	 * We want to release as many surplus pages as possible, spread
	 * evenly across all nodes. Iterate across all nodes until we
	 * can no longer free unreserved surplus pages. This occurs when
	 * the nodes with surplus pages have no free pages.
	 */
	unsigned long remaining_iterations = num_online_nodes();

	/* Uncommit the reservation */
	resv_huge_pages -= unused_resv_pages;

	nr_pages = min(unused_resv_pages, surplus_huge_pages);

	while (remaining_iterations-- && nr_pages) {
		nid = next_node(nid, node_online_map);
		if (nid == MAX_NUMNODES)
			nid = first_node(node_online_map);

		if (!surplus_huge_pages_node[nid])
			continue;

		if (!list_empty(&hugepage_freelists[nid])) {
			page = list_entry(hugepage_freelists[nid].next,
					  struct page, lru);
			list_del(&page->lru);
			update_and_free_page(page);
			free_huge_pages--;
			free_huge_pages_node[nid]--;
			surplus_huge_pages--;
			surplus_huge_pages_node[nid]--;
			nr_pages--;
			remaining_iterations = num_online_nodes();
		}
	}
}

static struct page *alloc_huge_page(struct vm_area_struct *vma,
				    unsigned long addr, int avoid_reserve)
{
	struct page *page;
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned int chg = 0;

	/*
	 * Processes that did not create the mapping will have no reserves and
	 * will not have accounted against quota. Check that the quota can be
	 * made before satisfying the allocation
	 */
	if (!(vma->vm_flags & VM_SHARED) &&
			!is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		chg = 1;
		if (hugetlb_get_quota(inode->i_mapping, chg))
			return ERR_PTR(-ENOSPC);
	}

	spin_lock(&hugetlb_lock);
	page = dequeue_huge_page_vma(vma, addr, avoid_reserve);
	spin_unlock(&hugetlb_lock);

	if (!page) {
		page = alloc_buddy_huge_page(vma, addr);
		if (!page) {
			hugetlb_put_quota(inode->i_mapping, chg);
			return ERR_PTR(-VM_FAULT_OOM);
		}
	}

	set_page_refcounted(page);
	set_page_private(page, (unsigned long) mapping);

	return page;
}

static int __init hugetlb_init(void)
{
	unsigned long i;

	if (HPAGE_SHIFT == 0)
		return 0;

	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&hugepage_freelists[i]);

	hugetlb_next_nid = first_node(node_online_map);

	for (i = 0; i < max_huge_pages; ++i) {
		if (!alloc_fresh_huge_page())
			break;
	}
	max_huge_pages = free_huge_pages = nr_huge_pages = i;
	printk("Total HugeTLB memory allocated, %ld\n", free_huge_pages);
	return 0;
}
module_init(hugetlb_init);

static int __init hugetlb_setup(char *s)
{
	if (sscanf(s, "%lu", &max_huge_pages) <= 0)
		max_huge_pages = 0;
	return 1;
}
__setup("hugepages=", hugetlb_setup);

static unsigned int cpuset_mems_nr(unsigned int *array)
{
	int node;
	unsigned int nr = 0;

	for_each_node_mask(node, cpuset_current_mems_allowed)
		nr += array[node];

	return nr;
}

#ifdef CONFIG_SYSCTL
#ifdef CONFIG_HIGHMEM
static void try_to_free_low(unsigned long count)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; ++i) {
		struct page *page, *next;
		list_for_each_entry_safe(page, next, &hugepage_freelists[i], lru) {
			if (count >= nr_huge_pages)
				return;
			if (PageHighMem(page))
				continue;
			list_del(&page->lru);
			update_and_free_page(page);
			free_huge_pages--;
			free_huge_pages_node[page_to_nid(page)]--;
		}
	}
}
#else
static inline void try_to_free_low(unsigned long count)
{
}
#endif

#define persistent_huge_pages (nr_huge_pages - surplus_huge_pages)
static unsigned long set_max_huge_pages(unsigned long count)
{
	unsigned long min_count, ret;

	/*
	 * Increase the pool size
	 * First take pages out of surplus state.  Then make up the
	 * remaining difference by allocating fresh huge pages.
	 *
	 * We might race with alloc_buddy_huge_page() here and be unable
	 * to convert a surplus huge page to a normal huge page. That is
	 * not critical, though, it just means the overall size of the
	 * pool might be one hugepage larger than it needs to be, but
	 * within all the constraints specified by the sysctls.
	 */
	spin_lock(&hugetlb_lock);
	while (surplus_huge_pages && count > persistent_huge_pages) {
		if (!adjust_pool_surplus(-1))
			break;
	}

	while (count > persistent_huge_pages) {
		/*
		 * If this allocation races such that we no longer need the
		 * page, free_huge_page will handle it by freeing the page
		 * and reducing the surplus.
		 */
		spin_unlock(&hugetlb_lock);
		ret = alloc_fresh_huge_page();
		spin_lock(&hugetlb_lock);
		if (!ret)
			goto out;

	}

	/*
	 * Decrease the pool size
	 * First return free pages to the buddy allocator (being careful
	 * to keep enough around to satisfy reservations).  Then place
	 * pages into surplus state as needed so the pool will shrink
	 * to the desired size as pages become free.
	 *
	 * By placing pages into the surplus state independent of the
	 * overcommit value, we are allowing the surplus pool size to
	 * exceed overcommit. There are few sane options here. Since
	 * alloc_buddy_huge_page() is checking the global counter,
	 * though, we'll note that we're not allowed to exceed surplus
	 * and won't grow the pool anywhere else. Not until one of the
	 * sysctls are changed, or the surplus pages go out of use.
	 */
	min_count = resv_huge_pages + nr_huge_pages - free_huge_pages;
	min_count = max(count, min_count);
	try_to_free_low(min_count);
	while (min_count < persistent_huge_pages) {
		struct page *page = dequeue_huge_page();
		if (!page)
			break;
		update_and_free_page(page);
	}
	while (count < persistent_huge_pages) {
		if (!adjust_pool_surplus(1))
			break;
	}
out:
	ret = persistent_huge_pages;
	spin_unlock(&hugetlb_lock);
	return ret;
}

int hugetlb_sysctl_handler(struct ctl_table *table, int write,
			   struct file *file, void __user *buffer,
			   size_t *length, loff_t *ppos)
{
	proc_doulongvec_minmax(table, write, file, buffer, length, ppos);
	max_huge_pages = set_max_huge_pages(max_huge_pages);
	return 0;
}

int hugetlb_treat_movable_handler(struct ctl_table *table, int write,
			struct file *file, void __user *buffer,
			size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, file, buffer, length, ppos);
	if (hugepages_treat_as_movable)
		htlb_alloc_mask = GFP_HIGHUSER_MOVABLE;
	else
		htlb_alloc_mask = GFP_HIGHUSER;
	return 0;
}

int hugetlb_overcommit_handler(struct ctl_table *table, int write,
			struct file *file, void __user *buffer,
			size_t *length, loff_t *ppos)
{
	proc_doulongvec_minmax(table, write, file, buffer, length, ppos);
	spin_lock(&hugetlb_lock);
	nr_overcommit_huge_pages = sysctl_overcommit_huge_pages;
	spin_unlock(&hugetlb_lock);
	return 0;
}

#endif /* CONFIG_SYSCTL */

int hugetlb_report_meminfo(char *buf)
{
	return sprintf(buf,
			"HugePages_Total: %5lu\n"
			"HugePages_Free:  %5lu\n"
			"HugePages_Rsvd:  %5lu\n"
			"HugePages_Surp:  %5lu\n"
			"Hugepagesize:    %5lu kB\n",
			nr_huge_pages,
			free_huge_pages,
			resv_huge_pages,
			surplus_huge_pages,
			HPAGE_SIZE/1024);
}

int hugetlb_report_node_meminfo(int nid, char *buf)
{
	return sprintf(buf,
		"Node %d HugePages_Total: %5u\n"
		"Node %d HugePages_Free:  %5u\n"
		"Node %d HugePages_Surp:  %5u\n",
		nid, nr_huge_pages_node[nid],
		nid, free_huge_pages_node[nid],
		nid, surplus_huge_pages_node[nid]);
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	return nr_huge_pages * (HPAGE_SIZE / PAGE_SIZE);
}

static int hugetlb_acct_memory(long delta)
{
	int ret = -ENOMEM;

	spin_lock(&hugetlb_lock);
	/*
	 * When cpuset is configured, it breaks the strict hugetlb page
	 * reservation as the accounting is done on a global variable. Such
	 * reservation is completely rubbish in the presence of cpuset because
	 * the reservation is not checked against page availability for the
	 * current cpuset. Application can still potentially OOM'ed by kernel
	 * with lack of free htlb page in cpuset that the task is in.
	 * Attempt to enforce strict accounting with cpuset is almost
	 * impossible (or too ugly) because cpuset is too fluid that
	 * task or memory node can be dynamically moved between cpusets.
	 *
	 * The change of semantics for shared hugetlb mapping with cpuset is
	 * undesirable. However, in order to preserve some of the semantics,
	 * we fall back to check against current free page availability as
	 * a best attempt and hopefully to minimize the impact of changing
	 * semantics that cpuset has.
	 */
	if (delta > 0) {
		if (gather_surplus_pages(delta) < 0)
			goto out;

		if (delta > cpuset_mems_nr(free_huge_pages_node)) {
			return_unused_surplus_pages(delta);
			goto out;
		}
	}

	ret = 0;
	if (delta < 0)
		return_unused_surplus_pages((unsigned long) -delta);

out:
	spin_unlock(&hugetlb_lock);
	return ret;
}

static void hugetlb_vm_op_close(struct vm_area_struct *vma)
{
	unsigned long reserve = vma_resv_huge_pages(vma);
	if (reserve)
		hugetlb_acct_memory(-reserve);
}

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static int hugetlb_vm_op_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	BUG();
	return 0;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.fault = hugetlb_vm_op_fault,
	.close = hugetlb_vm_op_close,
};

static pte_t make_huge_pte(struct vm_area_struct *vma, struct page *page,
				int writable)
{
	pte_t entry;

	if (writable) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else {
		entry = huge_pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = pte_mkhuge(entry);

	return entry;
}

static void set_huge_ptep_writable(struct vm_area_struct *vma,
				   unsigned long address, pte_t *ptep)
{
	pte_t entry;

	entry = pte_mkwrite(pte_mkdirty(huge_ptep_get(ptep)));
	if (huge_ptep_set_access_flags(vma, address, ptep, entry, 1)) {
		update_mmu_cache(vma, address, entry);
	}
}


int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			    struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr;
	int cow;

	cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;

	for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_SIZE) {
		src_pte = huge_pte_offset(src, addr);
		if (!src_pte)
			continue;
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto nomem;

		/* If the pagetables are shared don't copy or take references */
		if (dst_pte == src_pte)
			continue;

		spin_lock(&dst->page_table_lock);
		spin_lock_nested(&src->page_table_lock, SINGLE_DEPTH_NESTING);
		if (!huge_pte_none(huge_ptep_get(src_pte))) {
			if (cow)
				huge_ptep_set_wrprotect(src, addr, src_pte);
			entry = huge_ptep_get(src_pte);
			ptepage = pte_page(entry);
			get_page(ptepage);
			set_huge_pte_at(dst, addr, dst_pte, entry);
		}
		spin_unlock(&src->page_table_lock);
		spin_unlock(&dst->page_table_lock);
	}
	return 0;

nomem:
	return -ENOMEM;
}

void __unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end, struct page *ref_page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *ptep;
	pte_t pte;
	struct page *page;
	struct page *tmp;
	/*
	 * A page gathering list, protected by per file i_mmap_lock. The
	 * lock is used to avoid list corruption from multiple unmapping
	 * of the same page since we are using page->lru.
	 */
	LIST_HEAD(page_list);

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON(start & ~HPAGE_MASK);
	BUG_ON(end & ~HPAGE_MASK);

	spin_lock(&mm->page_table_lock);
	for (address = start; address < end; address += HPAGE_SIZE) {
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;

		if (huge_pmd_unshare(mm, &address, ptep))
			continue;

		/*
		 * If a reference page is supplied, it is because a specific
		 * page is being unmapped, not a range. Ensure the page we
		 * are about to unmap is the actual page of interest.
		 */
		if (ref_page) {
			pte = huge_ptep_get(ptep);
			if (huge_pte_none(pte))
				continue;
			page = pte_page(pte);
			if (page != ref_page)
				continue;

			/*
			 * Mark the VMA as having unmapped its page so that
			 * future faults in this VMA will fail rather than
			 * looking like data was lost
			 */
			set_vma_resv_flags(vma, HPAGE_RESV_UNMAPPED);
		}

		pte = huge_ptep_get_and_clear(mm, address, ptep);
		if (huge_pte_none(pte))
			continue;

		page = pte_page(pte);
		if (pte_dirty(pte))
			set_page_dirty(page);
		list_add(&page->lru, &page_list);
	}
	spin_unlock(&mm->page_table_lock);
	flush_tlb_range(vma, start, end);
	list_for_each_entry_safe(page, tmp, &page_list, lru) {
		list_del(&page->lru);
		put_page(page);
	}
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end, struct page *ref_page)
{
	/*
	 * It is undesirable to test vma->vm_file as it should be non-null
	 * for valid hugetlb area. However, vm_file will be NULL in the error
	 * cleanup path of do_mmap_pgoff. When hugetlbfs ->mmap method fails,
	 * do_mmap_pgoff() nullifies vma->vm_file before calling this function
	 * to clean up. Since no pte has actually been setup, it is safe to
	 * do nothing in this case.
	 */
	if (vma->vm_file) {
		spin_lock(&vma->vm_file->f_mapping->i_mmap_lock);
		__unmap_hugepage_range(vma, start, end, ref_page);
		spin_unlock(&vma->vm_file->f_mapping->i_mmap_lock);
	}
}

/*
 * This is called when the original mapper is failing to COW a MAP_PRIVATE
 * mappping it owns the reserve page for. The intention is to unmap the page
 * from other VMAs and let the children be SIGKILLed if they are faulting the
 * same region.
 */
int unmap_ref_private(struct mm_struct *mm,
					struct vm_area_struct *vma,
					struct page *page,
					unsigned long address)
{
	struct vm_area_struct *iter_vma;
	struct address_space *mapping;
	struct prio_tree_iter iter;
	pgoff_t pgoff;

	/*
	 * vm_pgoff is in PAGE_SIZE units, hence the different calculation
	 * from page cache lookup which is in HPAGE_SIZE units.
	 */
	address = address & huge_page_mask(hstate_vma(vma));
	pgoff = ((address - vma->vm_start) >> PAGE_SHIFT)
		+ (vma->vm_pgoff >> PAGE_SHIFT);
	mapping = (struct address_space *)page_private(page);

	vma_prio_tree_foreach(iter_vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		/* Do not unmap the current VMA */
		if (iter_vma == vma)
			continue;

		/*
		 * Unmap the page from other VMAs without their own reserves.
		 * They get marked to be SIGKILLed if they fault in these
		 * areas. This is because a future no-page fault on this VMA
		 * could insert a zeroed page instead of the data existing
		 * from the time of fork. This would look like data corruption
		 */
		if (!is_vma_resv_set(iter_vma, HPAGE_RESV_OWNER))
			unmap_hugepage_range(iter_vma,
				address, address + HPAGE_SIZE,
				page);
	}

	return 1;
}

static int hugetlb_cow(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, pte_t pte,
			struct page *pagecache_page)
{
	struct page *old_page, *new_page;
	int avoidcopy;
	int outside_reserve = 0;

	old_page = pte_page(pte);

retry_avoidcopy:
	/* If no-one else is actually using this page, avoid the copy
	 * and just make the page writable */
	avoidcopy = (page_count(old_page) == 1);
	if (avoidcopy) {
		set_huge_ptep_writable(vma, address, ptep);
		return 0;
	}

	/*
	 * If the process that created a MAP_PRIVATE mapping is about to
	 * perform a COW due to a shared page count, attempt to satisfy
	 * the allocation without using the existing reserves. The pagecache
	 * page is used to determine if the reserve at this address was
	 * consumed or not. If reserves were used, a partial faulted mapping
	 * at the time of fork() could consume its reserves on COW instead
	 * of the full address range.
	 */
	if (!(vma->vm_flags & VM_SHARED) &&
			is_vma_resv_set(vma, HPAGE_RESV_OWNER) &&
			old_page != pagecache_page)
		outside_reserve = 1;

	page_cache_get(old_page);
	new_page = alloc_huge_page(vma, address, outside_reserve);

	if (IS_ERR(new_page)) {
		page_cache_release(old_page);

		/*
		 * If a process owning a MAP_PRIVATE mapping fails to COW,
		 * it is due to references held by a child and an insufficient
		 * huge page pool. To guarantee the original mappers
		 * reliability, unmap the page from child processes. The child
		 * may get SIGKILLed if it later faults.
		 */
		if (outside_reserve) {
			BUG_ON(huge_pte_none(pte));
			if (unmap_ref_private(mm, vma, old_page, address)) {
				BUG_ON(page_count(old_page) != 1);
				BUG_ON(huge_pte_none(pte));
				goto retry_avoidcopy;
			}
			WARN_ON_ONCE(1);
		}

		return -PTR_ERR(new_page);
	}

	spin_unlock(&mm->page_table_lock);
	copy_huge_page(new_page, old_page, address, vma);
	__SetPageUptodate(new_page);
	spin_lock(&mm->page_table_lock);

	ptep = huge_pte_offset(mm, address & HPAGE_MASK);
	if (likely(pte_same(huge_ptep_get(ptep), pte))) {
		/* Break COW */
		huge_ptep_clear_flush(vma, address, ptep);
		set_huge_pte_at(mm, address, ptep,
				make_huge_pte(vma, new_page, 1));
		/* Make the old page be freed below */
		new_page = old_page;
	}
	page_cache_release(new_page);
	page_cache_release(old_page);
	return 0;
}

/* Return the pagecache page at a given address within a VMA */
static struct page *hugetlbfs_pagecache_page(struct vm_area_struct *vma,
			unsigned long address)
{
	struct address_space *mapping;
	pgoff_t idx;

	mapping = vma->vm_file->f_mapping;
	idx = vma_pagecache_offset(vma, address);

	return find_lock_page(mapping, idx);
}

static int hugetlb_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, int write_access)
{
	int ret = VM_FAULT_SIGBUS;
	pgoff_t idx;
	unsigned long size;
	struct page *page;
	struct address_space *mapping;
	pte_t new_pte;

	/*
	 * Currently, we are forced to kill the process in the event the
	 * original mapper has unmapped pages from the child due to a failed
	 * COW. Warn that such a situation has occured as it may not be obvious
	 */
	if (is_vma_resv_set(vma, HPAGE_RESV_UNMAPPED)) {
		printk(KERN_WARNING
			"PID %d killed due to inadequate hugepage pool\n",
			current->pid);
		return ret;
	}

	mapping = vma->vm_file->f_mapping;
	idx = vma_pagecache_offset(vma, address);

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
retry:
	page = find_lock_page(mapping, idx);
	if (!page) {
		size = i_size_read(mapping->host) >> HPAGE_SHIFT;
		if (idx >= size)
			goto out;
		page = alloc_huge_page(vma, address, 0);
		if (IS_ERR(page)) {
			ret = -PTR_ERR(page);
			goto out;
		}
		clear_huge_page(page, address);
		__SetPageUptodate(page);

		if (vma->vm_flags & VM_SHARED) {
			int err;
			struct inode *inode = mapping->host;

			err = add_to_page_cache(page, mapping, idx, GFP_KERNEL);
			if (err) {
				put_page(page);
				if (err == -EEXIST)
					goto retry;
				goto out;
			}

			spin_lock(&inode->i_lock);
			inode->i_blocks += BLOCKS_PER_HUGEPAGE;
			spin_unlock(&inode->i_lock);
		} else
			lock_page(page);
	}

	spin_lock(&mm->page_table_lock);
	size = i_size_read(mapping->host) >> HPAGE_SHIFT;
	if (idx >= size)
		goto backout;

	ret = 0;
	if (!huge_pte_none(huge_ptep_get(ptep)))
		goto backout;

	new_pte = make_huge_pte(vma, page, ((vma->vm_flags & VM_WRITE)
				&& (vma->vm_flags & VM_SHARED)));
	set_huge_pte_at(mm, address, ptep, new_pte);

	if (write_access && !(vma->vm_flags & VM_SHARED)) {
		/* Optimization, do the COW without a second fault */
		ret = hugetlb_cow(mm, vma, address, ptep, new_pte, page);
	}

	spin_unlock(&mm->page_table_lock);
	unlock_page(page);
out:
	return ret;

backout:
	spin_unlock(&mm->page_table_lock);
	unlock_page(page);
	put_page(page);
	goto out;
}

int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, int write_access)
{
	pte_t *ptep;
	pte_t entry;
	int ret;
	static DEFINE_MUTEX(hugetlb_instantiation_mutex);

	ptep = huge_pte_alloc(mm, address);
	if (!ptep)
		return VM_FAULT_OOM;

	/*
	 * Serialize hugepage allocation and instantiation, so that we don't
	 * get spurious allocation failures if two CPUs race to instantiate
	 * the same page in the page cache.
	 */
	mutex_lock(&hugetlb_instantiation_mutex);
	entry = huge_ptep_get(ptep);
	if (huge_pte_none(entry)) {
		ret = hugetlb_no_page(mm, vma, address, ptep, write_access);
		mutex_unlock(&hugetlb_instantiation_mutex);
		return ret;
	}

	ret = 0;

	spin_lock(&mm->page_table_lock);
	/* Check for a racing update before calling hugetlb_cow */
	if (likely(pte_same(entry, huge_ptep_get(ptep))))
		if (write_access && !pte_write(entry)) {
			struct page *page;
			page = hugetlbfs_pagecache_page(vma, address);
			ret = hugetlb_cow(mm, vma, address, ptep, entry, page);
			if (page) {
				unlock_page(page);
				put_page(page);
			}
		}
	spin_unlock(&mm->page_table_lock);
	mutex_unlock(&hugetlb_instantiation_mutex);

	return ret;
}

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			struct page **pages, struct vm_area_struct **vmas,
			unsigned long *position, int *length, int i,
			int write)
{
	unsigned long pfn_offset;
	unsigned long vaddr = *position;
	int remainder = *length;

	spin_lock(&mm->page_table_lock);
	while (vaddr < vma->vm_end && remainder) {
		pte_t *pte;
		struct page *page;

		/*
		 * Some archs (sparc64, sh*) have multiple pte_ts to
		 * each hugepage.  We have to make * sure we get the
		 * first, for the page indexing below to work.
		 */
		pte = huge_pte_offset(mm, vaddr & HPAGE_MASK);

		if (!pte || huge_pte_none(huge_ptep_get(pte)) ||
		    (write && !pte_write(huge_ptep_get(pte)))) {
			int ret;

			spin_unlock(&mm->page_table_lock);
			ret = hugetlb_fault(mm, vma, vaddr, write);
			spin_lock(&mm->page_table_lock);
			if (!(ret & VM_FAULT_ERROR))
				continue;

			remainder = 0;
			if (!i)
				i = -EFAULT;
			break;
		}

		pfn_offset = (vaddr & ~HPAGE_MASK) >> PAGE_SHIFT;
		page = pte_page(huge_ptep_get(pte));
same_page:
		if (pages) {
			get_page(page);
			pages[i] = page + pfn_offset;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++pfn_offset;
		--remainder;
		++i;
		if (vaddr < vma->vm_end && remainder &&
				pfn_offset < HPAGE_SIZE/PAGE_SIZE) {
			/*
			 * We use pfn_offset to avoid touching the pageframes
			 * of this compound page.
			 */
			goto same_page;
		}
	}
	spin_unlock(&mm->page_table_lock);
	*length = remainder;
	*position = vaddr;

	return i;
}

void hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end, pgprot_t newprot)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long start = address;
	pte_t *ptep;
	pte_t pte;

	BUG_ON(address >= end);
	flush_cache_range(vma, address, end);

	spin_lock(&vma->vm_file->f_mapping->i_mmap_lock);
	spin_lock(&mm->page_table_lock);
	for (; address < end; address += HPAGE_SIZE) {
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;
		if (huge_pmd_unshare(mm, &address, ptep))
			continue;
		if (!huge_pte_none(huge_ptep_get(ptep))) {
			pte = huge_ptep_get_and_clear(mm, address, ptep);
			pte = pte_mkhuge(pte_modify(pte, newprot));
			set_huge_pte_at(mm, address, ptep, pte);
		}
	}
	spin_unlock(&mm->page_table_lock);
	spin_unlock(&vma->vm_file->f_mapping->i_mmap_lock);

	flush_tlb_range(vma, start, end);
}

struct file_region {
	struct list_head link;
	long from;
	long to;
};

static long region_add(struct list_head *head, long f, long t)
{
	struct file_region *rg, *nrg, *trg;

	/* Locate the region we are either in or before. */
	list_for_each_entry(rg, head, link)
		if (f <= rg->to)
			break;

	/* Round our left edge to the current segment if it encloses us. */
	if (f > rg->from)
		f = rg->from;

	/* Check for and consume any regions we now overlap with. */
	nrg = rg;
	list_for_each_entry_safe(rg, trg, rg->link.prev, link) {
		if (&rg->link == head)
			break;
		if (rg->from > t)
			break;

		/* If this area reaches higher then extend our area to
		 * include it completely.  If this is not the first area
		 * which we intend to reuse, free it. */
		if (rg->to > t)
			t = rg->to;
		if (rg != nrg) {
			list_del(&rg->link);
			kfree(rg);
		}
	}
	nrg->from = f;
	nrg->to = t;
	return 0;
}

static long region_chg(struct list_head *head, long f, long t)
{
	struct file_region *rg, *nrg;
	long chg = 0;

	/* Locate the region we are before or in. */
	list_for_each_entry(rg, head, link)
		if (f <= rg->to)
			break;

	/* If we are below the current region then a new region is required.
	 * Subtle, allocate a new region at the position but make it zero
	 * size such that we can guarantee to record the reservation. */
	if (&rg->link == head || t < rg->from) {
		nrg = kmalloc(sizeof(*nrg), GFP_KERNEL);
		if (!nrg)
			return -ENOMEM;
		nrg->from = f;
		nrg->to   = f;
		INIT_LIST_HEAD(&nrg->link);
		list_add(&nrg->link, rg->link.prev);

		return t - f;
	}

	/* Round our left edge to the current segment if it encloses us. */
	if (f > rg->from)
		f = rg->from;
	chg = t - f;

	/* Check for and consume any regions we now overlap with. */
	list_for_each_entry(rg, rg->link.prev, link) {
		if (&rg->link == head)
			break;
		if (rg->from > t)
			return chg;

		/* We overlap with this area, if it extends futher than
		 * us then we must extend ourselves.  Account for its
		 * existing reservation. */
		if (rg->to > t) {
			chg += rg->to - t;
			t = rg->to;
		}
		chg -= rg->to - rg->from;
	}
	return chg;
}

static long region_truncate(struct list_head *head, long end)
{
	struct file_region *rg, *trg;
	long chg = 0;

	/* Locate the region we are either in or before. */
	list_for_each_entry(rg, head, link)
		if (end <= rg->to)
			break;
	if (&rg->link == head)
		return 0;

	/* If we are in the middle of a region then adjust it. */
	if (end > rg->from) {
		chg = rg->to - end;
		rg->to = end;
		rg = list_entry(rg->link.next, typeof(*rg), link);
	}

	/* Drop any remaining regions. */
	list_for_each_entry_safe(rg, trg, rg->link.prev, link) {
		if (&rg->link == head)
			break;
		chg += rg->to - rg->from;
		list_del(&rg->link);
		kfree(rg);
	}
	return chg;
}

int hugetlb_reserve_pages(struct inode *inode,
					long from, long to,
					struct vm_area_struct *vma)
{
	long ret, chg;

	/*
	 * Shared mappings base their reservation on the number of pages that
	 * are already allocated on behalf of the file. Private mappings need
	 * to reserve the full area even if read-only as mprotect() may be
	 * called to make the mapping read-write. Assume !vma is a shm mapping
	 */
	if (!vma || vma->vm_flags & VM_SHARED)
		chg = region_chg(&inode->i_mapping->private_list, from, to);
	else {
		chg = to - from;
		set_vma_resv_huge_pages(vma, chg);
		set_vma_resv_flags(vma, HPAGE_RESV_OWNER);
	}

	if (chg < 0)
		return chg;

	if (hugetlb_get_quota(inode->i_mapping, chg))
		return -ENOSPC;
	ret = hugetlb_acct_memory(chg);
	if (ret < 0) {
		hugetlb_put_quota(inode->i_mapping, chg);
		return ret;
	}
	if (!vma || vma->vm_flags & VM_SHARED)
		region_add(&inode->i_mapping->private_list, from, to);
	return 0;
}

void hugetlb_unreserve_pages(struct inode *inode, long offset, long freed)
{
	long chg = region_truncate(&inode->i_mapping->private_list, offset);

	spin_lock(&inode->i_lock);
	inode->i_blocks -= BLOCKS_PER_HUGEPAGE * freed;
	spin_unlock(&inode->i_lock);

	hugetlb_put_quota(inode->i_mapping, (chg - freed));
	hugetlb_acct_memory(-(chg - freed));
}
