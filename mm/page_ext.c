// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/page_ext.h>
#include <linux/memory.h>
#include <linux/vmalloc.h>
#include <linux/kmemleak.h>
#include <linux/page_owner.h>
#include <linux/page_idle.h>

/*
 * struct page extension
 *
 * This is the feature to manage memory for extended data per page.
 *
 * Until now, we must modify struct page itself to store extra data per page.
 * This requires rebuilding the kernel and it is really time consuming process.
 * And, sometimes, rebuild is impossible due to third party module dependency.
 * At last, enlarging struct page could cause un-wanted system behaviour change.
 *
 * This feature is intended to overcome above mentioned problems. This feature
 * allocates memory for extended data per page in certain place rather than
 * the struct page itself. This memory can be accessed by the accessor
 * functions provided by this code. During the boot process, it checks whether
 * allocation of huge chunk of memory is needed or not. If not, it avoids
 * allocating memory at all. With this advantage, we can include this feature
 * into the kernel in default and can avoid rebuild and solve related problems.
 *
 * To help these things to work well, there are two callbacks for clients. One
 * is the need callback which is mandatory if user wants to avoid useless
 * memory allocation at boot-time. The other is optional, init callback, which
 * is used to do proper initialization after memory is allocated.
 *
 * The need callback is used to decide whether extended memory allocation is
 * needed or not. Sometimes users want to deactivate some features in this
 * boot and extra memory would be unneccessary. In this case, to avoid
 * allocating huge chunk of memory, each clients represent their need of
 * extra memory through the need callback. If one of the need callbacks
 * returns true, it means that someone needs extra memory so that
 * page extension core should allocates memory for page extension. If
 * none of need callbacks return true, memory isn't needed at all in this boot
 * and page extension core can skip to allocate memory. As result,
 * none of memory is wasted.
 *
 * When need callback returns true, page_ext checks if there is a request for
 * extra memory through size in struct page_ext_operations. If it is non-zero,
 * extra space is allocated for each page_ext entry and offset is returned to
 * user through offset in struct page_ext_operations.
 *
 * The init callback is used to do proper initialization after page extension
 * is completely initialized. In sparse memory system, extra memory is
 * allocated some time later than memmap is allocated. In other words, lifetime
 * of memory for page extension isn't same with memmap for struct page.
 * Therefore, clients can't store extra data until page extension is
 * initialized, even if pages are allocated and used freely. This could
 * cause inadequate state of extra data per page, so, to prevent it, client
 * can utilize this callback to initialize the state of it correctly.
 */

static struct page_ext_operations *page_ext_ops[] = {
#ifdef CONFIG_DEBUG_PAGEALLOC
	&debug_guardpage_ops,
#endif
#ifdef CONFIG_PAGE_OWNER
	&page_owner_ops,
#endif
#if defined(CONFIG_IDLE_PAGE_TRACKING) && !defined(CONFIG_64BIT)
	&page_idle_ops,
#endif
};

static unsigned long total_usage;
static unsigned long extra_mem;

static bool __init invoke_need_callbacks(void)
{
	int i;
	int entries = ARRAY_SIZE(page_ext_ops);
	bool need = false;

	for (i = 0; i < entries; i++) {
		if (page_ext_ops[i]->need && page_ext_ops[i]->need()) {
			page_ext_ops[i]->offset = sizeof(struct page_ext) +
						extra_mem;
			extra_mem += page_ext_ops[i]->size;
			need = true;
		}
	}

	return need;
}

static void __init invoke_init_callbacks(void)
{
	int i;
	int entries = ARRAY_SIZE(page_ext_ops);

	for (i = 0; i < entries; i++) {
		if (page_ext_ops[i]->init)
			page_ext_ops[i]->init();
	}
}

static unsigned long get_entry_size(void)
{
	return sizeof(struct page_ext) + extra_mem;
}

static inline struct page_ext *get_entry(void *base, unsigned long index)
{
	return base + get_entry_size() * index;
}

#if !defined(CONFIG_SPARSEMEM)


void __meminit pgdat_page_ext_init(struct pglist_data *pgdat)
{
	pgdat->node_page_ext = NULL;
}

