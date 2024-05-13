// SPDX-License-Identifier: GPL-2.0
/*
 * mm/debug.c
 *
 * mm/ specific debug routines.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/trace_events.h>
#include <linux/memcontrol.h>
#include <trace/events/mmflags.h>
#include <linux/migrate.h>
#include <linux/page_owner.h>
#include <linux/ctype.h>

#include "internal.h"
#include <trace/events/migrate.h>

/*
 * Define EM() and EMe() so that MIGRATE_REASON from trace/events/migrate.h can
 * be used to populate migrate_reason_names[].
 */
#undef EM
#undef EMe
#define EM(a, b)	b,
#define EMe(a, b)	b

const char *migrate_reason_names[MR_TYPES] = {
	MIGRATE_REASON
};

const struct trace_print_flags pageflag_names[] = {
	__def_pageflag_names,
	{0, NULL}
};

const struct trace_print_flags pagetype_names[] = {
	__def_pagetype_names,
	{0, NULL}
};

const struct trace_print_flags gfpflag_names[] = {
	__def_gfpflag_names,
	{0, NULL}
};

const struct trace_print_flags vmaflag_names[] = {
	__def_vmaflag_names,
	{0, NULL}
};

static void __dump_page(struct page *page)
{
	struct folio *folio = page_folio(page);
	struct page *head = &folio->page;
	struct address_space *mapping;
	bool compound = PageCompound(page);
	/*
	 * Accessing the pageblock without the zone lock. It could change to
	 * "isolate" again in the meantime, but since we are just dumping the
	 * state for debugging, it should be fine to accept a bit of
	 * inaccuracy here due to racing.
	 */
	bool page_cma = is_migrate_cma_page(page);
	int mapcount;
	char *type = "";

	if (page < head || (page >= head + MAX_ORDER_NR_PAGES)) {
		/*
		 * Corrupt page, so we cannot call page_mapping. Instead, do a
		 * safe subset of the steps that page_mapping() does. Caution:
		 * this will be misleading for tail pages, PageSwapCache pages,
		 * and potentially other situations. (See the page_mapping()
		 * implementation for what's missing here.)
		 */
		unsigned long tmp = (unsigned long)page->mapping;

		if (tmp & PAGE_MAPPING_ANON)
			mapping = NULL;
		else
			mapping = (void *)(tmp & ~PAGE_MAPPING_FLAGS);
		head = page;
		folio = (struct folio *)page;
		compound = false;
	} else {
		mapping = page_mapping(page);
	}

	/*
	 * Avoid VM_BUG_ON() in page_mapcount().
	 * page->_mapcount space in struct page is used by sl[aou]b pages to
	 * encode own info.
	 */
	mapcount = PageSlab(head) ? 0 : page_mapcount(page);

	pr_warn("page:%p refcount:%d mapcount:%d mapping:%p index:%#lx pfn:%#lx\n",
			page, page_ref_count(head), mapcount, mapping,
			page_to_pgoff(page), page_to_pfn(page));
	if (compound) {
		pr_warn("head:%p order:%u entire_mapcount:%d nr_pages_mapped:%d pincount:%d\n",
				head, compound_order(head),
				folio_entire_mapcount(folio),
				folio_nr_pages_mapped(folio),
				atomic_read(&folio->_pincount));
	}

#ifdef CONFIG_MEMCG
	if (head->memcg_data)
		pr_warn("memcg:%lx\n", head->memcg_data);
#endif
	if (PageKsm(page))
		type = "ksm ";
	else if (PageAnon(page))
		type = "anon ";
	else if (mapping)
		dump_mapping(mapping);
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1);

	pr_warn("%sflags: %pGp%s\n", type, &head->flags,
		page_cma ? " CMA" : "");
	pr_warn("page_type: %pGt\n", &head->page_type);

	print_hex_dump(KERN_WARNING, "raw: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), page,
			sizeof(struct page), false);
	if (head != page)
		print_hex_dump(KERN_WARNING, "head: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), head,
			sizeof(struct page), false);
}

void dump_page(struct page *page, const char *reason)
{
	if (PagePoisoned(page))
		pr_warn("page:%p is uninitialized and poisoned", page);
	else
		__dump_page(page);
	if (reason)
		pr_warn("page dumped because: %s\n", reason);
	dump_page_owner(page);
}
EXPORT_SYMBOL(dump_page);

#ifdef CONFIG_DEBUG_VM

