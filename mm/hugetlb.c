/*
 * Generic hugetlb support.
 * (C) William Irwin, April 2004
 */
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/mmu_notifier.h>
#include <linux/nodemask.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/cpuset.h>
#include <linux/mutex.h>
#include <linux/bootmem.h>
#include <linux/sysfs.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <linux/hugetlb.h>
#include "internal.h"

const unsigned long hugetlb_zero = 0, hugetlb_infinity = ~0UL;
static gfp_t htlb_alloc_mask = GFP_HIGHUSER;
unsigned long hugepages_treat_as_movable;

static int max_hstate;
unsigned int default_hstate_idx;
struct hstate hstates[HUGE_MAX_HSTATE];

__initdata LIST_HEAD(huge_boot_pages);

/* for command line parsing */
static struct hstate * __initdata parsed_hstate;
static unsigned long __initdata default_hstate_max_huge_pages;
static unsigned long __initdata default_hstate_size;

#define for_each_hstate(h) \
	for ((h) = hstates; (h) < &hstates[max_hstate]; (h)++)

/*
 * Protects updates to hugepage_freelists, nr_huge_pages, and free_huge_pages
 */
static DEFINE_SPINLOCK(hugetlb_lock);

/*
 * Region tracking -- allows tracking of reservations and instantiated pages
 *                    across the pages in a mapping.
 *
 * The region data structures are protected by a combination of the mmap_sem
 * and the hugetlb_instantion_mutex.  To access or modify a region the caller
 * must either hold the mmap_sem for write, or the mmap_sem for read and
 * the hugetlb_instantiation mutex:
 *
 * 	down_write(&mm->mmap_sem);
 * or
 * 	down_read(&mm->mmap_sem);
 * 	mutex_lock(&hugetlb_instantiation_mutex);
 */
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

static long region_count(struct list_head *head, long f, long t)
{
	struct file_region *rg;
	long chg = 0;

	/* Locate each segment we overlap with, and count that overlap. */
	list_for_each_entry(rg, head, link) {
		int seg_from;
		int seg_to;

		if (rg->to <= f)
			continue;
		if (rg->from >= t)
			break;

		seg_from = max(rg->from, f);
		seg_to = min(rg->to, t);

		chg += seg_to - seg_from;
	}

	return chg;
}

/*
 * Convert the address within this vma to the page offset within
 * the mapping, in pagecache page units; huge pages here.
 */
static pgoff_t vma_hugecache_offset(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	return ((address - vma->vm_start) >> huge_page_shift(h)) +
			(vma->vm_pgoff >> huge_page_order(h));
}

/*
 * Return the size of the pages allocated when backing a VMA. In the majority
 * cases this will be same size as used by the page table entries.
 */
unsigned long vma_kernel_pagesize(struct vm_area_struct *vma)
{
	struct hstate *hstate;

	if (!is_vm_hugetlb_page(vma))
		return PAGE_SIZE;

	hstate = hstate_vma(vma);

	return 1UL << (hstate->order + PAGE_SHIFT);
}
EXPORT_SYMBOL_GPL(vma_kernel_pagesize);

/*
 * Return the page size being used by the MMU to back a VMA. In the majority
 * of cases, the page size used by the kernel matches the MMU size. On
 * architectures where it differs, an architecture-specific version of this
 * function is required.
 */
#ifndef vma_mmu_pagesize
unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	return vma_kernel_pagesize(vma);
}
#endif

/*
 * Flags for MAP_PRIVATE reservations.  These are stored in the bottom
 * bits of the reservation map pointer, which are always clear due to
 * alignment.
 */
#define HPAGE_RESV_OWNER    (1UL << 0)
#define HPAGE_RESV_UNMAPPED (1UL << 1)
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
 *
 * The private mapping reservation is represented in a subtly different
 * manner to a shared mapping.  A shared mapping has a region map associated
 * with the underlying file, this region map represents the backing file
 * pages which have ever had a reservation assigned which this persists even
 * after the page is instantiated.  A private mapping has a region map
 * associated with the original mmap which is attached to all VMAs which
 * reference it, this region map represents those offsets which have consumed
 * reservation ie. where pages have been instantiated.
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

struct resv_map {
	struct kref refs;
	struct list_head regions;
};

static struct resv_map *resv_map_alloc(void)
{
	struct resv_map *resv_map = kmalloc(sizeof(*resv_map), GFP_KERNEL);
	if (!resv_map)
		return NULL;

	kref_init(&resv_map->refs);
	INIT_LIST_HEAD(&resv_map->regions);

	return resv_map;
}

static void resv_map_release(struct kref *ref)
{
	struct resv_map *resv_map = container_of(ref, struct resv_map, refs);

	/* Clear out any active regions before we release the map. */
	region_truncate(&resv_map->regions, 0);
	kfree(resv_map);
}

static struct resv_map *vma_resv_map(struct vm_area_struct *vma)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	if (!(vma->vm_flags & VM_MAYSHARE))
		return (struct resv_map *)(get_vma_private_data(vma) &
							~HPAGE_RESV_MASK);
	return NULL;
}

static void set_vma_resv_map(struct vm_area_struct *vma, struct resv_map *map)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	VM_BUG_ON(vma->vm_flags & VM_MAYSHARE);

	set_vma_private_data(vma, (get_vma_private_data(vma) &
				HPAGE_RESV_MASK) | (unsigned long)map);
}

static void set_vma_resv_flags(struct vm_area_struct *vma, unsigned long flags)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	VM_BUG_ON(vma->vm_flags & VM_MAYSHARE);

	set_vma_private_data(vma, get_vma_private_data(vma) | flags);
}

static int is_vma_resv_set(struct vm_area_struct *vma, unsigned long flag)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));

	return (get_vma_private_data(vma) & flag) != 0;
}

/* Decrement the reserved pages in the hugepage pool by one */
static void decrement_hugepage_resv_vma(struct hstate *h,
			struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_NORESERVE)
		return;

	if (vma->vm_flags & VM_MAYSHARE) {
		/* Shared mappings always use reserves */
		h->resv_huge_pages--;
	} else if (is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		/*
		 * Only the process that called mmap() has reserves for
		 * private mappings.
		 */
		h->resv_huge_pages--;
	}
}

/* Reset counters to 0 and clear all HPAGE_RESV_* flags */
void reset_vma_resv_huge_pages(struct vm_area_struct *vma)
{
	VM_BUG_ON(!is_vm_hugetlb_page(vma));
	if (!(vma->vm_flags & VM_MAYSHARE))
		vma->vm_private_data = (void *)0;
}