struct page_ext *lookup_page_ext(const struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	unsigned long index;
	struct page_ext *base;

	base = NODE_DATA(page_to_nid(page))->node_page_ext;
	/*
	 * The sanity checks the page allocator does upon freeing a
	 * page can reach here before the page_ext arrays are
	 * allocated when feeding a range of pages to the allocator
	 * for the first time during bootup or memory hotplug.
	 */
	if (unlikely(!base))
		return NULL;
	index = pfn - round_down(node_start_pfn(page_to_nid(page)),
					MAX_ORDER_NR_PAGES);
	return get_entry(base, index);
}

static int __init alloc_node_page_ext(int nid)
{
	struct page_ext *base;
	unsigned long table_size;
	unsigned long nr_pages;

	nr_pages = NODE_DATA(nid)->node_spanned_pages;
	if (!nr_pages)
		return 0;

	/*
	 * Need extra space if node range is not aligned with
	 * MAX_ORDER_NR_PAGES. When page allocator's buddy algorithm
	 * checks buddy's status, range could be out of exact node range.
	 */
	if (!IS_ALIGNED(node_start_pfn(nid), MAX_ORDER_NR_PAGES) ||
		!IS_ALIGNED(node_end_pfn(nid), MAX_ORDER_NR_PAGES))
		nr_pages += MAX_ORDER_NR_PAGES;

	table_size = get_entry_size() * nr_pages;

	base = memblock_alloc_try_nid(
			table_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
			MEMBLOCK_ALLOC_ACCESSIBLE, nid);
	if (!base)
		return -ENOMEM;
	NODE_DATA(nid)->node_page_ext = base;
	total_usage += table_size;
	return 0;
}

void __init page_ext_init_flatmem(void)
{

	int nid, fail;

	if (!invoke_need_callbacks())
		return;

	for_each_online_node(nid)  {
		fail = alloc_node_page_ext(nid);
		if (fail)
			goto fail;
	}
	pr_info("allocated %ld bytes of page_ext\n", total_usage);
	invoke_init_callbacks();
	return;

fail:
	pr_crit("allocation of page_ext failed.\n");
	panic("Out of memory");
}

#else /* CONFIG_FLAT_NODE_MEM_MAP */

struct page_ext *lookup_page_ext(const struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	struct mem_section *section = __pfn_to_section(pfn);
	/*
	 * The sanity checks the page allocator does upon freeing a
	 * page can reach here before the page_ext arrays are
	 * allocated when feeding a range of pages to the allocator
	 * for the first time during bootup or memory hotplug.
	 */
	if (!section->page_ext)
		return NULL;
	return get_entry(section->page_ext, pfn);
}

static void *__meminit alloc_page_ext(size_t size, int nid)
{
	gfp_t flags = GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN;
	void *addr = NULL;

	addr = alloc_pages_exact_nid(nid, size, flags);
	if (addr) {
		kmemleak_alloc(addr, size, 1, flags);
		return addr;
	}

	addr = vzalloc_node(size, nid);

	return addr;
}

static int __meminit init_section_page_ext(unsigned long pfn, int nid)
{
	struct mem_section *section;
	struct page_ext *base;
	unsigned long table_size;

	section = __pfn_to_section(pfn);

	if (section->page_ext)
		return 0;

	table_size = get_entry_size() * PAGES_PER_SECTION;
	base = alloc_page_ext(table_size, nid);

	/*
	 * The value stored in section->page_ext is (base - pfn)
	 * and it does not point to the memory block allocated above,
	 * causing kmemleak false positives.
	 */
	kmemleak_not_leak(base);

	if (!base) {
		pr_err("page ext allocation failure\n");
		return -ENOMEM;
	}

	/*
	 * The passed "pfn" may not be aligned to SECTION.  For the calculation
	 * we need to apply a mask.
	 */
	pfn &= PAGE_SECTION_MASK;
	section->page_ext = (void *)base - get_entry_size() * pfn;
	total_usage += table_size;
	return 0;
}
#ifdef CONFIG_MEMORY_HOTPLUG
static void free_page_ext(void *addr)
{
	if (is_vmalloc_addr(addr)) {
		vfree(addr);
	} else {
		struct page *page = virt_to_page(addr);
		size_t table_size;

		table_size = get_entry_size() * PAGES_PER_SECTION;

		BUG_ON(PageReserved(page));
		kmemleak_free(addr);
		free_pages_exact(addr, table_size);
	}
}

