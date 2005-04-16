/*
 * Generic hugetlb support.
 * (C) William Irwin, April 2004
 */
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/nodemask.h>

const unsigned long hugetlb_zero = 0, hugetlb_infinity = ~0UL;
static unsigned long nr_huge_pages, free_huge_pages;
unsigned long max_huge_pages;
static struct list_head hugepage_freelists[MAX_NUMNODES];
static unsigned int nr_huge_pages_node[MAX_NUMNODES];
static unsigned int free_huge_pages_node[MAX_NUMNODES];
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
		nr_huge_pages++;
		nr_huge_pages_node[page_to_nid(page)]++;
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
EXPORT_SYMBOL(hugetlb_total_pages);

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

void zap_hugepage_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long length)
{
	struct mm_struct *mm = vma->vm_mm;

	spin_lock(&mm->page_table_lock);
	unmap_hugepage_range(vma, start, start + length);
	spin_unlock(&mm->page_table_lock);
}
