/*
 * mm/debug.c
 *
 * mm/ specific debug routines.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ftrace_event.h>
#include <linux/memcontrol.h>

static const struct trace_print_flags pageflag_names[] = {
	{1UL << PG_locked,		"locked"	},
	{1UL << PG_error,		"error"		},
	{1UL << PG_referenced,		"referenced"	},
	{1UL << PG_uptodate,		"uptodate"	},
	{1UL << PG_dirty,		"dirty"		},
	{1UL << PG_lru,			"lru"		},
	{1UL << PG_active,		"active"	},
	{1UL << PG_slab,		"slab"		},
	{1UL << PG_owner_priv_1,	"owner_priv_1"	},
	{1UL << PG_arch_1,		"arch_1"	},
	{1UL << PG_reserved,		"reserved"	},
	{1UL << PG_private,		"private"	},
	{1UL << PG_private_2,		"private_2"	},
	{1UL << PG_writeback,		"writeback"	},
#ifdef CONFIG_PAGEFLAGS_EXTENDED
	{1UL << PG_head,		"head"		},
	{1UL << PG_tail,		"tail"		},
#else
	{1UL << PG_compound,		"compound"	},
#endif
	{1UL << PG_swapcache,		"swapcache"	},
	{1UL << PG_mappedtodisk,	"mappedtodisk"	},
	{1UL << PG_reclaim,		"reclaim"	},
	{1UL << PG_swapbacked,		"swapbacked"	},
	{1UL << PG_unevictable,		"unevictable"	},
#ifdef CONFIG_MMU
	{1UL << PG_mlocked,		"mlocked"	},
#endif
#ifdef CONFIG_ARCH_USES_PG_UNCACHED
	{1UL << PG_uncached,		"uncached"	},
#endif
#ifdef CONFIG_MEMORY_FAILURE
	{1UL << PG_hwpoison,		"hwpoison"	},
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	{1UL << PG_compound_lock,	"compound_lock"	},
#endif
};

static void dump_flags(unsigned long flags,
			const struct trace_print_flags *names, int count)
{
	const char *delim = "";
	unsigned long mask;
	int i;

	printk(KERN_ALERT "flags: %#lx(", flags);

	/* remove zone id */
	flags &= (1UL << NR_PAGEFLAGS) - 1;

	for (i = 0; i < count && flags; i++) {

		mask = names[i].mask;
		if ((flags & mask) != mask)
			continue;

		flags &= ~mask;
		printk("%s%s", delim, names[i].name);
		delim = "|";
	}

	/* check for left over flags */
	if (flags)
		printk("%s%#lx", delim, flags);

	printk(")\n");
}

void dump_page_badflags(struct page *page, const char *reason,
		unsigned long badflags)
{
	printk(KERN_ALERT
	       "page:%p count:%d mapcount:%d mapping:%p index:%#lx\n",
		page, atomic_read(&page->_count), page_mapcount(page),
		page->mapping, page->index);
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS);
	dump_flags(page->flags, pageflag_names, ARRAY_SIZE(pageflag_names));
	if (reason)
		pr_alert("page dumped because: %s\n", reason);
	if (page->flags & badflags) {
		pr_alert("bad because of flags:\n");
		dump_flags(page->flags & badflags,
				pageflag_names, ARRAY_SIZE(pageflag_names));
	}
	mem_cgroup_print_bad_page(page);
}

void dump_page(struct page *page, const char *reason)
{
	dump_page_badflags(page, reason, 0);
}
EXPORT_SYMBOL(dump_page);

#ifdef CONFIG_DEBUG_VM