static void __free_page_ext(unsigned long pfn)
{
	struct mem_section *ms;
	struct page_ext *base;

	ms = __pfn_to_section(pfn);
	if (!ms || !ms->page_ext)
		return;
	base = get_entry(ms->page_ext, pfn);
	free_page_ext(base);
	ms->page_ext = NULL;
}

static int __meminit online_page_ext(unsigned long start_pfn,
				unsigned long nr_pages,
				int nid)
{
	unsigned long start, end, pfn;
	int fail = 0;

	start = SECTION_ALIGN_DOWN(start_pfn);
	end = SECTION_ALIGN_UP(start_pfn + nr_pages);

	if (nid == NUMA_NO_NODE) {
		/*
		 * In this case, "nid" already exists and contains valid memory.
		 * "start_pfn" passed to us is a pfn which is an arg for
		 * online__pages(), and start_pfn should exist.
		 */
		nid = pfn_to_nid(start_pfn);
		VM_BUG_ON(!node_state(nid, N_ONLINE));
	}

	for (pfn = start; !fail && pfn < end; pfn += PAGES_PER_SECTION) {
		if (!pfn_present(pfn))
			continue;
		fail = init_section_page_ext(pfn, nid);
	}
	if (!fail)
		return 0;

	/* rollback */
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION)
		__free_page_ext(pfn);

	return -ENOMEM;
}

static int __meminit offline_page_ext(unsigned long start_pfn,
				unsigned long nr_pages, int nid)
{
	unsigned long start, end, pfn;

	start = SECTION_ALIGN_DOWN(start_pfn);
	end = SECTION_ALIGN_UP(start_pfn + nr_pages);

	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION)
		__free_page_ext(pfn);
	return 0;

}

static int __meminit page_ext_callback(struct notifier_block *self,
			       unsigned long action, void *arg)
{
	struct memory_notify *mn = arg;
	int ret = 0;

	switch (action) {
	case MEM_GOING_ONLINE:
		ret = online_page_ext(mn->start_pfn,
				   mn->nr_pages, mn->status_change_nid);
		break;
	case MEM_OFFLINE:
		offline_page_ext(mn->start_pfn,
				mn->nr_pages, mn->status_change_nid);
		break;
	case MEM_CANCEL_ONLINE:
		offline_page_ext(mn->start_pfn,
				mn->nr_pages, mn->status_change_nid);
		break;
	case MEM_GOING_OFFLINE:
		break;
	case MEM_ONLINE:
	case MEM_CANCEL_OFFLINE:
		break;
	}

	return notifier_from_errno(ret);
}

#endif

void __init page_ext_init(void)
{
	unsigned long pfn;
	int nid;

	if (!invoke_need_callbacks())
		return;

	for_each_node_state(nid, N_MEMORY) {
		unsigned long start_pfn, end_pfn;

		start_pfn = node_start_pfn(nid);
		end_pfn = node_end_pfn(nid);
		/*
		 * start_pfn and end_pfn may not be aligned to SECTION and the
		 * page->flags of out of node pages are not initialized.  So we
		 * scan [start_pfn, the biggest section's pfn < end_pfn) here.
		 */
		for (pfn = start_pfn; pfn < end_pfn;
			pfn = ALIGN(pfn + 1, PAGES_PER_SECTION)) {

			if (!pfn_valid(pfn))
				continue;
			/*
			 * Nodes's pfns can be overlapping.
			 * We know some arch can have a nodes layout such as
			 * -------------pfn-------------->
			 * N0 | N1 | N2 | N0 | N1 | N2|....
			 */
			if (pfn_to_nid(pfn) != nid)
				continue;
			if (init_section_page_ext(pfn, nid))
				goto oom;
			cond_resched();
		}
	}
	hotplug_memory_notifier(page_ext_callback, 0);
	pr_info("allocated %ld bytes of page_ext\n", total_usage);
	invoke_init_callbacks();
	return;

oom:
	panic("Out of memory");
}

void __meminit pgdat_page_ext_init(struct pglist_data *pgdat)
{
}

#endif
