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
static unsigned long nr_huge_pages, free_huge_pages, reserved_huge_pages;
unsigned long max_huge_pages;
static struct list_head hugepage_freelists[MAX_NUMNODES];
static unsigned int nr_huge_pages_node[MAX_NUMNODES];
static unsigned int free_huge_pages_node[MAX_NUMNODES];
/*
 * Protects updates to hugepage_freelists, nr_huge_pages, and free_huge_pages
 */
static DEFINE_SPINLOCK(hugetlb_lock);

static void clear_huge_page(struct page *page, unsigned long addr)
{
	int i;

	might_sleep();
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); i++) {
		cond_resched();
		clear_user_highpage(page + i, addr);
	}
}

static void copy_huge_page(struct page *dst, struct page *src,
			   unsigned long addr)
{
	int i;

	might_sleep();
	for (i = 0; i < HPAGE_SIZE/PAGE_SIZE; i++) {
		cond_resched();
		copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE);
	}
}

static void enqueue_huge_page(struct page *page)
{
	int nid = page_to_nid(page);
	list_add(&page->lru, &hugepage_freelists[nid]);
	free_huge_pages++;
	free_huge_pages_node[nid]++;
}

static struct page *dequeue_huge_page(struct vm_area_struct *vma,
				unsigned long address)
{
	int nid = numa_node_id();
	struct page *page = NULL;
	struct zonelist *zonelist = huge_zonelist(vma, address);
	struct zone **z;

	for (z = zonelist->zones; *z; z++) {
		nid = (*z)->zone_pgdat->node_id;
		if (cpuset_zone_allowed(*z, GFP_HIGHUSER) &&
		    !list_empty(&hugepage_freelists[nid]))
			break;
	}

	if (*z) {
		page = list_entry(hugepage_freelists[nid].next,
				  struct page, lru);
		list_del(&page->lru);
		free_huge_pages--;
		free_huge_pages_node[nid]--;
	}
	return page;
}

static void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));

	INIT_LIST_HEAD(&page->lru);

	spin_lock(&hugetlb_lock);
	enqueue_huge_page(page);
	spin_unlock(&hugetlb_lock);
}

static int alloc_fresh_huge_page(void)
{
	static int nid = 0;
	struct page *page;
	page = alloc_pages_node(nid, GFP_HIGHUSER|__GFP_COMP|__GFP_NOWARN,
					HUGETLB_PAGE_ORDER);
	nid = next_node(nid, node_online_map);
	if (nid == MAX_NUMNODES)
		nid = first_node(node_online_map);
	if (page) {
		page[1].lru.next = (void *)free_huge_page;	/* dtor */
		spin_lock(&hugetlb_lock);
		nr_huge_pages++;
		nr_huge_pages_node[page_to_nid(page)]++;
		spin_unlock(&hugetlb_lock);
		put_page(page); /* free it into the hugepage allocator */
		return 1;
	}
	return 0;
}

static struct page *alloc_huge_page(struct vm_area_struct *vma,
				    unsigned long addr)
{
	struct inode *inode = vma->vm_file->f_dentry->d_inode;
	struct page *page;
	int use_reserve = 0;
	unsigned long idx;

	spin_lock(&hugetlb_lock);

	if (vma->vm_flags & VM_MAYSHARE) {

		/* idx = radix tree index, i.e. offset into file in
		 * HPAGE_SIZE units */
		idx = ((addr - vma->vm_start) >> HPAGE_SHIFT)
			+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));

		/* The hugetlbfs specific inode info stores the number
		 * of "guaranteed available" (huge) pages.  That is,
		 * the first 'prereserved_hpages' pages of the inode
		 * are either already instantiated, or have been
		 * pre-reserved (by hugetlb_reserve_for_inode()). Here
		 * we're in the process of instantiating the page, so
		 * we use this to determine whether to draw from the
		 * pre-reserved pool or the truly free pool. */
		if (idx < HUGETLBFS_I(inode)->prereserved_hpages)
			use_reserve = 1;
	}

	if (!use_reserve) {
		if (free_huge_pages <= reserved_huge_pages)
			goto fail;
	} else {
		BUG_ON(reserved_huge_pages == 0);
		reserved_huge_pages--;
	}

	page = dequeue_huge_page(vma, addr);
	if (!page)
		goto fail;

	spin_unlock(&hugetlb_lock);
	set_page_refcounted(page);
	return page;

 fail:
	WARN_ON(use_reserve); /* reserved allocations shouldn't fail */
	spin_unlock(&hugetlb_lock);
	return NULL;
}