void dump_vma(const struct vm_area_struct *vma)
{
	pr_emerg("vma %px start %px end %px mm %px\n"
		"prot %lx anon_vma %px vm_ops %px\n"
		"pgoff %lx file %px private_data %px\n"
		"flags: %#lx(%pGv)\n",
		vma, (void *)vma->vm_start, (void *)vma->vm_end, vma->vm_mm,
		(unsigned long)pgprot_val(vma->vm_page_prot),
		vma->anon_vma, vma->vm_ops, vma->vm_pgoff,
		vma->vm_file, vma->vm_private_data,
		vma->vm_flags, &vma->vm_flags);
}
EXPORT_SYMBOL(dump_vma);

void dump_mm(const struct mm_struct *mm)
{
	pr_emerg("mm %px task_size %lu\n"
#ifdef CONFIG_MMU
		"get_unmapped_area %px\n"
#endif
		"mmap_base %lu mmap_legacy_base %lu\n"
		"pgd %px mm_users %d mm_count %d pgtables_bytes %lu map_count %d\n"
		"hiwater_rss %lx hiwater_vm %lx total_vm %lx locked_vm %lx\n"
		"pinned_vm %llx data_vm %lx exec_vm %lx stack_vm %lx\n"
		"start_code %lx end_code %lx start_data %lx end_data %lx\n"
		"start_brk %lx brk %lx start_stack %lx\n"
		"arg_start %lx arg_end %lx env_start %lx env_end %lx\n"
		"binfmt %px flags %lx\n"
#ifdef CONFIG_AIO
		"ioctx_table %px\n"
#endif
#ifdef CONFIG_MEMCG
		"owner %px "
#endif
		"exe_file %px\n"
#ifdef CONFIG_MMU_NOTIFIER
		"notifier_subscriptions %px\n"
#endif
#ifdef CONFIG_NUMA_BALANCING
		"numa_next_scan %lu numa_scan_offset %lu numa_scan_seq %d\n"
#endif
		"tlb_flush_pending %d\n"
		"def_flags: %#lx(%pGv)\n",

		mm, mm->task_size,
#ifdef CONFIG_MMU
		mm->get_unmapped_area,
#endif
		mm->mmap_base, mm->mmap_legacy_base,
		mm->pgd, atomic_read(&mm->mm_users),
		atomic_read(&mm->mm_count),
		mm_pgtables_bytes(mm),
		mm->map_count,
		mm->hiwater_rss, mm->hiwater_vm, mm->total_vm, mm->locked_vm,
		(u64)atomic64_read(&mm->pinned_vm),
		mm->data_vm, mm->exec_vm, mm->stack_vm,
		mm->start_code, mm->end_code, mm->start_data, mm->end_data,
		mm->start_brk, mm->brk, mm->start_stack,
		mm->arg_start, mm->arg_end, mm->env_start, mm->env_end,
		mm->binfmt, mm->flags,
#ifdef CONFIG_AIO
		mm->ioctx_table,
#endif
#ifdef CONFIG_MEMCG
		mm->owner,
#endif
		mm->exe_file,
#ifdef CONFIG_MMU_NOTIFIER
		mm->notifier_subscriptions,
#endif
#ifdef CONFIG_NUMA_BALANCING
		mm->numa_next_scan, mm->numa_scan_offset, mm->numa_scan_seq,
#endif
		atomic_read(&mm->tlb_flush_pending),
		mm->def_flags, &mm->def_flags
	);
}
EXPORT_SYMBOL(dump_mm);

static bool page_init_poisoning __read_mostly = true;

static int __init setup_vm_debug(char *str)
{
	bool __page_init_poisoning = true;

	/*
	 * Calling vm_debug with no arguments is equivalent to requesting
	 * to enable all debugging options we can control.
	 */
	if (*str++ != '=' || !*str)
		goto out;

	__page_init_poisoning = false;
	if (*str == '-')
		goto out;

	while (*str) {
		switch (tolower(*str)) {
		case'p':
			__page_init_poisoning = true;
			break;
		default:
			pr_err("vm_debug option '%c' unknown. skipped\n",
			       *str);
		}

		str++;
	}
out:
	if (page_init_poisoning && !__page_init_poisoning)
		pr_warn("Page struct poisoning disabled by kernel command line option 'vm_debug'\n");

	page_init_poisoning = __page_init_poisoning;

	return 1;
}
__setup("vm_debug", setup_vm_debug);

void page_init_poison(struct page *page, size_t size)
{
	if (page_init_poisoning)
		memset(page, PAGE_POISON_PATTERN, size);
}

void vma_iter_dump_tree(const struct vma_iterator *vmi)
{
#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	mas_dump(&vmi->mas);
	mt_dump(vmi->mas.tree, mt_dump_hex);
#endif	/* CONFIG_DEBUG_VM_MAPLE_TREE */
}

#endif		/* CONFIG_DEBUG_VM */