/* Returns true if the VMA has associated reserve pages */
static int vma_has_reserves(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_MAYSHARE)
		return 1;
	if (is_vma_resv_set(vma, HPAGE_RESV_OWNER))
		return 1;
	return 0;
}

static void clear_gigantic_page(struct page *page,
			unsigned long addr, unsigned long sz)
{
	int i;
	struct page *p = page;

	might_sleep();
	for (i = 0; i < sz/PAGE_SIZE; i++, p = mem_map_next(p, page, i)) {
		cond_resched();
		clear_user_highpage(p, addr + i * PAGE_SIZE);
	}
}
static void clear_huge_page(struct page *page,
			unsigned long addr, unsigned long sz)
{
	int i;

	if (unlikely(sz > MAX_ORDER_NR_PAGES)) {
		clear_gigantic_page(page, addr, sz);
		return;
	}

	might_sleep();
	for (i = 0; i < sz/PAGE_SIZE; i++) {
		cond_resched();
		clear_user_highpage(page + i, addr + i * PAGE_SIZE);
	}
}

static void copy_gigantic_page(struct page *dst, struct page *src,
			   unsigned long addr, struct vm_area_struct *vma)
{
	int i;
	struct hstate *h = hstate_vma(vma);
	struct page *dst_base = dst;
	struct page *src_base = src;
	might_sleep();
	for (i = 0; i < pages_per_huge_page(h); ) {
		cond_resched();
		copy_user_highpage(dst, src, addr + i*PAGE_SIZE, vma);

		i++;
		dst = mem_map_next(dst, dst_base, i);
		src = mem_map_next(src, src_base, i);
	}
}
static void copy_huge_page(struct page *dst, struct page *src,
			   unsigned long addr, struct vm_area_struct *vma)
{
	int i;
	struct hstate *h = hstate_vma(vma);

	if (unlikely(pages_per_huge_page(h) > MAX_ORDER_NR_PAGES)) {
		copy_gigantic_page(dst, src, addr, vma);
		return;
	}

	might_sleep();
	for (i = 0; i < pages_per_huge_page(h); i++) {
		cond_resched();
		copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE, vma);
	}
}

static void enqueue_huge_page(struct hstate *h, struct page *page)
{
	int nid = page_to_nid(page);
	list_add(&page->lru, &h->hugepage_freelists[nid]);
	h->free_huge_pages++;
	h->free_huge_pages_node[nid]++;
}

static struct page *dequeue_huge_page_vma(struct hstate *h,
				struct vm_area_struct *vma,
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
	if (!vma_has_reserves(vma) &&
			h->free_huge_pages - h->resv_huge_pages == 0)
		return NULL;

	/* If reserves cannot be used, ensure enough pages are in the pool */
	if (avoid_reserve && h->free_huge_pages - h->resv_huge_pages == 0)
		return NULL;

	for_each_zone_zonelist_nodemask(zone, z, zonelist,
						MAX_NR_ZONES - 1, nodemask) {
		nid = zone_to_nid(zone);
		if (cpuset_zone_allowed_softwall(zone, htlb_alloc_mask) &&
		    !list_empty(&h->hugepage_freelists[nid])) {
			page = list_entry(h->hugepage_freelists[nid].next,
					  struct page, lru);
			list_del(&page->lru);
			h->free_huge_pages--;
			h->free_huge_pages_node[nid]--;

			if (!avoid_reserve)
				decrement_hugepage_resv_vma(h, vma);

			break;
		}
	}
	mpol_cond_put(mpol);
	return page;
}

static void update_and_free_page(struct hstate *h, struct page *page)
{
	int i;

	VM_BUG_ON(h->order >= MAX_ORDER);

	h->nr_huge_pages--;
	h->nr_huge_pages_node[page_to_nid(page)]--;
	for (i = 0; i < pages_per_huge_page(h); i++) {
		page[i].flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
				1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback);
	}
	set_compound_page_dtor(page, NULL);
	set_page_refcounted(page);
	arch_release_hugepage(page);
	__free_pages(page, huge_page_order(h));
}

struct hstate *size_to_hstate(unsigned long size)
{
	struct hstate *h;

	for_each_hstate(h) {
		if (huge_page_size(h) == size)
			return h;
	}
	return NULL;
}

static void free_huge_page(struct page *page)
{
	/*
	 * Can't pass hstate in here because it is called from the
	 * compound page destructor.
	 */
	struct hstate *h = page_hstate(page);
	int nid = page_to_nid(page);
	struct address_space *mapping;

	mapping = (struct address_space *) page_private(page);
	set_page_private(page, 0);
	BUG_ON(page_count(page));
	INIT_LIST_HEAD(&page->lru);

	spin_lock(&hugetlb_lock);
	if (h->surplus_huge_pages_node[nid] && huge_page_order(h) < MAX_ORDER) {
		update_and_free_page(h, page);
		h->surplus_huge_pages--;
		h->surplus_huge_pages_node[nid]--;
	} else {
		enqueue_huge_page(h, page);
	}
	spin_unlock(&hugetlb_lock);
	if (mapping)
		hugetlb_put_quota(mapping, 1);
}

static void prep_new_huge_page(struct hstate *h, struct page *page, int nid)
{
	set_compound_page_dtor(page, free_huge_page);
	spin_lock(&hugetlb_lock);
	h->nr_huge_pages++;
	h->nr_huge_pages_node[nid]++;
	spin_unlock(&hugetlb_lock);
	put_page(page); /* free it into the hugepage allocator */
}

static void prep_compound_gigantic_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;
	struct page *p = page + 1;

	/* we rely on prep_new_huge_page to set the destructor */
	set_compound_order(page, order);
	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++, p = mem_map_next(p, page, i)) {
		__SetPageTail(p);
		p->first_page = page;
	}
}

int PageHuge(struct page *page)
{
	compound_page_dtor *dtor;

	if (!PageCompound(page))
		return 0;

	page = compound_head(page);
	dtor = get_compound_page_dtor(page);

	return dtor == free_huge_page;
}

static struct page *alloc_fresh_huge_page_node(struct hstate *h, int nid)
{
	struct page *page;

	if (h->order >= MAX_ORDER)
		return NULL;

	page = alloc_pages_exact_node(nid,
		htlb_alloc_mask|__GFP_COMP|__GFP_THISNODE|
						__GFP_REPEAT|__GFP_NOWARN,
		huge_page_order(h));
	if (page) {
		if (arch_prepare_hugepage(page)) {
			__free_pages(page, huge_page_order(h));
			return NULL;
		}
		prep_new_huge_page(h, page, nid);
	}