/* hugetlb_extend_reservation()
 *
 * Ensure that at least 'atleast' hugepages are, and will remain,
 * available to instantiate the first 'atleast' pages of the given
 * inode.  If the inode doesn't already have this many pages reserved
 * or instantiated, set aside some hugepages in the reserved pool to
 * satisfy later faults (or fail now if there aren't enough, rather
 * than getting the SIGBUS later).
 */
int hugetlb_extend_reservation(struct hugetlbfs_inode_info *info,
			       unsigned long atleast)
{
	struct inode *inode = &info->vfs_inode;
	unsigned long change_in_reserve = 0;
	int ret = 0;

	spin_lock(&hugetlb_lock);
	read_lock_irq(&inode->i_mapping->tree_lock);

	if (info->prereserved_hpages >= atleast)
		goto out;

	/* Because we always call this on shared mappings, none of the
	 * pages beyond info->prereserved_hpages can have been
	 * instantiated, so we need to reserve all of them now. */
	change_in_reserve = atleast - info->prereserved_hpages;

	if ((reserved_huge_pages + change_in_reserve) > free_huge_pages) {
		ret = -ENOMEM;
		goto out;
	}

	reserved_huge_pages += change_in_reserve;
	info->prereserved_hpages = atleast;

 out:
	read_unlock_irq(&inode->i_mapping->tree_lock);
	spin_unlock(&hugetlb_lock);

	return ret;
}

/* hugetlb_truncate_reservation()
 *
 * This returns pages reserved for the given inode to the general free
 * hugepage pool.  If the inode has any pages prereserved, but not
 * instantiated, beyond offset (atmost << HPAGE_SIZE), then release
 * them.
 */
void hugetlb_truncate_reservation(struct hugetlbfs_inode_info *info,
				  unsigned long atmost)
{
	struct inode *inode = &info->vfs_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned long idx;
	unsigned long change_in_reserve = 0;
	struct page *page;

	spin_lock(&hugetlb_lock);
	read_lock_irq(&inode->i_mapping->tree_lock);

	if (info->prereserved_hpages <= atmost)
		goto out;

	/* Count pages which were reserved, but not instantiated, and
	 * which we can now release. */
	for (idx = atmost; idx < info->prereserved_hpages; idx++) {
		page = radix_tree_lookup(&mapping->page_tree, idx);
		if (!page)
			/* Pages which are already instantiated can't
			 * be unreserved (and in fact have already
			 * been removed from the reserved pool) */
			change_in_reserve++;
	}

	BUG_ON(reserved_huge_pages < change_in_reserve);
	reserved_huge_pages -= change_in_reserve;
	info->prereserved_hpages = atmost;

 out:
	read_unlock_irq(&inode->i_mapping->tree_lock);
	spin_unlock(&hugetlb_lock);
}

static int __init hugetlb_init(void)
{
	unsigned long i;

	if (HPAGE_SHIFT == 0)
		return 0;

	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&hugepage_freelists[i]);

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

#ifdef CONFIG_SYSCTL
static void update_and_free_page(struct page *page)
{
	int i;
	nr_huge_pages--;
	nr_huge_pages_node[page_zone(page)->zone_pgdat->node_id]--;
	for (i = 0; i < (HPAGE_SIZE / PAGE_SIZE); i++) {
		page[i].flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
				1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback);
	}
	page[1].lru.next = NULL;
	set_page_refcounted(page);
	__free_pages(page, HUGETLB_PAGE_ORDER);
}

#ifdef CONFIG_HIGHMEM
static void try_to_free_low(unsigned long count)
{
	int i, nid;
	for (i = 0; i < MAX_NUMNODES; ++i) {
		struct page *page, *next;
		list_for_each_entry_safe(page, next, &hugepage_freelists[i], lru) {
			if (PageHighMem(page))
				continue;
			list_del(&page->lru);
			update_and_free_page(page);
			nid = page_zone(page)->zone_pgdat->node_id;
			free_huge_pages--;
			free_huge_pages_node[nid]--;
			if (count >= nr_huge_pages)
				return;
		}
	}
}
#else
static inline void try_to_free_low(unsigned long count)
{
}
#endif

