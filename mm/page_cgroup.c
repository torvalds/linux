#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/bit_spinlock.h>
#include <linux/page_cgroup.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <linux/vmalloc.h>
#include <linux/cgroup.h>

static void __meminit
__init_page_cgroup(struct page_cgroup *pc, unsigned long pfn)
{
	pc->flags = 0;
	pc->mem_cgroup = NULL;
	pc->page = pfn_to_page(pfn);
}
static unsigned long total_usage;

#if !defined(CONFIG_SPARSEMEM)


void __init pgdat_page_cgroup_init(struct pglist_data *pgdat)
{
	pgdat->node_page_cgroup = NULL;
}

struct page_cgroup *lookup_page_cgroup(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	unsigned long offset;
	struct page_cgroup *base;

	base = NODE_DATA(page_to_nid(page))->node_page_cgroup;
	if (unlikely(!base))
		return NULL;

	offset = pfn - NODE_DATA(page_to_nid(page))->node_start_pfn;
	return base + offset;
}

static int __init alloc_node_page_cgroup(int nid)
{
	struct page_cgroup *base, *pc;
	unsigned long table_size;
	unsigned long start_pfn, nr_pages, index;

	start_pfn = NODE_DATA(nid)->node_start_pfn;
	nr_pages = NODE_DATA(nid)->node_spanned_pages;

	table_size = sizeof(struct page_cgroup) * nr_pages;

	base = __alloc_bootmem_node_nopanic(NODE_DATA(nid),
			table_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	if (!base)
		return -ENOMEM;
	for (index = 0; index < nr_pages; index++) {
		pc = base + index;
		__init_page_cgroup(pc, start_pfn + index);
	}
	NODE_DATA(nid)->node_page_cgroup = base;
	total_usage += table_size;
	return 0;
}

void __init page_cgroup_init(void)
{

	int nid, fail;

	if (mem_cgroup_subsys.disabled)
		return;

	for_each_online_node(nid)  {
		fail = alloc_node_page_cgroup(nid);
		if (fail)
			goto fail;
	}
	printk(KERN_INFO "allocated %ld bytes of page_cgroup\n", total_usage);
	printk(KERN_INFO "please try cgroup_disable=memory option if you"
	" don't want\n");
	return;
fail:
	printk(KERN_CRIT "allocation of page_cgroup was failed.\n");
	printk(KERN_CRIT "please try cgroup_disable=memory boot option\n");
	panic("Out of memory");
}

#else /* CONFIG_FLAT_NODE_MEM_MAP */

struct page_cgroup *lookup_page_cgroup(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	struct mem_section *section = __pfn_to_section(pfn);

	return section->page_cgroup + pfn;
}

int __meminit init_section_page_cgroup(unsigned long pfn)
{
	struct mem_section *section;
	struct page_cgroup *base, *pc;
	unsigned long table_size;
	int nid, index;

	section = __pfn_to_section(pfn);

	if (section->page_cgroup)
		return 0;

	nid = page_to_nid(pfn_to_page(pfn));

	table_size = sizeof(struct page_cgroup) * PAGES_PER_SECTION;
	if (slab_is_available()) {
		base = kmalloc_node(table_size, GFP_KERNEL, nid);
		if (!base)
			base = vmalloc_node(table_size, nid);
	} else {
		base = __alloc_bootmem_node_nopanic(NODE_DATA(nid), table_size,
				PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	}

	if (!base) {
		printk(KERN_ERR "page cgroup allocation failure\n");
		return -ENOMEM;
	}

	for (index = 0; index < PAGES_PER_SECTION; index++) {
		pc = base + index;
		__init_page_cgroup(pc, pfn + index);
	}

	section = __pfn_to_section(pfn);
	section->page_cgroup = base - pfn;
	total_usage += table_size;
	return 0;
}
#ifdef CONFIG_MEMORY_HOTPLUG
void __free_page_cgroup(unsigned long pfn)
{
	struct mem_section *ms;
	struct page_cgroup *base;

	ms = __pfn_to_section(pfn);
	if (!ms || !ms->page_cgroup)
		return;
	base = ms->page_cgroup + pfn;
	if (is_vmalloc_addr(base)) {
		vfree(base);
		ms->page_cgroup = NULL;
	} else {
		struct page *page = virt_to_page(base);
		if (!PageReserved(page)) { /* Is bootmem ? */
			kfree(base);
			ms->page_cgroup = NULL;
		}
	}
}

int online_page_cgroup(unsigned long start_pfn,
			unsigned long nr_pages,
			int nid)
{
	unsigned long start, end, pfn;
	int fail = 0;

	start = start_pfn & ~(PAGES_PER_SECTION - 1);
	end = ALIGN(start_pfn + nr_pages, PAGES_PER_SECTION);

	for (pfn = start; !fail && pfn < end; pfn += PAGES_PER_SECTION) {
		if (!pfn_present(pfn))
			continue;
		fail = init_section_page_cgroup(pfn);
	}
	if (!fail)
		return 0;

	/* rollback */
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION)
		__free_page_cgroup(pfn);

	return -ENOMEM;
}

int offline_page_cgroup(unsigned long start_pfn,
		unsigned long nr_pages, int nid)
{
	unsigned long start, end, pfn;

	start = start_pfn & ~(PAGES_PER_SECTION - 1);
	end = ALIGN(start_pfn + nr_pages, PAGES_PER_SECTION);

	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION)
		__free_page_cgroup(pfn);
	return 0;

}

static int page_cgroup_callback(struct notifier_block *self,
			       unsigned long action, void *arg)
{
	struct memory_notify *mn = arg;
	int ret = 0;
	switch (action) {
	case MEM_GOING_ONLINE:
		ret = online_page_cgroup(mn->start_pfn,
				   mn->nr_pages, mn->status_change_nid);
		break;
	case MEM_CANCEL_ONLINE:
	case MEM_OFFLINE:
		offline_page_cgroup(mn->start_pfn,
				mn->nr_pages, mn->status_change_nid);
		break;
	case MEM_GOING_OFFLINE:
		break;
	case MEM_ONLINE:
	case MEM_CANCEL_OFFLINE:
		break;
	}
	ret = notifier_from_errno(ret);
	return ret;
}

#endif

void __init page_cgroup_init(void)
{
	unsigned long pfn;
	int fail = 0;

	if (mem_cgroup_subsys.disabled)
		return;

	for (pfn = 0; !fail && pfn < max_pfn; pfn += PAGES_PER_SECTION) {
		if (!pfn_present(pfn))
			continue;
		fail = init_section_page_cgroup(pfn);
	}
	if (fail) {
		printk(KERN_CRIT "try cgroup_disable=memory boot option\n");
		panic("Out of memory");
	} else {
		hotplug_memory_notifier(page_cgroup_callback, 0);
	}
	printk(KERN_INFO "allocated %ld bytes of page_cgroup\n", total_usage);
	printk(KERN_INFO "please try cgroup_disable=memory option if you don't"
	" want\n");
}

void __init pgdat_page_cgroup_init(struct pglist_data *pgdat)
{
	return;
}

#endif