	return page;
}

/*
 * Use a helper variable to find the next node and then
 * copy it back to next_nid_to_alloc afterwards:
 * otherwise there's a window in which a racer might
 * pass invalid nid MAX_NUMNODES to alloc_pages_exact_node.
 * But we don't need to use a spin_lock here: it really
 * doesn't matter if occasionally a racer chooses the
 * same nid as we do.  Move nid forward in the mask even
 * if we just successfully allocated a hugepage so that
 * the next caller gets hugepages on the next node.
 */
static int hstate_next_node_to_alloc(struct hstate *h)
{
	int next_nid;
	next_nid = next_node(h->next_nid_to_alloc, node_online_map);
	if (next_nid == MAX_NUMNODES)
		next_nid = first_node(node_online_map);
	h->next_nid_to_alloc = next_nid;
	return next_nid;
}

static int alloc_fresh_huge_page(struct hstate *h)
{
	struct page *page;
	int start_nid;
	int next_nid;
	int ret = 0;

	start_nid = h->next_nid_to_alloc;
	next_nid = start_nid;

	do {
		page = alloc_fresh_huge_page_node(h, next_nid);
		if (page)
			ret = 1;
		next_nid = hstate_next_node_to_alloc(h);
	} while (!page && next_nid != start_nid);

	if (ret)
		count_vm_event(HTLB_BUDDY_PGALLOC);
	else
		count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);

	return ret;
}

/*
 * helper for free_pool_huge_page() - find next node
 * from which to free a huge page
 */
static int hstate_next_node_to_free(struct hstate *h)
{
	int next_nid;
	next_nid = next_node(h->next_nid_to_free, node_online_map);
	if (next_nid == MAX_NUMNODES)
		next_nid = first_node(node_online_map);
	h->next_nid_to_free = next_nid;
	return next_nid;
}

/*
 * Free huge page from pool from next node to free.
 * Attempt to keep persistent huge pages more or less
 * balanced over allowed nodes.
 * Called with hugetlb_lock locked.
 */
static int free_pool_huge_page(struct hstate *h, bool acct_surplus)
{
	int start_nid;
	int next_nid;
	int ret = 0;

	start_nid = h->next_nid_to_free;
	next_nid = start_nid;

	do {
		/*
		 * If we're returning unused surplus pages, only examine
		 * nodes with surplus pages.
		 */
		if ((!acct_surplus || h->surplus_huge_pages_node[next_nid]) &&
		    !list_empty(&h->hugepage_freelists[next_nid])) {
			struct page *page =
				list_entry(h->hugepage_freelists[next_nid].next,
					  struct page, lru);
			list_del(&page->lru);
			h->free_huge_pages--;
			h->free_huge_pages_node[next_nid]--;
			if (acct_surplus) {
				h->surplus_huge_pages--;
				h->surplus_huge_pages_node[next_nid]--;
			}
			update_and_free_page(h, page);
			ret = 1;
		}
		next_nid = hstate_next_node_to_free(h);
	} while (!ret && next_nid != start_nid);

	return ret;
}

static struct page *alloc_buddy_huge_page(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	struct page *page;
	unsigned int nid;

	if (h->order >= MAX_ORDER)
		return NULL;

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
	if (h->surplus_huge_pages >= h->nr_overcommit_huge_pages) {
		spin_unlock(&hugetlb_lock);
		return NULL;
	} else {
		h->nr_huge_pages++;
		h->surplus_huge_pages++;
	}
	spin_unlock(&hugetlb_lock);

	page = alloc_pages(htlb_alloc_mask|__GFP_COMP|
					__GFP_REPEAT|__GFP_NOWARN,
					huge_page_order(h));

	if (page && arch_prepare_hugepage(page)) {
		__free_pages(page, huge_page_order(h));
		return NULL;
	}

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
		h->nr_huge_pages_node[nid]++;
		h->surplus_huge_pages_node[nid]++;
		__count_vm_event(HTLB_BUDDY_PGALLOC);
	} else {
		h->nr_huge_pages--;
		h->surplus_huge_pages--;
		__count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);
	}
	spin_unlock(&hugetlb_lock);

	return page;
}

/*
 * Increase the hugetlb pool such that it can accomodate a reservation
 * of size 'delta'.
 */