static unsigned long set_max_huge_pages(unsigned long count)
{
	while (count > nr_huge_pages) {
		if (!alloc_fresh_huge_page())
			return nr_huge_pages;
	}
	if (count >= nr_huge_pages)
		return nr_huge_pages;

	spin_lock(&hugetlb_lock);
	try_to_free_low(count);
	while (count < nr_huge_pages) {
		struct page *page = dequeue_huge_page(NULL, 0);
		if (!page)
			break;
		update_and_free_page(page);
	}
	spin_unlock(&hugetlb_lock);
	return nr_huge_pages;
}

int hugetlb_sysctl_handler(struct ctl_table *table, int write,
			   struct file *file, void __user *buffer,
			   size_t *length, loff_t *ppos)
{
	proc_doulongvec_minmax(table, write, file, buffer, length, ppos);
	max_huge_pages = set_max_huge_pages(max_huge_pages);
	return 0;
}
#endif /* CONFIG_SYSCTL */

int hugetlb_report_meminfo(char *buf)
{
	return sprintf(buf,
			"HugePages_Total: %5lu\n"
			"HugePages_Free:  %5lu\n"
		        "HugePages_Rsvd:  %5lu\n"
			"Hugepagesize:    %5lu kB\n",
			nr_huge_pages,
			free_huge_pages,
		        reserved_huge_pages,
			HPAGE_SIZE/1024);
}

int hugetlb_report_node_meminfo(int nid, char *buf)
{
	return sprintf(buf,
		"Node %d HugePages_Total: %5u\n"
		"Node %d HugePages_Free:  %5u\n",
		nid, nr_huge_pages_node[nid],
		nid, free_huge_pages_node[nid]);
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	return nr_huge_pages * (HPAGE_SIZE / PAGE_SIZE);
}

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static struct page *hugetlb_nopage(struct vm_area_struct *vma,
				unsigned long address, int *unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
};

static pte_t make_huge_pte(struct vm_area_struct *vma, struct page *page,
				int writable)
{
	pte_t entry;

	if (writable) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else {
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = pte_mkhuge(entry);

	return entry;
}

static void set_huge_ptep_writable(struct vm_area_struct *vma,
				   unsigned long address, pte_t *ptep)
{
	pte_t entry;

	entry = pte_mkwrite(pte_mkdirty(*ptep));
	ptep_set_access_flags(vma, address, ptep, entry, 1);
	update_mmu_cache(vma, address, entry);
	lazy_mmu_prot_update(entry);
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
		spin_lock(&dst->page_table_lock);
		spin_lock(&src->page_table_lock);
		if (!pte_none(*src_pte)) {
			if (cow)
				ptep_set_wrprotect(src, addr, src_pte);
			entry = *src_pte;
			ptepage = pte_page(entry);
			get_page(ptepage);
			add_mm_counter(dst, file_rss, HPAGE_SIZE / PAGE_SIZE);
			set_huge_pte_at(dst, addr, dst_pte, entry);
		}
		spin_unlock(&src->page_table_lock);
		spin_unlock(&dst->page_table_lock);
	}
	return 0;

nomem:
	return -ENOMEM;
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *ptep;
	pte_t pte;
	struct page *page;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON(start & ~HPAGE_MASK);
	BUG_ON(end & ~HPAGE_MASK);

	spin_lock(&mm->page_table_lock);

	/* Update high watermark before we lower rss */
	update_hiwater_rss(mm);

	for (address = start; address < end; address += HPAGE_SIZE) {
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;

		pte = huge_ptep_get_and_clear(mm, address, ptep);
		if (pte_none(pte))
			continue;

		page = pte_page(pte);
		put_page(page);
		add_mm_counter(mm, file_rss, (int) -(HPAGE_SIZE / PAGE_SIZE));
	}

	spin_unlock(&mm->page_table_lock);
	flush_tlb_range(vma, start, end);
}

static int hugetlb_cow(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, pte_t pte)
{
	struct page *old_page, *new_page;
	int avoidcopy;

	old_page = pte_page(pte);

	/* If no-one else is actually using this page, avoid the copy
	 * and just make the page writable */
	avoidcopy = (page_count(old_page) == 1);
	if (avoidcopy) {
		set_huge_ptep_writable(vma, address, ptep);
		return VM_FAULT_MINOR;
	}

	page_cache_get(old_page);
	new_page = alloc_huge_page(vma, address);

	if (!new_page) {
		page_cache_release(old_page);
		return VM_FAULT_OOM;
	}

	spin_unlock(&mm->page_table_lock);
	copy_huge_page(new_page, old_page, address);
	spin_lock(&mm->page_table_lock);

	ptep = huge_pte_offset(mm, address & HPAGE_MASK);
	if (likely(pte_same(*ptep, pte))) {
		/* Break COW */
		set_huge_pte_at(mm, address, ptep,
				make_huge_pte(vma, new_page, 1));
		/* Make the old page be freed below */
		new_page = old_page;
	}
	page_cache_release(new_page);
	page_cache_release(old_page);
	return VM_FAULT_MINOR;
}