static const struct trace_print_flags vmaflags_names[] = {
	{VM_READ,			"read"		},
	{VM_WRITE,			"write"		},
	{VM_EXEC,			"exec"		},
	{VM_SHARED,			"shared"	},
	{VM_MAYREAD,			"mayread"	},
	{VM_MAYWRITE,			"maywrite"	},
	{VM_MAYEXEC,			"mayexec"	},
	{VM_MAYSHARE,			"mayshare"	},
	{VM_GROWSDOWN,			"growsdown"	},
	{VM_PFNMAP,			"pfnmap"	},
	{VM_DENYWRITE,			"denywrite"	},
	{VM_LOCKED,			"locked"	},
	{VM_IO,				"io"		},
	{VM_SEQ_READ,			"seqread"	},
	{VM_RAND_READ,			"randread"	},
	{VM_DONTCOPY,			"dontcopy"	},
	{VM_DONTEXPAND,			"dontexpand"	},
	{VM_ACCOUNT,			"account"	},
	{VM_NORESERVE,			"noreserve"	},
	{VM_HUGETLB,			"hugetlb"	},
	{VM_NONLINEAR,			"nonlinear"	},
#if defined(CONFIG_X86)
	{VM_PAT,			"pat"		},
#elif defined(CONFIG_PPC)
	{VM_SAO,			"sao"		},
#elif defined(CONFIG_PARISC) || defined(CONFIG_METAG) || defined(CONFIG_IA64)
	{VM_GROWSUP,			"growsup"	},
#elif !defined(CONFIG_MMU)
	{VM_MAPPED_COPY,		"mappedcopy"	},
#else
	{VM_ARCH_1,			"arch_1"	},
#endif
	{VM_DONTDUMP,			"dontdump"	},
#ifdef CONFIG_MEM_SOFT_DIRTY
	{VM_SOFTDIRTY,			"softdirty"	},
#endif
	{VM_MIXEDMAP,			"mixedmap"	},
	{VM_HUGEPAGE,			"hugepage"	},
	{VM_NOHUGEPAGE,			"nohugepage"	},
	{VM_MERGEABLE,			"mergeable"	},
};

void dump_vma(const struct vm_area_struct *vma)
{
	printk(KERN_ALERT
		"vma %p start %p end %p\n"
		"next %p prev %p mm %p\n"
		"prot %lx anon_vma %p vm_ops %p\n"
		"pgoff %lx file %p private_data %p\n",
		vma, (void *)vma->vm_start, (void *)vma->vm_end, vma->vm_next,
		vma->vm_prev, vma->vm_mm,
		(unsigned long)pgprot_val(vma->vm_page_prot),
		vma->anon_vma, vma->vm_ops, vma->vm_pgoff,
		vma->vm_file, vma->vm_private_data);
	dump_flags(vma->vm_flags, vmaflags_names, ARRAY_SIZE(vmaflags_names));
}
EXPORT_SYMBOL(dump_vma);

void dump_mm(const struct mm_struct *mm)
{
	printk(KERN_ALERT
		"mm %p mmap %p seqnum %d task_size %lu\n"
#ifdef CONFIG_MMU
		"get_unmapped_area %p\n"
#endif
		"mmap_base %lu mmap_legacy_base %lu highest_vm_end %lu\n"
		"pgd %p mm_users %d mm_count %d nr_ptes %lu map_count %d\n"
		"hiwater_rss %lx hiwater_vm %lx total_vm %lx locked_vm %lx\n"
		"pinned_vm %lx shared_vm %lx exec_vm %lx stack_vm %lx\n"
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
#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
		"tlb_flush_pending %d\n"
#endif
		"%s",	/* This is here to hold the comma */

		mm, mm->mmap, mm->vmacache_seqnum, mm->task_size,
#ifdef CONFIG_MMU
		mm->get_unmapped_area,
#endif
		mm->mmap_base, mm->mmap_legacy_base, mm->highest_vm_end,
		mm->pgd, atomic_read(&mm->mm_users),
		atomic_read(&mm->mm_count),
		atomic_long_read((atomic_long_t *)&mm->nr_ptes),
		mm->map_count,
		mm->hiwater_rss, mm->hiwater_vm, mm->total_vm, mm->locked_vm,
		mm->pinned_vm, mm->shared_vm, mm->exec_vm, mm->stack_vm,
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
#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
		mm->tlb_flush_pending,
#endif
		""		/* This is here to not have a comma! */
		);

		dump_flags(mm->def_flags, vmaflags_names,
				ARRAY_SIZE(vmaflags_names));
}

#endif		/* CONFIG_DEBUG_VM */