static int gather_surplus_pages(struct hstate *h, int delta)
{
	struct list_head surplus_list;
	struct page *page, *tmp;
	int ret, i;
	int needed, allocated;

	needed = (h->resv_huge_pages + delta) - h->free_huge_pages;
	if (needed <= 0) {
		h->resv_huge_pages += delta;
		return 0;
	}

	allocated = 0;
	INIT_LIST_HEAD(&surplus_list);

	ret = -ENOMEM;
retry:
	spin_unlock(&hugetlb_lock);
	for (i = 0; i < needed; i++) {
		page = alloc_buddy_huge_page(h, NULL, 0);
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
	needed = (h->resv_huge_pages + delta) -
			(h->free_huge_pages + allocated);
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
	h->resv_huge_pages += delta;
	ret = 0;
free:
	/* Free the needed pages to the hugetlb pool */
	list_for_each_entry_safe(page, tmp, &surplus_list, lru) {
		if ((--needed) < 0)
			break;
		list_del(&page->lru);
		enqueue_huge_page(h, page);
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
 * Called with hugetlb_lock held.
 */
static void return_unused_surplus_pages(struct hstate *h,
					unsigned long unused_resv_pages)
{
	unsigned long nr_pages;

	/* Uncommit the reservation */
	h->resv_huge_pages -= unused_resv_pages;

	/* Cannot return gigantic pages currently */
	if (h->order >= MAX_ORDER)
		return;

	nr_pages = min(unused_resv_pages, h->surplus_huge_pages);

	/*
	 * We want to release as many surplus pages as possible, spread
	 * evenly across all nodes. Iterate across all nodes until we
	 * can no longer free unreserved surplus pages. This occurs when
	 * the nodes with surplus pages have no free pages.
	 * free_pool_huge_page() will balance the the frees across the
	 * on-line nodes for us and will handle the hstate accounting.
	 */
	while (nr_pages--) {
		if (!free_pool_huge_page(h, 1))
			break;
	}
}

/*
 * Determine if the huge page at addr within the vma has an associated
 * reservation.  Where it does not we will need to logically increase
 * reservation and actually increase quota before an allocation can occur.
 * Where any new reservation would be required the reservation change is
 * prepared, but not committed.  Once the page has been quota'd allocated
 * an instantiated the change should be committed via vma_commit_reservation.
 * No action is required on failure.
 */
static long vma_needs_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	if (vma->vm_flags & VM_MAYSHARE) {
		pgoff_t idx = vma_hugecache_offset(h, vma, addr);
		return region_chg(&inode->i_mapping->private_list,
							idx, idx + 1);

	} else if (!is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		return 1;

	} else  {
		long err;
		pgoff_t idx = vma_hugecache_offset(h, vma, addr);
		struct resv_map *reservations = vma_resv_map(vma);

		err = region_chg(&reservations->regions, idx, idx + 1);
		if (err < 0)
			return err;
		return 0;
	}
}
static void vma_commit_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	if (vma->vm_flags & VM_MAYSHARE) {
		pgoff_t idx = vma_hugecache_offset(h, vma, addr);
		region_add(&inode->i_mapping->private_list, idx, idx + 1);

	} else if (is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		pgoff_t idx = vma_hugecache_offset(h, vma, addr);
		struct resv_map *reservations = vma_resv_map(vma);

		/* Mark this page used in the map. */
		region_add(&reservations->regions, idx, idx + 1);
	}
}

static struct page *alloc_huge_page(struct vm_area_struct *vma,
				    unsigned long addr, int avoid_reserve)
{
	struct hstate *h = hstate_vma(vma);
	struct page *page;
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	long chg;

	/*
	 * Processes that did not create the mapping will have no reserves and
	 * will not have accounted against quota. Check that the quota can be
	 * made before satisfying the allocation
	 * MAP_NORESERVE mappings may also need pages and quota allocated
	 * if no reserve mapping overlaps.
	 */
	chg = vma_needs_reservation(h, vma, addr);
	if (chg < 0)
		return ERR_PTR(chg);
	if (chg)
		if (hugetlb_get_quota(inode->i_mapping, chg))
			return ERR_PTR(-ENOSPC);

	spin_lock(&hugetlb_lock);
	page = dequeue_huge_page_vma(h, vma, addr, avoid_reserve);
	spin_unlock(&hugetlb_lock);

	if (!page) {
		page = alloc_buddy_huge_page(h, vma, addr);
		if (!page) {
			hugetlb_put_quota(inode->i_mapping, chg);
			return ERR_PTR(-VM_FAULT_OOM);
		}
	}

	set_page_refcounted(page);
	set_page_private(page, (unsigned long) mapping);

	vma_commit_reservation(h, vma, addr);

	return page;
}

int __weak alloc_bootmem_huge_page(struct hstate *h)
{
	struct huge_bootmem_page *m;
	int nr_nodes = nodes_weight(node_online_map);

	while (nr_nodes) {
		void *addr;

		addr = __alloc_bootmem_node_nopanic(
				NODE_DATA(h->next_nid_to_alloc),
				huge_page_size(h), huge_page_size(h), 0);

		hstate_next_node_to_alloc(h);
		if (addr) {
			/*
			 * Use the beginning of the huge page to store the
			 * huge_bootmem_page struct (until gather_bootmem
			 * puts them into the mem_map).
			 */
			m = addr;
			goto found;
		}
		nr_nodes--;
	}
	return 0;

found:
	BUG_ON((unsigned long)virt_to_phys(m) & (huge_page_size(h) - 1));
	/* Put them into a private list first because mem_map is not up yet */
	list_add(&m->list, &huge_boot_pages);
	m->hstate = h;
	return 1;
}

static void prep_compound_huge_page(struct page *page, int order)
{
	if (unlikely(order > (MAX_ORDER - 1)))
		prep_compound_gigantic_page(page, order);
	else
		prep_compound_page(page, order);
}

/* Put bootmem huge pages into the standard lists after mem_map is up */
static void __init gather_bootmem_prealloc(void)
{
	struct huge_bootmem_page *m;

	list_for_each_entry(m, &huge_boot_pages, list) {
		struct page *page = virt_to_page(m);
		struct hstate *h = m->hstate;
		__ClearPageReserved(page);
		WARN_ON(page_count(page) != 1);
		prep_compound_huge_page(page, h->order);
		prep_new_huge_page(h, page, page_to_nid(page));
	}
}

static void __init hugetlb_hstate_alloc_pages(struct hstate *h)
{
	unsigned long i;

	for (i = 0; i < h->max_huge_pages; ++i) {
		if (h->order >= MAX_ORDER) {
			if (!alloc_bootmem_huge_page(h))
				break;
		} else if (!alloc_fresh_huge_page(h))
			break;
	}
	h->max_huge_pages = i;
}

static void __init hugetlb_init_hstates(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		/* oversize hugepages were init'ed in early boot */
		if (h->order < MAX_ORDER)
			hugetlb_hstate_alloc_pages(h);
	}
}

static char * __init memfmt(char *buf, unsigned long n)
{
	if (n >= (1UL << 30))
		sprintf(buf, "%lu GB", n >> 30);
	else if (n >= (1UL << 20))
		sprintf(buf, "%lu MB", n >> 20);
	else
		sprintf(buf, "%lu KB", n >> 10);
	return buf;
}

static void __init report_hugepages(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		char buf[32];
		printk(KERN_INFO "HugeTLB registered %s page size, "
				 "pre-allocated %ld pages\n",
			memfmt(buf, huge_page_size(h)),
			h->free_huge_pages);
	}
}

#ifdef CONFIG_HIGHMEM
static void try_to_free_low(struct hstate *h, unsigned long count)
{
	int i;

	if (h->order >= MAX_ORDER)
		return;

	for (i = 0; i < MAX_NUMNODES; ++i) {
		struct page *page, *next;
		struct list_head *freel = &h->hugepage_freelists[i];
		list_for_each_entry_safe(page, next, freel, lru) {
			if (count >= h->nr_huge_pages)
				return;
			if (PageHighMem(page))
				continue;
			list_del(&page->lru);
			update_and_free_page(h, page);
			h->free_huge_pages--;
			h->free_huge_pages_node[page_to_nid(page)]--;
		}
	}
}
#else
static inline void try_to_free_low(struct hstate *h, unsigned long count)
{
}
#endif

/*
 * Increment or decrement surplus_huge_pages.  Keep node-specific counters
 * balanced by operating on them in a round-robin fashion.
 * Returns 1 if an adjustment was made.
 */
