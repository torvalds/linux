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

const struct trace_print_flags gfpflag_names[] = {
	__def_gfpflag_names,
	{0, NULL}
};

const struct trace_print_flags vmaflag_names[] = {
	__def_vmaflag_names,
	{0, NULL}
};

#define DEF_PAGETYPE_NAME(_name) [PGTY_##_name - 0xf0] =  __stringify(_name)

static const char *page_type_names[] = {
	DEF_PAGETYPE_NAME(slab),
	DEF_PAGETYPE_NAME(hugetlb),
	DEF_PAGETYPE_NAME(offline),
	DEF_PAGETYPE_NAME(guard),
	DEF_PAGETYPE_NAME(table),
	DEF_PAGETYPE_NAME(buddy),
	DEF_PAGETYPE_NAME(unaccepted),
};

static const char *page_type_name(unsigned int page_type)
{
	unsigned i = (page_type >> 24) - 0xf0;

	if (i >= ARRAY_SIZE(page_type_names))
		return "unknown";
	return page_type_names[i];
}

static void __dump_folio(struct folio *folio, struct page *page,
		unsigned long pfn, unsigned long idx)
{
	struct address_space *mapping = folio_mapping(folio);
	int mapcount = atomic_read(&page->_mapcount);
	char *type = "";

	mapcount = page_mapcount_is_type(mapcount) ? 0 : mapcount + 1;
	pr_warn("page: refcount:%d mapcount:%d mapping:%p index:%#lx pfn:%#lx\n",
			folio_ref_count(folio), mapcount, mapping,
			folio->index + idx, pfn);
	if (folio_test_large(folio)) {
		int pincount = 0;

		if (folio_has_pincount(folio))
			pincount = atomic_read(&folio->_pincount);

		pr_warn("head: order:%u mapcount:%d entire_mapcount:%d nr_pages_mapped:%d pincount:%d\n",
				folio_order(folio),
				folio_mapcount(folio),
				folio_entire_mapcount(folio),
				folio_nr_pages_mapped(folio),
				pincount);
	}

#ifdef CONFIG_MEMCG
	if (folio->memcg_data)
		pr_warn("memcg:%lx\n", folio->memcg_data);
#endif
	if (folio_test_ksm(folio))
		type = "ksm ";
	else if (folio_test_anon(folio))
		type = "anon ";
	else if (mapping)
		dump_mapping(mapping);
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1);

	/*
	 * Accessing the pageblock without the zone lock. It could change to
	 * "isolate" again in the meantime, but since we are just dumping the
	 * state for debugging, it should be fine to accept a bit of
	 * inaccuracy here due to racing.
	 */
	pr_warn("%sflags: %pGp%s\n", type, &folio->flags,
		is_migrate_cma_folio(folio, pfn) ? " CMA" : "");
	if (page_has_type(&folio->page))
		pr_warn("page_type: %x(%s)\n", folio->page.page_type >> 24,
				page_type_name(folio->page.page_type));

	print_hex_dump(KERN_WARNING, "raw: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), page,
			sizeof(struct page), false);
	if (folio_test_large(folio))
		print_hex_dump(KERN_WARNING, "head: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), folio,
			2 * sizeof(struct page), false);
}

static void __dump_page(const struct page *page)
{
	struct folio *foliop, folio;
	struct page precise;
	unsigned long head;
	unsigned long pfn = page_to_pfn(page);
	unsigned long idx, nr_pages = 1;
	int loops = 5;

again:
	memcpy(&precise, page, sizeof(*page));
	head = precise.compound_head;
	if ((head & 1) == 0) {
		foliop = (struct folio *)&precise;
		idx = 0;
		if (!folio_test_large(foliop))
			goto dump;
		foliop = (struct folio *)page;
	} else {
		foliop = (struct folio *)(head - 1);
		idx = folio_page_idx(foliop, page);
	}

	if (idx < MAX_FOLIO_NR_PAGES) {
		memcpy(&folio, foliop, 2 * sizeof(struct page));
		nr_pages = folio_nr_pages(&folio);
		if (nr_pages > 1)
			memcpy(&folio.__page_2, &foliop->__page_2,
			       sizeof(struct page));
		foliop = &folio;
	}

	if (idx > nr_pages) {
		if (loops-- > 0)
			goto again;
		pr_warn("page does not match folio\n");
		precise.compound_head &= ~1UL;
		foliop = (struct folio *)&precise;
		idx = 0;
	}

dump:
	__dump_folio(foliop, &precise, pfn, idx);
}

