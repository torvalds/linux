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

const char *migrate_reason_names[MR_TYPES] = {
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
	struct page *head = compound_head(page);
	struct address_space *mapping;
	bool page_poisoned = PagePoisoned(page);
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

	/*
	 * If struct page is poisoned don't access Page*() functions as that
	 * leads to recursive loop. Page*() check for poisoned pages, and calls
	 * dump_page() when detected.
	 */
	if (page_poisoned) {
		pr_warn("page:%px is uninitialized and poisoned", page);
		goto hex_only;
	}

	if (page < head || (page >= head + MAX_ORDER_NR_PAGES)) {
		/* Corrupt page, cannot call page_mapping */
		mapping = page->mapping;
		head = page;
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

	if (compound)
		if (hpage_pincount_available(page)) {
			pr_warn("page:%px refcount:%d mapcount:%d mapping:%p "
				"index:%#lx head:%px order:%u "
				"compound_mapcount:%d compound_pincount:%d\n",
				page, page_ref_count(head), mapcount,
				mapping, page_to_pgoff(page), head,
				compound_order(head), compound_mapcount(page),
				compound_pincount(page));
		} else {
			pr_warn("page:%px refcount:%d mapcount:%d mapping:%p "
				"index:%#lx head:%px order:%u "
				"compound_mapcount:%d\n",
				page, page_ref_count(head), mapcount,
				mapping, page_to_pgoff(page), head,
				compound_order(head), compound_mapcount(page));
		}
	else
		pr_warn("page:%px refcount:%d mapcount:%d mapping:%p index:%#lx\n",
			page, page_ref_count(page), mapcount,
			mapping, page_to_pgoff(page));
	if (PageKsm(page))
		type = "ksm ";
	else if (PageAnon(page))
		type = "anon ";
	else if (mapping) {
		const struct inode *host;
		const struct address_space_operations *a_ops;
		const struct hlist_node *dentry_first;
		const struct dentry *dentry_ptr;
		struct dentry dentry;

		/*
		 * mapping can be invalid pointer and we don't want to crash
		 * accessing it, so probe everything depending on it carefully
		 */
		if (copy_from_kernel_nofault(&host, &mapping->host,
					sizeof(struct inode *)) ||
		    copy_from_kernel_nofault(&a_ops, &mapping->a_ops,
				sizeof(struct address_space_operations *))) {
			pr_warn("failed to read mapping->host or a_ops, mapping not a valid kernel address?\n");
			goto out_mapping;
		}

		if (!host) {
			pr_warn("mapping->a_ops:%ps\n", a_ops);
			goto out_mapping;
		}

		if (copy_from_kernel_nofault(&dentry_first,
			&host->i_dentry.first, sizeof(struct hlist_node *))) {
			pr_warn("mapping->a_ops:%ps with invalid mapping->host inode address %px\n",
				a_ops, host);
			goto out_mapping;
		}

		if (!dentry_first) {
			pr_warn("mapping->a_ops:%ps\n", a_ops);
			goto out_mapping;
		}

		dentry_ptr = container_of(dentry_first, struct dentry, d_u.d_alias);
		if (copy_from_kernel_nofault(&dentry, dentry_ptr,
							sizeof(struct dentry))) {
			pr_warn("mapping->aops:%ps with invalid mapping->host->i_dentry.first %px\n",
				a_ops, dentry_ptr);
		} else {
			/*
			 * if dentry is corrupted, the %pd handler may still
			 * crash, but it's unlikely that we reach here with a
			 * corrupted struct page
			 */
			pr_warn("mapping->aops:%ps dentry name:\"%pd\"\n",
								a_ops, &dentry);
		}
	}
out_mapping:
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1);

	pr_warn("%sflags: %#lx(%pGp)%s\n", type, page->flags, &page->flags,
		page_cma ? " CMA" : "");

hex_only:
	print_hex_dump(KERN_WARNING, "raw: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), page,
			sizeof(struct page), false);
	if (head != page)
		print_hex_dump(KERN_WARNING, "head: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), head,
			sizeof(struct page), false);

	if (reason)
		pr_warn("page dumped because: %s\n", reason);

#ifdef CONFIG_MEMCG
	if (!page_poisoned && page->mem_cgroup)
		pr_warn("page->mem_cgroup:%px\n", page->mem_cgroup);
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
	pr_emerg("vma %px start %px end %px\n"
		"next %px prev %px mm %px\n"
		"prot %lx anon_vma %px vm_ops %px\n"
		"pgoff %lx file %px private_data %px\n"
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
	pr_emerg("mm %px mmap %px seqnum %llu task_size %lu\n"
#ifdef CONFIG_MMU
		"get_unmapped_area %px\n"
#endif
		"mmap_base %lu mmap_legacy_base %lu highest_vm_end %lu\n"
		"pgd %px mm_users %d mm_count %d pgtables_bytes %lu map_count %d\n"
		"hiwater_rss %lx hiwater_vm %lx total_vm %lx locked_vm %lx\n"
		"pinned_vm %llx data_vm %lx exec_vm %lx stack_vm %lx\n"
		"start_code %lx end_code %lx start_data %lx end_data %lx\n"
		"start_brk %lx brk %lx start_stack %lx\n"
		"arg_start %lx arg_end %lx env_start %lx env_end %lx\n"
		"binfmt %px flags %lx core_state %px\n"
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

		mm, mm->mmap, (long long) mm->vmacache_seqnum, mm->task_size,
#ifdef CONFIG_MMU
		mm->get_unmapped_area,
#endif
		mm->mmap_base, mm->mmap_legacy_base, mm->highest_vm_end,
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
		mm->binfmt, mm->flags, mm->core_state,
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
EXPORT_SYMBOL_GPL(page_init_poison);
#endif		/* CONFIG_DEBUG_VM */