static int adjust_pool_surplus(struct hstate *h, int delta)
{
	int start_nid, next_nid;
	int ret = 0;

	VM_BUG_ON(delta != -1 && delta != 1);

	if (delta < 0)
		start_nid = h->next_nid_to_alloc;
	else
		start_nid = h->next_nid_to_free;
	next_nid = start_nid;

	do {
		int nid = next_nid;
		if (delta < 0)  {
			next_nid = hstate_next_node_to_alloc(h);
			/*
			 * To shrink on this node, there must be a surplus page
			 */
			if (!h->surplus_huge_pages_node[nid])
				continue;
		}
		if (delta > 0) {
			next_nid = hstate_next_node_to_free(h);
			/*
			 * Surplus cannot exceed the total number of pages
			 */
			if (h->surplus_huge_pages_node[nid] >=
						h->nr_huge_pages_node[nid])
				continue;
		}

		h->surplus_huge_pages += delta;
		h->surplus_huge_pages_node[nid] += delta;
		ret = 1;
		break;
	} while (next_nid != start_nid);

	return ret;
}

#define persistent_huge_pages(h) (h->nr_huge_pages - h->surplus_huge_pages)
static unsigned long set_max_huge_pages(struct hstate *h, unsigned long count)
{
	unsigned long min_count, ret;

	if (h->order >= MAX_ORDER)
		return h->max_huge_pages;

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
	while (h->surplus_huge_pages && count > persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, -1))
			break;
	}

	while (count > persistent_huge_pages(h)) {
		/*
		 * If this allocation races such that we no longer need the
		 * page, free_huge_page will handle it by freeing the page
		 * and reducing the surplus.
		 */
		spin_unlock(&hugetlb_lock);
		ret = alloc_fresh_huge_page(h);
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
	min_count = h->resv_huge_pages + h->nr_huge_pages - h->free_huge_pages;
	min_count = max(count, min_count);
	try_to_free_low(h, min_count);
	while (min_count < persistent_huge_pages(h)) {
		if (!free_pool_huge_page(h, 0))
			break;
	}
	while (count < persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, 1))
			break;
	}
out:
	ret = persistent_huge_pages(h);
	spin_unlock(&hugetlb_lock);
	return ret;
}

#define HSTATE_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define HSTATE_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static struct kobject *hugepages_kobj;
static struct kobject *hstate_kobjs[HUGE_MAX_HSTATE];

static struct hstate *kobj_to_hstate(struct kobject *kobj)
{
	int i;
	for (i = 0; i < HUGE_MAX_HSTATE; i++)
		if (hstate_kobjs[i] == kobj)
			return &hstates[i];
	BUG();
	return NULL;
}

static ssize_t nr_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj);
	return sprintf(buf, "%lu\n", h->nr_huge_pages);
}
static ssize_t nr_hugepages_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long input;
	struct hstate *h = kobj_to_hstate(kobj);

	err = strict_strtoul(buf, 10, &input);
	if (err)
		return 0;

	h->max_huge_pages = set_max_huge_pages(h, input);

	return count;
}
HSTATE_ATTR(nr_hugepages);

static ssize_t nr_overcommit_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj);
	return sprintf(buf, "%lu\n", h->nr_overcommit_huge_pages);
}
static ssize_t nr_overcommit_hugepages_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long input;
	struct hstate *h = kobj_to_hstate(kobj);

	err = strict_strtoul(buf, 10, &input);
	if (err)
		return 0;

	spin_lock(&hugetlb_lock);
	h->nr_overcommit_huge_pages = input;
	spin_unlock(&hugetlb_lock);

	return count;
}
HSTATE_ATTR(nr_overcommit_hugepages);

static ssize_t free_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj);
	return sprintf(buf, "%lu\n", h->free_huge_pages);
}
HSTATE_ATTR_RO(free_hugepages);

static ssize_t resv_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj);
	return sprintf(buf, "%lu\n", h->resv_huge_pages);
}
HSTATE_ATTR_RO(resv_hugepages);

static ssize_t surplus_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj);
	return sprintf(buf, "%lu\n", h->surplus_huge_pages);
}
HSTATE_ATTR_RO(surplus_hugepages);

static struct attribute *hstate_attrs[] = {
	&nr_hugepages_attr.attr,
	&nr_overcommit_hugepages_attr.attr,
	&free_hugepages_attr.attr,
	&resv_hugepages_attr.attr,
	&surplus_hugepages_attr.attr,
	NULL,
};

static struct attribute_group hstate_attr_group = {
	.attrs = hstate_attrs,
};

static int __init hugetlb_sysfs_add_hstate(struct hstate *h)
{
	int retval;

	hstate_kobjs[h - hstates] = kobject_create_and_add(h->name,
							hugepages_kobj);
	if (!hstate_kobjs[h - hstates])
		return -ENOMEM;

	retval = sysfs_create_group(hstate_kobjs[h - hstates],
							&hstate_attr_group);
	if (retval)
		kobject_put(hstate_kobjs[h - hstates]);

	return retval;
}

static void __init hugetlb_sysfs_init(void)
{
	struct hstate *h;
	int err;

	hugepages_kobj = kobject_create_and_add("hugepages", mm_kobj);
	if (!hugepages_kobj)
		return;

	for_each_hstate(h) {
		err = hugetlb_sysfs_add_hstate(h);
		if (err)
			printk(KERN_ERR "Hugetlb: Unable to add hstate %s",
								h->name);
	}
}

static void __exit hugetlb_exit(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		kobject_put(hstate_kobjs[h - hstates]);
	}

	kobject_put(hugepages_kobj);
}
module_exit(hugetlb_exit);

static int __init hugetlb_init(void)
{
	/* Some platform decide whether they support huge pages at boot
	 * time. On these, such as powerpc, HPAGE_SHIFT is set to 0 when
	 * there is no such support
	 */
	if (HPAGE_SHIFT == 0)
		return 0;

	if (!size_to_hstate(default_hstate_size)) {
		default_hstate_size = HPAGE_SIZE;
		if (!size_to_hstate(default_hstate_size))
			hugetlb_add_hstate(HUGETLB_PAGE_ORDER);
	}
	default_hstate_idx = size_to_hstate(default_hstate_size) - hstates;
	if (default_hstate_max_huge_pages)
		default_hstate.max_huge_pages = default_hstate_max_huge_pages;

	hugetlb_init_hstates();

	gather_bootmem_prealloc();

	report_hugepages();

	hugetlb_sysfs_init();

	return 0;
}
module_init(hugetlb_init);

