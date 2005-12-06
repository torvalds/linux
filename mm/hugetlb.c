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
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/hugetlb.h>

const unsigned long hugetlb_zero = 0, hugetlb_infinity = ~0UL;
static unsigned long nr_huge_pages, free_huge_pages;
unsigned long max_huge_pages;
static struct list_head hugepage_freelists[MAX_NUMNODES];
static unsigned int nr_huge_pages_node[MAX_NUMNODES];
static unsigned int free_huge_pages_node[MAX_NUMNODES];

/*
 * Protects updates to hugepage_freelists, nr_huge_pages, and free_huge_pages
 */
static DEFINE_SPINLOCK(hugetlb_lock);

static void enqueue_huge_page(struct page *page)
{
	int nid = page_to_nid(page);
	list_add(&page->lru, &hugepage_freelists[nid]);
	free_huge_pages++;
	free_huge_pages_node[nid]++;
}

static struct page *dequeue_huge_page(void)
{
	int nid = numa_node_id();
	struct page *page = NULL;

	if (list_empty(&hugepage_freelists[nid])) {
		for (nid = 0; nid < MAX_NUMNODES; ++nid)
			if (!list_empty(&hugepage_freelists[nid]))
				break;
	}
	if (nid >= 0 && nid < MAX_NUMNODES &&
	    !list_empty(&hugepage_freelists[nid])) {
		page = list_entry(hugepage_freelists[nid].next,
				  struct page, lru);
		list_del(&page->lru);
		free_huge_pages--;
		free_huge_pages_node[nid]--;
	}
	return page;
}

static struct page *alloc_fresh_huge_page(void)
{
	static int nid = 0;
	struct page *page;
	page = alloc_pages_node(nid, GFP_HIGHUSER|__GFP_COMP|__GFP_NOWARN,
					HUGETLB_PAGE_ORDER);
	nid = (nid + 1) % num_online_nodes();
	if (page) {
		spin_lock(&hugetlb_lock);
		nr_huge_pages++;
		nr_huge_pages_node[page_to_nid(page)]++;
		spin_unlock(&hugetlb_lock);
	}
	return page;
}

void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));

	INIT_LIST_HEAD(&page->lru);
	page[1].mapping = NULL;

	spin_lock(&hugetlb_lock);
	enqueue_huge_page(page);
	spin_unlock(&hugetlb_lock);
}

struct page *alloc_huge_page(void)
{
	struct page *page;
	int i;

	spin_lock(&hugetlb_lock);
	page = dequeue_huge_page();
	if (!page) {
		spin_unlock(&hugetlb_lock);
		return NULL;
	}
	spin_unlock(&hugetlb_lock);
	set_page_count(page, 1);
	page[1].mapping = (void *)free_huge_page;
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); ++i)
		clear_highpage(&page[i]);
	return page;
}

static int __init hugetlb_init(void)
{
	unsigned long i;
	struct page *page;

	if (HPAGE_SHIFT == 0)
		return 0;

	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&hugepage_freelists[i]);

	for (i = 0; i < max_huge_pages; ++i) {
		page = alloc_fresh_huge_page();
		if (!page)
			break;
		spin_lock(&hugetlb_lock);
		enqueue_huge_page(page);
		spin_unlock(&hugetlb_lock);
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
		set_page_count(&page[i], 0);
	}
	set_page_count(page, 1);
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
		struct page *page = alloc_fresh_huge_page();
		if (!page)
			return nr_huge_pages;
		spin_lock(&hugetlb_lock);
		enqueue_huge_page(page);
		spin_unlock(&hugetlb_lock);
	}
	if (count >= nr_huge_pages)
		return nr_huge_pages;

	spin_lock(&hugetlb_lock);
	try_to_free_low(count);
	while (count < nr_huge_pages) {
		struct page *page = dequeue_huge_page();
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
			"Hugepagesize:    %5lu kB\n",
			nr_huge_pages,
			free_huge_pages,
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

int is_hugepage_mem_enough(size_t size)
{
	return (size + ~HPAGE_MASK)/HPAGE_SIZE <= free_huge_pages;
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

static pte_t make_huge_pte(struct vm_area_struct *vma, struct page *page)
{
	pte_t entry;

	if (vma->vm_flags & VM_WRITE) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else {
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = pte_mkhuge(entry);

	return entry;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			    struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr;

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

static struct page *find_lock_huge_page(struct address_space *mapping,
			unsigned long idx)
{
	struct page *page;
	int err;
	struct inode *inode = mapping->host;
	unsigned long size;

retry:
	page = find_lock_page(mapping, idx);
	if (page)
		goto out;

	/* Check to make sure the mapping hasn't been truncated */
	size = i_size_read(inode) >> HPAGE_SHIFT;
	if (idx >= size)
		goto out;

	if (hugetlb_get_quota(mapping))
		goto out;
	page = alloc_huge_page();
	if (!page) {
		hugetlb_put_quota(mapping);
		goto out;
	}

	err = add_to_page_cache(page, mapping, idx, GFP_KERNEL);
	if (err) {
		put_page(page);
		hugetlb_put_quota(mapping);
		if (err == -EEXIST)
			goto retry;
		page = NULL;
	}
out:
	return page;
}

int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, int write_access)
{
	int ret = VM_FAULT_SIGBUS;
	unsigned long idx;
	unsigned long size;
	pte_t *pte;
	struct page *page;
	struct address_space *mapping;

	pte = huge_pte_alloc(mm, address);
	if (!pte)
		goto out;

	mapping = vma->vm_file->f_mapping;
	idx = ((address - vma->vm_start) >> HPAGE_SHIFT)
		+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
	page = find_lock_huge_page(mapping, idx);
	if (!page)
		goto out;

	spin_lock(&mm->page_table_lock);
	size = i_size_read(mapping->host) >> HPAGE_SHIFT;
	if (idx >= size)
		goto backout;

	ret = VM_FAULT_MINOR;
	if (!pte_none(*pte))
		goto backout;

	add_mm_counter(mm, file_rss, HPAGE_SIZE / PAGE_SIZE);
	set_huge_pte_at(mm, address, pte, make_huge_pte(vma, page));
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

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			struct page **pages, struct vm_area_struct **vmas,
			unsigned long *position, int *length, int i)
{
	unsigned long vpfn, vaddr = *position;
	int remainder = *length;

	vpfn = vaddr/PAGE_SIZE;
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

		if (pages) {
			page = &pte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];
			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++vpfn;
		--remainder;
		++i;
	}
	spin_unlock(&mm->page_table_lock);
	*length = remainder;
	*position = vaddr;

	return i;
}