void dump_page(const struct page *page, const char *reason)
{
	if (PagePoisoned(page))
		pr_warn("page:%p is uninitialized and poisoned\n", page);
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
#ifdef CONFIG_PER_VMA_LOCK
		"refcnt %x\n"
#endif
		"flags: %#lx(%pGv)\n",
		vma, (void *)vma->vm_start, (void *)vma->vm_end, vma->vm_mm,
		(unsigned long)pgprot_val(vma->vm_page_prot),
		vma->anon_vma, vma->vm_ops, vma->vm_pgoff,
		vma->vm_file, vma->vm_private_data,
#ifdef CONFIG_PER_VMA_LOCK
		refcount_read(&vma->vm_refcnt),
#endif
		vma->vm_flags, &vma->vm_flags);
}
EXPORT_SYMBOL(dump_vma);

void dump_mm(const struct mm_struct *mm)
{
	pr_emerg("mm %px task_size %lu\n"
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

void dump_vmg(const struct vma_merge_struct *vmg, const char *reason)
{
	if (reason)
		pr_warn("vmg %px dumped because: %s\n", vmg, reason);

	if (!vmg) {
		pr_warn("vmg %px state: (NULL)\n", vmg);
		return;
	}

	pr_warn("vmg %px state: mm %px pgoff %lx\n"
		"vmi %px [%lx,%lx)\n"
		"prev %px middle %px next %px target %px\n"
		"start %lx end %lx flags %lx\n"
		"file %px anon_vma %px policy %px\n"
		"uffd_ctx %px\n"
		"anon_name %px\n"
		"state %x\n"
		"just_expand %d\n"
		"__adjust_middle_start %d __adjust_next_start %d\n"
		"__remove_middle %d __remove_next %d\n",
		vmg, vmg->mm, vmg->pgoff,
		vmg->vmi, vmg->vmi ? vma_iter_addr(vmg->vmi) : 0,
		vmg->vmi ? vma_iter_end(vmg->vmi) : 0,
		vmg->prev, vmg->middle, vmg->next, vmg->target,
		vmg->start, vmg->end, vmg->flags,
		vmg->file, vmg->anon_vma, vmg->policy,
#ifdef CONFIG_USERFAULTFD
		vmg->uffd_ctx.ctx,
#else
		(void *)0,
#endif
		vmg->anon_name,
		(int)vmg->state,
		vmg->just_expand,
		vmg->__adjust_middle_start, vmg->__adjust_next_start,
		vmg->__remove_middle, vmg->__remove_next);

	if (vmg->mm) {
		pr_warn("vmg %px mm:\n", vmg);
		dump_mm(vmg->mm);
	} else {
		pr_warn("vmg %px mm: (NULL)\n", vmg);
	}

	if (vmg->prev) {
		pr_warn("vmg %px prev:\n", vmg);
		dump_vma(vmg->prev);
	} else {
		pr_warn("vmg %px prev: (NULL)\n", vmg);
	}

	if (vmg->middle) {
		pr_warn("vmg %px middle:\n", vmg);
		dump_vma(vmg->middle);
	} else {
		pr_warn("vmg %px middle: (NULL)\n", vmg);
	}

	if (vmg->next) {
		pr_warn("vmg %px next:\n", vmg);
		dump_vma(vmg->next);
	} else {
		pr_warn("vmg %px next: (NULL)\n", vmg);
	}

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
	if (vmg->vmi) {
		pr_warn("vmg %px vmi:\n", vmg);
		vma_iter_dump_tree(vmg->vmi);
	} else {
		pr_warn("vmg %px vmi: (NULL)\n", vmg);
	}
#endif
}
EXPORT_SYMBOL(dump_vmg);

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