/* Should be called on processing a hugepagesz=... option */
void __init hugetlb_add_hstate(unsigned order)
{
	struct hstate *h;
	unsigned long i;

	if (size_to_hstate(PAGE_SIZE << order)) {
		printk(KERN_WARNING "hugepagesz= specified twice, ignoring\n");
		return;
	}
	BUG_ON(max_hstate >= HUGE_MAX_HSTATE);
	BUG_ON(order == 0);
	h = &hstates[max_hstate++];
	h->order = order;
	h->mask = ~((1ULL << (order + PAGE_SHIFT)) - 1);
	h->nr_huge_pages = 0;
	h->free_huge_pages = 0;
	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&h->hugepage_freelists[i]);
	h->next_nid_to_alloc = first_node(node_online_map);
	h->next_nid_to_free = first_node(node_online_map);
	snprintf(h->name, HSTATE_NAME_LEN, "hugepages-%lukB",
					huge_page_size(h)/1024);

	parsed_hstate = h;
}

static int __init hugetlb_nrpages_setup(char *s)
{
	unsigned long *mhp;
	static unsigned long *last_mhp;

	/*
	 * !max_hstate means we haven't parsed a hugepagesz= parameter yet,
	 * so this hugepages= parameter goes to the "default hstate".
	 */
	if (!max_hstate)
		mhp = &default_hstate_max_huge_pages;
	else
		mhp = &parsed_hstate->max_huge_pages;

	if (mhp == last_mhp) {
		printk(KERN_WARNING "hugepages= specified twice without "
			"interleaving hugepagesz=, ignoring\n");
		return 1;
	}

	if (sscanf(s, "%lu", mhp) <= 0)
		*mhp = 0;

	/*
	 * Global state is always initialized later in hugetlb_init.
	 * But we need to allocate >= MAX_ORDER hstates here early to still
	 * use the bootmem allocator.
	 */
	if (max_hstate && parsed_hstate->order >= MAX_ORDER)
		hugetlb_hstate_alloc_pages(parsed_hstate);

	last_mhp = mhp;

	return 1;
}
__setup("hugepages=", hugetlb_nrpages_setup);

static int __init hugetlb_default_setup(char *s)
{
	default_hstate_size = memparse(s, &s);
	return 1;
}
__setup("default_hugepagesz=", hugetlb_default_setup);

static unsigned int cpuset_mems_nr(unsigned int *array)
{
	int node;
	unsigned int nr = 0;

	for_each_node_mask(node, cpuset_current_mems_allowed)
		nr += array[node];

	return nr;
}

#ifdef CONFIG_SYSCTL
int hugetlb_sysctl_handler(struct ctl_table *table, int write,
			   void __user *buffer,
			   size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp;

	if (!write)
		tmp = h->max_huge_pages;

	table->data = &tmp;
	table->maxlen = sizeof(unsigned long);
	proc_doulongvec_minmax(table, write, buffer, length, ppos);

	if (write)
		h->max_huge_pages = set_max_huge_pages(h, tmp);

	return 0;
}

int hugetlb_treat_movable_handler(struct ctl_table *table, int write,
			void __user *buffer,
			size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, buffer, length, ppos);
	if (hugepages_treat_as_movable)
		htlb_alloc_mask = GFP_HIGHUSER_MOVABLE;
	else
		htlb_alloc_mask = GFP_HIGHUSER;
	return 0;
}

int hugetlb_overcommit_handler(struct ctl_table *table, int write,
			void __user *buffer,
			size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp;

	if (!write)
		tmp = h->nr_overcommit_huge_pages;

	table->data = &tmp;
	table->maxlen = sizeof(unsigned long);
	proc_doulongvec_minmax(table, write, buffer, length, ppos);

	if (write) {
		spin_lock(&hugetlb_lock);
		h->nr_overcommit_huge_pages = tmp;
		spin_unlock(&hugetlb_lock);
	}

	return 0;
}

#endif /* CONFIG_SYSCTL */

void hugetlb_report_meminfo(struct seq_file *m)
{
	struct hstate *h = &default_hstate;
	seq_printf(m,
			"HugePages_Total:   %5lu\n"
			"HugePages_Free:    %5lu\n"
			"HugePages_Rsvd:    %5lu\n"
			"HugePages_Surp:    %5lu\n"
			"Hugepagesize:   %8lu kB\n",
			h->nr_huge_pages,
			h->free_huge_pages,
			h->resv_huge_pages,
			h->surplus_huge_pages,
			1UL << (huge_page_order(h) + PAGE_SHIFT - 10));
}

int hugetlb_report_node_meminfo(int nid, char *buf)
{
	struct hstate *h = &default_hstate;
	return sprintf(buf,
		"Node %d HugePages_Total: %5u\n"
		"Node %d HugePages_Free:  %5u\n"
		"Node %d HugePages_Surp:  %5u\n",
		nid, h->nr_huge_pages_node[nid],
		nid, h->free_huge_pages_node[nid],
		nid, h->surplus_huge_pages_node[nid]);
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	struct hstate *h = &default_hstate;
	return h->nr_huge_pages * pages_per_huge_page(h);
}

static int hugetlb_acct_memory(struct hstate *h, long delta)
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
		if (gather_surplus_pages(h, delta) < 0)
			goto out;

		if (delta > cpuset_mems_nr(h->free_huge_pages_node)) {
			return_unused_surplus_pages(h, delta);
			goto out;
		}
	}

	ret = 0;
	if (delta < 0)
		return_unused_surplus_pages(h, (unsigned long) -delta);

out:
	spin_unlock(&hugetlb_lock);
	return ret;
}

static void hugetlb_vm_op_open(struct vm_area_struct *vma)
{
	struct resv_map *reservations = vma_resv_map(vma);

	/*
	 * This new VMA should share its siblings reservation map if present.
	 * The VMA will only ever have a valid reservation map pointer where
	 * it is being copied for another still existing VMA.  As that VMA
	 * has a reference to the reservation map it cannot dissappear until
	 * after this open call completes.  It is therefore safe to take a
	 * new reference here without additional locking.
	 */
	if (reservations)
		kref_get(&reservations->refs);
}