int hugetlb_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep, int write_access)
{
	int ret = VM_FAULT_SIGBUS;
	unsigned long idx;
	unsigned long size;
	struct page *page;
	struct address_space *mapping;
	pte_t new_pte;

	mapping = vma->vm_file->f_mapping;
	idx = ((address - vma->vm_start) >> HPAGE_SHIFT)
		+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
retry:
	page = find_lock_page(mapping, idx);
	if (!page) {
		if (hugetlb_get_quota(mapping))
			goto out;
		page = alloc_huge_page(vma, address);
		if (!page) {
			hugetlb_put_quota(mapping);
			ret = VM_FAULT_OOM;
			goto out;
		}
		clear_huge_page(page, address);

		if (vma->vm_flags & VM_SHARED) {
			int err;

			err = add_to_page_cache(page, mapping, idx, GFP_KERNEL);
			if (err) {
				put_page(page);
				hugetlb_put_quota(mapping);
				if (err == -EEXIST)
					goto retry;
				goto out;
			}
		} else
			lock_page(page);
	}

	spin_lock(&mm->page_table_lock);
	size = i_size_read(mapping->host) >> HPAGE_SHIFT;
	if (idx >= size)
		goto backout;

	ret = VM_FAULT_MINOR;
	if (!pte_none(*ptep))
		goto backout;

	add_mm_counter(mm, file_rss, HPAGE_SIZE / PAGE_SIZE);
	new_pte = make_huge_pte(vma, page, ((vma->vm_flags & VM_WRITE)
				&& (vma->vm_flags & VM_SHARED)));
	set_huge_pte_at(mm, address, ptep, new_pte);

	if (write_access && !(vma->vm_flags & VM_SHARED)) {
		/* Optimization, do the COW without a second fault */
		ret = hugetlb_cow(mm, vma, address, ptep, new_pte);
	}

	spin_unlock(&mm->page_table_lock);
	unlock_page(page);
out:
	return ret;

backout:
	spin_unlock(&mm->page_table_lock);
	hugetlb_put_quota(mapping);
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
	entry = *ptep;
	if (pte_none(entry)) {
		ret = hugetlb_no_page(mm, vma, address, ptep, write_access);
		mutex_unlock(&hugetlb_instantiation_mutex);
		return ret;
	}

	ret = VM_FAULT_MINOR;

	spin_lock(&mm->page_table_lock);
	/* Check for a racing update before calling hugetlb_cow */
	if (likely(pte_same(entry, *ptep)))
		if (write_access && !pte_write(entry))
			ret = hugetlb_cow(mm, vma, address, ptep, entry);
	spin_unlock(&mm->page_table_lock);
	mutex_unlock(&hugetlb_instantiation_mutex);

	return ret;
}

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			struct page **pages, struct vm_area_struct **vmas,
			unsigned long *position, int *length, int i)
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

		if (!pte || pte_none(*pte)) {
			int ret;

			spin_unlock(&mm->page_table_lock);
			ret = hugetlb_fault(mm, vma, vaddr, 0);
			spin_lock(&mm->page_table_lock);
			if (ret == VM_FAULT_MINOR)
				continue;

			remainder = 0;
			if (!i)
				i = -EFAULT;
			break;
		}

		pfn_offset = (vaddr & ~HPAGE_MASK) >> PAGE_SHIFT;
		page = pte_page(*pte);
same_page:
		get_page(page);
		if (pages)
			pages[i] = page + pfn_offset;

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

	spin_lock(&mm->page_table_lock);
	for (; address < end; address += HPAGE_SIZE) {
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;
		if (!pte_none(*ptep)) {
			pte = huge_ptep_get_and_clear(mm, address, ptep);
			pte = pte_mkhuge(pte_modify(pte, newprot));
			set_huge_pte_at(mm, address, ptep, pte);
			lazy_mmu_prot_update(pte);
		}
	}
	spin_unlock(&mm->page_table_lock);

	flush_tlb_range(vma, start, end);
}

