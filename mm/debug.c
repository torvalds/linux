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

#include "internal.h"

char *migrate_reason_names[MR_TYPES] = {
	"compaction",
	"memory_failure",
	"memory_hotplug",
	"syscall_or_cpuset",
	"mempolicy_mbind",
	"numa_misplaced",
	"cma",
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

void __dump_page(struct page *page, const char *reason)
{
	/*
	 * Avoid VM_BUG_ON() in page_mapcount().
	 * page->_mapcount space in struct page is used by sl[aou]b pages to
	 * encode own info.
	 */
	int mapcount = PageSlab(page) ? 0 : page_mapcount(page);

	pr_emerg("page:%p count:%d mapcount:%d mapping:%p index:%#lx",
		  page, page_ref_count(page), mapcount,
		  page->mapping, page_to_pgoff(page));
	if (PageCompound(page))
		pr_cont(" compound_mapcount: %d", compound_mapcount(page));
	pr_cont("\n");
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1);

	pr_emerg("flags: %#lx(%pGp)\n", page->flags, &page->flags);

	print_hex_dump(KERN_ALERT, "raw: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), page,
			sizeof(struct page), false);

	if (reason)
		pr_alert("page dumped because: %s\n", reason);

#ifdef CONFIG_MEMCG
	if (page->mem_cgroup)
		pr_alert("page->mem_cgroup:%p\n", page->mem_cgroup);
#endif
}

void dump_page(struct page *page, const char *reason)
{
	__dump_page(page, reason);
	dump_page_owner(page);
}
EXPORT_SYMBOL(dump_page);

#ifdef CONFIG_DEBUG_VM

void dump_vma(const struct vm_area_struct *vma)
{
	pr_emerg("vma %p start %p end %p\n"
		"next %p prev %p mm %p\n"
		"prot %lx anon_vma %p vm_ops %p\n"
		"pgoff %lx file %p private_data %p\n"
		"flags: %#lx(%pGv)\n",
		vma, (void *)vma->vm_start, (void *)vma->vm_end, vma->vm_next,
		vma->vm_prev, vma->vm_mm,
		(unsigned long)pgprot_val(vma->vm_page_prot),
		vma->anon_vma, vma->vm_ops, vma->vm_pgoff,
		vma->vm_file, vma->vm_private_data,
		vma->vm_flags, &vma->vm_flags);
}
EXPORT_SYMBOL(dump_vma);

void dump_mm(const struct mm_struct *mm)
{
	pr_emerg("mm %p mmap %p seqnum %d task_size %lu\n"
#ifdef CONFIG_MMU
		"get_unmapped_area %p\n"
#endif
		"mmap_base %lu mmap_legacy_base %lu highest_vm_end %lu\n"
		"pgd %p mm_users %d mm_count %d\n"
		"nr_ptes %lu nr_pmds %lu nr_puds %lu map_count %d\n"
		"hiwater_rss %lx hiwater_vm %lx total_vm %lx locked_vm %lx\n"
		"pinned_vm %lx data_vm %lx exec_vm %lx stack_vm %lx\n"
		"start_code %lx end_code %lx start_data %lx end_data %lx\n"
		"start_brk %lx brk %lx start_stack %lx\n"
		"arg_start %lx arg_end %lx env_start %lx env_end %lx\n"
		"binfmt %p flags %lx core_state %p\n"
#ifdef CONFIG_AIO
		"ioctx_table %p\n"
#endif
#ifdef CONFIG_MEMCG
		"owner %p "
#endif
		"exe_file %p\n"
#ifdef CONFIG_MMU_NOTIFIER
		"mmu_notifier_mm %p\n"
#endif
#ifdef CONFIG_NUMA_BALANCING
		"numa_next_scan %lu numa_scan_offset %lu numa_scan_seq %d\n"
#endif
		"tlb_flush_pending %d\n"
		"def_flags: %#lx(%pGv)\n",

		mm, mm->mmap, mm->vmacache_seqnum, mm->task_size,
#ifdef CONFIG_MMU
		mm->get_unmapped_area,
#endif
		mm->mmap_base, mm->mmap_legacy_base, mm->highest_vm_end,
		mm->pgd, atomic_read(&mm->mm_users),
		atomic_read(&mm->mm_count),
		atomic_long_read((atomic_long_t *)&mm->nr_ptes),
		mm_nr_pmds(mm),
		mm_nr_puds(mm),
		mm->map_count,
		mm->hiwater_rss, mm->hiwater_vm, mm->total_vm, mm->locked_vm,
		mm->pinned_vm, mm->data_vm, mm->exec_vm, mm->stack_vm,
		mm->start_code, mm->end_code, mm->start_data, mm->end_data,
		mm->start_brk, mm->brk, mm->start_stack,
		mm->arg_start, mm->arg_end, mm->env_start, mm->env_end,
		mm->binfmt, mm->flags, mm->core_state,
#ifdef CONFIG_AIO
		mm->ioctx_table,
#endif
#ifdef CONFIG_MEMCG
		mm->owner,
#endif
		mm->exe_file,
#ifdef CONFIG_MMU_NOTIFIER
		mm->mmu_notifier_mm,
#endif
#ifdef CONFIG_NUMA_BALANCING
		mm->numa_next_scan, mm->numa_scan_offset, mm->numa_scan_seq,
#endif
		atomic_read(&mm->tlb_flush_pending),
		mm->def_flags, &mm->def_flags
	);
}

#endif		/* CONFIG_DEBUG_VM */