static void hugetlb_vm_op_close(struct vm_area_struct *vma)
{
	struct hstate *h = hstate_vma(vma);
	struct resv_map *reservations = vma_resv_map(vma);
	unsigned long reserve;
	unsigned long start;
	unsigned long end;

	if (reservations) {
		start = vma_hugecache_offset(h, vma, vma->vm_start);
		end = vma_hugecache_offset(h, vma, vma->vm_end);

		reserve = (end - start) -
			region_count(&reservations->regions, start, end);

		kref_put(&reservations->refs, resv_map_release);

		if (reserve) {
			hugetlb_acct_memory(h, -reserve);
			hugetlb_put_quota(vma->vm_file->f_mapping, reserve);
		}
	}
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

const struct vm_operations_struct hugetlb_vm_ops = {
	.fault = hugetlb_vm_op_fault,
	.open = hugetlb_vm_op_open,
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
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);

	cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;

	for (addr = vma->vm_start; addr < vma->vm_end; addr += sz) {
		src_pte = huge_pte_offset(src, addr);
		if (!src_pte)
			continue;
		dst_pte = huge_pte_alloc(dst, addr, sz);
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
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);

	/*
	 * A page gathering list, protected by per file i_mmap_lock. The
	 * lock is used to avoid list corruption from multiple unmapping
	 * of the same page since we are using page->lru.
	 */
	LIST_HEAD(page_list);

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON(start & ~huge_page_mask(h));
	BUG_ON(end & ~huge_page_mask(h));

	mmu_notifier_invalidate_range_start(mm, start, end);
	spin_lock(&mm->page_table_lock);
	for (address = start; address < end; address += sz) {
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
	mmu_notifier_invalidate_range_end(mm, start, end);
	list_for_each_entry_safe(page, tmp, &page_list, lru) {
		list_del(&page->lru);
		put_page(page);
	}
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end, struct page *ref_page)
{
	spin_lock(&vma->vm_file->f_mapping->i_mmap_lock);
	__unmap_hugepage_range(vma, start, end, ref_page);
	spin_unlock(&vma->vm_file->f_mapping->i_mmap_lock);
}

/*
 * This is called when the original mapper is failing to COW a MAP_PRIVATE
 * mappping it owns the reserve page for. The intention is to unmap the page
 * from other VMAs and let the children be SIGKILLed if they are faulting the
 * same region.
 */
static int unmap_ref_private(struct mm_struct *mm, struct vm_area_struct *vma,
				struct page *page, unsigned long address)
{
	struct hstate *h = hstate_vma(vma);
	struct vm_area_struct *iter_vma;
	struct address_space *mapping;
	struct prio_tree_iter iter;
	pgoff_t pgoff;

	/*
	 * vm_pgoff is in PAGE_SIZE units, hence the different calculation
	 * from page cache lookup which is in HPAGE_SIZE units.
	 */
	address = address & huge_page_mask(h);
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
				address, address + huge_page_size(h),
				page);
	}

	return 1;
}

static int hugetlb_cow(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, pte_t pte,
			struct page *pagecache_page)
{
	struct hstate *h = hstate_vma(vma);
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
	if (!(vma->vm_flags & VM_MAYSHARE) &&
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

	ptep = huge_pte_offset(mm, address & huge_page_mask(h));
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
static struct page *hugetlbfs_pagecache_page(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	struct address_space *mapping;
	pgoff_t idx;

	mapping = vma->vm_file->f_mapping;
	idx = vma_hugecache_offset(h, vma, address);

	return find_lock_page(mapping, idx);
}

/*
 * Return whether there is a pagecache page to back given address within VMA.
 * Caller follow_hugetlb_page() holds page_table_lock so we cannot lock_page.
 */
static bool hugetlbfs_pagecache_present(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	struct address_space *mapping;
	pgoff_t idx;
	struct page *page;

	mapping = vma->vm_file->f_mapping;
	idx = vma_hugecache_offset(h, vma, address);

	page = find_get_page(mapping, idx);
	if (page)
		put_page(page);
	return page != NULL;
}

static int hugetlb_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, unsigned int flags)
{
	struct hstate *h = hstate_vma(vma);
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
	idx = vma_hugecache_offset(h, vma, address);

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
retry:
	page = find_lock_page(mapping, idx);
	if (!page) {
		size = i_size_read(mapping->host) >> huge_page_shift(h);
		if (idx >= size)
			goto out;
		page = alloc_huge_page(vma, address, 0);
		if (IS_ERR(page)) {
			ret = -PTR_ERR(page);
			goto out;
		}
		clear_huge_page(page, address, huge_page_size(h));
		__SetPageUptodate(page);

		if (vma->vm_flags & VM_MAYSHARE) {
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
			inode->i_blocks += blocks_per_huge_page(h);
			spin_unlock(&inode->i_lock);
		} else
			lock_page(page);
	}

	/*
	 * If we are going to COW a private mapping later, we examine the
	 * pending reservations for this page now. This will ensure that
	 * any allocations necessary to record that reservation occur outside
	 * the spinlock.
	 */
	if ((flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED))
		if (vma_needs_reservation(h, vma, address) < 0) {
			ret = VM_FAULT_OOM;
			goto backout_unlocked;
		}

	spin_lock(&mm->page_table_lock);
	size = i_size_read(mapping->host) >> huge_page_shift(h);
	if (idx >= size)
		goto backout;

	ret = 0;
	if (!huge_pte_none(huge_ptep_get(ptep)))
		goto backout;

	new_pte = make_huge_pte(vma, page, ((vma->vm_flags & VM_WRITE)
				&& (vma->vm_flags & VM_SHARED)));
	set_huge_pte_at(mm, address, ptep, new_pte);

	if ((flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		/* Optimization, do the COW without a second fault */
		ret = hugetlb_cow(mm, vma, address, ptep, new_pte, page);
	}

	spin_unlock(&mm->page_table_lock);
	unlock_page(page);
out:
	return ret;

backout:
	spin_unlock(&mm->page_table_lock);
backout_unlocked:
	unlock_page(page);
	put_page(page);
	goto out;
}

int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, unsigned int flags)
{
	pte_t *ptep;
	pte_t entry;
	int ret;
	struct page *pagecache_page = NULL;
	static DEFINE_MUTEX(hugetlb_instantiation_mutex);
	struct hstate *h = hstate_vma(vma);

	ptep = huge_pte_alloc(mm, address, huge_page_size(h));
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
		ret = hugetlb_no_page(mm, vma, address, ptep, flags);
		goto out_mutex;
	}

	ret = 0;

	/*
	 * If we are going to COW the mapping later, we examine the pending
	 * reservations for this page now. This will ensure that any
	 * allocations necessary to record that reservation occur outside the
	 * spinlock. For private mappings, we also lookup the pagecache
	 * page now as it is used to determine if a reservation has been
	 * consumed.
	 */
	if ((flags & FAULT_FLAG_WRITE) && !pte_write(entry)) {
		if (vma_needs_reservation(h, vma, address) < 0) {
			ret = VM_FAULT_OOM;
			goto out_mutex;
		}

		if (!(vma->vm_flags & VM_MAYSHARE))
			pagecache_page = hugetlbfs_pagecache_page(h,
								vma, address);
	}

	spin_lock(&mm->page_table_lock);
	/* Check for a racing update before calling hugetlb_cow */
	if (unlikely(!pte_same(entry, huge_ptep_get(ptep))))
		goto out_page_table_lock;


	if (flags & FAULT_FLAG_WRITE) {
		if (!pte_write(entry)) {
			ret = hugetlb_cow(mm, vma, address, ptep, entry,
							pagecache_page);
			goto out_page_table_lock;
		}
		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (huge_ptep_set_access_flags(vma, address, ptep, entry,
						flags & FAULT_FLAG_WRITE))
		update_mmu_cache(vma, address, entry);

out_page_table_lock:
	spin_unlock(&mm->page_table_lock);

	if (pagecache_page) {
		unlock_page(pagecache_page);
		put_page(pagecache_page);
	}

out_mutex:
	mutex_unlock(&hugetlb_instantiation_mutex);

	return ret;
}

/* Can be overriden by architectures */
__attribute__((weak)) struct page *
follow_huge_pud(struct mm_struct *mm, unsigned long address,
	       pud_t *pud, int write)
{
	BUG();
	return NULL;
}

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			struct page **pages, struct vm_area_struct **vmas,
			unsigned long *position, int *length, int i,
			unsigned int flags)
{
	unsigned long pfn_offset;
	unsigned long vaddr = *position;
	int remainder = *length;
	struct hstate *h = hstate_vma(vma);

	spin_lock(&mm->page_table_lock);
	while (vaddr < vma->vm_end && remainder) {
		pte_t *pte;
		int absent;
		struct page *page;

		/*
		 * Some archs (sparc64, sh*) have multiple pte_ts to
		 * each hugepage.  We have to make sure we get the
		 * first, for the page indexing below to work.
		 */
		pte = huge_pte_offset(mm, vaddr & huge_page_mask(h));
		absent = !pte || huge_pte_none(huge_ptep_get(pte));

		/*
		 * When coredumping, it suits get_dump_page if we just return
		 * an error where there's an empty slot with no huge pagecache
		 * to back it.  This way, we avoid allocating a hugepage, and
		 * the sparse dumpfile avoids allocating disk blocks, but its
		 * huge holes still show up with zeroes where they need to be.
		 */
		if (absent && (flags & FOLL_DUMP) &&
		    !hugetlbfs_pagecache_present(h, vma, vaddr)) {
			remainder = 0;
			break;
		}

		if (absent ||
		    ((flags & FOLL_WRITE) && !pte_write(huge_ptep_get(pte)))) {
			int ret;

			spin_unlock(&mm->page_table_lock);
			ret = hugetlb_fault(mm, vma, vaddr,
				(flags & FOLL_WRITE) ? FAULT_FLAG_WRITE : 0);
			spin_lock(&mm->page_table_lock);
			if (!(ret & VM_FAULT_ERROR))
				continue;

			remainder = 0;
			break;
		}

		pfn_offset = (vaddr & ~huge_page_mask(h)) >> PAGE_SHIFT;
		page = pte_page(huge_ptep_get(pte));
same_page:
		if (pages) {
			pages[i] = mem_map_offset(page, pfn_offset);
			get_page(pages[i]);
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++pfn_offset;
		--remainder;
		++i;
		if (vaddr < vma->vm_end && remainder &&
				pfn_offset < pages_per_huge_page(h)) {
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

	return i ? i : -EFAULT;
}

void hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end, pgprot_t newprot)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long start = address;
	pte_t *ptep;
	pte_t pte;
	struct hstate *h = hstate_vma(vma);

	BUG_ON(address >= end);
	flush_cache_range(vma, address, end);

	spin_lock(&vma->vm_file->f_mapping->i_mmap_lock);
	spin_lock(&mm->page_table_lock);
	for (; address < end; address += huge_page_size(h)) {
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

int hugetlb_reserve_pages(struct inode *inode,
					long from, long to,
					struct vm_area_struct *vma,
					int acctflag)
{
	long ret, chg;
	struct hstate *h = hstate_inode(inode);

	/*
	 * Only apply hugepage reservation if asked. At fault time, an
	 * attempt will be made for VM_NORESERVE to allocate a page
	 * and filesystem quota without using reserves
	 */
	if (acctflag & VM_NORESERVE)
		return 0;

	/*
	 * Shared mappings base their reservation on the number of pages that
	 * are already allocated on behalf of the file. Private mappings need
	 * to reserve the full area even if read-only as mprotect() may be
	 * called to make the mapping read-write. Assume !vma is a shm mapping
	 */
	if (!vma || vma->vm_flags & VM_MAYSHARE)
		chg = region_chg(&inode->i_mapping->private_list, from, to);
	else {
		struct resv_map *resv_map = resv_map_alloc();
		if (!resv_map)
			return -ENOMEM;

		chg = to - from;

		set_vma_resv_map(vma, resv_map);
		set_vma_resv_flags(vma, HPAGE_RESV_OWNER);
	}

	if (chg < 0)
		return chg;

	/* There must be enough filesystem quota for the mapping */
	if (hugetlb_get_quota(inode->i_mapping, chg))
		return -ENOSPC;

	/*
	 * Check enough hugepages are available for the reservation.
	 * Hand back the quota if there are not
	 */
	ret = hugetlb_acct_memory(h, chg);
	if (ret < 0) {
		hugetlb_put_quota(inode->i_mapping, chg);
		return ret;
	}

	/*
	 * Account for the reservations made. Shared mappings record regions
	 * that have reservations as they are shared by multiple VMAs.
	 * When the last VMA disappears, the region map says how much
	 * the reservation was and the page cache tells how much of
	 * the reservation was consumed. Private mappings are per-VMA and
	 * only the consumed reservations are tracked. When the VMA
	 * disappears, the original reservation is the VMA size and the
	 * consumed reservations are stored in the map. Hence, nothing
	 * else has to be done for private mappings here
	 */
	if (!vma || vma->vm_flags & VM_MAYSHARE)
		region_add(&inode->i_mapping->private_list, from, to);
	return 0;
}

void hugetlb_unreserve_pages(struct inode *inode, long offset, long freed)
{
	struct hstate *h = hstate_inode(inode);
	long chg = region_truncate(&inode->i_mapping->private_list, offset);

	spin_lock(&inode->i_lock);
	inode->i_blocks -= (blocks_per_huge_page(h) * freed);
	spin_unlock(&inode->i_lock);

	hugetlb_put_quota(inode->i_mapping, (chg - freed));
	hugetlb_acct_memory(h, -(chg - freed));
}
